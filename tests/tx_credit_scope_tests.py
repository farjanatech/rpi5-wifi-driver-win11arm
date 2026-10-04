"""v0.7.1.11 host-TX glom framing-fix isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "a8eb3a6ff480821be885faf398e25857a248dc8f"  # failed v0.7.1.10 evidence
ALLOWED = {
    "src/driver/driver.c",
    "src/driver/driver.h",
    "src/cyw43455/network.c",
    "src/cyw43455/transport_send.h",
    "src/cyw43455/tx_glom2_config.h",
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
        assert actual == before, "Protected v0.7.1.10 source changed: " + name

    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production surface: " + str(current ^ set(files))

    driver_h = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in driver_h
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in driver_h
    assert "#define RPI5CYW_TX_GLOM2 0" in driver_h
    assert "TxGlomExtendedDataSingles" in driver_h
    assert "TxGlomExtendedControlSingles" in driver_h

    # Pair selection/ownership must remain byte-for-byte the failed candidate's
    # already-green host implementation. This fix is framing/lifecycle only.
    queue = (ROOT / "src/cyw43455/tx_queue.h").read_text()
    assert queue == git("show", BASELINE + ":src/cyw43455/tx_queue.h")

    sender = (ROOT / "src/cyw43455/transport_send.h").read_text()
    assert "ULONG header=A->TxGlomEnabled?20u:12u;" in sender
    assert "word=(total-4)|(1u<<24)" in sender
    assert "N->Tx[12]=N->TxSeq;N->Tx[13]=Channel;N->Tx[15]=20;" in sender
    assert "RtlCopyMemory(N->Tx+20,Data,Length);" in sender
    assert "TxGlomExtendedControlSingles" in sender
    assert "TxGlomExtendedDataSingles" in sender
    assert sender.count("static NTSTATUS CywSendDataPair") == 1
    assert "N->TxSeq=(UCHAR)(N->TxSeq+2)" in sender

    config = (ROOT / "src/cyw43455/tx_glom2_config.h").read_text()
    assert "static VOID CywTxGlom2ResetProtocol" in config
    assert 'CywInt(A,"bus:rxglom",1)' in config
    assert "A->FirmwareError==0xffffffe9UL" in config

    network = (ROOT / "src/cyw43455/network.c").read_text()
    assert "CywTxGlom2ResetProtocol(A);" in network
    assert network.index("CywTxGlom2ResetProtocol(A);") < network.index("CywReadFirmwareFile(")
    assert network.index("TRY(CywConfigureRxAggregation(A));") < network.index("TRY(CywConfigureTxGlom2(A));")
    for required in (
        "status=CywTxSubmitWithBacklog(A,&N->Sends,Nbl);",
        "if(CywTxRetryEligible(A))sentBefore=0;",
        "Status=CywTxCreditPostReceivePump(A,&N->Sends,&sentAfter);",
    ):
        assert required in network

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/04/2026,0\.7\.1\.11\s*$", inf)
    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.11'" in installer

    project = (ROOT / "rpi5-cyw43455.vcxproj").read_text()
    assert '<Rpi5TxGlom2 Condition="\'$(Rpi5TxGlom2)\'==\'\'">1</Rpi5TxGlom2>' in project
    assert "RPI5CYW_TX_GLOM2=$(Rpi5TxGlom2)" in project

    print("PASS: v0.7.1.11 changes only post-negotiation TX framing/lifecycle diagnostics; v0.7.1.10 pair selection, v0.7.1.9 backlog, RX, SDIO, firmware and radio surfaces remain protected.")

if __name__ == "__main__":
    main()
