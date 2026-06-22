#include "MainWindow.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

#include <stdexcept>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {

constexpr quint16 kSearchPort = 46000;

QString makeReqId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}

bool isIpv4Address(const QString& text) {
    QHostAddress address;
    return address.setAddress(text) && address.protocol() == QAbstractSocket::IPv4Protocol;
}

quint32 ipv4ToHostInt(const QString& text) {
    return QHostAddress(text).toIPv4Address();
}

bool sameSubnet(const QString& ip, const QString& mask, const QString& gateway) {
    if (gateway.isEmpty()) {
        return true;
    }
    const quint32 maskValue = ipv4ToHostInt(mask);
    return (ipv4ToHostInt(ip) & maskValue) == (ipv4ToHostInt(gateway) & maskValue);
}

QString cardSummary(const QJsonObject& card) {
    if (card.isEmpty()) {
        return "-";
    }
    return QString("%1 %2 %3")
        .arg(card.value("ethName").toString())
        .arg(card.value("dhcp").toInt() ? "dhcp" : "static")
        .arg(card.value("ipAddr").toString("-"));
}

}  // namespace

UdpCommandWorker::UdpCommandWorker(Command command, QString interfaceIp, QString targetIp, QString reqId,
                                   QJsonObject payload, int timeoutSeconds, QObject* parent)
    : QThread(parent),
      m_command(command),
      m_interfaceIp(std::move(interfaceIp)),
      m_targetIp(std::move(targetIp)),
      m_reqId(std::move(reqId)),
      m_payload(std::move(payload)),
      m_timeoutSeconds(timeoutSeconds) {}

void UdpCommandWorker::run() {
#ifdef Q_OS_WIN
    WSADATA wsaData{};
    const int startupRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupRet != 0) {
        emit commandFailed(QString("WSAStartup failed: %1").arg(startupRet));
        emit commandFinished();
        return;
    }

    SOCKET sock = INVALID_SOCKET;
    auto closeSocket = [&sock]() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    };

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        emit commandFailed(QString("socket failed: %1").arg(WSAGetLastError()));
        WSACleanup();
        emit commandFinished();
        return;
    }

    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(kSearchPort);
    if (bind(sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        const int err = WSAGetLastError();
        closeSocket();
        emit commandFailed(QString("bind UDP 46000 failed: %1").arg(err));
        WSACleanup();
        emit commandFinished();
        return;
    }

    ip_mreq membership{};
    membership.imr_multiaddr.s_addr = inet_addr("239.255.0.0");
    membership.imr_interface.s_addr = inet_addr(m_interfaceIp.toLatin1().constData());
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&membership),
                   sizeof(membership)) != 0) {
        const int err = WSAGetLastError();
        closeSocket();
        emit commandFailed(QString("IP_ADD_MEMBERSHIP failed: %1").arg(err));
        WSACleanup();
        emit commandFinished();
        return;
    }

    in_addr outgoing{};
    outgoing.s_addr = inet_addr(m_interfaceIp.toLatin1().constData());
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&outgoing), sizeof(outgoing));

    DWORD timeoutMs = static_cast<DWORD>(m_timeoutSeconds * 1000);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kSearchPort);
    const QString targetIp = m_targetIp.isEmpty() ? QStringLiteral("239.255.0.0") : m_targetIp;
    target.sin_addr.s_addr = inet_addr(targetIp.toLatin1().constData());

    const QByteArray data = QJsonDocument(m_payload).toJson(QJsonDocument::Compact);
    const int repeat = m_command == Command::Probe ? 3 : 1;
    for (int i = 0; i < repeat; ++i) {
        sendto(sock, data.constData(), data.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
        if (i + 1 < repeat) {
            msleep(300);
        }
    }

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + (m_timeoutSeconds * 1000);
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;
        const int ready = select(0, &readfds, nullptr, nullptr, &tv);
        if (ready <= 0) {
            continue;
        }

        char buffer[65535];
        sockaddr_in sender{};
        int senderLen = sizeof(sender);
        const int received = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&sender),
                                      &senderLen);
        if (received <= 0) {
            continue;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(QByteArray(buffer, received), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const auto object = doc.object();
        if (object.value("reqId").toString() != m_reqId || object.value("type").toString() != "ack") {
            continue;
        }
        const QString expectedCmd = m_command == Command::Probe ? "probe" : "modifyNetCard";
        if (object.value("cmd").toString() != expectedCmd) {
            continue;
        }

        char senderText[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &sender.sin_addr, senderText, sizeof(senderText));
        emit datagramReceived(object, QString::fromLatin1(senderText));
        if (m_command == Command::ModifyNetCard) {
            break;
        }
    }

    closeSocket();
    WSACleanup();
    emit commandFinished();
#else
    emit commandFailed("Native UDP worker is currently implemented for Windows only.");
    emit commandFinished();
#endif
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    refreshInterfaces();
}

