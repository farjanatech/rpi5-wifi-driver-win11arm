"""v0.7.1.13 package-only consolidation guard."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "6ae93623c8767eda050b8c408250d3ec3ce19bfb"  # green hardware v0.7.1.11

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def main():
    git("merge-base", "--is-ancestor", BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", BASELINE, "src").splitlines()
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current == set(files), "Unexpected production source surface: " + str(current ^ set(files))
    for name in files:
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", BASELINE + ":" + name)
        assert actual == before, "v0.7.1.13 must not change proven v0.7.1.11 driver source: " + name

    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/04/2026,0\.7\.1\.13\s*$", inf)
    installer = (ROOT / "installer/Install-RPi5-WiFi-Driver.ps1").read_text()
    assert "$script:InstallerVersion = '0.7.1.13'" in installer
    assert "RPi5-WiFi-AllInOne.ps1" in installer
    assert "Collect-RPi5-WiFi-Diagnostics.ps1" not in installer
    assert "Set-RPi5-WiFi-Autoconnect.ps1" not in installer

    package = (ROOT / "scripts/package-ci.ps1").read_text()
    assert "build-all-in-one-utility.ps1" in package
    for old in (
        "utility\\Connect-RPi5-WiFi.ps1",
        "utility\\Test-RPi5-WiFi-Performance.ps1",
        "utility\\Get-RPi5-WiFi-Radio.ps1",
        "diagnostics\\Collect-RPi5-WiFi-Diagnostics.ps1",
    ):
        assert old not in package, "Legacy user-facing utility still staged: " + old

    wrapper = (ROOT / "utility/RPi5-WiFi-AllInOne.Template.ps1").read_text()
    assert "https://speed.cloudflare.com/__up" in wrapper
    assert "4 x 8 MiB" in wrapper
    assert "UploadQueueRejectsDelta" in wrapper
    assert "Driver baseline=v0.7.1.11 runtime; no new TX tuning in this package." in wrapper

    perf = (ROOT / "utility/Test-RPi5-WiFi-Performance.ps1").read_text()
    assert "[string]$OutputRoot" in perf
    assert "if ($OutputRoot)" in perf

    print("PASS: v0.7.1.13 is package/measurement consolidation only; every production driver source file is byte-for-byte green v0.7.1.11.")

if __name__ == "__main__":
    main()
