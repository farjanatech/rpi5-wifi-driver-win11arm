"""Fault-inject wire events into the actual network.c event handler."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cl")
    parser.add_argument("--asan", action="store_true")
    parser.add_argument("--out", type=Path, default=ROOT / "ci-logs/link-events")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/cyw43455/network.c").read_text()
    start = source.index("static VOID CywEvent(")
    end = source.index("static VOID CywReceive(", start)
    (out / "link_event.inc").write_text(source[start:end])
    exe = out / "link-event-tests.exe"
    flags = ["/O1", "/Zi", "/fsanitize=address"] if args.asan else []
    subprocess.run([args.cc, "/nologo", "/W4", "/WX", "/TC", "/std:c17",
                    *flags, f"/I{out}", str(ROOT / "tests/link_event_tests.c"),
                    f"/Fe:{exe}", f"/Fo:{out / 'link-event-tests.obj'}"], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