void MainWindow::buildUi() {
    setWindowTitle("Aibox Search Tool");
    resize(1180, 760);

    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    setCentralWidget(root);

    m_interfaceCombo = new QComboBox(root);
    m_refreshInterfacesButton = new QPushButton("刷新网卡", root);
    m_searchButton = new QPushButton("组播搜索", root);
    m_targetIpEdit = new QLineEdit(root);
    m_targetIpEdit->setPlaceholderText("可选：指定设备 IP 单播探测");
    m_probeIpButton = new QPushButton("指定 IP 探测", root);
    m_timeoutSpin = new QSpinBox(root);
    m_timeoutSpin->setRange(1, 30);
    m_timeoutSpin->setValue(5);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("本机网卡", root));
    top->addWidget(m_interfaceCombo, 2);
    top->addWidget(m_refreshInterfacesButton);
    top->addWidget(new QLabel("超时(s)", root));
    top->addWidget(m_timeoutSpin);
    top->addWidget(m_searchButton);
    top->addWidget(m_targetIpEdit, 1);
    top->addWidget(m_probeIpButton);
    layout->addLayout(top);

    m_deviceTable = new QTableWidget(0, 7, root);
    m_deviceTable->setHorizontalHeaderLabels({"IP", "SN", "型号", "版本", "WAN", "LAN", "更新时间"});
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_deviceTable, 3);

    auto* configGroup = new QGroupBox("网络配置（当前旧协议：仅建议修改 WAN / eth0）", root);
    auto* grid = new QGridLayout(configGroup);
    m_selectedLabel = new QLabel("未选择设备", configGroup);
    grid->addWidget(m_selectedLabel, 0, 0, 1, 4);

    m_wanRadio = new QRadioButton("WAN / eth0", configGroup);
    m_wanRadio->setChecked(true);
    m_lanRadio = new QRadioButton("LAN / eth1（旧协议暂不建议修改）", configGroup);
    m_lanRadio->setEnabled(false);
    m_dhcpCheck = new QCheckBox("启用 DHCP", configGroup);
    grid->addWidget(m_wanRadio, 1, 0);
    grid->addWidget(m_lanRadio, 1, 1);
    grid->addWidget(m_dhcpCheck, 1, 2);

    m_ipEdit = new QLineEdit(configGroup);
    m_maskEdit = new QLineEdit(configGroup);
    m_gatewayEdit = new QLineEdit(configGroup);
    m_dns1Edit = new QLineEdit(configGroup);
    m_dns2Edit = new QLineEdit(configGroup);
    auto* form = new QFormLayout();
    form->addRow("IP", m_ipEdit);
    form->addRow("子网掩码", m_maskEdit);
    form->addRow("网关", m_gatewayEdit);
    form->addRow("DNS1", m_dns1Edit);
    form->addRow("DNS2", m_dns2Edit);

    m_passwordEdit = new QLineEdit(configGroup);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("管理员密码或密码 MD5");
    m_passwordMd5Check = new QCheckBox("输入为明文，发送 MD5 大写", configGroup);
    m_passwordMd5Check->setChecked(true);
    m_applyButton = new QPushButton("应用到选中设备", configGroup);
    m_applyButton->setEnabled(false);
    auto* authForm = new QFormLayout();
    authForm->addRow("管理员密码", m_passwordEdit);
    authForm->addRow("", m_passwordMd5Check);
    authForm->addRow("", m_applyButton);

    grid->addLayout(form, 2, 0, 1, 2);
    grid->addLayout(authForm, 2, 2, 1, 2);
    layout->addWidget(configGroup);

    m_logText = new QTextEdit(root);
    m_logText->setReadOnly(true);
    layout->addWidget(m_logText, 1);

    connect(m_refreshInterfacesButton, &QPushButton::clicked, this, &MainWindow::refreshInterfaces);
    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::searchMulticast);
    connect(m_probeIpButton, &QPushButton::clicked, this, &MainWindow::probeTargetIp);
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::selectDevice);
    connect(m_dhcpCheck, &QCheckBox::toggled, this, &MainWindow::updateDhcpState);
    connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::applyNetwork);
}

