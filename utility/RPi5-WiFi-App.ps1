[CmdletBinding()]
param([switch]$LibraryOnly,[string]$PreviewPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'RPi5-WiFi-Operations.ps1')
. (Join-Path $PSScriptRoot 'RPi5-WiFi-Scan.ps1') -ScanLibraryOnly

function Test-Rpi5AppIdle {
    param($State)
    return $null -ne $State -and $State.Status -eq 0 -and $State.Phase -eq 500 -and -not $State.Authenticated
}
function Test-Rpi5AppSsid {
    param([string]$Ssid)
    try{
        $encoding=[Text.UTF8Encoding]::new($false,$true)
        if($encoding.GetByteCount($Ssid) -notin 1..32){return $false}
        foreach($character in $Ssid.ToCharArray()){
            if([Globalization.CharUnicodeInfo]::GetUnicodeCategory($character) -in @(
                [Globalization.UnicodeCategory]::Control,[Globalization.UnicodeCategory]::Format,
                [Globalization.UnicodeCategory]::LineSeparator,[Globalization.UnicodeCategory]::ParagraphSeparator)){return $false}
        }
        return $true
    }catch{return $false}
}
function Invoke-Rpi5AppConnect {
    param([string]$Country,[string]$Ssid,[byte[]]$PasswordBytes,
        [scriptblock]$Derive={param([byte[]]$Password,[byte[]]$Name) [Rpi5WifiControl]::Derive($Password,$Name)},
        [scriptblock]$Send={param($Data) [void][Rpi5WifiControl]::Call(0x12A000,$Data)})
    $pmk=$null;$request=$null
    try{
        if($Country -cnotmatch '^[A-Z]{2}$' -or -not (Test-Rpi5AppSsid $Ssid)){
            throw 'Confirm the physical country and enter an SSID of 1-32 UTF-8 bytes.'
        }
        if($null -eq $PasswordBytes -or $PasswordBytes.Length -lt 8 -or $PasswordBytes.Length -gt 63){
            throw 'WPA2 password must contain 8-63 printable ASCII characters.'
        }
        foreach($value in $PasswordBytes){if($value -lt 32 -or $value -gt 126){throw 'WPA2 password must contain 8-63 printable ASCII characters.'}}
        $ssidBytes=[Text.UTF8Encoding]::new($false,$true).GetBytes($Ssid)
        $pmk=[byte[]](& $Derive $PasswordBytes $ssidBytes)
        if($pmk.Length -ne 32){throw 'WPA2 key derivation returned an invalid result.'}
        $request=[byte[]]::new(76)
        [BitConverter]::GetBytes([uint32]1).CopyTo($request,0)
        [BitConverter]::GetBytes([uint32]$ssidBytes.Length).CopyTo($request,4)
        [Text.Encoding]::ASCII.GetBytes($Country).CopyTo($request,8)
        $ssidBytes.CopyTo($request,12);$pmk.CopyTo($request,44)
        $null=& $Send $request
    }finally{
        foreach($buffer in @($PasswordBytes,$pmk,$request)){
            if($null -ne $buffer){[Array]::Clear($buffer,0,$buffer.Length)}
        }
    }
}
function Get-Rpi5AppOperationOutcome {
    param([string]$Operation,$State,[double]$ElapsedSeconds,$Scan,[uint32]$PreviousGeneration)
    if($Operation -eq 'Scan'){
        if($null -ne $Scan -and $Scan.Generation -ne $PreviousGeneration){
            if($Scan.State -eq 3 -and $Scan.Status -eq 0){return 'Complete'}
            if($Scan.State -in @(4,5) -or ($Scan.Status -ne 0 -and $Scan.Status -ne 259)){return 'Failed'}
        }
        if($ElapsedSeconds -ge 90){return 'Timeout'}
    }elseif($Operation -eq 'Connect'){
        if($State.Authenticated -and $State.Status -eq 0){return 'Complete'}
        if($State.Status -ne 0){return 'Failed'}
        if($ElapsedSeconds -ge 180){return 'Timeout'}
    }elseif($Operation -eq 'Disconnect'){
        if(Test-Rpi5AppIdle $State){return 'Complete'}
        if($ElapsedSeconds -ge 20){return 'Timeout'}
    }
    return 'Wait'
}
function Submit-Rpi5AppScan {
    param($Context,[string]$Country,
        [scriptblock]$Start={param($Data) [void](Invoke-Rpi5ScanControl 0x12A014 $Data)},
        [scriptblock]$Read={ConvertFrom-Rpi5ScanReport (Invoke-Rpi5ScanControl 0x126018 $null)})
    # Record ownership before issuing the request. Capture its generation before
    # returning to the UI message loop, including an immediate close action.
    $Context.Operation='Scan';$Context.Watch=[Diagnostics.Stopwatch]::StartNew();$Context.ScanSubmitted=$false
    $null=& $Start (ConvertTo-Rpi5ScanRequest $Country)
    $Context.ScanSubmitted=$true
    $report=& $Read
    if($report.Generation -eq $Context.PreviousGeneration){throw 'A fresh scan generation was not observed.'}
    $Context.ScanGeneration=$report.Generation
}
function Invoke-Rpi5AppScanCancel {
    param($Context,
        [scriptblock]$Read={ConvertFrom-Rpi5ScanReport (Invoke-Rpi5ScanControl 0x126018 $null)},
        [scriptblock]$Cancel={param($Data) [void](Invoke-Rpi5ScanControl 0x12A01C $Data)})
    if(-not $Context.ScanSubmitted){return $false}
    $PreviousGeneration=[uint32]$Context.PreviousGeneration;$KnownGeneration=$Context.ScanGeneration
    # One bounded recovery read if the immediate post-start read failed.
    # Never guess a generation or cancel a stale prior scan.
    if($null -eq $KnownGeneration){
        try{
            $report=& $Read
            if($report.Generation -ne $PreviousGeneration -and $report.State -in @(1,2)){$KnownGeneration=$report.Generation}
        }catch{return $false}
    }
    if($null -eq $KnownGeneration -or [uint32]$KnownGeneration -eq 0){return $false}
    try{$null=& $Cancel ([BitConverter]::GetBytes([uint32]$KnownGeneration));return $true}catch{return $false}
}
function Select-Rpi5AppNetwork {
    param($Context,$Entry)
    # Never leave a previously supported SSID/password behind after the user
    # clicks an unsupported row and believes that row became the new target.
    $Context.Ssid.Text='';$Context.Password.Clear()
    if($Entry.Connectable){$Context.Ssid.Text=$Entry.Ssid;$Context.Message='Selected SSID; automatic band selection remains enabled.'}
    else{$Context.Message='This entry is hidden, not displayable or not supported by WPA2-Personal/AES. Enter a known compatible hidden SSID manually if needed.'}
}
if($LibraryOnly){return}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
if(-not $PreviewPath -and $env:PROCESSOR_ARCHITECTURE -ne 'ARM64'){
    [void][Windows.Forms.MessageBox]::Show('Run this app on the Raspberry Pi 5 in native Windows ARM64 PowerShell. No action was taken.','RPi5 Wi-Fi')
    return
}
if(-not $PreviewPath){
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
 if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    try{
        # This is the requested interactive app, not a background helper.
        Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList @(
            '-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',('"{0}"' -f $PSCommandPath))
    }catch{[void][Windows.Forms.MessageBox]::Show('Administrator approval is required. No connection or scan was requested.','RPi5 Wi-Fi')}
    return
 }
}
if([Threading.Thread]::CurrentThread.ApartmentState -ne [Threading.ApartmentState]::STA){throw 'Open RPi5-WiFi-App.cmd to use the required STA UI thread.'}
# LibraryOnly stops before the legacy connector can inspect a private profile.
if(-not $PreviewPath){. (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') -LibraryOnly}
[Windows.Forms.Application]::EnableVisualStyles()
$form=[Windows.Forms.Form]::new()
$form.Text='RPi5 Wi-Fi Connect';$form.ClientSize=[Drawing.Size]::new(850,565)
$form.MinimumSize=[Drawing.Size]::new(866,604);$form.StartPosition='CenterScreen'
$form.Font=[Drawing.Font]::new('Segoe UI',9)
function Show-Rpi5AppLabel {
    param([string]$Text,[int]$X,[int]$Y,[int]$Width,[int]$Height)
    $label=[Windows.Forms.Label]::new();$label.Text=$Text;$label.SetBounds($X,$Y,$Width,$Height)
    $form.Controls.Add($label);return $label
}
$null=Show-Rpi5AppLabel 'Wi-Fi connection app for the Ethernet-style CYW43455 adapter. WPA2-Personal / AES only.' 16 14 812 24
$null=Show-Rpi5AppLabel 'Physical country:' 16 49 112 24
$countryBox=[Windows.Forms.TextBox]::new();$countryBox.SetBounds(132,46,52,26)
$countryBox.MaxLength=2;$countryBox.CharacterCasing='Upper';$form.Controls.Add($countryBox)
$countryConfirm=[Windows.Forms.CheckBox]::new();$countryConfirm.Text='I confirm this is where the Pi is located'
$countryConfirm.SetBounds(198,47,360,26);$form.Controls.Add($countryConfirm)
$scanButton=[Windows.Forms.Button]::new();$scanButton.Text='Scan networks';$scanButton.SetBounds(690,43,140,32)
$scanButton.Anchor='Top,Right';$form.Controls.Add($scanButton)
$networks=[Windows.Forms.ListView]::new();$networks.SetBounds(16,88,814,220)
$networks.Anchor='Top,Left,Right';$networks.View='Details';$networks.FullRowSelect=$true;$networks.MultiSelect=$false;$networks.HideSelection=$false
foreach($column in @(@('Network (SSID)',210),@('Signal',65),@('Band',65),@('Channel',60),@('Security',215),@('BSSID',165))){
    [void]$networks.Columns.Add([string]$column[0],[int]$column[1])
}
$form.Controls.Add($networks)
$null=Show-Rpi5AppLabel 'The selected SSID uses automatic band selection; the listed BSSID is not forced. Hidden network? Type its exact SSID below.' 16 314 814 32
$null=Show-Rpi5AppLabel 'Network name:' 16 355 110 24
$ssidBox=[Windows.Forms.TextBox]::new();$ssidBox.SetBounds(132,351,698,28);$ssidBox.Anchor='Top,Left,Right';$ssidBox.MaxLength=32;$form.Controls.Add($ssidBox)
$null=Show-Rpi5AppLabel 'WPA2 password:' 16 392 115 24
$passwordBox=[Windows.Forms.TextBox]::new();$passwordBox.SetBounds(132,389,438,28);$passwordBox.UseSystemPasswordChar=$true;$passwordBox.MaxLength=63;$form.Controls.Add($passwordBox)
$connectButton=[Windows.Forms.Button]::new();$connectButton.Text='Connect';$connectButton.SetBounds(584,386,115,32);$form.Controls.Add($connectButton)
$disconnectButton=[Windows.Forms.Button]::new();$disconnectButton.Text='Disconnect';$disconnectButton.SetBounds(713,386,117,32);$disconnectButton.Anchor='Top,Right';$form.Controls.Add($disconnectButton)
$statusLabel=Show-Rpi5AppLabel 'Reading driver status...' 16 431 814 75
$statusLabel.BorderStyle='FixedSingle';$statusLabel.Anchor='Top,Left,Right';$statusLabel.Padding=[Windows.Forms.Padding]::new(6)
$null=Show-Rpi5AppLabel 'No passwords are saved. Autoconnect remains managed separately. Closing this window does not disconnect Wi-Fi.' 16 517 814 34
$timer=[Windows.Forms.Timer]::new();$timer.Interval=1000
$script:Rpi5App=@{Form=$form;Country=$countryBox;Confirm=$countryConfirm;Networks=$networks;Ssid=$ssidBox;Password=$passwordBox;
    ScanButton=$scanButton;ConnectButton=$connectButton;DisconnectButton=$disconnectButton;StatusLabel=$statusLabel;
    Operation='';Lease=$null;Watch=$null;PreviousGeneration=[uint32]0;ScanGeneration=$null;ScanSubmitted=$false;ScanSupported=$null;Updating=$false;State=$null;
    Message='';Timer=$timer}
if($PreviewPath){
    # CI-only offline rendering of the ACTUAL layout. This branch exits before
    # event handlers, timer startup, the normal modal loop or control-device calls.
    # Two bounded paint passes below make native child controls visible to GDI.
    # Restrict output to this checkout's existing ci-logs directory.
    $previewRoot=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'ci-logs'))+[IO.Path]::DirectorySeparatorChar
    $previewFile=[IO.Path]::GetFullPath($PreviewPath)
    if(-not $previewFile.StartsWith($previewRoot,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetExtension($previewFile) -ine '.png' -or -not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($previewFile)) -PathType Container)){
        $timer.Dispose();$form.Dispose();throw 'Offline preview must target a PNG inside the existing ci-logs directory.'
    }
    $countryBox.Text='BD';$countryConfirm.Checked=$true;$ssidBox.Text='Example network'
    foreach($row in @(@('Example network','-48 dBm','5 GHz','36','WPA2-Personal / AES','02:00:00:00:00:01'),
        @('Example network','-60 dBm','2.4 GHz','6','WPA2-Personal / AES','02:00:00:00:00:02'),
        @('Example open network','-55 dBm','2.4 GHz','1','Open (unsupported)','02:00:00:00:00:03'))){
        $item=[Windows.Forms.ListViewItem]::new([string]$row[0]);foreach($value in $row[1..5]){[void]$item.SubItems.Add([string]$value)}
        if($row[4] -like '*unsupported*'){$item.ForeColor=[Drawing.Color]::DimGray}
        [void]$networks.Items.Add($item)
    }
    $scanButton.Enabled=$false;$connectButton.Enabled=$false;$disconnectButton.Enabled=$false
    $statusLabel.Text='Offline layout preview. Synthetic networks only. No driver, scan, password derivation or connection has been used.'
    $bitmap=$null
    try{
        # A hidden parent leaves child controls invisible even after CreateControl;
        # show the modeless preview before painting. No handlers are registered yet.
        $form.ShowInTaskbar=$false;$form.Show();$form.PerformLayout()
        [Windows.Forms.Application]::DoEvents()
        $form.Refresh();foreach($control in $form.Controls){$control.Refresh()}
        [Windows.Forms.Application]::DoEvents()
        foreach($control in @($countryBox,$networks,$ssidBox,$passwordBox,$statusLabel)){
            if(-not $control.Visible -or -not $control.IsHandleCreated){throw 'Offline preview control was not made visible.'}
        }
        $bitmap=[Drawing.Bitmap]::new($form.Width,$form.Height)
        $form.DrawToBitmap($bitmap,[Drawing.Rectangle]::new(0,0,$form.Width,$form.Height))
        $bitmap.Save($previewFile,[Drawing.Imaging.ImageFormat]::Png)
    }finally{if($null -ne $bitmap){$bitmap.Dispose()};$timer.Dispose();$form.Close();$form.Dispose();$script:Rpi5App=$null}
    return
}
function Complete-Rpi5AppOperation {
    param([string]$Message)
    $script:Rpi5App.Message=$Message;$script:Rpi5App.Operation='';$script:Rpi5App.Watch=$null;$script:Rpi5App.ScanSubmitted=$false
    Exit-Rpi5Operation $script:Rpi5App.Lease;$script:Rpi5App.Lease=$null
}
function Stop-Rpi5AppScan {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions','',Justification='Internal GUI close/timeout cleanup cancels only the user-started scan; command-line confirmation must not block window closing.')]
    [CmdletBinding()]
    param()
    if($script:Rpi5App.Operation -eq 'Scan' -and $script:Rpi5App.ScanSubmitted){
        [void](Invoke-Rpi5AppScanCancel $script:Rpi5App)
    }
}
function Show-Rpi5AppButtonState {
    $app=$script:Rpi5App;$idle=Test-Rpi5AppIdle $app.State
    $free=-not $app.Operation
    $confirmed=$app.Confirm.Checked -and $app.Country.Text -cmatch '^[A-Z]{2}$'
    $app.ScanButton.Enabled=$free -and $idle -and $confirmed -and $app.ScanSupported -ne $false
    $app.ConnectButton.Enabled=$free -and $idle -and $confirmed -and (Test-Rpi5AppSsid $app.Ssid.Text) -and $app.Password.Text.Length -ge 8
    $app.DisconnectButton.Enabled=$free -and $null -ne $app.State -and $app.State.Phase -ge 500 -and (-not $idle)
    $app.Country.Enabled=$free;$app.Confirm.Enabled=$free;$app.Ssid.Enabled=$free;$app.Password.Enabled=$free;$app.Networks.Enabled=$free
}
function Show-Rpi5AppStatus {
    $app=$script:Rpi5App
    if($app.Updating){return};$app.Updating=$true
    try{
        # STATUS copies driver memory only. Never call radio refresh or scan
        # from this timer unless observing an explicitly requested scan.
        $app.State=ConvertFrom-Rpi5LiveState ([Rpi5WifiControl]::Call(0x126004,$null))
        if($app.Operation){
            $report=$null
            if($app.Operation -eq 'Scan'){
                $report=ConvertFrom-Rpi5ScanReport (Invoke-Rpi5ScanControl 0x126018 $null)
                if($report.Generation -ne $app.PreviousGeneration){$app.ScanGeneration=$report.Generation}
            }
            $outcome=Get-Rpi5AppOperationOutcome $app.Operation $app.State $app.Watch.Elapsed.TotalSeconds $report $app.PreviousGeneration
            if($outcome -eq 'Complete'){
                if($app.Operation -eq 'Scan'){
                    $app.Networks.Items.Clear()
                    foreach($entry in @($report.Entries | Sort-Object RssiDbm -Descending)){
                        $item=[Windows.Forms.ListViewItem]::new([string]$entry.DisplaySsid)
                        foreach($value in @($(if($null -eq $entry.RssiDbm){'Unknown'}else{"$($entry.RssiDbm) dBm"}),$entry.Band,[string]$entry.Channel,$entry.Security,$entry.Bssid)){
                            [void]$item.SubItems.Add([string]$value)
                        }
                        $item.Tag=$entry;if(-not $entry.Connectable){$item.ForeColor=[Drawing.Color]::DimGray}
                        [void]$app.Networks.Items.Add($item)
                    }
                    $message="Scan finished: $($report.Count) network entries. Hidden networks require manual SSID entry."
                    if($report.Truncated){$message+=' Results were truncated to the bounded report.'}
                    Complete-Rpi5AppOperation $message
                }elseif($app.Operation -eq 'Connect'){
                    Complete-Rpi5AppOperation 'Wi-Fi authenticated. IP configuration and Internet reachability are separate; use Check readiness to verify them.'
                }else{Complete-Rpi5AppOperation 'Disconnected. You can scan or connect now.'}
            }elseif($outcome -eq 'Timeout'){
                Stop-Rpi5AppScan
                Complete-Rpi5AppOperation 'Observation timed out. A scan cancellation was requested if its generation was known. No connection reset or retry was attempted.'
            }elseif($outcome -eq 'Failed'){
                $message='Operation failed. Collect diagnostics; no automatic retry was attempted.'
                if($null -ne $report){$message+=' Scan status 0x{0:X8}, firmware error {1}.' -f $report.Status,$report.FirmwareError}
                Complete-Rpi5AppOperation $message
            }
        }
        $stateText=if($app.State.Authenticated){'Authenticated link (not an Internet test).'}elseif(Test-Rpi5AppIdle $app.State){'Disconnected and ready.'}else{"Driver phase $($app.State.Phase), status 0x{0:X8}." -f $app.State.Status}
        if($app.Operation){$stateText+=" $($app.Operation) in progress; $([int]$app.Watch.Elapsed.TotalSeconds)s."}
        $app.StatusLabel.Text=$stateText+[Environment]::NewLine+$app.Message
    }catch{
        $app.State=$null
        if($app.Operation){Stop-Rpi5AppScan;Complete-Rpi5AppOperation 'Operation observation failed. No automatic retry was attempted.'}
        $app.StatusLabel.Text='Driver status unavailable. Confirm the matching driver is installed and run diagnostics. '+$app.Message
    }finally{$app.Updating=$false;Show-Rpi5AppButtonState}
}
function Start-Rpi5AppOperation {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions','',Justification='Internal GUI button handler: the explicit click and confirmed country authorize the one request; no command-line confirmation or background retry is appropriate.')]
    [CmdletBinding()]
    param([ValidateSet('Scan','Connect','Disconnect')][string]$Operation)
    $app=$script:Rpi5App
    if($app.Operation){return}
    $passwordBytes=$null
    try{
        $app.Lease=Enter-Rpi5Operation -WaitSeconds 0
        $state=ConvertFrom-Rpi5LiveState ([Rpi5WifiControl]::Call(0x126004,$null))
        if($Operation -ne 'Disconnect' -and -not (Test-Rpi5AppIdle $state)){throw 'Disconnected driver required.'}
        if($Operation -ne 'Disconnect' -and (-not $app.Confirm.Checked -or $app.Country.Text -cnotmatch '^[A-Z]{2}$')){throw 'Confirm physical country.'}
        if($Operation -eq 'Scan'){
            try{$oldReport=ConvertFrom-Rpi5ScanReport (Invoke-Rpi5ScanControl 0x126018 $null)}
            catch{$app.ScanSupported=$false;throw 'Scan ABI unavailable.'}
            if($oldReport.State -in @(1,2)){throw 'A scan is already running.'}
            $app.PreviousGeneration=$oldReport.Generation;$app.ScanGeneration=$null
            Submit-Rpi5AppScan $app $app.Country.Text
            $app.ScanSupported=$true
        }elseif($Operation -eq 'Connect'){
            # A password textbox necessarily holds a managed string. Clear it
            # promptly; do not pretend managed string copies can be zeroed.
            if($app.Password.Text -cnotmatch '\A[\x20-\x7E]{8,63}\z'){throw 'Invalid WPA2 password.'}
            $passwordBytes=[Text.Encoding]::ASCII.GetBytes($app.Password.Text);$app.Password.Clear()
            Invoke-Rpi5AppConnect $app.Country.Text $app.Ssid.Text $passwordBytes
        }else{[void][Rpi5WifiControl]::Call(0x12A008,$null)}
        $app.Operation=$Operation;$app.Watch=[Diagnostics.Stopwatch]::StartNew();$app.Message='Request submitted once.'
    }catch{
        if($app.Operation -eq 'Scan'){Stop-Rpi5AppScan}
        $message='Request not started. Confirm the country, WPA2/AES password and disconnected driver state; another Wi-Fi operation may be busy.'
        if($Operation -eq 'Scan' -and $app.ScanSupported -eq $false){$message='This driver does not provide the supported scan ABI. Manual SSID connection remains available.'}
        Complete-Rpi5AppOperation $message
    }finally{
        if($null -ne $passwordBytes){[Array]::Clear($passwordBytes,0,$passwordBytes.Length)}
        if($Operation -eq 'Connect'){$app.Password.Clear()}
        Show-Rpi5AppStatus
    }
}
$countryBox.add_TextChanged({$script:Rpi5App.Confirm.Checked=$false;Show-Rpi5AppButtonState})
$countryConfirm.add_CheckedChanged({Show-Rpi5AppButtonState})
$ssidBox.add_TextChanged({Show-Rpi5AppButtonState})
$passwordBox.add_TextChanged({Show-Rpi5AppButtonState})
$networks.add_SelectedIndexChanged({
    $app=$script:Rpi5App
    if($app.Networks.SelectedItems.Count -eq 1){
        $entry=$app.Networks.SelectedItems[0].Tag
        Select-Rpi5AppNetwork $app $entry
        Show-Rpi5AppStatus
    }
})
$scanButton.add_Click({Start-Rpi5AppOperation Scan})
$connectButton.add_Click({Start-Rpi5AppOperation Connect})
$disconnectButton.add_Click({Start-Rpi5AppOperation Disconnect})
$timer.add_Tick({Show-Rpi5AppStatus})
$form.add_Shown({Show-Rpi5AppStatus;$script:Rpi5App.Timer.Start()})
$form.add_FormClosing({$script:Rpi5App.Timer.Stop();Stop-Rpi5AppScan;$script:Rpi5App.Password.Clear()})
try{[void]$form.ShowDialog()}finally{
    $timer.Stop();$timer.Dispose();$passwordBox.Clear()
    Exit-Rpi5Operation $script:Rpi5App.Lease;$script:Rpi5App.Lease=$null
    $form.Dispose();$script:Rpi5App=$null
}
