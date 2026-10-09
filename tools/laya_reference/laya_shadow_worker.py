"""Framing helper for offline reference manifests.

The historical filename is retained for manifest compatibility. There is no
model backend, socket listener, CLI, or deployable worker in this module.
"""
import socket
import time


def read_exact(conn, count, deadline=None):
    out = bytearray()
    while len(out) < count:
        if deadline is not None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise socket.timeout('request_deadline_exceeded')
            conn.settimeout(remaining)
        part = conn.recv(count - len(out))
        if not part:
            raise ValueError('truncated_message')
        out.extend(part)
    return bytes(out)