void MainWindow::refreshInterfaces() {
    m_interfaceCombo->clear();
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto& entry : iface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            const QString ipText = ip.toString();
            if (ipText.startsWith("169.254.")) {
                continue;
            }
            m_interfaceCombo->addItem(QString("%1  %2").arg(ipText, iface.humanReadableName()), iface.name());
        }
    }
    if (m_interfaceCombo->count() == 0) {
        m_interfaceCombo->addItem("0.0.0.0  default", QString());
    }
    appendLog(QString("加载本机网卡 %1 个").arg(m_interfaceCombo->count()));
}

QString MainWindow::selectedInterfaceIp() const {
    const auto iface = selectedInterface();
    for (const auto& entry : iface.addressEntries()) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            return entry.ip().toString();
        }
    }
    return "0.0.0.0";
}

QNetworkInterface MainWindow::selectedInterface() const {
    const QString name = m_interfaceCombo->currentData().toString();
    if (name.isEmpty()) {
        return {};
    }
    return QNetworkInterface::interfaceFromName(name);
}

void MainWindow::searchMulticast() {
    startProbe(QString());
}

void MainWindow::probeTargetIp() {
    const QString target = m_targetIpEdit->text().trimmed();
    if (!isIpv4Address(target)) {
        QMessageBox::warning(this, "参数错误", "指定设备 IP 不是合法 IPv4 地址");
        return;
    }
    startProbe(target);
}

void MainWindow::startProbe(const QString& targetIp) {
    m_pendingReqId = makeReqId();
    appendLog(QString("开始搜索，网卡=%1 target=%2")
                  .arg(selectedInterfaceIp(), targetIp.isEmpty() ? m_group.toString() : targetIp));
    QJsonObject object{{"cmd", "probe"}, {"type", "req"}, {"reqId", m_pendingReqId}};
    startWorker(UdpCommandWorker::Command::Probe, targetIp, object);
}

