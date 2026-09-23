# Shared functions only. Loading this file does not read profiles or change state.
function Enter-Rpi5Operation {
    param([ValidateRange(0,10)][int]$WaitSeconds=10,
        [scriptblock]$CreateMutex={ param($Acl)
            $created=$false
            [Threading.Mutex]::new($false,'Global\RPi5WiFi.Operation.v1',[ref]$created,$Acl)
        })
    $security=[Security.AccessControl.MutexSecurity]::new()
    $security.SetAccessRuleProtection($true,$false)
    foreach($sid in @('S-1-5-18','S-1-5-32-544')) {
        $security.AddAccessRule([Security.AccessControl.MutexAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid),'FullControl','Allow'))
    }
    $mutex=$null; $owned=$false; $abandoned=$false
    try {
        $mutex=& $CreateMutex $security
        $rules=$mutex.GetAccessControl().GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])
        foreach($rule in $rules) {
            if($rule.AccessControlType -eq 'Allow' -and
                $rule.IdentityReference.Value -notin @('S-1-5-18','S-1-5-32-544')) {
                throw 'Unsafe operation lock permissions.'
            }
        }
        try { $owned=$mutex.WaitOne($WaitSeconds*1000) }
        catch {
            # PowerShell can wrap method exceptions (including script-backed
            # test methods). Only abandonment from this WaitOne means ownership
            # transferred. Bound inspection; every unrelated error is rethrown.
            $cause=$_.Exception; $depth=0; $isAbandoned=$false
            while($null -ne $cause -and $depth -lt 8) {
                if($cause -is [Threading.AbandonedMutexException]) { $isAbandoned=$true; break }
                $cause=$cause.InnerException; $depth++
            }
            if(-not $isAbandoned) { throw }
            $owned=$true; $abandoned=$true
        }
        if(-not $owned) { throw 'Another Wi-Fi operation is running. Wait for it to finish and try again.' }
        # Named mutex ownership is recursive on the same thread. Nested scripts
        # must run in-process, never as a child waiting on its parent's lease.
        return [pscustomobject]@{ Mutex=$mutex; Owned=$true; Abandoned=$abandoned }
    } catch {
        if($null -ne $mutex) { if($owned){$mutex.ReleaseMutex()}; $mutex.Dispose() }
        throw
    }
}
function Exit-Rpi5Operation {
    param($Lease)
    if($null -ne $Lease -and $Lease.Owned) {
        try { $Lease.Mutex.ReleaseMutex() }
        finally { $Lease.Owned=$false; $Lease.Mutex.Dispose() }
    }
}
function ConvertFrom-Rpi5LiveState {
    param([byte[]]$State)
    if($null -eq $State -or $State.Length -lt 32) { throw 'Incomplete driver status.' }
    [pscustomobject]@{ Phase=[BitConverter]::ToUInt32($State,4);
        Status=[BitConverter]::ToUInt32($State,8);
        Authenticated=([BitConverter]::ToUInt32($State,28) -eq 1) }
}
function Test-Rpi5UsableAddress {
    param($Address)
    if($null -eq $Address -or $Address.AddressState -ne 'Preferred') { return $false }
    $parsed=$null
    if(-not [Net.IPAddress]::TryParse([string]$Address.IPAddress,[ref]$parsed) -or
        $parsed.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { return $false }
    $bytes=$parsed.GetAddressBytes()
    return $bytes[0] -ne 0 -and $bytes[0] -ne 127 -and $bytes[0] -lt 224 -and
        -not ($bytes[0] -eq 169 -and $bytes[1] -eq 254)
}
function Get-Rpi5ConnectionReadiness {
    $live=ConvertFrom-Rpi5LiveState ([Rpi5WifiControl]::Call(0x126004,$null))
    $adapters=@(Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*')
    $ipv4=$false; $route=$false; $index=0
    if($adapters.Count -eq 1) {
        $index=[int]$adapters[0].ifIndex
        $ipv4=@(Get-NetIPAddress -InterfaceIndex $index -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { Test-Rpi5UsableAddress $_ }).Count -gt 0
        $route=@(Get-NetRoute -InterfaceIndex $index -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
            Where-Object { $_.NextHop -and $_.NextHop -ne '0.0.0.0' }).Count -gt 0 -and
            $adapters[0].Status -eq 'Up'
    }
    [pscustomobject]@{ Phase=$live.Phase; Status=$live.Status; Authenticated=$live.Authenticated;
        IPv4Ready=$ipv4; RouteReady=$route; InterfaceIndex=$index;
        Ready=($live.Status -eq 0 -and $live.Authenticated -and $ipv4 -and $route) }
}
function Wait-Rpi5ConnectionState {
    param([ValidateSet('Disconnected','Authenticated','Ready')][string]$Target,
        [ValidateRange(1,120)][int]$Seconds,
        [scriptblock]$Observe={ Get-Rpi5ConnectionReadiness },
        [scriptblock]$Delay={ Start-Sleep -Milliseconds 500 }, [scriptblock]$Now)
    $watch=[Diagnostics.Stopwatch]::StartNew()
    if($null -eq $Now) { $Now={ $watch.Elapsed.TotalSeconds }.GetNewClosure() }
    $start=& $Now
    do {
        $sample=& $Observe
        if($sample.Status -ne 0 -and $Target -ne 'Disconnected') { throw 'Driver reports an error. Collect diagnostics; no repeated recovery will be attempted.' }
        $match=switch($Target) {
            'Disconnected' { $sample.Status -eq 0 -and -not $sample.Authenticated -and $sample.Phase -eq 500 }
            'Authenticated' { $sample.Authenticated -and $sample.Phase -ge 600 }
            'Ready' { $sample.Ready }
        }
        if($match) { return $sample }
        if(((& $Now)-$start) -ge $Seconds) { break }
        & $Delay
    } while($true)
    throw "Connection observation timed out: $Target. No automatic reset or repeated connection was attempted."
}
function Invoke-Rpi5FreshConnectionBoundary {
    param($Initial, [scriptblock]$Disconnect={ [void][Rpi5WifiControl]::Call(0x12A008,$null) },
        [scriptblock]$Wait={ Wait-Rpi5ConnectionState -Target Disconnected -Seconds 15 })
    # An already-idle unauthenticated device needs no needless disconnect.
    # The original ABI has no request generation. If another old utility has a
    # request queued, the single following CONNECT may fail Busy; never loop it.
    if($Initial.Authenticated -or $Initial.Phase -ne 500 -or $Initial.Status -ne 0) {
        & $Disconnect
        # The previous operation status may remain nonzero briefly until the
        # queued disconnect runs. Only the bounded disconnected wait handles it.
        $null=& $Wait
    }
}
function Get-Rpi5BootIdentity {
    return (Get-CimInstance Win32_OperatingSystem -ErrorAction Stop).LastBootUpTime.ToUniversalTime().ToString('o')
}
function Test-Rpi5OwnedStartupTask {
    param($Task,[string]$Directory,[string]$PowerShellPath)
    try {
        $scriptPath=Join-Path $Directory 'Connect-RPi5-WiFi.ps1'
        $configPath=Join-Path $Directory 'WiFi.private.json'
        $expected='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -Startup -ConfigPath "{1}"' -f $scriptPath,$configPath
        return $null -ne $Task -and @($Task.Actions).Count -eq 1 -and
            $Task.Principal.UserId -in @('SYSTEM','S-1-5-18') -and
            $Task.Actions[0].Execute -ieq $PowerShellPath -and
            $Task.Actions[0].Arguments -ceq $expected -and
            $Task.Actions[0].WorkingDirectory -ieq $Directory
    } catch { return $false }
}
function Test-Rpi5ProtectedDirectory {
    param([string]$Path)
    $item=Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if(-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe startup directory.' }
    $acl=Get-Acl -LiteralPath $Path
    if(-not $acl.AreAccessRulesProtected) { throw 'Startup directory permissions are not private.' }
    foreach($rule in $acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])) {
        if($rule.AccessControlType -eq 'Allow' -and $rule.IdentityReference.Value -notin @('S-1-5-18','S-1-5-32-544')) {
            throw 'Startup directory permissions are not private.'
        }
    }
}
function Write-Rpi5StartupReceipt {
    param([string]$StartedUtc, [ValidateSet('Ready','Failed')][string]$Outcome, $Connection)
    $directory=Join-Path $env:ProgramData 'RPi5WiFi'
    Test-Rpi5ProtectedDirectory $directory
    $path=Join-Path $directory 'Startup-receipt.json'
    $temporary=Join-Path $directory 'Startup-receipt.pending.json'
    foreach($name in @($path,$temporary)) {
        if(Test-Path -LiteralPath $name) {
            $item=Get-Item -LiteralPath $name -Force
            if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe receipt file.' }
        }
    }
    $receipt=[ordered]@{ SchemaVersion=1; UtilityVersion='0.6.27'; BootUtc=(Get-Rpi5BootIdentity);
        StartedUtc=$StartedUtc; CompletedUtc=[datetime]::UtcNow.ToString('o'); Outcome=$Outcome;
        Authenticated=$false; IPv4Ready=$false; RouteReady=$false; Phase=$null; Status=$null }
    if($null -ne $Connection) {
        foreach($name in @('Authenticated','IPv4Ready','RouteReady','Phase','Status')) { $receipt[$name]=$Connection.$name }
    }
    $receipt | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding UTF8
    if(Test-Path -LiteralPath $path) { [IO.File]::Replace($temporary,$path,$null) }
    else { [IO.File]::Move($temporary,$path) }
}
function Get-Rpi5StartupAssessment {
    param($Receipt,[string]$BootUtc,[string]$Version='0.6.27')
    $result=[ordered]@{ Outcome='NotTested'; Reason='No current, valid startup receipt'; Receipt=$null }
    if($null -eq $Receipt) { return [pscustomobject]$result }
    try {
        if($Receipt.SchemaVersion -ne 1 -or $Receipt.UtilityVersion -ne $Version -or
            $Receipt.BootUtc -ne $BootUtc -or $Receipt.Outcome -notin @('Ready','Failed')) { return [pscustomobject]$result }
        $boot=[datetime]::Parse($BootUtc).ToUniversalTime()
        $start=[datetime]::Parse($Receipt.StartedUtc).ToUniversalTime()
        $end=[datetime]::Parse($Receipt.CompletedUtc).ToUniversalTime()
        if($start -lt $boot -or $end -lt $start -or $end -gt [datetime]::UtcNow.AddMinutes(1)) { return [pscustomobject]$result }
        foreach($flag in @('Authenticated','IPv4Ready','RouteReady')) {
            if($Receipt.$flag -isnot [bool]) { return [pscustomobject]$result }
        }
        $ready=$Receipt.Outcome -eq 'Ready' -and $Receipt.Authenticated -eq $true -and
            $Receipt.IPv4Ready -eq $true -and $Receipt.RouteReady -eq $true -and $Receipt.Status -eq 0
        $result.Outcome=if($ready){'Passed'}else{'Failed'}
        $result.Reason='Current-boot startup observation; not a continuous-uptime guarantee'
        # Explicit allowlist. Never propagate arbitrary JSON properties.
        $result.Receipt=[pscustomobject]@{ UtilityVersion=$Receipt.UtilityVersion; BootUtc=$Receipt.BootUtc;
            StartedUtc=$Receipt.StartedUtc; CompletedUtc=$Receipt.CompletedUtc; Outcome=$Receipt.Outcome }
    } catch { return [pscustomobject]$result }
    return [pscustomobject]$result
}
function Read-Rpi5StartupAssessment {
    try {
        $directory=Join-Path $env:ProgramData 'RPi5WiFi'
        Test-Rpi5ProtectedDirectory $directory
        $file=Get-Item -LiteralPath (Join-Path $directory 'Startup-receipt.json') -Force
        if($file.PSIsContainer -or $file.Length -gt 8192 -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Invalid receipt.' }
        $receipt=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        return Get-Rpi5StartupAssessment $receipt (Get-Rpi5BootIdentity)
    } catch { return [pscustomobject]@{Outcome='NotTested';Reason='Startup evidence unavailable';Receipt=$null} }
}
