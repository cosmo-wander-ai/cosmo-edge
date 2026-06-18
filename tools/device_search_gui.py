#!/usr/bin/env python3
"""
PySide6 GUI for SearchMain device discovery and legacy WAN IP configuration.

Install dependency on Windows:
    python -m pip install PySide6

Run:
    python tools/device_search_gui.py
"""

from __future__ import annotations

import hashlib
import ipaddress
import json
import platform
import select
import socket
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from typing import Any

try:
    from PySide6.QtCore import QObject, Qt, QThread, Signal
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QComboBox,
        QFormLayout,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QHeaderView,
        QLabel,
        QLineEdit,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QRadioButton,
        QSpinBox,
        QTableWidget,
        QTableWidgetItem,
        QTextEdit,
        QVBoxLayout,
        QWidget,
    )
except ModuleNotFoundError as exc:  # pragma: no cover - exercised by users without PySide6.
    raise SystemExit("PySide6 is not installed. Run: python -m pip install PySide6") from exc


DEFAULT_GROUP = "239.255.0.0"
DEFAULT_PORT = 46000


@dataclass
class LocalInterface:
    name: str
    ip: str
    prefix: str = ""

    @property
    def label(self) -> str:
        suffix = f"/{self.prefix}" if self.prefix else ""
        return f"{self.ip}{suffix}  {self.name}"


def list_local_interfaces() -> list[LocalInterface]:
    if platform.system().lower() == "windows":
        return list_windows_interfaces()
    return list_socket_interfaces()


def list_windows_interfaces() -> list[LocalInterface]:
    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        (
            "Get-NetIPAddress -AddressFamily IPv4 "
            "| Where-Object {$_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*'} "
            "| Select-Object InterfaceAlias,IPAddress,PrefixLength "
            "| ConvertTo-Json"
        ),
    ]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=5, check=False)
        data = json.loads(result.stdout or "[]")
    except Exception:
        return list_socket_interfaces()

    if isinstance(data, dict):
        data = [data]

    interfaces: list[LocalInterface] = []
    for item in data:
        ip = str(item.get("IPAddress", ""))
        if not ip:
            continue
        interfaces.append(
            LocalInterface(
                name=str(item.get("InterfaceAlias", "")),
                ip=ip,
                prefix=str(item.get("PrefixLength", "")),
            )
        )
    return interfaces or list_socket_interfaces()


def list_socket_interfaces() -> list[LocalInterface]:
    ips: set[str] = set()
    try:
        for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(item[4][0])
    except OSError:
        pass
    ips = {ip for ip in ips if not ip.startswith("127.") and not ip.startswith("169.254.")}
    return [LocalInterface(name="local", ip=ip) for ip in sorted(ips)]


def create_search_socket(group: str, port: int, interface_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    sock.bind(("", port))
    membership = socket.inet_aton(group) + socket.inet_aton(interface_ip)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
    if interface_ip != "0.0.0.0":
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(interface_ip))
    sock.setblocking(False)
    return sock


def send_json(sock: socket.socket, target_ip: str, port: int, payload: dict[str, Any]) -> None:
    data = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    sock.sendto(data, (target_ip, port))


def receive_matching(
    sock: socket.socket, timeout: float, req_id: str, cmd: str
) -> list[dict[str, Any]]:
    deadline = time.monotonic() + timeout
    replies: list[dict[str, Any]] = []
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        readable, _, _ = select.select([sock], [], [], remaining)
        if not readable:
            break
        data, addr = sock.recvfrom(65535)
        try:
            payload = json.loads(data.decode("utf-8", errors="replace"))
        except json.JSONDecodeError:
            continue
        if payload.get("cmd") != cmd or payload.get("type") != "ack":
            continue
        if payload.get("reqId") != req_id:
            continue
        payload["_peerIp"] = addr[0]
        replies.append(payload)
    return replies


def device_key(payload: dict[str, Any]) -> str:
    return device_info_value(payload, "deviceSn") or str(payload.get("_peerIp", ""))


