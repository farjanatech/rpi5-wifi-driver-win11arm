[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Directory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Inspect staged artifacts only; never execute firmware or install a driver.
$expected = @(
    @{ Name='cyfmac43455-sdio.bin'; Length=631467; Hash='CF79E8E8727D103A94CD243F1D98770FA29F5DA25DF251D0D31B3696F3B4AC6A' },
    @{ Name='cyfmac43455-sdio.clm_blob'; Length=7163; Hash='2DBD7D22FC9AF0EB560CEAB45B19646D211BC7B34A1DD00C6BFAC5DD6BA25E8A' },
    @{ Name='brcmfmac43455-sdio.txt'; Length=2074; Hash='CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D' },
    @{ Name='FIRMWARE-COPYRIGHT.txt'; Length=426736; Hash='8D392DD71477A3CCB700A38B0AB7FAF96B62B0C5A83FCE45AB855FD8A2CAA387' }
)
foreach ($entry in $expected) {
    $path = Join-Path $Directory $entry.Name
    if ((Get-Item -LiteralPath $path).Length -ne $entry.Length -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Hash) {
        throw "Unexpected staged firmware/licence bytes: $($entry.Name)"
    }
}
$firmware = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $Directory 'cyfmac43455-sdio.bin')))
if ($firmware -notmatch 'Version: 7\.45\.229') { throw 'Unexpected firmware version/variant.' }
$source = Get-Content -LiteralPath (Join-Path $Directory 'FIRMWARE-SOURCE.txt') -Raw
if ($source -notmatch '929bdd689d1e18d0ef71214741d4e16eec74409c' -or
    $source -notmatch '7.45.229' -or $source -notmatch 'sup_wpa' -or
    $source -notmatch 'portable firmware-owned regulatory mode') { throw 'Firmware source/scope notice missing.' }
$copyright = Get-Content -LiteralPath (Join-Path $Directory 'FIRMWARE-COPYRIGHT.txt') -Raw
if ($copyright -notmatch 'License: binary-redist-Cypress' -or
    $copyright -notmatch 'DRIVER END USER LICENSE AGREEMENT') { throw 'Complete upstream firmware licence missing.' }
if (Get-ChildItem -LiteralPath $Directory -Filter '*43430*') { throw 'Wrong-chip firmware included.' }
if (Get-ChildItem -LiteralPath $Directory -Filter '*minimal.bin') { throw 'Wrong firmware variant included.' }
Write-Output 'PASS: official RPi standard firmware/CLM, unchanged Pi calibration, version, complete licence and source pin. Hardware compatibility untested.'
