[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Directory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Inspect staged artifacts only; never execute firmware or install a driver.
$expected = @(
    @{ Name='cyfmac43455-sdio.bin'; Length=609309; Hash='D608F866582519C0A28D86DB43040F4F1B98DD1D153E72E9752586546B4A36C3' },
    @{ Name='cyfmac43455-sdio.clm_blob'; Length=2676; Hash='9823842CAE9FB9A5DD1E5FB31F595516EC7DEEE341354BEF30BB3026EEE29CC1' },
    @{ Name='brcmfmac43455-sdio.txt'; Length=2074; Hash='CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D' },
    @{ Name='FIRMWARE-COPYRIGHT.txt'; Length=430523; Hash='07082FD0BB65E32C73AF39A2292DD6C83F169BE7427D338BA1FE34F9B86C0E30' }
)
foreach ($entry in $expected) {
    $path = Join-Path $Directory $entry.Name
    if ((Get-Item -LiteralPath $path).Length -ne $entry.Length -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Hash) {
        throw "Unexpected staged firmware/licence bytes: $($entry.Name)"
    }
}
$firmware = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $Directory 'cyfmac43455-sdio.bin')))
if ($firmware -notmatch 'Version: 7\.45\.265 \(28bca26 CY\)' -or
    $firmware -notmatch 'FWID 01-b677b91b') { throw 'Unexpected firmware version/variant.' }
$source = Get-Content -LiteralPath (Join-Path $Directory 'FIRMWARE-SOURCE.txt') -Raw
if ($source -notmatch '3bab0f823f5b53150b76aab77093adef6655b920' -or
    $source -notmatch 'US-only' -or $source -notmatch 'previously rejected BD') { throw 'Firmware source/scope notice missing.' }
$copyright = Get-Content -LiteralPath (Join-Path $Directory 'FIRMWARE-COPYRIGHT.txt') -Raw
if ($copyright -notmatch 'License: binary-redist-Cypress' -or
    $copyright -notmatch 'DRIVER END USER LICENSE AGREEMENT') { throw 'Complete upstream firmware licence missing.' }
if (Get-ChildItem -LiteralPath $Directory -Filter '*43430*') { throw 'Wrong-chip firmware included.' }
if (Get-ChildItem -LiteralPath $Directory -Filter '*minimal.bin') { throw 'Wrong firmware variant included.' }
Write-Output 'PASS: official RPi standard firmware/CLM, unchanged Pi calibration, version, complete licence and source pin. Hardware compatibility untested.'
