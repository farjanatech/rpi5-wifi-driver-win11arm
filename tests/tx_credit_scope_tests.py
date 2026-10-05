"""v0.7.1.15 bounded TX service-burst isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_BASELINE = "6ae93623c8767eda050b8c408250d3ec3ce19bfb"  # green v0.7.1.11
PACKAGE_BASELINE = "e7de4e26d317bca947171c25967b10201380711a"  # v0.7.1.14-fix2, utility frozen
ALLOWED_SRC = {
    "src/driver/driver.h",
    "src/driver/driver.c",
    "src/cyw43455/network.c",
    "src/cyw43455/transport_send.h",
    "src/cyw43455/tx_queue.h",
}

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def extract(text, start, end):
    a = text.index(start)
    b = text.index(end, a)
    return text[a:b]

def main():
    git("merge-base", "--is-ancestor", RUNTIME_BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", RUNTIME_BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production source surface: " + str(current ^ set(files))

    changed = set()
    for name in files:
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", RUNTIME_BASELINE + ":" + name)
        if actual != before:
            changed.add(name)
        if name not in ALLOWED_SRC:
            assert actual == before, "Protected v0.7.1.11 production source changed: " + name
    assert changed == ALLOWED_SRC, "Unexpected/missing service-burst production changes: " + str(changed ^ ALLOWED_SRC)

    # User explicitly requested no more utility work. Freeze every utility file
    # byte-for-byte at the last v0.7.1.14-fix2 package baseline.
    utility_files = git("ls-tree", "-r", "--name-only", PACKAGE_BASELINE, "utility").splitlines()
    for name in utility_files:
        assert (ROOT / name).read_text(encoding="utf-8") == git("show", PACKAGE_BASELINE + ":" + name),             "Utility changed despite freeze request: " + name

    header = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in header
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in header
    assert "#define RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD 32u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST_MAX 2u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST2 1" in header

    queue = (ROOT / "src/cyw43455/tx_queue.h").read_text()
    assert "Q->Frames>=RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD" in queue
    assert "Q->Frames>=16" not in queue
    assert "serviceSecond" in queue and "CywTxTransferBurstSecond" in queue
    assert "CywTxSecondFramePending" in queue
    assert "Remaining<2" in queue

    sender = (ROOT / "src/cyw43455/transport_send.h").read_text()
    assert "CywSendDataBurstStart" in sender
    assert "CywSendDataBurstSecond" in sender
    assert "WantSecond" in sender
    assert "CywTxCredit((UCHAR)(N->TxSeq+1),N->TxMax,0)" in sender
    assert "TxServiceBurstSavedStatusChecks" in sender
    # Existing two-frame glom wire protocol remains byte-for-byte baseline.
    sender_base = git("show", RUNTIME_BASELINE + ":src/cyw43455/transport_send.h")
    pair_marker = "#if RPI5CYW_TX_GLOM2\n/* Linux brcmfmac host TX glom format"
    assert sender[sender.index(pair_marker):] == sender_base[sender_base.index(pair_marker):],         "Existing two-frame glom sender changed"

    network = (ROOT / "src/cyw43455/network.c").read_text()
    assert "CywTxTransferBurstStart" in network and "CywTxTransferBurstSecond" in network
    assert "CywTxRecordSuccessfulTransfer" in network

    driver = (ROOT / "src/driver/driver.c").read_text()
    assert 'SET_DWORD(L"DiagVersion", 41);' in driver
    for name in (
        "TxGlomPressureThreshold","TxServiceBurstEnabled","TxServiceBurstMax",
        "TxServiceBurstGrants","TxServiceBurstSecondAttempts",
        "TxServiceBurstSecondSuccess","TxServiceBurstSecondBusy",
        "TxServiceBurstSecondErrors","TxServiceBurstSavedStatusChecks",
    ):
        assert name in driver

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.15\s*$", inf)
    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.15'" in installer

    package = (ROOT / "scripts/package-tx-credit.ps1").read_text()
    assert "0.7.1.15 EXPERIMENTAL TX SERVICE-BURST2" in package
    assert "tx_service_burst2=1" in package
    assert "tx_service_burst_max=2" in package
    assert "tx_glom_pressure_threshold=32" in package
    assert "0.7.1.14-fix2-unchanged" in package

    focused = (ROOT / "tests/tx_service_burst2_queue_tests.c").read_text()
    assert "never escapes a pump" in focused
    assert "TxServiceBurstSavedStatusChecks" in focused

    print("PASS: v0.7.1.15 changes only the measured TX service/status amortization path plus diagnostics/versioning; v0.7.1.11 framing, glom wire format, RX, SDIO, firmware/radio and all v0.7.1.14 utility files remain protected.")

if __name__ == "__main__":
    main()
