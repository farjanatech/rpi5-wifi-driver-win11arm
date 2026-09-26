[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Firmware packaging is intended to run in GitHub Actions only.' }
$revision = '3bab0f823f5b53150b76aab77093adef6655b920'
$base = "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$revision/"
$files = @(
    @{ Name='cyfmac43455-sdio.bin'; Path='debian/added-firmware/cypress/cyfmac43455-sdio-standard.bin'; Hash='D608F866582519C0A28D86DB43040F4F1B98DD1D153E72E9752586546B4A36C3' },
    @{ Name='cyfmac43455-sdio.clm_blob'; Path='debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob'; Hash='9823842CAE9FB9A5DD1E5FB31F595516EC7DEEE341354BEF30BB3026EEE29CC1' },
    @{ Name='brcmfmac43455-sdio.txt'; Path='debian/added-firmware/brcm/brcmfmac43455-sdio.txt'; Hash='CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D' },
    @{ Name='FIRMWARE-COPYRIGHT.txt'; Path='debian/copyright'; Hash='07082FD0BB65E32C73AF39A2292DD6C83F169BE7427D338BA1FE34F9B86C0E30' }
)
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Name
    Invoke-WebRequest -Uri ($base+$file.Path) -OutFile $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Pinned firmware verification failed: $($file.Name)"
    }
}
# Preserve all upstream bytes, including the full copyright/licence document.
# The standard firmware basename is aliased to the existing driver's path.
@"
Firmware source: RPi-Distro/firmware-nonfree revision $revision.
debian/added-firmware/cypress/cyfmac43455-sdio-standard.bin -> cyfmac43455-sdio.bin
Version: 7.45.265 (28bca26 CY); FWID 01-b677b91b; 609309 bytes.
debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob; 2676 bytes.
debian/added-firmware/brcm/brcmfmac43455-sdio.txt; 2074 bytes; unchanged calibration.
Pi 5 Model B aliases select these files; standard has update-alternatives priority 50.
Full upstream debian/copyright (including binary-redist-Cypress) is included unmodified.
Use only with the intended Cypress CYW43455 device under those separate terms, not GPL.
US-only host package: existing country SET/readback checks remain mandatory.
This firmware/CLM pair previously rejected BD; it is NOT a Bangladesh update.
Physical US operation, WPA2 offload compatibility and speed are not verified by CI.
This is not a regulatory certification. No firmware/CLM bytes or power tables are edited.
"@ |
    Set-Content -LiteralPath (Join-Path $Destination 'FIRMWARE-SOURCE.txt') -Encoding ASCII
