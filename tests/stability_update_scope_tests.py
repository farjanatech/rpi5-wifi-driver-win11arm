"""Protect the working Damian baseline outside the reviewed stability changes."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "c8dee764d3f530cb58e1c359914a8910893761d3"
CHANGED = {
    "src/driver/driver.c", "src/driver/driver.h", "src/cyw43455/network.c",
    "src/cyw43455/tx_queue.h", "src/cyw43455/tx_credit_runtime.h",
    "src/sdio/sdio.c", "src/sdio/sdio.h",
}
ADDED = {
    "src/driver/diag_snapshot.h", "src/driver/diag_mailbox.h",
    "src/driver/diag_values.h", "src/cyw43455/diagnostics_worker.h",
}

def read(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def verify_kernel_scope():
    files = git("ls-tree", "-r", "--name-only", BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files) | ADDED, "Unexpected kernel file set"
    for name in files + ["rpi5-cyw43455.vcxproj", "rpi5-cyw43455.sln", "packages.config", "scripts/fetch-firmware.ps1"]:
        if name not in CHANGED:
            assert read(name) == git("show", BASELINE + ":" + name), "Protected source changed: " + name
    # Protect all old exported values while moving their serialization. The
    # schema adds exporter health and warm-card-reset outcomes.
    old = git("show", BASELINE + ":src/driver/driver.c")
    body = old[old.index("#define SET_DWORD"):old.index("    ZwClose(KeyHandle);", old.index("#define SET_DWORD"))]
    body = re.sub(r"ZwSetValueKey\(KeyHandle,\s*&ValueName,\s*0,\s*", "CywDiagAppend(Buffer, &ValueName, ", body)
    body = body.replace('SET_DWORD(L"DiagVersion", 45);', 'SET_DWORD(L"DiagVersion", 47);')
    actual = read("src/driver/diag_values.h")
    actual = actual[actual.index("#define SET_DWORD"):actual.rindex("}")]
    actual = re.sub(r'    SET_DWORD\(L"Diagnostics(?:AsyncEnabled|AsyncStatus|CaptureSkipped|CaptureOverflow)"[^\n]*\n', '', actual)
    actual = re.sub(r'    SET_DWORD\(L"WarmCardReset(?:Attempts|Status)"[^\n]*\n', "", actual)
    assert actual == body, "Existing diagnostics schema changed"
    network = read("src/cyw43455/network.c")
    capture = network.split("static VOID CywMeasuredDiagnostics", 1)[1].split("static NTSTATUS CywMeasuredTxPump", 1)[0]
    for forbidden in ("Zw", "KeWait", "ExAllocate", "Rpi5CywWriteDiagnostics("):
        assert forbidden not in capture, "Blocking runtime diagnostics: " + forbidden
    assert "CywDiagnosticsStop(N->Diagnostics)" in network
    assert 'DriverVer=10/10/2026,0.7.1.23' in read("package/rpi5cyw.inf").replace(" ", "")
    print("PASS: scoped diagnostics, FIFO refill and warm recovery changes; FIFO no-replay, radio, queue limits and build policy preserved.")

if __name__ == "__main__":
    verify_kernel_scope()
