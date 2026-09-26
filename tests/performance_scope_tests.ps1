Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='6e652fb86aef595cf6f6fbf6f05c1769d5673055'
function Get-PerformanceSource([string]$Path,[switch]$Original) {
    if($Original) {
        $lines=@(& git -C $root show ($baseline+':'+$Path))
        if($LASTEXITCODE -ne 0){throw "Cannot read protected baseline: $Path"}
        return ($lines -join "`n").TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n").TrimEnd()
}
function Compare-PerformanceSource([string]$Actual,[string]$Expected,[string]$Label) {
    if($Actual -cne $Expected){throw "Protected baseline changed: $Label"}
}
$allowed=@('.github/workflows/build-arm64-driver.yml','README.md','THIRD_PARTY_NOTICES.md',
    'docs/PERFORMANCE-0.7.0-alpha.3-us.md','scripts/fetch-firmware.ps1','scripts/package-ci.ps1',
    'tests/performance_scope_tests.ps1','tests/firmware_package_tests.ps1','tests/scan_control_tests.c',
    'tests/us_region_tests.c','src/cyw43455/us_region.h','src/cyw43455/network.c',
    'src/cyw43455/scan_control.h','installer/Install-RPi5-WiFi-Driver.ps1','package/rpi5cyw.inf')
$changed=@(& git -C $root diff --name-only $baseline)
if($LASTEXITCODE -ne 0){throw 'Cannot compare alpha.1 scope.'}
foreach($path in $changed){if($path -notin $allowed){throw "Unexpected change outside firmware/US scope: $path"}}
$paths=@(& git -C $root ls-tree -r --name-only $baseline -- src utility connector installer diagnostics rpi5-cyw43455.vcxproj)
if($LASTEXITCODE -ne 0 -or !$paths.Count){throw 'Cannot enumerate protected files.'}
foreach($path in $paths) {
    if($path -notin @('installer/Install-RPi5-WiFi-Driver.ps1','src/cyw43455/network.c','src/cyw43455/scan_control.h')) {
        # Compare Git blobs without decoding binary assets (for example the icon).
        & git -C $root diff --quiet $baseline -- $path
        if($LASTEXITCODE -ne 0){throw "Protected baseline changed or comparison failed: $path"}
        continue
    }
    $actual=Get-PerformanceSource $path
    switch($path) {
        'installer/Install-RPi5-WiFi-Driver.ps1' { $actual=$actual.Replace("'0.7.0.2'","'0.7.0'") }
        'src/cyw43455/network.c' {
            $actual=$actual.Replace("#include `"us_region.h`"`n",'').Replace('CywUsValidConnect(','CywValidConnect(')
        }
        'src/cyw43455/scan_control.h' {
            $actual=$actual.Replace("#include `"us_region.h`"`n",'').Replace(' && CywUsCountryAllowed(Buffer+4)','')
        }
    }
    Compare-PerformanceSource $actual (Get-PerformanceSource $path -Original) $path
}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File) {
    $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
    if($relative -notin $paths -and $relative -ne 'src/cyw43455/us_region.h'){throw "Unexpected driver source: $relative"}
}
$inf=Get-PerformanceSource 'package/rpi5cyw.inf'
if($inf -notmatch '09/26/2026,0\.7\.0\.2'){throw 'US candidate version missing.'}
Compare-PerformanceSource ($inf.Replace('09/26/2026,0.7.0.2','09/24/2026,0.7.0.0')) (Get-PerformanceSource 'package/rpi5cyw.inf' -Original) 'INF except version'
if((Get-PerformanceSource 'src/cyw43455/network.c') -notmatch 'CywUsValidConnect\(Irp->AssociatedIrp.SystemBuffer\)' -or
   (Get-PerformanceSource 'src/cyw43455/scan_control.h') -notmatch 'CywUsCountryAllowed\(Buffer\+4\)') {
    throw 'US request admission gates not wired into production.'
}
Write-Output 'PASS: alpha.1 packet path, scheduling, queue, bus, authentication, firmware country readback, utilities and connector unchanged; only firmware package and US admission differ.'