def device_info_value(payload: dict[str, Any], key: str) -> str:
    for item in payload.get("resData", {}).get("devInfoList", []):
        if item.get("key") == key:
            return str(item.get("value", ""))
    return ""


def main_card(payload: dict[str, Any]) -> dict[str, Any]:
    cards = payload.get("resData", {}).get("netCardList", [])
    for card in cards:
        if card.get("mainCard"):
            return card
    return cards[0] if cards else {}


def sub_card(payload: dict[str, Any]) -> dict[str, Any]:
    for card in payload.get("resData", {}).get("netCardList", []):
        if not card.get("mainCard"):
            return card
    return {}


def validate_ipv4(value: str, field: str, required: bool = True) -> None:
    if not value:
        if required:
            raise ValueError(f"{field}不能为空")
        return
    try:
        ipaddress.IPv4Address(value)
    except ipaddress.AddressValueError as exc:
        raise ValueError(f"{field}不是合法 IPv4 地址") from exc


def validate_static_network(ip: str, mask: str, gateway: str) -> None:
    validate_ipv4(ip, "IP")
    validate_ipv4(mask, "子网掩码")
    validate_ipv4(gateway, "网关", required=False)
    network = ipaddress.IPv4Network(f"{ip}/{mask}", strict=False)
    if gateway and ipaddress.IPv4Address(gateway) not in network:
        raise ValueError("网关必须和 IP 在同一网段")


class SearchWorker(QObject):
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(self, interface_ip: str, group: str, port: int, timeout: float, target_ip: str = ""):
        super().__init__()
        self.interface_ip = interface_ip
        self.group = group
        self.port = port
        self.timeout = timeout
        self.target_ip = target_ip.strip()

    def run(self) -> None:
        try:
            devices: dict[str, dict[str, Any]] = {}
            req_id = uuid.uuid4().hex
            with create_search_socket(self.group, self.port, self.interface_ip) as sock:
                target = self.target_ip or self.group
                for index in range(3):
                    send_json(sock, target, self.port, {"cmd": "probe", "type": "req", "reqId": req_id})
                    if index < 2:
                        time.sleep(0.3)
                for payload in receive_matching(sock, self.timeout, req_id, "probe"):
                    devices[device_key(payload)] = payload
            self.finished.emit(devices)
        except Exception as exc:
            self.failed.emit(str(exc))


