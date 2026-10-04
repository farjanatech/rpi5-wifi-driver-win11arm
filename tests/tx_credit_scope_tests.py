"""v0.7.1.10 two-frame TX-glom isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "0bc5ed12e9438cd82555d7bc8ae77211aa6f236b"
ALLOWED = {
    "src/driver/driver.c",
    "src/driver/driver.h",
    "src/cyw43455/network.c",
    "src/cyw43455/transport_send.h",
    "src/cyw43455/tx_queue.h",
    "src/cyw43455/tx_types.h",
}
NEW = {"src/cyw43455/tx_glom2_config.h"}

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
        assert actual == before, "Protected v0.7.1.9 source changed: " + name

    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current - set(files) == NEW, "Unexpected new production surface: " + str(current - set(files))

    driver_h = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in driver_h
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in driver_h
    assert "#define RPI5CYW_TX_GLOM2 0" in driver_h

    types = (ROOT / "src/cyw43455/tx_types.h").read_text()
    assert "CYW_PENDING_SEND Entries[CYW_TX_LIMIT]" in types
    assert "CYW_PENDING_SEND Backlog[CYW_TX_BACKLOG_LIMIT]" in types
    assert "UCHAR Frame2[1518]" in types

    queue = (ROOT / "src/cyw43455/tx_queue.h").read_text()
    # v0.7.1.9 backlog remains intact.
    for required in (
        "CywTxSubmitWithBacklog",
        "if(!Q->BacklogCount && CywTxActiveFits",
        "A->TxQueueFull++;A->TxBacklogFull++",
        "CywTxPromoteOneLocked",
        "Q->Outstanding+Q->BacklogCount+Q->Completing",
    ):
        assert required in queue, "Backlog invariant lost: " + required
    # Glom is pressure-only, exactly two independent one-frame NBLs.
    assert "static BOOLEAN CywTxPairCandidate" in queue
    assert "Remaining<2" in queue
    assert "(Q->BacklogCount!=0 || Q->Frames>=32)" in queue
    assert queue.count("Q->Entries[0].Frames==1") == 1
    assert queue.count("Q->Entries[1].Frames==1") == 1
    assert "CywTxTransferPair(A,Q->Frame,len+4,Q->Frame2,len2+4)" in queue
    assert "if(status!=STATUS_DEVICE_BUSY)" in queue
    assert "A->TxPackets+=2;*Sent+=2;" in queue

    network = (ROOT / "src/cyw43455/network.c").read_text()
    for required in (
        "status=CywTxSubmitWithBacklog(A,&N->Sends,Nbl);",
        "if(CywTxRetryEligible(A))sentBefore=0;",
        "retryMs=CywTxRetrySelect(&A->TxRetry,KeQueryInterruptTime(),",
        "Status=CywTxCreditPostReceivePump(A,&N->Sends,&sentAfter);",
        "TRY(CywConfigureRxAggregation(A));",
        "TRY(CywConfigureTxGlom2(A));",
    ):
        assert required in network, "v0.7.1.9 invariant lost: " + required
    assert network.index("TRY(CywConfigureRxAggregation(A));") < network.index("TRY(CywConfigureTxGlom2(A));")

    config = (ROOT / "src/cyw43455/tx_glom2_config.h").read_text()
    assert 'CywInt(A,"bus:rxglom",1)' in config
    assert "A->FirmwareError==0xffffffe9UL" in config
    assert "return status;" in config

    sender = (ROOT / "src/cyw43455/transport_send.h").read_text()
    assert sender.count("static NTSTATUS CywSendDataPair") == 1
    assert "CywTransportService(A,FALSE)" in sender
    assert "CywTxCredit((UCHAR)(N->TxSeq+1),N->TxMax,0)" in sender
    assert "CywPut16(N->Tx,(USHORT)padded)" in sender
    assert "word=(logical2-4)|(1u<<24)" in sender
    assert "N->TxSeq=(UCHAR)(N->TxSeq+2)" in sender
    assert sender.count("SdioFifoTransfer(A,N->Tx,padded,TRUE)") >= 2

    # Exact stable retry/scheduler path stays unchanged from v0.7.1.9.
    retry = (ROOT / "src/cyw43455/tx_retry.h").read_text()
    assert "#define CYW_TX_RETRY_FAST_LIMIT 4u" in retry
    pump = (ROOT / "src/cyw43455/tx_credit_pump.h").read_text()
    assert pump.count("CywTxPostReceivePump(A,Q,Sent)") == 1
    assert "CywMeasuredTxPump(" not in pump and "for(" not in pump and "while(" not in pump

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/04/2026,0\.7\.1\.10\s*$", inf)

    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.10'" in installer

    project = (ROOT / "rpi5-cyw43455.vcxproj").read_text()
    assert '<Rpi5TxCreditScheduling Condition="\'$(Rpi5TxCreditScheduling)\'==\'\'">1</Rpi5TxCreditScheduling>' in project
    assert '<Rpi5TxGlom2 Condition="\'$(Rpi5TxGlom2)\'==\'\'">1</Rpi5TxGlom2>' in project
    assert "RPI5CYW_TX_GLOM2=$(Rpi5TxGlom2)" in project

    print("PASS: v0.7.1.10 is isolated to negotiated pressure-only two-frame host TX glom; v0.7.1.9 backlog, RX, SDIO, firmware, radio and lifecycle surfaces remain protected.")

if __name__ == "__main__":
    main()
