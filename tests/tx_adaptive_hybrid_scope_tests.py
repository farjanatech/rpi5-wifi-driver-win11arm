"""v0.7.1.19 adaptive TX hybrid isolation guard."""
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASELINE="50e5b5d9e4456c93b0504e07c8756504284e8a7c"  # green v0.7.1.18
MAIN_UPLOAD_REFERENCE="d5d61aa0c2d864162615589fc93171252c5a6305"
UTILITY_BASELINE="e7de4e26d317bca947171c25967b10201380711a"
ALLOWED_SRC={
    "src/driver/driver.h","src/driver/driver.c",
    "src/cyw43455/tx_types.h","src/cyw43455/tx_queue.h",
    "src/cyw43455/transport_send.h",
}
def git(*args):
    return subprocess.check_output(["git","-C",str(ROOT),*args],text=True)
def main():
    git("merge-base","--is-ancestor",BASELINE,"HEAD")
    files=git("ls-tree","-r","--name-only",BASELINE,"src").splitlines()
    current={p.relative_to(ROOT).as_posix() for p in (ROOT/"src").rglob("*") if p.is_file()}
    assert current==set(files),"Unexpected production source surface: "+str(current^set(files))
    changed=set()
    for name in files:
        actual=(ROOT/name).read_text(encoding="utf-8")
        before=git("show",BASELINE+":"+name)
        if actual!=before:changed.add(name)
        if name not in ALLOWED_SRC:
            assert actual==before,"Protected v0.7.1.18 production source changed: "+name
    assert changed==ALLOWED_SRC,"Unexpected/missing adaptive source changes: "+str(changed^ALLOWED_SRC)

    # The hardware-fixed RX/F2 path and scheduling/connection behavior stay frozen.
    for name in (
        "src/sdio/fifo_blocks.h","src/sdio/sdio.c","src/cyw43455/network.c",
        "src/cyw43455/connection.h","src/cyw43455/firmware.c",
        "src/cyw43455/tx_pressure_pump.h","src/cyw43455/tx_credit_pump.h",
        "src/cyw43455/tx_retry_gate.h","src/cyw43455/tx_glom2_config.h",
    ):
        assert (ROOT/name).read_text(encoding="utf-8")==git("show",BASELINE+":"+name), "Protected source changed: "+name

    fifo=(ROOT/"src/sdio/fifo_blocks.h").read_text()
    assert "READ_REGISTER_BUFFER_ULONG" not in fifo and "WRITE_REGISTER_BUFFER_ULONG" not in fifo
    assert "SdioRead32(A,SDHCI_BUFFER)" in fifo
    assert "SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+pos))" in fifo

    utility_files=git("ls-tree","-r","--name-only",UTILITY_BASELINE,"utility").splitlines()
    for name in utility_files:
        assert (ROOT/name).read_text(encoding="utf-8")==git("show",UTILITY_BASELINE+":"+name), "Frozen utility changed: "+name

    header=(ROOT/"src/driver/driver.h").read_text()
    assert "#define RPI5CYW_TX_ADAPTIVE_SMALL_MAX 512u" in header
    assert "#define RPI5CYW_TX_ADAPTIVE_HYBRID 1" in header
    assert "#define RPI5CYW_TX_LIMIT 64u" in header
    assert "#define RPI5CYW_TX_BACKLOG_LIMIT 128u" in header
    assert "#define RPI5CYW_TX_SERVICE_BURST_MAX 4u" in header

    types=(ROOT/"src/cyw43455/tx_types.h").read_text()
    assert "MaxFrameLength" in types
    queue=(ROOT/"src/cyw43455/tx_queue.h").read_text()
    for token in ("CywTxAdaptiveSmallLength","CywTxAdaptiveSmallItem","CywTxAdaptiveSmallNb",
                  "TxAdaptiveSmallBacklogAccepted","TxAdaptiveBulkBackpressure",
                  "TxAdaptiveBulkFreshF1Attempts","TxAdaptiveSmallGlomChains"):
        assert token in queue
    assert "CywTxAdaptiveSmallNb(Q->Entries[0].Next)" in queue
    assert "if(!smallFrame)serviceRemaining=0;" in queue

    sender=(ROOT/"src/cyw43455/transport_send.h").read_text()
    assert "Length>RPI5CYW_TX_ADAPTIVE_SMALL_DATA_MAX" in sender

    driver=(ROOT/"src/driver/driver.c").read_text()
    assert 'SET_DWORD(L"DiagVersion", 44);' in driver
    for key in ("TxAdaptiveHybridEnabled","TxAdaptiveSmallMax","TxAdaptiveSmallFrames",
                "TxAdaptiveBulkFrames","TxAdaptiveBulkBackpressure",
                "TxAdaptiveSmallGlomChains","TxAdaptiveBulkFreshF1Attempts"):
        assert key in driver

    inf=(ROOT/"package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.19\s*$",inf)
    installer=(ROOT/"installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.19'" in installer
    package=(ROOT/"scripts/package-tx-credit.ps1").read_text()
    assert "0.7.1.19 EXPERIMENTAL ADAPTIVE TX HYBRID" in package
    assert "adaptive_small_max=512" in package
    assert "adaptive_bulk_backlog=0" in package
    assert "adaptive_bulk_fresh_f1=1" in package
    assert "main_upload_reference="+MAIN_UPLOAD_REFERENCE in package
    assert "fixed_port_pio=v0.7.1.17-unchanged" in package

    print("PASS: v0.7.1.19 changes only adaptive TX classification/diagnostics/versioning; small frames retain backlog+Glom2+Burst4, bulk frames get active-only/fresh-F1 pacing, and v0.7.1.18 RX/fixed-port/firmware/scheduling remain protected.")
if __name__=="__main__":
    main()
