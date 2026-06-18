#!/usr/bin/env python3
"""
Search devices that implement the SearchMain multicast probe protocol.

The device service listens on 239.255.0.0:46000 and responds to:
    {"cmd":"probe","type":"req","reqId":"..."}

Run from a machine on the same L2 network as the device:
    python3 tools/device_search.py
    python3 tools/device_search.py --interface-ip 192.168.0.99 --timeout 5
"""

from __future__ import annotations

import argparse
import json
import select
import socket
import sys
import time
import uuid
from typing import Any


DEFAULT_GROUP = "239.255.0.0"
DEFAULT_PORT = 46000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Search aibox devices by multicast probe.")
    parser.add_argument("--group", default=DEFAULT_GROUP, help=f"multicast group, default {DEFAULT_GROUP}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"UDP port, default {DEFAULT_PORT}")
    parser.add_argument("--timeout", type=float, default=3.0, help="receive timeout in seconds")
    parser.add_argument("--repeat", type=int, default=3, help="number of probe packets to send")
    parser.add_argument(
        "--interval", type=float, default=0.3, help="interval between repeated probes in seconds"
    )
    parser.add_argument(
        "--interface-ip",
        default="0.0.0.0",
        help="local interface IPv4 for multicast membership/outgoing packets",
    )
    parser.add_argument("--json", action="store_true", help="print raw discovered devices as JSON")
    return parser.parse_args()


def create_socket(group: str, port: int, interface_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass

    try:
        sock.bind(("", port))
    except OSError as exc:
        raise SystemExit(
            f"bind UDP port {port} failed: {exc}. "
            "Close other search tools/services on this host, or run from another machine."
        ) from exc

    membership = socket.inet_aton(group) + socket.inet_aton(interface_ip)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)

    if interface_ip != "0.0.0.0":
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(interface_ip))

    sock.setblocking(False)
    return sock


def device_key(peer_ip: str, payload: dict[str, Any]) -> str:
    dev_info = payload.get("resData", {}).get("devInfoList", [])
    sn = ""
    for item in dev_info:
        if item.get("key") == "deviceSn":
            sn = item.get("value", "")
            break
    return sn or peer_ip


def send_probe(sock: socket.socket, group: str, port: int, req_id: str) -> None:
    payload = {"cmd": "probe", "type": "req", "reqId": req_id}
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    sock.sendto(data, (group, port))


def receive_devices(sock: socket.socket, timeout: float, req_id: str) -> dict[str, dict[str, Any]]:
    devices: dict[str, dict[str, Any]] = {}
    deadline = time.monotonic() + timeout
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

        if payload.get("cmd") != "probe" or payload.get("type") != "ack":
            continue
        if payload.get("reqId") != req_id:
            continue

        peer_ip = addr[0]
        payload["_peerIp"] = peer_ip
        devices[device_key(peer_ip, payload)] = payload

    return devices


def value_from_dev_info(payload: dict[str, Any], key: str) -> str:
    for item in payload.get("resData", {}).get("devInfoList", []):
        if item.get("key") == key:
            return str(item.get("value", ""))
    return ""


def print_devices(devices: dict[str, dict[str, Any]]) -> None:
    if not devices:
        print("No devices found.")
        return

    for index, payload in enumerate(devices.values(), 1):
        peer_ip = payload.get("_peerIp", "")
        model = value_from_dev_info(payload, "deviceType")
        version = value_from_dev_info(payload, "softwareVersion")
        sn = value_from_dev_info(payload, "deviceSn")
        print(f"[{index}] ip={peer_ip} sn={sn or '-'} model={model or '-'} version={version or '-'}")

        for card in payload.get("resData", {}).get("netCardList", []):
            role = "main" if card.get("mainCard") else "sub"
            dhcp = "dhcp" if card.get("dhcp") else "static"
            eth = card.get("ethName", "")
            ip = card.get("ipAddr", "")
            mask = card.get("netMask", "")
            gateway = card.get("gateway", "")
            mac = card.get("mac", "")
            print(f"    {role}: {eth} {dhcp} ip={ip or '-'} mask={mask or '-'} gw={gateway or '-'} mac={mac or '-'}")


def main() -> int:
    args = parse_args()
    req_id = uuid.uuid4().hex

    with create_socket(args.group, args.port, args.interface_ip) as sock:
        for index in range(max(1, args.repeat)):
            send_probe(sock, args.group, args.port, req_id)
            if index + 1 < args.repeat:
                time.sleep(args.interval)

        devices = receive_devices(sock, args.timeout, req_id)

    if args.json:
        print(json.dumps(list(devices.values()), ensure_ascii=False, indent=2))
    else:
        print_devices(devices)

    return 0 if devices else 1


if __name__ == "__main__":
    sys.exit(main())
