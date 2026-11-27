"""Check that a worker exit queued with SIGTERM never causes a restart on Linux."""
import os
import re
import signal
import subprocess
import tempfile
import time
from pathlib import Path


def wait_until(read_value):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        value = read_value()
        if value:
            return value
        time.sleep(0.02)
    raise RuntimeError("Timed out waiting for " + read_value.__name__)


def run(log):
    watchdog = subprocess.Popen(
        ["./watchdog", "-l", str(log), "-w", "probe:daemon", "-w", "peer:daemon"],
        start_new_session=True,
    )

    def find_probe():
        if not log.exists():
            return None
        content = log.read_text()
        match = re.search(r"\[START\s*\] worker=probe pid=(\d+)", content)
        if not match or "worker=peer pid=" not in content:
            return None
        pid = int(match.group(1))
        proc = Path("/proc") / str(pid)
        if (proc / "exe").resolve() == Path("worker").resolve() and re.search(
            rf"^PPid:\s+{watchdog.pid}$", (proc / "status").read_text(), re.M
        ):
            return pid
        return None

    try:
        probe = wait_until(find_probe)
        watchdog.send_signal(signal.SIGSTOP)
        os.waitpid(watchdog.pid, os.WUNTRACED)
        os.kill(probe, signal.SIGKILL)

        def probe_has_exited():
            return re.search(r"^State:\s+Z", Path(f"/proc/{probe}/status").read_text(), re.M)

        wait_until(probe_has_exited)
        watchdog.terminate()
        watchdog.send_signal(signal.SIGCONT)
        if watchdog.wait(timeout=8) != 0:
            raise RuntimeError("Supervisor exited with an error")
        content = log.read_text()
        if ("[RESTART" in content or content.count("[START") != 2 or
                "all child processes stopped and reaped" not in content):
            raise RuntimeError("Unexpected lifecycle after overlapping signals:\n" + content)
        print("[PASS] Pending shutdown takes priority over worker restart")
    finally:
        try:
            os.killpg(watchdog.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        watchdog.wait()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        run(Path(directory) / "watchdog.log")
