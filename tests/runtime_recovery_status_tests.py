"""Execute network.c's actual recovery callback with fault-injected lifecycle calls."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cl")
    parser.add_argument("--out", type=Path, default=ROOT / "ci-logs/recovery-status")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/cyw43455/network.c").read_text()
    start = source.index("typedef struct _CYW_RUNTIME_RECOVERY_WORK {")
    end = source.index("static BOOLEAN CywQueueRuntimeRecovery(", start)
    (out / "runtime_recovery_work.inc").write_text(source[start:end])
    exe = out / "recovery-status-tests.exe"
    subprocess.run([args.cc, "/nologo", "/W4", "/WX", "/TC", "/std:c17",
                    f"/I{out}", str(ROOT / "tests/runtime_recovery_status_tests.c"),
                    f"/Fe:{exe}", f"/Fo:{out / 'recovery-status-tests.obj'}"], check=True)
    subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
