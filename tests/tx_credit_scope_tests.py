"""v0.7.1.9 new-improvement isolation guard. Run from any working directory."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "d5d61aa0c2d864162615589fc93171252c5a6305"
ALLOWED = {
    "src/driver/driver.c",
    "src/driver/driver.h",
    "src/cyw43455/network.c",
    "src/cyw43455/tx_types.h",
    "src/cyw43455/tx_queue.h",
}

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def main():
    git("merge-base", "--is-ancestor", BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", BASELINE, "src").splitlines()
    for name in files:
        if name in ALLOWED:
            continue
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", BASELINE + ":" + name)
        assert actual == before, "Protected v0.7.1.8 source changed: " + name

    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production surface: " + str(current ^ set(files))

    driver_h = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in driver_h
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in driver_h

    types = (ROOT / "src/cyw43455/tx_types.h").read_text()
    assert "CYW_PENDING_SEND Entries[CYW_TX_LIMIT]" in types
    assert "CYW_PENDING_SEND Backlog[CYW_TX_BACKLOG_LIMIT]" in types

    queue = (ROOT / "src/cyw43455/tx_queue.h").read_text()
    assert "CywTxSubmitWithBacklog" in queue
    assert "if(!Q->BacklogCount && CywTxActiveFits" in queue
    assert "A->TxQueueFull++;A->TxBacklogFull++" in queue
    assert "CywTxPromoteOneLocked" in queue
    assert "Q->Outstanding+Q->BacklogCount+Q->Completing" in queue

    network = (ROOT / "src/cyw43455/network.c").read_text()
    assert "status=CywTxSubmitWithBacklog(A,&N->Sends,Nbl);" in network
    assert "if(CywTxRetryEligible(A))sentBefore=0;" in network
    assert "retryMs=CywTxRetrySelect(&A->TxRetry,KeQueryInterruptTime()," in network
    assert "Status=CywTxCreditPostReceivePump(A,&N->Sends,&sentAfter);" in network

    retry = (ROOT / "src/cyw43455/tx_retry.h").read_text()
    assert "#define CYW_TX_RETRY_FAST_LIMIT 4u" in retry
    pump = (ROOT / "src/cyw43455/tx_credit_pump.h").read_text()
    assert pump.count("CywTxPostReceivePump(A,Q,Sent)") == 1
    assert "CywMeasuredTxPump(" not in pump and "for(" not in pump and "while(" not in pump

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/04/2026,0\.7\.1\.9\s*$", inf)

    project = (ROOT / "rpi5-cyw43455.vcxproj").read_text()
    assert '<Rpi5TxCreditScheduling Condition="\'$(Rpi5TxCreditScheduling)\'==\'\'">1</Rpi5TxCreditScheduling>' in project
    package = (ROOT / "scripts/package-tx-credit.ps1").read_text()
    assert "[ValidateSet('0','1')][string]$TxCreditScheduling='1'" in package
    print("PASS: v0.7.1.9 changes are limited to bounded TX backlog/diagnostics; 64-frame active queue, mode-1 credit wake, RX, SDIO, firmware and radio paths remain protected.")

if __name__ == "__main__":
    main()
