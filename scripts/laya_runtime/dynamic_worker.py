#!/usr/bin/env python3
"""Managed worker for typed alarm and ROI-flow visual decisions."""
import argparse
from functools import partial
from pathlib import Path

from managed_worker import serve
from visual_backend import Backend
from visual_protocol import read_request, write_response, failure_response


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", required=True, type=Path)
    p.add_argument("--manifest-sha256", required=True)
    p.add_argument("--cache", required=True, type=Path)
    p.add_argument("--runtime", default="/run/cosmo-visual-decision")
    p.add_argument("--logs", default="/data/cosmo-laya/visual-logs")
    a = p.parse_args()
    serve(a.manifest, a.manifest_sha256, a.runtime, a.logs,
          backend_factory=partial(Backend, cache_directory=a.cache), request_reader=read_request,
          response_writer=write_response, error_response=failure_response)


if __name__ == "__main__":
    main()
