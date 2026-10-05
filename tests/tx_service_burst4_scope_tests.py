"""v0.7.1.18 Service-Burst4 isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "c9d715760da49ef491b681a1ba5b57e6ef6ef13b"  # green v0.7.1.17 fixed-port
UTILITY_BASELINE = "e7de4e26d317bca947171c25967b10201380711a"  # frozen all-in-one
ALLOWED_SRC = {
    "src/driver/driver.h",
    "src/driver/driver.c",
    "src/cyw43455/transport_send.h",
    "src/cyw43455/tx_queue.h",
    "src/cyw43455/network.c",
}

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def main():
    git("merge-base", "--is-ancestor", BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production source surface: " + str(current ^ set(files))
    changed = set()
    for name in files:
        actual=(ROOT/name).read_text(encoding="utf-8")
        before=git("show", BASELINE+":"+name)
        if actual != before: changed.add(name)
        if name not in ALLOWED_SRC:
            assert actual == before, "Protected v0.7.1.17 production source changed: " + name
    assert changed == ALLOWED_SRC, "Unexpected/missing Burst4 source changes: " + str(changed ^ ALLOWED_SRC)

    # Fixed-port F2 path that repaired v0.7.1.16 is untouchable.
    fifo=(ROOT/"src/sdio/fifo_blocks.h").read_text()
    assert fifo == git("show", BASELINE+":src/sdio/fifo_blocks.h")
    assert "READ_REGISTER_BUFFER_ULONG" not in fifo and "WRITE_REGISTER_BUFFER_ULONG" not in fifo
    assert "SdioRead32(A,SDHCI_BUFFER)" in fifo
    assert "SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+pos))" in fifo

    # RX, firmware, connection, pressure pump, retry and Glom2 are frozen.
    for name in (
        "src/cyw43455/connection.h","src/cyw43455/firmware.c",
        "src/cyw43455/tx_pressure_pump.h","src/cyw43455/tx_credit_pump.h",
        "src/cyw43455/tx_retry_gate.h","src/cyw43455/tx_glom2_config.h",
        "src/sdio/sdio.c",
    ):
        assert (ROOT/name).read_text() == git("show",BASELINE+":"+name), "Protected source changed: "+name

    utility_files=git("ls-tree","-r","--name-only",UTILITY_BASELINE,"utility").splitlines()
    for name in utility_files:
        assert (ROOT/name).read_text(encoding="utf-8")==git("show",UTILITY_BASELINE+":"+name), "Frozen utility changed: "+name

    network=(ROOT/"src/cyw43455/network.c").read_text()
    base_network=git("show",BASELINE+":src/cyw43455/network.c")
    old_wrapper='''#if RPI5CYW_TX_SERVICE_BURST2
static NTSTATUS CywTxTransferBurstStart(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length,
    BOOLEAN WantSecond,BOOLEAN * PermitSecond)
{
    NTSTATUS Status=CywSendDataBurstStart(A,Data,Length,WantSecond,PermitSecond);
    if(NT_SUCCESS(Status))CywTxRecordSuccessfulTransfer(A,Data,Length);
    return Status;
}
static NTSTATUS CywTxTransferBurstSecond(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length)
{
    NTSTATUS Status=CywSendDataBurstSecond(A,Data,Length);
    if(NT_SUCCESS(Status))CywTxRecordSuccessfulTransfer(A,Data,Length);
    return Status;
}
#endif'''
    new_wrapper='''#if RPI5CYW_TX_SERVICE_BURST4
static NTSTATUS CywTxTransferBurstStart(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length,
    ULONG WantFrames,PULONG PermitFollowing)
{
    NTSTATUS Status=CywSendDataBurstStart(A,Data,Length,WantFrames,PermitFollowing);
    if(NT_SUCCESS(Status))CywTxRecordSuccessfulTransfer(A,Data,Length);
    return Status;
}
static NTSTATUS CywTxTransferBurstReuse(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length,ULONG Position)
{
    NTSTATUS Status=CywSendDataBurstReuse(A,Data,Length,Position);
    if(NT_SUCCESS(Status))CywTxRecordSuccessfulTransfer(A,Data,Length);
    return Status;
}
#endif'''
    assert old_wrapper in base_network
    assert network == base_network.replace(old_wrapper,new_wrapper), "network.c changed outside the Burst4 wrapper"

    header=(ROOT/"src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_SERVICE_BURST_MAX 4u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST4 1" in header
    assert "RPI5CYW_TX_SERVICE_BURST2" not in header
    assert "#define RPI5CYW_TX_LIMIT 64u" in header and "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in header

    sender=(ROOT/"src/cyw43455/transport_send.h").read_text()
    assert "WantFrames" in sender and "PermitFollowing" in sender
    assert "ReusePosition" in sender
    assert "CywTransportService(A,FALSE)" in sender
    assert "CywTxCredit(N->TxSeq,N->TxMax,0)" in sender
    assert "RPI5CYW_TX_SERVICE_BURST_MAX" in sender

    queue=(ROOT/"src/cyw43455/tx_queue.h").read_text()
    assert "CywTxBurstPendingFrames" in queue
    assert "serviceRemaining" in queue and "reusePosition" in queue
    assert "CywTxTransferBurstReuse" in queue

    driver=(ROOT/"src/driver/driver.c").read_text()
    assert 'SET_DWORD(L"DiagVersion", 43);' in driver
    for key in ("TxServiceBurstThirdAttempts","TxServiceBurstFourthAttempts","TxServiceBurstGrantedFollowers"):
        assert key in driver

    inf=(ROOT/"package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.18\s*$",inf)
    installer=(ROOT/"installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.18'" in installer
    package=(ROOT/"scripts/package-tx-credit.ps1").read_text()
    assert "0.7.1.18 EXPERIMENTAL SERVICE-BURST4" in package
    assert "Direct experiment baseline: 0.7.1.17 / c9d715760da49ef491b681a1ba5b57e6ef6ef13b" in package
    assert "Service-Burst4 status amortization: ENABLED, hard cap = 4 ordinary F2 frames" in package
    assert "tx_service_burst4=1" in package and "tx_service_burst_max=4" in package
    assert "performance_baseline=c9d715760da49ef491b681a1ba5b57e6ef6ef13b" in package
    assert "fixed_port_pio=v0.7.1.17-unchanged" in package

    print("PASS: v0.7.1.18 changes only bounded TX Service-Burst4 plus diagnostics/versioning; fixed-port F2, RX, firmware, queue sizes, Glom2, retry/pump policy and frozen utilities remain protected.")

if __name__=="__main__":
    main()
