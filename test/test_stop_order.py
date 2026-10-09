"""Run the real stop script with shell-function process mocks; never signal a real PID."""
import pathlib
import subprocess
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "stop.sh"
HARNESS = r'''
set -u
engine=1
srs=1
nginx=1
term=0
ticks=0
bad_order=0
kill() {
    case "$2" in
        910001)
            if [ "$1" = -15 ]; then term=1; fi
            if [ "$1" = -9 ] && [ "$MODE" != stuck ]; then engine=0; fi ;;
        910002) if [ "$engine" = 1 ]; then bad_order=1; fi; srs=0 ;;
        910003) if [ "$engine" = 1 ]; then bad_order=1; fi; nginx=0 ;;
        *) echo 'unexpected PID in mock' >&2; exit 90 ;;
    esac
}
pidof() {
    case "$1" in
        cosmo-engine) [ "$engine" = 1 ] && printf '910001\n' ;;
        srs) [ "$srs" = 1 ] && printf '910002\n' ;;
        nginx) [ "$nginx" = 1 ] && printf '910003\n' ;;
        *) return 1 ;;
    esac
}
sleep() {
    ticks=$((ticks + 1))
    if [ "$MODE" = graceful ] && [ "$term" = 1 ] && [ "$ticks" -ge 2 ]; then engine=0; fi
}
verify() {
    result=$?
    trap - EXIT
    if [ "$bad_order" != 0 ]; then echo 'FAIL: dependencies stopped before engine exited'; exit 91; fi
    if [ "$MODE" = stuck ]; then
        if [ "$result" = 0 ] || [ "$srs" != 1 ] || [ "$nginx" != 1 ]; then exit 92; fi
    elif [ "$result" != 0 ] || [ "$engine" != 0 ] || [ "$srs" != 0 ] || [ "$nginx" != 0 ]; then
        exit 93
    fi
    exit 0
}
trap verify EXIT
export COSMO_STOP_TIMEOUT_SECONDS=2
source "$1"
'''


class StopOrderTests(unittest.TestCase):
    def test_engine_drains_before_dependencies_stop(self):
        for mode in ("graceful", "forced", "stuck"):
            with self.subTest(mode=mode):
                result = subprocess.run(
                    ["bash", "-c", "MODE=" + mode + "\n" + HARNESS, "stop-test", str(SCRIPT)],
                    capture_output=True, text=True, timeout=5,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
