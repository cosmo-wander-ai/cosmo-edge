#!/usr/bin/env python3
"""Compile the real watchdog/worker with wrapped syscalls; never access hardware.

Run on Linux with a C++17 compiler. --smoke reproduces the RKNN no-op regression
without requiring the subsequent error-handling improvements.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    variants = {
        "rknn": (["COSMO_NN_USE_RKNN_BACKEND"], "enabled"),
        "sophon": (["COSMO_NN_USE_SOPHON_BACKEND"], "enabled"),
        "cpu": (["COSMO_NN_USE_CPU_BACKEND"], "disabled"),
        "rknn-dev": (["COSMO_NN_USE_RKNN_BACKEND", "COSMO_DEV_MODE"], "disabled"),
        "sophon-dev": (["COSMO_NN_USE_SOPHON_BACKEND", "COSMO_DEV_MODE"], "disabled"),
    }
    with tempfile.TemporaryDirectory(prefix="watchdog-test-") as directory:
        for name, (defines, mode) in variants.items():
            binary = Path(directory) / name
            command = [os.environ.get("CXX", "g++"), "-std=c++17", "-pthread"]
            command += [f"-D{define}" for define in defines]
            command += ["-I", str(root / "test/watchdog/stubs"), "-I", str(root / "src")]
            command += [str(root / source) for source in (
                "src/platform/WatchDog.cc", "src/util/Thread.cc",
                "src/util/ThreadRegistry.cc", "test/watchdog/WatchDogStandaloneTest.cc",
            )]
            command += [f"-Wl,--wrap={call}" for call in ("open", "ioctl", "write", "close")]
            command += ["-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary), mode] + (["smoke"] if args.smoke else []), check=True)
            print(f"PASS: {name}", flush=True)


if __name__ == "__main__":
    main()