void MainWindow::startWorker(UdpCommandWorker::Command command, const QString& targetIp,
                             const QJsonObject& payload) {
    if (m_worker) {
        m_worker->wait();
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    setBusy(true);
    m_pendingCommand = command;
    m_worker = new UdpCommandWorker(command, selectedInterfaceIp(), targetIp, m_pendingReqId, payload,
                                    m_timeoutSpin->value(), this);
    connect(m_worker, &UdpCommandWorker::datagramReceived, this, &MainWindow::handleWorkerDatagram);
    connect(m_worker, &UdpCommandWorker::commandFailed, this, [this](const QString& message) {
        appendLog(QString("UDP 操作失败：%1").arg(message));
        QMessageBox::warning(this, "UDP 操作失败", message);
    });
    connect(m_worker, &UdpCommandWorker::commandFinished, this, [this]() {
        if (m_pendingCommand == UdpCommandWorker::Command::Probe) {
            appendLog(QString("搜索结束，当前列表 %1 台设备").arg(m_devices.size()));
        }
        setBusy(false);
        m_worker->deleteLater();
        m_worker = nullptr;
    });
    m_worker->start();
}

void MainWindow::handleWorkerDatagram(const QJsonObject& object, const QString& senderIp) {
    if (m_pendingCommand == UdpCommandWorker::Command::Probe) {
        handleProbeAck(object, senderIp);
    } else {
        handleModifyAck(object);
    }
}

void MainWindow::handleProbeAck(const QJsonObject& object, const QString& senderIp) {
    const QString key = deviceKey(object, QHostAddress(senderIp));
    DeviceRecord record;
    record.key = key;
    record.peerIp = senderIp;
    record.payload = object;
    record.payload.insert("_peerIp", record.peerIp);
    m_devices.insert(key, record);
    renderDevices();
}

void MainWindow::handleModifyAck(const QJsonObject& object) {
    setBusy(false);
    const int code = object.value("resCode").toInt(-1);
    const QString msg = object.value("resMsg").toString();
    appendLog(QString("modifyNetCard 响应：resCode=%1 resMsg=%2").arg(code).arg(msg));
    if (code == 0) {
        QMessageBox::information(this, "已发送", "设备已响应成功。请等待网络切换后用新 IP 重新搜索。");
    } else {
        QMessageBox::warning(this, "修改失败", QString("resCode=%1\n%2").arg(code).arg(msg));
    }
}

void MainWindow::renderDevices() {
    m_deviceTable->setRowCount(0);
    for (const auto& record : m_devices) {
        const int row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        const auto wan = mainCard(record.payload);
        const auto lan = subCard(record.payload);
        const QStringList values{
            record.peerIp,
            devInfoValue(record.payload, "deviceSn"),
            devInfoValue(record.payload, "deviceType"),
            devInfoValue(record.payload, "softwareVersion"),
            cardSummary(wan),
            cardSummary(lan),
            QDateTime::currentDateTime().toString("HH:mm:ss"),
        };
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values.at(col));
            item->setData(Qt::UserRole, record.key);
            m_deviceTable->setItem(row, col, item);
        }
    }
}

void MainWindow::selectDevice() {
    const auto selected = m_deviceTable->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    m_selectedKey = selected.first()->data(Qt::UserRole).toString();
    if (!m_devices.contains(m_selectedKey)) {
        return;
    }
    loadDeviceToForm(m_devices.value(m_selectedKey).payload);
    m_applyButton->setEnabled(true);
}

void MainWindow::loadDeviceToForm(const QJsonObject& payload) {
    const auto wan = mainCard(payload);
    m_selectedLabel->setText(QString("当前设备：%1  SN=%2")
                                 .arg(payload.value("_peerIp").toString(), devInfoValue(payload, "deviceSn")));
    m_dhcpCheck->setChecked(wan.value("dhcp").toInt() != 0);
    m_ipEdit->setText(wan.value("ipAddr").toString());
    m_maskEdit->setText(wan.value("netMask").toString());
    m_gatewayEdit->setText(wan.value("gateway").toString());
    m_dns1Edit->setText(wan.value("dns1").toString());
    m_dns2Edit->setText(wan.value("dns2").toString());
    updateDhcpState();
}

void MainWindow::updateDhcpState() {
    const bool enabled = !m_dhcpCheck->isChecked();
    m_ipEdit->setEnabled(enabled);
    m_maskEdit->setEnabled(enabled);
    m_gatewayEdit->setEnabled(enabled);
}

void MainWindow::applyNetwork() {
    if (!m_devices.contains(m_selectedKey)) {
        QMessageBox::warning(this, "未选择设备", "请先选择设备");
        return;
    }
    if (m_passwordEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "缺少密码", "请输入管理员密码或密码 MD5");
        return;
    }
    QString error;
    if (!validateNetworkForm(&error)) {
        QMessageBox::warning(this, "网络参数错误", error);
        return;
    }
    if (QMessageBox::question(this, "确认修改",
                              "将使用旧搜索协议修改 WAN/eth0。配置错误可能导致设备临时失联，确认继续？") !=
        QMessageBox::Yes) {
        return;
    }

    try {
        const auto record = m_devices.value(m_selectedKey);
        m_pendingReqId = makeReqId();

        const int dhcp = m_dhcpCheck->isChecked() ? 1 : 0;
        QJsonObject netCard{
            {"mainCard", 1},
            {"dhcp", dhcp},
            {"ethName", "eth0"},
            {"ipAddr", dhcp ? QString() : m_ipEdit->text().trimmed()},
            {"netMask", dhcp ? QString() : m_maskEdit->text().trimmed()},
            {"gateway", dhcp ? QString() : m_gatewayEdit->text().trimmed()},
            {"mac", ""},
            {"dns1", m_dns1Edit->text().trimmed()},
            {"dns2", m_dns2Edit->text().trimmed()},
        };
        QJsonObject object{
            {"cmd", "modifyNetCard"},
            {"type", "req"},
            {"reqId", m_pendingReqId},
            {"deviceSn", devInfoValue(record.payload, "deviceSn")},
            {"passwd", passwordToSend()},
            {"netCard", netCard},
        };
        appendLog(QString("发送 modifyNetCard 到 %1").arg(record.peerIp));
        startWorker(UdpCommandWorker::Command::ModifyNetCard, record.peerIp, object);
    } catch (const std::exception& ex) {
        setBusy(false);
        QMessageBox::warning(this, "修改失败", ex.what());
    }
}

