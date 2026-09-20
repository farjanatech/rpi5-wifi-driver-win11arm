[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Firmware packaging is intended to run in GitHub Actions only.' }
$revision = '929bdd689d1e18d0ef71214741d4e16eec74409c'
$base = "https://raw.githubusercontent.com/ahmedarif193/reactos/$revision/drivers/network/dd/cyw43455/fw/"
$boardNotice = 'https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/c91cd2804cf7463aab913e7247c176049f16bbd6/debian/copyright'
$files = @(
    @{ Name='cyfmac43455-sdio.bin'; Uri=($base+'brcmfmac43455-sdio.bin'); Hash='CF79E8E8727D103A94CD243F1D98770FA29F5DA25DF251D0D31B3696F3B4AC6A' },
    @{ Name='cyfmac43455-sdio.clm_blob'; Uri=($base+'brcmfmac43455-sdio.clm_blob'); Hash='2DBD7D22FC9AF0EB560CEAB45B19646D211BC7B34A1DD00C6BFAC5DD6BA25E8A' },
    @{ Name='brcmfmac43455-sdio.txt'; Uri=($base+'brcmfmac43455-sdio.txt'); Hash='CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D' },
    @{ Name='FIRMWARE-BROADCOM-LICENCE.txt'; Uri=($base+'LICENCE.broadcom_bcm43xx'); Hash='B16056FC91B82A0E3E8DE8F86C2DAC98201AA9DC3CBD33E8D38F1B087FCEC30D' },
    @{ Name='FIRMWARE-WHENCE.txt'; Uri=($base+'WHENCE'); Hash='CAFC36756B8E5BD7446438EB6D02DC9455F0A7C73578F01F7B88AB3BA0D92E03' },
    @{ Name='FIRMWARE-COPYRIGHT.txt'; Uri=$boardNotice; Hash='8D392DD71477A3CCB700A38B0AB7FAF96B62B0C5A83FCE45AB855FD8A2CAA387' }
)
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Name
    Invoke-WebRequest -Uri $file.Uri -OutFile $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Pinned firmware verification failed: $($file.Name)"
    }
}
# Preserve all upstream bytes. Only the two firmware filenames are aliased to
# the existing driver's paths; no chip/host/UEFI change is needed.
@"
Firmware and CLM: ahmedarif193/reactos revision $revision, drivers/network/dd/cyw43455/fw.
brcmfmac43455-sdio.bin -> cyfmac43455-sdio.bin; version 7.45.229; 631467 bytes.
brcmfmac43455-sdio.clm_blob -> cyfmac43455-sdio.clm_blob; 7163 bytes.
brcmfmac43455-sdio.txt is identical to the previously packaged Pi 5 calibration.
Broadcom licence and upstream WHENCE are included unmodified; 43430 files are NOT bundled.
Additional board-file copyright retained from RPi-Distro/firmware-nonfree c91cd2804cf7463aab913e7247c176049f16bbd6.
Experimental compatibility candidate, not a newer firmware version or a regulatory certification.
"@ |
    Set-Content -LiteralPath (Join-Path $Destination 'FIRMWARE-SOURCE.txt') -Encoding ASCII
