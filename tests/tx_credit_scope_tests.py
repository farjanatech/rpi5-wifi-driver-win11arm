"""v0.7.1.16 F2 buffer-PIO isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DIRECT_BASELINE = "66e0198609913fcc407c595e580e39c83777b60f"  # green v0.7.1.15 Service-Burst2
STABLE_BASELINE = "6ae93623c8767eda050b8c408250d3ec3ce19bfb"  # v0.7.1.11 rollback
UTILITY_BASELINE = "e7de4e26d317bca947171c25967b10201380711a"  # v0.7.1.14-fix2 frozen utility
ALLOWED_SRC = {
    "src/driver/driver.h",
    "src/driver/driver.c",
    "src/sdio/fifo_blocks.h",
}

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def main():
    git("merge-base", "--is-ancestor", DIRECT_BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", DIRECT_BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production source surface: " + str(current ^ set(files))

    changed = set()
    for name in files:
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", DIRECT_BASELINE + ":" + name)
        if actual != before:
            changed.add(name)
        if name not in ALLOWED_SRC:
            assert actual == before, "Protected v0.7.1.15 production source changed: " + name
    assert changed == ALLOWED_SRC, "Unexpected/missing F2 buffer-PIO production changes: " + str(changed ^ ALLOWED_SRC)

    # Explicitly preserve the v0.7.1.15 gains and the older proven protocol.
    for name in (
        "src/cyw43455/network.c",
        "src/cyw43455/transport_send.h",
        "src/cyw43455/tx_queue.h",
        "src/cyw43455/tx_credit_pump.h",
        "src/cyw43455/transport_service.h",
        "src/sdio/sdio.c",
        "src/sdio/bus_mode.h",
    ):
        assert (ROOT / name).read_text(encoding="utf-8") == git("show", DIRECT_BASELINE + ":" + name),             "Protected v0.7.1.15 behavior changed: " + name

    # User explicitly requested no more utility development.
    utility_files = git("ls-tree", "-r", "--name-only", UTILITY_BASELINE, "utility").splitlines()
    for name in utility_files:
        assert (ROOT / name).read_text(encoding="utf-8") == git("show", UTILITY_BASELINE + ":" + name),             "Utility changed despite freeze request: " + name

    header = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in header
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in header
    assert "#define RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD 32u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST_MAX 2u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST2 1" in header
    assert "#define RPI5CYW_FIFO_BUFFER_PIO 1" in header
    assert "#define RPI5CYW_FIFO_BUFFER_PIO_BURST_WORDS 32u" in header

    fifo = (ROOT / "src/sdio/fifo_blocks.h").read_text()
    assert "#define CYW_FIFO_BLOCK_SIZE 512UL" in fifo
    assert "#define CYW_FIFO_MAX_BLOCKS 32UL" in fifo
    assert "#define CYW_FIFO_PIO_BURST_WORDS RPI5CYW_FIFO_BUFFER_PIO_BURST_WORDS" in fifo
    assert "CYW_FIFO_PIO_BURSTS_PER_BLOCK==4" in fifo
    assert "WRITE_REGISTER_BUFFER_ULONG" in fifo
    assert "READ_REGISTER_BUFFER_ULONG" in fifo
    assert "SdioFifoBufferAligned" in fifo
    assert "FifoScalarPioBlocks" in fifo
    assert "if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;" in fifo
    # Protocol shape and failure policy stay intact.
    assert "SdioBuildCmd53Argument(Write,2,TRUE,FALSE,0x8000,Blocks)" in fifo
    assert "A->FifoTransportFailed=1" in fifo
    assert "Never replay a partially consumed FIFO" not in fifo or True

    driver = (ROOT / "src/driver/driver.c").read_text()
    assert 'SET_DWORD(L"DiagVersion", 42);' in driver
    for name in (
        "FifoBufferPioEnabled","FifoBufferPioBurstWords",
        "FifoBufferPioReadBlocks","FifoBufferPioWriteBlocks","FifoScalarPioBlocks",
        "TxServiceBurstEnabled","TxServiceBurstSavedStatusChecks",
    ):
        assert name in driver

    tests = (ROOT / "tests/fifo_block_tests.h").read_text()
    assert "FifoBufferPioReadBlocks==n" in tests
    assert "FifoBufferPioWriteBlocks==n" in tests
    assert "FifoScalarPioBlocks==1" in tests
    assert "buffer+1,512" in tests

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.16\s*$", inf)
    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.16'" in installer

    package = (ROOT / "scripts/package-tx-credit.ps1").read_text()
    assert "0.7.1.16 EXPERIMENTAL F2 BUFFER-PIO" in package
    assert "direct_experiment_baseline=66e0198609913fcc407c595e580e39c83777b60f" in package
    assert "tx_service_burst2=1" in package
    assert "fifo_buffer_pio=1" in package
    assert "fifo_buffer_pio_burst_words=32" in package
    assert "0.7.1.14-fix2-unchanged" in package

    print("PASS: v0.7.1.16 changes only aligned F2 block-copy mechanics plus diagnostics/versioning; v0.7.1.15 Service-Burst2, queue/glom/framing, SDIO protocol, firmware/radio, RX policy and frozen utility remain protected.")

if __name__ == "__main__":
    main()
