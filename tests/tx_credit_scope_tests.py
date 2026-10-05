"""v0.7.1.17 fixed-port PIO rollback isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DIRECT_BASELINE = "66e0198609913fcc407c595e580e39c83777b60f"  # green v0.7.1.15 Service-Burst2
STABLE_BASELINE = "6ae93623c8767eda050b8c408250d3ec3ce19bfb"  # v0.7.1.11 rollback
UTILITY_BASELINE = "e7de4e26d317bca947171c25967b10201380711a"  # v0.7.1.14-fix2 frozen utility

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def main():
    git("merge-base", "--is-ancestor", DIRECT_BASELINE, "HEAD")

    # Safety correction: production source must be byte-for-byte v0.7.1.15.
    files = git("ls-tree", "-r", "--name-only", DIRECT_BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production source surface: " + str(current ^ set(files))
    for name in files:
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", DIRECT_BASELINE + ":" + name)
        assert actual == before, "v0.7.1.17 must restore exact v0.7.1.15 production source: " + name

    # User utility remains frozen.
    utility_files = git("ls-tree", "-r", "--name-only", UTILITY_BASELINE, "utility").splitlines()
    for name in utility_files:
        assert (ROOT / name).read_text(encoding="utf-8") == git("show", UTILITY_BASELINE + ":" + name), \
            "Utility changed despite freeze request: " + name

    header = (ROOT / "src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_LIMIT 64u" in header
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in header
    assert "#define RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD 32u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST_MAX 2u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST2 1" in header
    assert "RPI5CYW_FIFO_BUFFER_PIO" not in header

    fifo = (ROOT / "src/sdio/fifo_blocks.h").read_text()
    assert "#define CYW_FIFO_BLOCK_SIZE 512UL" in fifo
    assert "#define CYW_FIFO_MAX_BLOCKS 32UL" in fifo
    assert "READ_REGISTER_BUFFER_ULONG" not in fifo
    assert "WRITE_REGISTER_BUFFER_ULONG" not in fifo
    assert "SdioFifoBufferAligned" not in fifo
    assert "SdioRead32(A,SDHCI_BUFFER)" in fifo
    assert "SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+pos))" in fifo
    assert "for(offset=0;offset<CYW_FIFO_BLOCK_SIZE;offset+=4)" in fifo
    assert "if(A->IoStopped) {status=STATUS_INVALID_DEVICE_STATE;goto Failed;}" in fifo
    assert "SdioBuildCmd53Argument(Write,2,TRUE,FALSE,0x8000,Blocks)" in fifo
    assert "A->FifoTransportFailed=1" in fifo

    # Restore the matching host FIFO regression model too.
    fifo_test = (ROOT / "tests/fifo_block_tests.h").read_text(encoding="utf-8")
    assert fifo_test == git("show", DIRECT_BASELINE + ":tests/fifo_block_tests.h")
    assert "FifoWrites==n*128" in fifo_test and "FifoReads==n*128" in fifo_test

    driver = (ROOT / "src/driver/driver.c").read_text()
    assert 'SET_DWORD(L"DiagVersion", 41);' in driver
    assert "FifoBufferPioEnabled" not in driver

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.17\s*$", inf)
    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.17'" in installer

    package = (ROOT / "scripts/package-tx-credit.ps1").read_text()
    assert "0.7.1.17 FIXED-PORT PIO ROLLBACK" in package
    assert "driver_version=0.7.1.17-fixed-port-rollback" in package
    assert "fifo_buffer_pio=0" in package
    assert "fifo_fixed_port_pio=1" in package
    assert "v0_7_1_16_buffer_pio=rejected-hardware" in package
    assert "0.7.1.14-fix2-unchanged" in package

    readme = (ROOT / "README.md").read_text()
    assert "v0.7.1.17" in readme
    assert "v0.7.1.16" in readme and "rejected" in readme.lower()

    print("PASS: v0.7.1.17 restores exact v0.7.1.15 production source, forbids register-buffer APIs on the fixed SDHCI FIFO port, preserves Service-Burst2 and frozen utilities, and marks v0.7.1.16 rejected.")

if __name__ == "__main__":
    main()