void MainWindow::setBusy(bool busy) {
    m_searchButton->setEnabled(!busy);
    m_probeIpButton->setEnabled(!busy);
    m_applyButton->setEnabled(!busy && m_devices.contains(m_selectedKey));
}

void MainWindow::appendLog(const QString& message) {
    m_logText->append(QString("%1  %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), message));
}

QJsonObject MainWindow::mainCard(const QJsonObject& payload) const {
    const auto cards = payload.value("resData").toObject().value("netCardList").toArray();
    for (const auto& value : cards) {
        const auto card = value.toObject();
        if (card.value("mainCard").toInt() != 0) {
            return card;
        }
    }
    return cards.isEmpty() ? QJsonObject{} : cards.first().toObject();
}

QJsonObject MainWindow::subCard(const QJsonObject& payload) const {
    const auto cards = payload.value("resData").toObject().value("netCardList").toArray();
    for (const auto& value : cards) {
        const auto card = value.toObject();
        if (card.value("mainCard").toInt() == 0) {
            return card;
        }
    }
    return {};
}

QString MainWindow::devInfoValue(const QJsonObject& payload, const QString& key) const {
    const auto items = payload.value("resData").toObject().value("devInfoList").toArray();
    for (const auto& value : items) {
        const auto item = value.toObject();
        if (item.value("key").toString() == key) {
            return item.value("value").toString();
        }
    }
    return {};
}

QString MainWindow::deviceKey(const QJsonObject& payload, const QHostAddress& sender) const {
    const QString sn = devInfoValue(payload, "deviceSn");
    return sn.isEmpty() ? sender.toString() : sn;
}

QString MainWindow::passwordToSend() const {
    const QString password = m_passwordEdit->text().trimmed();
    if (!m_passwordMd5Check->isChecked()) {
        return password.toUpper();
    }
    const auto digest = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Md5).toHex();
    return QString::fromLatin1(digest).toUpper();
}

bool MainWindow::validateNetworkForm(QString* error) const {
    if (m_dhcpCheck->isChecked()) {
        return true;
    }
    const QString ip = m_ipEdit->text().trimmed();
    const QString mask = m_maskEdit->text().trimmed();
    const QString gateway = m_gatewayEdit->text().trimmed();
    const QString dns1 = m_dns1Edit->text().trimmed();
    const QString dns2 = m_dns2Edit->text().trimmed();
    if (!isIpv4Address(ip)) {
        *error = "IP 不是合法 IPv4 地址";
        return false;
    }
    if (!isIpv4Address(mask)) {
        *error = "子网掩码不是合法 IPv4 地址";
        return false;
    }
    if (!gateway.isEmpty() && !isIpv4Address(gateway)) {
        *error = "网关不是合法 IPv4 地址";
        return false;
    }
    if (!sameSubnet(ip, mask, gateway)) {
        *error = "网关必须和 IP 在同一网段";
        return false;
    }
    if (!dns1.isEmpty() && !isIpv4Address(dns1)) {
        *error = "DNS1 不是合法 IPv4 地址";
        return false;
    }
    if (!dns2.isEmpty() && !isIpv4Address(dns2)) {
        *error = "DNS2 不是合法 IPv4 地址";
        return false;
    }
    return true;
}
