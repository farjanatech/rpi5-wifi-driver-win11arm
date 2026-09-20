[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Firmware packaging is intended to run in GitHub Actions only.' }
$revision = 'c91cd2804cf7463aab913e7247c176049f16bbd6'
$base = "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$revision/"
$files = @(
    @{ Name='cyfmac43455-sdio.bin'; Path='debian/config/brcm80211/cypress/cyfmac43455-sdio-standard.bin'; Hash='D608F866582519C0A28D86DB43040F4F1B98DD1D153E72E9752586546B4A36C3' },
    @{ Name='cyfmac43455-sdio.clm_blob'; Path='debian/config/brcm80211/cypress/cyfmac43455-sdio.clm_blob'; Hash='9823842CAE9FB9A5DD1E5FB31F595516EC7DEEE341354BEF30BB3026EEE29CC1' },
    @{ Name='brcmfmac43455-sdio.txt'; Path='debian/config/brcm80211/brcm/brcmfmac43455-sdio.txt'; Hash='CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D' },
    @{ Name='FIRMWARE-COPYRIGHT.txt'; Path='debian/copyright'; Hash='8D392DD71477A3CCB700A38B0AB7FAF96B62B0C5A83FCE45AB855FD8A2CAA387' }
)
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Name
    Invoke-WebRequest -Uri ($base + $file.Path) -OutFile $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Pinned firmware verification failed: $($file.Name)"
    }
}
# Pi 5 aliases resolve to the standard Cypress image and this NVRAM file.
# Preserve firmware and calibration files byte-for-byte, and their full license.
"RPi-Distro/firmware-nonfree revision $revision; standard image; Pi 5 Model B aliases." |
    Set-Content -LiteralPath (Join-Path $Destination 'FIRMWARE-SOURCE.txt') -Encoding ASCII