class ModifyWorker(QObject):
    finished = Signal(dict)
    failed = Signal(str)

    def __init__(
        self,
        interface_ip: str,
        group: str,
        port: int,
        timeout: float,
        device_ip: str,
        device_sn: str,
        password: str,
        send_md5: bool,
        net_card: dict[str, Any],
    ):
        super().__init__()
        self.interface_ip = interface_ip
        self.group = group
        self.port = port
        self.timeout = timeout
        self.device_ip = device_ip
        self.device_sn = device_sn
        self.password = password
        self.send_md5 = send_md5
        self.net_card = net_card

    def run(self) -> None:
        try:
            passwd = self.password.strip()
            if self.send_md5:
                passwd = hashlib.md5(passwd.encode("utf-8")).hexdigest().upper()
            req_id = uuid.uuid4().hex
            payload = {
                "cmd": "modifyNetCard",
                "type": "req",
                "reqId": req_id,
                "deviceSn": self.device_sn,
                "passwd": passwd,
                "netCard": self.net_card,
            }
            with create_search_socket(self.group, self.port, self.interface_ip) as sock:
                send_json(sock, self.device_ip, self.port, payload)
                replies = receive_matching(sock, self.timeout, req_id, "modifyNetCard")
            if replies:
                self.finished.emit(replies[0])
            else:
                self.failed.emit("未收到 modifyNetCard 响应。请检查设备日志、Windows 防火墙和 UDP 46000。")
        except Exception as exc:
            self.failed.emit(str(exc))


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("设备搜索与网络配置工具")
        self.resize(1180, 760)
        self.devices: dict[str, dict[str, Any]] = {}
        self.current_device: dict[str, Any] | None = None
        self.worker_thread: QThread | None = None
        self.worker: QObject | None = None

        self.interface_combo = QComboBox()
        self.refresh_interfaces_button = QPushButton("刷新网卡")
        self.search_button = QPushButton("组播搜索")
        self.target_ip_edit = QLineEdit()
        self.target_ip_edit.setPlaceholderText("可选：指定设备 IP 单播探测")
        self.probe_ip_button = QPushButton("指定 IP 探测")
        self.timeout_spin = QSpinBox()
        self.timeout_spin.setRange(1, 30)
        self.timeout_spin.setValue(5)

        self.device_table = QTableWidget(0, 7)
        self.device_table.setHorizontalHeaderLabels(["IP", "SN", "型号", "版本", "WAN", "LAN", "更新时间"])
        self.device_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.device_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.device_table.setEditTriggers(QTableWidget.NoEditTriggers)

        self.selected_label = QLabel("未选择设备")
        self.password_edit = QLineEdit()
        self.password_edit.setEchoMode(QLineEdit.Password)
        self.password_edit.setPlaceholderText("管理员密码或密码 MD5")
        self.password_md5_check = QCheckBox("输入为明文，发送 MD5 大写")
        self.password_md5_check.setChecked(True)

        self.wan_radio = QRadioButton("WAN / eth0")
        self.wan_radio.setChecked(True)
        self.lan_radio = QRadioButton("LAN / eth1（旧协议暂不建议修改）")
        self.lan_radio.setEnabled(False)
        self.dhcp_check = QCheckBox("启用 DHCP")
        self.ip_edit = QLineEdit()
        self.mask_edit = QLineEdit()
        self.gateway_edit = QLineEdit()
        self.dns1_edit = QLineEdit()
        self.dns2_edit = QLineEdit()
        self.apply_button = QPushButton("应用到选中设备")
        self.apply_button.setEnabled(False)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)

        self.build_ui()
        self.connect_signals()
        self.load_interfaces()

    def build_ui(self) -> None:
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)

        top = QHBoxLayout()
        top.addWidget(QLabel("本机网卡"))
        top.addWidget(self.interface_combo, 2)
        top.addWidget(self.refresh_interfaces_button)
        top.addWidget(QLabel("超时(s)"))
        top.addWidget(self.timeout_spin)
        top.addWidget(self.search_button)
        top.addWidget(self.target_ip_edit, 1)
        top.addWidget(self.probe_ip_button)
        layout.addLayout(top)

        layout.addWidget(self.device_table, 3)

        config_group = QGroupBox("网络配置（当前设备端旧协议：仅建议修改 WAN / eth0）")
        grid = QGridLayout(config_group)
        grid.addWidget(self.selected_label, 0, 0, 1, 4)
        grid.addWidget(self.wan_radio, 1, 0)
        grid.addWidget(self.lan_radio, 1, 1)
        grid.addWidget(self.dhcp_check, 1, 2)

        form = QFormLayout()
        form.addRow("IP", self.ip_edit)
        form.addRow("子网掩码", self.mask_edit)
        form.addRow("网关", self.gateway_edit)
        form.addRow("DNS1", self.dns1_edit)
        form.addRow("DNS2", self.dns2_edit)

        auth_form = QFormLayout()
        auth_form.addRow("管理员密码", self.password_edit)
        auth_form.addRow("", self.password_md5_check)
        auth_form.addRow("", self.apply_button)

        grid.addLayout(form, 2, 0, 1, 2)
        grid.addLayout(auth_form, 2, 2, 1, 2)
        layout.addWidget(config_group)
        layout.addWidget(self.log_text, 1)

    def connect_signals(self) -> None:
        self.refresh_interfaces_button.clicked.connect(self.load_interfaces)
        self.search_button.clicked.connect(lambda: self.start_search(""))
        self.probe_ip_button.clicked.connect(lambda: self.start_search(self.target_ip_edit.text()))
        self.device_table.itemSelectionChanged.connect(self.select_current_device)
        self.dhcp_check.toggled.connect(self.update_dhcp_state)
        self.apply_button.clicked.connect(self.apply_network)

    def load_interfaces(self) -> None:
        self.interface_combo.clear()
        interfaces = list_local_interfaces()
        if not interfaces:
            interfaces = [LocalInterface("default", "0.0.0.0")]
        for item in interfaces:
            self.interface_combo.addItem(item.label, item.ip)
        self.log(f"加载本机网卡 {len(interfaces)} 个")

    def selected_interface_ip(self) -> str:
        return str(self.interface_combo.currentData() or "0.0.0.0")

    def start_search(self, target_ip: str) -> None:
        target_ip = target_ip.strip()
        if target_ip:
            try:
                ipaddress.IPv4Address(target_ip)
            except ipaddress.AddressValueError:
                QMessageBox.warning(self, "参数错误", "指定设备 IP 不是合法 IPv4 地址")
                return

        self.set_busy(True)
        self.log(f"开始搜索，网卡={self.selected_interface_ip()} target={target_ip or DEFAULT_GROUP}")
        worker = SearchWorker(
            interface_ip=self.selected_interface_ip(),
            group=DEFAULT_GROUP,
            port=DEFAULT_PORT,
            timeout=float(self.timeout_spin.value()),
            target_ip=target_ip,
        )
        self.run_worker(worker, worker.run, self.on_search_finished)

    def on_search_finished(self, devices: dict[str, dict[str, Any]]) -> None:
        self.set_busy(False)
        self.devices.update(devices)
        self.render_devices()
        self.log(f"搜索完成，新增/更新 {len(devices)} 台设备")

    def render_devices(self) -> None:
        self.device_table.setRowCount(0)
        for payload in self.devices.values():
            row = self.device_table.rowCount()
            self.device_table.insertRow(row)
            wan = main_card(payload)
            lan = sub_card(payload)
            values = [
                payload.get("_peerIp", ""),
                device_info_value(payload, "deviceSn"),
                device_info_value(payload, "deviceType"),
                device_info_value(payload, "softwareVersion"),
                f"{wan.get('ethName', '')} {wan.get('ipAddr', '')}",
                f"{lan.get('ethName', '')} {lan.get('ipAddr', '')}",
                time.strftime("%H:%M:%S"),
            ]
            for col, value in enumerate(values):
                item = QTableWidgetItem(str(value))
                item.setData(Qt.UserRole, device_key(payload))
                self.device_table.setItem(row, col, item)

    def select_current_device(self) -> None:
        items = self.device_table.selectedItems()
        if not items:
            return
        key = items[0].data(Qt.UserRole)
        self.current_device = self.devices.get(key)
        if not self.current_device:
            return
        wan = main_card(self.current_device)
        self.selected_label.setText(
            f"当前设备：{self.current_device.get('_peerIp', '')}  "
            f"SN={device_info_value(self.current_device, 'deviceSn') or '-'}"
        )
        self.dhcp_check.setChecked(bool(wan.get("dhcp")))
        self.ip_edit.setText(str(wan.get("ipAddr", "")))
        self.mask_edit.setText(str(wan.get("netMask", "")))
        self.gateway_edit.setText(str(wan.get("gateway", "")))
        self.dns1_edit.setText(str(wan.get("dns1", "")))
        self.dns2_edit.setText(str(wan.get("dns2", "")))
        self.apply_button.setEnabled(True)
        self.update_dhcp_state()

    def update_dhcp_state(self) -> None:
        enabled = not self.dhcp_check.isChecked()
        self.ip_edit.setEnabled(enabled)
        self.mask_edit.setEnabled(enabled)
        self.gateway_edit.setEnabled(enabled)

    def apply_network(self) -> None:
        if not self.current_device:
            QMessageBox.warning(self, "未选择设备", "请先选择一台设备")
            return
        if not self.password_edit.text().strip():
            QMessageBox.warning(self, "缺少密码", "请输入管理员密码或密码 MD5")
            return

        dhcp = 1 if self.dhcp_check.isChecked() else 0
        if not dhcp:
            try:
                validate_static_network(self.ip_edit.text().strip(), self.mask_edit.text().strip(), self.gateway_edit.text().strip())
                validate_ipv4(self.dns1_edit.text().strip(), "DNS1", required=False)
                validate_ipv4(self.dns2_edit.text().strip(), "DNS2", required=False)
            except ValueError as exc:
                QMessageBox.warning(self, "网络参数错误", str(exc))
                return

        device_ip = str(self.current_device.get("_peerIp", ""))
        device_sn = device_info_value(self.current_device, "deviceSn")
        if not device_ip or not device_sn:
            QMessageBox.warning(self, "设备信息不完整", "缺少设备 IP 或 SN，无法发送修改请求")
            return

        net_card = {
            "mainCard": 1,
            "dhcp": dhcp,
            "ethName": "eth0",
            "ipAddr": "" if dhcp else self.ip_edit.text().strip(),
            "netMask": "" if dhcp else self.mask_edit.text().strip(),
            "gateway": "" if dhcp else self.gateway_edit.text().strip(),
            "mac": "",
            "dns1": self.dns1_edit.text().strip(),
            "dns2": self.dns2_edit.text().strip(),
        }

        if QMessageBox.question(
            self,
            "确认修改",
            "将使用旧搜索协议修改 WAN/eth0。配置错误可能导致设备临时失联，确认继续？",
        ) != QMessageBox.Yes:
            return

        self.set_busy(True)
        self.log(f"发送 modifyNetCard 到 {device_ip}，SN={device_sn}")
        worker = ModifyWorker(
            interface_ip=self.selected_interface_ip(),
            group=DEFAULT_GROUP,
            port=DEFAULT_PORT,
            timeout=float(self.timeout_spin.value()),
            device_ip=device_ip,
            device_sn=device_sn,
            password=self.password_edit.text(),
            send_md5=self.password_md5_check.isChecked(),
            net_card=net_card,
        )
        self.run_worker(worker, worker.run, self.on_modify_finished)

    def on_modify_finished(self, payload: dict[str, Any]) -> None:
        self.set_busy(False)
        code = payload.get("resCode")
        msg = payload.get("resMsg", "")
        self.log(f"modifyNetCard 响应：resCode={code} resMsg={msg}")
        if code == 0:
            QMessageBox.information(self, "已发送", "设备已响应成功。请等待网络切换后用新 IP 重新搜索。")
        else:
            QMessageBox.warning(self, "修改失败", f"resCode={code}\n{msg}")

    def run_worker(self, worker: QObject, method: Any, finished_slot: Any) -> None:
        thread = QThread(self)
        worker.moveToThread(thread)
        thread.started.connect(method)
        worker.finished.connect(finished_slot)
        worker.failed.connect(self.on_worker_failed)
        worker.finished.connect(thread.quit)
        worker.failed.connect(thread.quit)
        worker.finished.connect(worker.deleteLater)
        worker.failed.connect(worker.deleteLater)
        thread.finished.connect(thread.deleteLater)
        thread.finished.connect(lambda: self.set_busy(False))
        self.worker_thread = thread
        self.worker = worker
        thread.start()

    def on_worker_failed(self, message: str) -> None:
        self.set_busy(False)
        self.log(f"操作失败：{message}")
        QMessageBox.warning(self, "操作失败", message)

    def set_busy(self, busy: bool) -> None:
        self.search_button.setEnabled(not busy)
        self.probe_ip_button.setEnabled(not busy)
        self.apply_button.setEnabled((not busy) and self.current_device is not None)

    def log(self, message: str) -> None:
        self.log_text.append(f"{time.strftime('%H:%M:%S')}  {message}")


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
