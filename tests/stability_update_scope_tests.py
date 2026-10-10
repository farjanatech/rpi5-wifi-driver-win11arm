"""Protect the working Damian baseline outside the reviewed stability changes."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "c8dee764d3f530cb58e1c359914a8910893761d3"
CHANGED = {
    "src/driver/driver.c", "src/driver/driver.h", "src/cyw43455/network.c",
    "src/cyw43455/tx_queue.h", "src/cyw43455/tx_credit_runtime.h",
    "src/sdio/sdio.c", "src/sdio/sdio.h", "src/cyw43455/stability_diag.h",
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
    # The .24 candidate changes only event authorization/classification from
    # .23. Preserve every transport/lifecycle/timing change while isolating it.
    previous = "a35bbc5644a8bdb8e9e130adb5daf3bd1f0826f8"
    for name in git("ls-tree", "-r", "--name-only", previous, "src").splitlines():
        if name == "src/cyw43455/stability_diag.h":
            continue
        actual, old = read(name), git("show", previous + ":" + name)
        if name == "src/cyw43455/network.c":
            def without_event(text):
                start = text.index("static VOID CywEvent(")
                end = text.index("static VOID CywReceive(", start)
                return text[:start] + text[end:]
            actual, old = without_event(actual), without_event(old)
        assert actual == old, "Unexpected change outside link-event fix: " + name
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
    assert 'DriverVer=10/10/2026,0.7.1.24' in read("package/rpi5cyw.inf").replace(" ", "")
    print("PASS: scoped link-event fix; transport, lifecycle, radio, queue limits and build policy preserved from .23.")

if __name__ == "__main__":
    verify_kernel_scope()
