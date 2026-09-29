#!/usr/bin/env python3
"""Exercise the real TUI on Apple Linux. Requires pyte and readable hwmon."""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import tempfile
import termios
import time

import pyte

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, default=Path(__file__).resolve().parents[1] / "bin/btop")
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--fixture", type=Path, help="Test-only ioctl fixture shared library")
parser.add_argument("--mode", choices=("valid", "expire", "temperature", "off"), default="valid")
parser.add_argument("--quick", action="store_true")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

layouts = [
    (160, 50, False, "cpu mem net proc gpu0"),
    (100, 40, False, "cpu mem net proc gpu0"),
    (100, 40, True, "cpu mem net proc gpu0"),
    (80, 24, False, "cpu mem net proc"),
]
for width, height, bottom, boxes in layouts[:1] if args.quick else layouts:
    with tempfile.TemporaryDirectory(prefix="btop-apple-") as tmp:
        config = Path(tmp) / "btop.conf"
        config.write_text(f'''shown_boxes = "{boxes}"
shown_gpus = "apple"
color_theme = "Default"
update_ms = 500
cpu_bottom = {str(bottom).lower()}
check_temp = true
show_cpu_watts = true
show_battery = true
save_config_on_exit = false
''')
        pid, fd = pty.fork()
        if pid == 0:
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", height, width, 0, 0))
            os.environ.update(TERM="xterm-256color", LANG="C.UTF-8", XDG_STATE_HOME=tmp)
            if args.fixture:
                os.environ.update(LD_PRELOAD=str(args.fixture), BTOP_FIXTURE_MODE=args.mode)
            os.execv(str(args.binary), [str(args.binary), "--config", str(config), "--force-utf"])
        screen = pyte.Screen(width, height)
        stream = pyte.ByteStream(screen)
        raw = bytearray()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    break
                raw.extend(data)
                stream.feed(data)
        rendered = "\n".join(screen.display)
        name = f"{width}x{height}-{'bottom' if bottom else 'top'}"
        (args.output / f"{name}.txt").write_text(rendered)
        (args.output / f"{name}.ansi").write_bytes(raw)
        os.write(fd, b"q")
        deadline = time.monotonic() + 5
        status = None
        while time.monotonic() < deadline:
            child, child_status = os.waitpid(pid, os.WNOHANG)
            if child:
                status = child_status
                break
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    os.read(fd, 65536)
                except OSError:
                    pass
        if status is None:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        os.close(fd)
        assert status == 0, (name, "btop did not exit cleanly", status)
        for text in ("Sys", "Battery", "Charger", "Fan1", "SSD"):
            assert text in rendered, (name, "missing", text)
        if "gpu0" in boxes:
            assert "Apple" in rendered, name
            if not args.fixture:
                assert "live GPU counters unavailable" in rendered or re.search(r"GPU .*\d+%", rendered), name
            elif args.mode == "valid":
                assert re.search(r"GPU .*25%.*47°C", rendered), name
                assert "648 MHz" in rendered and "1.23W" in rendered, name
            elif args.mode == "expire":
                # Expired samples mean an idle GPU; the last temperature is kept.
                assert re.search(r"GPU .* 0%.*47°C", rendered), name
                assert "0 MHz" in rendered and "0.00W" in rendered and "1.23W" not in rendered, name
            elif args.mode == "temperature":
                assert re.search(r"GPU .* 0%.*47°C", rendered), name
                assert "648 MHz" not in rendered and "0.00W" in rendered, name
            elif args.mode == "off":
                assert re.search(r"GPU .* 0%", rendered) and "0.00W" in rendered, name
                assert "1.23W" not in rendered, name
        print(f"PASS {name}: live system watts, component sensors, GPU availability, clean exit")
