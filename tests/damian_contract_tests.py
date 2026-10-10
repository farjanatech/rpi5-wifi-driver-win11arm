"""Check the edition boundary and tightly scoped stability changes."""
from stability_update_scope_tests import verify_kernel_scope
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "c16aa318da490350126739add45223a186ab0a47"

def read(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")

def check(condition, message):
    if not condition:
        raise SystemExit(message)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uefi-root", type=Path)
    args = parser.parse_args()
    verify_kernel_scope()
    inf = read("package/rpi5cyw.inf")
    ids = re.findall(r"ACPI\\(RPI[0-9A-F]{4})", inf)
    check(ids == ["RPI1060"], f"Unsafe INF bindings: {ids}")
    check("10/10/2026,0.7.1.24" in inf, "Wrong package version")
    # All historical Wi-Fi consumers must follow the new node. RPI0011 is
    # permitted only in explicit IRQ coexistence/collision checks.
    for name in ["utility/Check-RPi5-WiFi-Readiness.ps1", "utility/Set-RPi5-WiFi-Autoconnect.ps1", "utility/Test-RPi5-WiFi-Performance.ps1"]:
        check("RPI1060" in read(name) and "RPI0011" not in read(name), f"Legacy Wi-Fi target: {name}")
    diagnostics = read("diagnostics/Collect-RPi5-WiFi-Diagnostics.ps1")
    check("$targetDevice = Get-Rpi5DeviceByAcpiId -AcpiId 'RPI1060'" in diagnostics, "Diagnostics targets wrong device")
    check("$irqDevice = Get-Rpi5DeviceByAcpiId -AcpiId 'RPI0011'" in diagnostics, "Missing IRQ coexistence diagnostics")
    check("RPI000F" not in diagnostics and "RPI0010" not in diagnostics, "Legacy platform diagnostics remain")
    if args.uefi_root:
        platform = args.uefi_root / "edk2-platforms"
        subprocess.run(["python", str(args.uefi_root / "tools/verify-damian-edition.py"), str(platform)], check=True)
        dsdt = (platform / "Platform/RaspberryPi/RPi5/AcpiTables/Dsdt.asl").read_text()
        node = dsdt.split("Device (WFD0) {", 1)[1].split('Include ("HardwareMetadata.asi")', 1)[0]
        check('Name (_HID, "RPI1060")' in node and "{ 306 }" in node, "WFD0 binding/IRQ mismatch")
        check("BCM2712_BRCMSTB_SDIO2_HOST_BASE, BCM2712_BRCMSTB_SDIO_HOST_LENGTH, 0" in node, "WFD0 resource mismatch")
        bcm = (platform / "Silicon/Broadcom/Bcm27xx/Include/IndustryStandard/Bcm2712.h").read_text()
        check(re.search(r"BCM2712_BRCMSTB_SDIO2_HOST_BASE\s+0x1001100000\b", bcm), "SDIO base mismatch")
        check(re.search(r"BCM2712_BRCMSTB_SDIO_HOST_LENGTH\s+0x260\b", bcm), "SDIO length mismatch")
    print("PASS: RPI1060-only package; scoped stability changes; build settings and radio firmware preserved")

if __name__ == "__main__":
    main()
