import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def main(program):
    """Odbiera powiadomienia z aplikacji na lokalnym gnieździe testowym."""
    with tempfile.TemporaryDirectory() as directory, socket.socket(
        socket.AF_UNIX, socket.SOCK_DGRAM
    ) as listener:
        workspace = Path(directory)
        proc_root = workspace / "proc"
        thermal_root = workspace / "thermal"
        thermal_zone = thermal_root / "thermal_zone0"
        proc_root.mkdir()
        thermal_zone.mkdir(parents=True)
        (proc_root / "uptime").write_text("123.45 0.00\n")
        (proc_root / "meminfo").write_text("MemTotal: 1000 kB\nMemAvailable: 600 kB\n")
        (thermal_zone / "type").write_text("cpu-thermal\n")
        (thermal_zone / "temp").write_text("60000\n")
        config = workspace / "config"
        config.write_text("interval_sec=1\n")
        status = workspace / "status"
        address = str(workspace / "notify")
        listener.bind(address)
        listener.settimeout(4)
        environment = dict(os.environ, NOTIFY_SOCKET=address, WATCHDOG_USEC="6000000")
        environment.pop("WATCHDOG_PID", None)
        command = [
            program, "--config", str(config), "--status-file", str(status),
            "--proc-root", str(proc_root), "--thermal-root", str(thermal_root),
        ]

        with subprocess.Popen(command, env=environment, stdout=subprocess.DEVNULL) as monitor:
            try:
                ready = listener.recv(4096).decode().splitlines()
                assert set(ready) == {"READY=1", "WATCHDOG=1"}, ready
                assert json.loads(status.read_text())["state"] == "NORMAL"
                # Utrata pliku metryki daje ERROR, a pętla wciąż działa.
                (proc_root / "meminfo").unlink()
                for _ in range(2):
                    assert listener.recv(4096) == b"WATCHDOG=1"
                assert json.loads(status.read_text())["state"] == "ERROR"
            finally:
                monitor.terminate()
                monitor.wait(timeout=4)
            assert monitor.returncode == 0

        status.unlink()
        config.write_text("interval_sec=4\n")
        rejected = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=4)
        assert rejected.returncode == 1
        assert "połowę limitu watchdoga" in rejected.stderr, rejected.stderr
        assert not status.exists()


if __name__ == "__main__":
    main(sys.argv[1])
