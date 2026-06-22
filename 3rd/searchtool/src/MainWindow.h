#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QMainWindow>
#include <QMap>
#include <QNetworkInterface>
#include <QThread>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QLabel;
class QTimer;

class UdpCommandWorker : public QThread {
    Q_OBJECT

public:
    enum class Command {
        Probe,
        ModifyNetCard,
    };

    UdpCommandWorker(Command command, QString interfaceIp, QString targetIp, QString reqId,
                     QJsonObject payload, int timeoutSeconds, QObject* parent = nullptr);

signals:
    void datagramReceived(QJsonObject object, QString senderIp);
    void commandFailed(QString message);
    void commandFinished();

protected:
    void run() override;

private:
    Command m_command;
    QString m_interfaceIp;
    QString m_targetIp;
    QString m_reqId;
    QJsonObject m_payload;
    int m_timeoutSeconds;
};

struct DeviceRecord {
    QString key;
    QString peerIp;
    QJsonObject payload;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshInterfaces();
    void searchMulticast();
    void probeTargetIp();
    void applyNetwork();
    void selectDevice();
    void updateDhcpState();

private:
    void buildUi();
    void startProbe(const QString& targetIp);
    void startWorker(UdpCommandWorker::Command command, const QString& targetIp, const QJsonObject& payload);
    void handleWorkerDatagram(const QJsonObject& object, const QString& senderIp);
    void handleProbeAck(const QJsonObject& object, const QString& senderIp);
    void handleModifyAck(const QJsonObject& object);
    void renderDevices();
    void loadDeviceToForm(const QJsonObject& payload);
    void setBusy(bool busy);
    void appendLog(const QString& message);

    QString selectedInterfaceIp() const;
    QNetworkInterface selectedInterface() const;
    QJsonObject mainCard(const QJsonObject& payload) const;
    QJsonObject subCard(const QJsonObject& payload) const;
    QString devInfoValue(const QJsonObject& payload, const QString& key) const;
    QString deviceKey(const QJsonObject& payload, const QHostAddress& sender) const;
    QString passwordToSend() const;
    bool validateNetworkForm(QString* error) const;

private:
    const QHostAddress m_group{"239.255.0.0"};

    QString m_pendingReqId;
    UdpCommandWorker::Command m_pendingCommand{UdpCommandWorker::Command::Probe};
    UdpCommandWorker* m_worker{nullptr};

    QMap<QString, DeviceRecord> m_devices;
    QString m_selectedKey;

    QComboBox* m_interfaceCombo{nullptr};
    QPushButton* m_refreshInterfacesButton{nullptr};
    QPushButton* m_searchButton{nullptr};
    QLineEdit* m_targetIpEdit{nullptr};
    QPushButton* m_probeIpButton{nullptr};
    QSpinBox* m_timeoutSpin{nullptr};
    QTableWidget* m_deviceTable{nullptr};

    QLabel* m_selectedLabel{nullptr};
    QLineEdit* m_passwordEdit{nullptr};
    QCheckBox* m_passwordMd5Check{nullptr};
    QRadioButton* m_wanRadio{nullptr};
    QRadioButton* m_lanRadio{nullptr};
    QCheckBox* m_dhcpCheck{nullptr};
    QLineEdit* m_ipEdit{nullptr};
    QLineEdit* m_maskEdit{nullptr};
    QLineEdit* m_gatewayEdit{nullptr};
    QLineEdit* m_dns1Edit{nullptr};
    QLineEdit* m_dns2Edit{nullptr};
    QPushButton* m_applyButton{nullptr};
    QTextEdit* m_logText{nullptr};
};
