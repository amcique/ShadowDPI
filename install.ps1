#requires -Version 5.1
<#
.SYNOPSIS
Installs, upgrades, reconfigures, removes, and scans ShadowDPI.

.DESCRIPTION
One script for the release. With no arguments it opens an interactive menu.
Install actions copy shadowdpi.exe, WinDivert.dll, WinDivert64.sys, the
documentation, and the tools from the folder that contains this script into
%ProgramFiles%\ShadowDPI. They also register an auto-start Windows service and
write install.json in the install folder. Reinstall and upgrade preserve the
service name, arguments, start type, and running state unless you pass an
explicit override.

The scan action is the strategy scanner. It starts one ShadowDPI instance per
candidate, waits for the filters to open through --ready-file, probes every URL
with curl.exe, and reports which strategies restore access. The model is a
baseline comparison: probes that fail without ShadowDPI are targets, and a
strategy passes only when every target is reachable and no baseline-working
probe regresses. An installed, running ShadowDPI service is stopped for the
scan and started again afterward.

The scanner does not follow redirects. A redirect to another host is treated as
a suspicious DPI response, and HTTP 400 is treated as a sign that fake packets
reached the server.

.PARAMETER Action
menu opens the interactive menu. install installs or reinstalls, upgrade
requires an existing installation, reconfigure changes service settings,
uninstall removes the service and the install folder, and scan runs the
strategy scanner. Defaults to menu.

.PARAMETER InstallPath
Install folder. Defaults to %ProgramFiles%\ShadowDPI. A registered service can
also identify the folder. Pass this parameter to later actions on a custom
install.

.PARAMETER ServiceName
Windows service name. Defaults to ShadowDPI for a new install. Reinstall and
upgrade preserve the existing name.

.PARAMETER ServiceArgs
Command-line arguments stored in the service binary path. Defaults to -9 for a
new install. Reinstall and upgrade preserve the existing arguments.

.PARAMETER NoService
Do not create a new Windows service. During reinstall or upgrade, an existing
service is stopped for the file update and started again afterward so its
configuration is preserved. During reconfigure, this removes the service.

.PARAMETER NoStart
Leave the service stopped after install, upgrade, or reconfigure.

.PARAMETER SourcePath
Folder that contains shadowdpi.exe and the WinDivert files. Defaults to the
folder that contains this script, which is the extracted release folder.

.PARAMETER Quiet
Do not ask configuration questions. Explicit parameters and defaults are used.

.PARAMETER Force
Skip confirmation prompts. Uninstall from a non-interactive session requires
this switch.

.PARAMETER Urls
One or more URLs to test. Overrides the URL file.

.PARAMETER UrlFile
A text file with one URL per line. Blank lines and lines starting with # or ;
are ignored. A bare host is treated as https://host/. Defaults to
tools\scan-urls.txt next to this script.

.PARAMETER Exe
Path to shadowdpi.exe. By default the script prefers an installed copy, then
the copy next to this script, then the usual build output directories.

.PARAMETER ScanLevel
quick tests presets 1-9. standard adds hand-picked combinations. full adds
parameter sweeps and per-host fake SNI payloads.

.PARAMETER Protocols
auto probes HTTPS with the curl default negotiation. tls12 and tls13 force one
version. all probes both forced versions.

.PARAMETER IpVersion
Address family passed to curl: 4, 6, or auto.

.PARAMETER TimeoutSec
Maximum time for one curl probe.

.PARAMETER StartupTimeoutSec
Maximum time to wait for the ShadowDPI ready file. Defaults to 10 seconds,
which leaves room for the first WinDivert driver load.

.PARAMETER Repeats
Probe attempts per URL. 0 selects 1 for quick and standard and 2 for full.

.PARAMETER DnsResolver
DNS resolver choice. ask shows a menu in an interactive session, cloudflare
uses 1.1.1.1, google uses 8.8.8.8, and system uses the Windows resolver.
Explicit DnsAddr or DnsV6Addr values skip the menu.

.PARAMETER DnsAddr
Skip the menu and use this IPv4 resolver for --dns-addr. When set, every
strategy also redirects DNS and an extra "DNS redirect only" strategy is added.

.PARAMETER DnsPort
Port for DnsAddr, and for the cloudflare and google resolver choices. Defaults
to 53.

.PARAMETER DnsV6Addr
Optional IPv6 resolver for --dnsv6-addr.

.PARAMETER DnsV6Port
Port for DnsV6Addr. Defaults to 53.

.PARAMETER Json
Write the complete result set to this file as UTF-8 without a BOM.

.PARAMETER MaxStrategies
Limit the number of candidates. 0 keeps the whole matrix.

.PARAMETER SettleMs
Delay after the ready file appears before the first probe.

.PARAMETER NoDnsFlush
Do not run ipconfig /flushdns before the baseline.

.PARAMETER DryRun
Print the candidate matrix and exit without touching the network.

.PARAMETER KeepArtifacts
Keep the temporary directory with ready files and process logs.

.EXAMPLE
.\install.ps1

.EXAMPLE
.\install.ps1 -Action install -ServiceArgs '-5'

.EXAMPLE
.\install.ps1 -Action upgrade

.EXAMPLE
.\install.ps1 -Action scan -ScanLevel quick

.EXAMPLE
.\install.ps1 -Action uninstall -Force
#>
[CmdletBinding()]
param(
    [ValidateSet('menu', 'install', 'upgrade', 'reconfigure', 'uninstall', 'scan')]
    [string]$Action = 'menu',

    [string]$InstallPath = '',
    [string]$ServiceName = '',
    [string]$ServiceArgs = '',
    [switch]$NoService,
    [switch]$NoStart,
    [string]$SourcePath = '',
    [switch]$Quiet,
    [switch]$Force,

    [string[]]$Urls,
    [string]$UrlFile = '',
    [string]$Exe = '',
    [ValidateSet('quick', 'standard', 'full')]
    [string]$ScanLevel = 'standard',
    [ValidateSet('auto', 'tls12', 'tls13', 'all')]
    [string]$Protocols = 'auto',
    [ValidateSet('4', '6', 'auto')]
    [string]$IpVersion = '4',
    [ValidateRange(1, 120)]
    [int]$TimeoutSec = 5,
    [ValidateRange(1, 60)]
    [int]$StartupTimeoutSec = 10,
    [ValidateRange(0, 10)]
    [int]$Repeats = 0,
    [ValidateSet('ask', 'cloudflare', 'google', 'system')]
    [string]$DnsResolver = 'ask',
    [string]$DnsAddr = '',
    [ValidateRange(1, 65535)]
    [int]$DnsPort = 53,
    [string]$DnsV6Addr = '',
    [ValidateRange(1, 65535)]
    [int]$DnsV6Port = 53,
    [string]$Json = '',
    [ValidateRange(0, 2147483647)]
    [int]$MaxStrategies = 0,
    [ValidateRange(0, 5000)]
    [int]$SettleMs = 200,
    [switch]$NoDnsFlush,
    [switch]$DryRun,
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'

$script:BoundParameters = @{}
foreach ($key in $PSBoundParameters.Keys) {
    $script:BoundParameters[$key] = $PSBoundParameters[$key]
}

$script:UserAgent = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36'
$script:WorkRoot = $null
$script:ActiveProcess = $null
$script:ScanRestartService = $null
$script:DefaultServiceName = 'ShadowDPI'
$script:DefaultServiceArgs = '-9'
$script:DefaultStartType = 'auto'
$script:InstallMetadataName = 'install.json'
$script:ScriptPath = $PSCommandPath
$script:VersionCache = @{}

function Write-Info {
    param([string]$Message)
    Write-Host $Message
}

function Write-Warn {
    param([string]$Message)
    Write-Warning $Message
}

function Read-ValueWithDefault {
    param(
        [string]$Prompt,
        [string]$Default = ''
    )

    $suffix = ''
    if ($Default) {
        $suffix = " [$Default]"
    }

    $text = [string](Read-Host ($Prompt + $suffix))
    $text = $text.Trim()
    if (-not $text) {
        return $Default
    }
    return $text
}

function Read-YesNo {
    param(
        [string]$Prompt,
        [bool]$Default = $true
    )

    $suffix = '[y/N]'
    if ($Default) {
        $suffix = '[Y/n]'
    }

    while ($true) {
        $text = [string](Read-Host ($Prompt + ' ' + $suffix))
        $text = $text.Trim()
        if (-not $text) {
            return $Default
        }

        switch ($text.ToLowerInvariant()) {
            'y' { return $true }
            'yes' { return $true }
            'n' { return $false }
            'no' { return $false }
        }

        Write-Warn 'Enter Y or N.'
    }
}

function Test-InteractiveHost {
    if ($Host.Name -ne 'ConsoleHost') {
        return $false
    }

    try {
        if ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected) {
            return $false
        }
    }
    catch {
        return $false
    }

    return $true
}

function Read-CustomDns {
    param(
        [int]$DefaultPort = 53,
        [int]$DefaultV6Port = 53
    )

    while ($true) {
        $address = [string](Read-Host 'DNS address (IPv4 or IPv6)')
        $address = $address.Trim()
        if (-not $address) {
            Write-Warn 'Enter an IPv4 or IPv6 address.'
            continue
        }

        $parsed = $null
        if (-not [System.Net.IPAddress]::TryParse($address, [ref]$parsed)) {
            Write-Warn "Not a valid IP address: $address"
            continue
        }

        $isV6 = $parsed.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetworkV6

        $portDefault = $DefaultPort
        if ($isV6) {
            $portDefault = $DefaultV6Port
        }

        $port = $portDefault
        while ($true) {
            $portText = [string](Read-Host "DNS port [$portDefault]")
            $portText = $portText.Trim()
            if (-not $portText) {
                break
            }

            $parsedPort = 0
            if ([int]::TryParse($portText, [ref]$parsedPort) -and $parsedPort -ge 1 -and $parsedPort -le 65535) {
                $port = $parsedPort
                break
            }

            Write-Warn 'Enter a port from 1 to 65535.'
        }

        return [pscustomobject]@{
            Address = $address
            Port    = $port
            IsV6    = $isV6
        }
    }
}

function Read-DnsChoice {
    param(
        [int]$DefaultPort = 53,
        [int]$DefaultV6Port = 53
    )

    Write-Info ''
    Write-Info 'DNS resolver for the scan'
    Write-Info '  1. Cloudflare 1.1.1.1 (recommended)'
    Write-Info '  2. Google 8.8.8.8'
    Write-Info '  3. Other'
    Write-Info '  4. Do not specify (use the system resolver)'

    while ($true) {
        $answer = [string](Read-Host 'Choice [1]')
        $answer = $answer.Trim()
        if (-not $answer) {
            $answer = '1'
        }

        if ($answer -eq '1') {
            return [pscustomobject]@{ Address = '1.1.1.1'; Port = $DefaultPort; IsV6 = $false }
        }
        if ($answer -eq '2') {
            return [pscustomobject]@{ Address = '8.8.8.8'; Port = $DefaultPort; IsV6 = $false }
        }
        if ($answer -eq '3') {
            return Read-CustomDns -DefaultPort $DefaultPort -DefaultV6Port $DefaultV6Port
        }
        if ($answer -eq '4') {
            return $null
        }

        Write-Warn 'Enter 1, 2, 3, or 4.'
    }
}

function Resolve-ExePath {
    param([string]$Path)

    if ($Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "shadowdpi.exe was not found at $Path"
        }
        return (Resolve-Path -LiteralPath $Path).Path
    }

    $installPath = Get-InstallPath
    $candidates = @(
        (Join-Path $installPath 'shadowdpi.exe'),
        (Join-Path $PSScriptRoot 'shadowdpi.exe'),
        (Join-Path $PSScriptRoot 'build\msvc-x64\Release\shadowdpi.exe'),
        (Join-Path $PSScriptRoot 'build\msvc-x64\Debug\shadowdpi.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'shadowdpi.exe was not found. Pass -Exe or run the script from an extracted release folder.'
}

function ConvertFrom-NativeOutput {
    param([object[]]$Items)

    $lines = @()
    foreach ($item in $Items) {
        if ($item -is [System.Management.Automation.ErrorRecord]) {
            $lines += [string]$item.Exception.Message
        }
        else {
            $lines += [string]$item
        }
    }

    return (($lines | Out-String).TrimEnd())
}

function Invoke-NativeText {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    # Windows PowerShell 5.1 wraps native stderr in an ErrorRecord, which would
    # become terminating under ErrorActionPreference=Stop. Unwrap the records
    # so the caller receives plain text.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $items = @(& $FilePath @Arguments 2>&1)
        return (ConvertFrom-NativeOutput -Items $items)
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

function Invoke-NativeResult {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $items = @(& $FilePath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
        return [pscustomobject]@{
            Output   = (ConvertFrom-NativeOutput -Items $items)
            ExitCode = $exitCode
        }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

function Invoke-ScCommandLine {
    param([string]$Arguments)

    $sc = Join-Path $env:SystemRoot 'System32\sc.exe'
    $stdoutFile = [IO.Path]::GetTempFileName()
    $stderrFile = [IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $sc -ArgumentList $Arguments -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile -WindowStyle Hidden
        $stdout = [string](Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue)
        $stderr = [string](Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue)
        $text = ($stdout + $stderr).Trim()
        return [pscustomobject]@{
            Output   = $text
            ExitCode = $process.ExitCode
        }
    }
    finally {
        Remove-Item -LiteralPath $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue
    }
}

function Resolve-FullPath {
    param([string]$Path)

    $text = ([string]$Path).Trim()
    if (-not $text) {
        throw 'A path is required.'
    }

    if ($text.StartsWith('"') -and $text.EndsWith('"') -and $text.Length -ge 2) {
        $text = $text.Substring(1, $text.Length - 2)
    }

    $text = [Environment]::ExpandEnvironmentVariables($text)
    if ($text -eq '~') {
        $text = $HOME
    }
    elseif ($text.StartsWith('~\') -or $text.StartsWith('~/')) {
        $text = Join-Path $HOME $text.Substring(2)
    }

    if (-not [IO.Path]::IsPathRooted($text)) {
        $text = Join-Path (Get-Location).ProviderPath $text
    }

    $full = [IO.Path]::GetFullPath($text)
    if ($full.Length -gt 3) {
        $full = $full.TrimEnd([char[]]@('\', '/'))
    }
    return $full
}

function Get-CanonicalPath {
    param([string]$Path)

    $full = Resolve-FullPath -Path $Path
    $root = [IO.Path]::GetPathRoot($full)
    if (-not $root) {
        return $full
    }

    $current = $root
    $relative = $full.Substring($root.Length)
    $parts = @($relative -split '[\\/]' | Where-Object { $_ })
    $depth = 0

    foreach ($part in $parts) {
        $depth++
        if ($depth -gt 64) {
            break
        }

        $next = Join-Path $current $part
        $item = $null
        try {
            $item = Get-Item -LiteralPath $next -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                $target = $item.Target
                if ($target -is [array]) {
                    $target = $target[0]
                }
                if ($target) {
                    if (-not [IO.Path]::IsPathRooted($target)) {
                        $target = Join-Path $current $target
                    }
                    $current = Resolve-FullPath -Path $target
                    continue
                }
            }
        }
        catch {
        }

        if ($item) {
            $current = $item.FullName
        }
        else {
            $current = $next
        }
    }

    return (Resolve-FullPath -Path $current)
}

function Test-PathWithin {
    param(
        [string]$Path,
        [string]$Root
    )

    $value = $Path.TrimEnd([char[]]@('\', '/'))
    $base = $Root.TrimEnd([char[]]@('\', '/'))
    if ($value.Equals($base, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    return $value.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Get-DefaultInstallPath {
    $base = $env:ProgramW6432
    if (-not $base) {
        $base = [Environment]::GetFolderPath('ProgramFiles')
    }
    if (-not $base) {
        $base = $env:ProgramFiles
    }
    if (-not $base) {
        throw 'The Program Files folder could not be located.'
    }

    return (Join-Path $base 'ShadowDPI')
}

function Assert-X64Windows {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'ShadowDPI requires 64-bit Windows.'
    }
}

function Get-VersionNumber {
    param([string]$Text)

    if (-not $Text) {
        return ''
    }

    $match = [regex]::Match($Text, '(\d+(?:\.\d+){1,3})')
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return ''
}

function Assert-ServiceName {
    param([string]$Name)

    if (-not $Name) {
        throw 'A service name is required.'
    }
    if ($Name.Length -gt 200) {
        throw 'The service name is too long.'
    }
    if (-not [regex]::IsMatch($Name, '^[A-Za-z0-9][A-Za-z0-9._ -]*$', [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
        throw "Invalid service name: $Name. Use letters, digits, spaces, dots, underscores, and hyphens."
    }
}

function Assert-SafeInstallPath {
    param([string]$Path)

    $full = Get-CanonicalPath -Path $Path
    $trimmed = $full.TrimEnd([char[]]@('\', '/'))
    $root = [IO.Path]::GetPathRoot($full)
    if ($root) {
        $rootTrimmed = $root.TrimEnd([char[]]@('\', '/'))
        if ($trimmed.Equals($rootTrimmed, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to use a drive or share root as the install path: $full"
        }
    }

    $protectedTrees = @(
        $env:SystemRoot,
        $env:ProgramData,
        (Join-Path $env:SystemDrive 'Users')
    )
    foreach ($candidate in $protectedTrees) {
        if (-not $candidate) {
            continue
        }
        $resolved = Get-CanonicalPath -Path $candidate
        if (Test-PathWithin -Path $full -Root $resolved) {
            throw "Refusing to use a protected system folder as the install path: $full"
        }
    }

    $exactRoots = @(
        $env:ProgramFiles,
        $env:ProgramW6432,
        ${env:ProgramFiles(x86)}
    )
    foreach ($candidate in $exactRoots) {
        if (-not $candidate) {
            continue
        }
        $resolved = Get-CanonicalPath -Path $candidate
        if ($trimmed.Equals($resolved.TrimEnd([char[]]@('\', '/')), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to use a protected system folder as the install path: $full"
        }
    }

    $programRoots = @($env:ProgramFiles, $env:ProgramW6432, ${env:ProgramFiles(x86)})
    foreach ($programRoot in $programRoots) {
        if (-not $programRoot) {
            continue
        }
        foreach ($name in 'Common Files', 'WindowsApps') {
            $resolved = Get-CanonicalPath -Path (Join-Path $programRoot $name)
            if (Test-PathWithin -Path $full -Root $resolved) {
                throw "Refusing to use a protected system folder as the install path: $full"
            }
        }
    }

    return $full
}

function Get-ServiceLaunch {
    param([string]$PathName)

    $result = [pscustomobject]@{
        ExePath   = ''
        Arguments = ''
    }
    if (-not $PathName) {
        return $result
    }

    $text = $PathName.Trim()
    if ($text.StartsWith('"')) {
        $end = $text.IndexOf('"', 1)
        if ($end -gt 1) {
            $result.ExePath = $text.Substring(1, $end - 1)
            $result.Arguments = $text.Substring($end + 1).Trim()
            return $result
        }
    }

    $match = [regex]::Match($text, '^(?i)(.*?\.exe)(?:\s+(.*))?$')
    if ($match.Success) {
        $result.ExePath = $match.Groups[1].Value.Trim()
        $result.Arguments = $match.Groups[2].Value.Trim()
    }

    return $result
}

function Get-ServiceInfo {
    param([string]$Name)

    if (-not $Name) {
        return $null
    }

    try {
        Assert-ServiceName -Name $Name
    }
    catch {
        return $null
    }

    $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        return $null
    }

    $startMode = ''
    $pathName = ''
    try {
        $filter = "Name='" + ($Name -replace "'", "''") + "'"
        $cim = Get-CimInstance -ClassName Win32_Service -Filter $filter -ErrorAction Stop
        if ($null -ne $cim) {
            $startMode = [string]$cim.StartMode
            $pathName = [string]$cim.PathName
        }
    }
    catch {
    }

    return [pscustomobject]@{
        Name      = [string]$service.Name
        Status    = [string]$service.Status
        StartMode = $startMode
        PathName  = $pathName
    }
}

function Wait-ServiceStatus {
    param(
        [string]$Name,
        [string]$Status,
        [int]$TimeoutSec = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        $service = Get-ServiceInfo -Name $Name
        if ($null -eq $service) {
            return ($Status -eq 'Deleted')
        }
        if ([string]$service.Status -eq $Status) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    }

    return $false
}

function Wait-ForNoShadowProcess {
    param(
        [int]$TimeoutSec = 10,
        [switch]$ThrowIfFound
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        $processes = @(Get-Process -Name 'shadowdpi' -ErrorAction SilentlyContinue)
        if ($processes.Count -eq 0) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    }

    if ($ThrowIfFound) {
        throw 'shadowdpi.exe is still running. Close console instances and try again.'
    }
    return $false
}

function Stop-ShadowService {
    param([string]$Name)

    $service = Get-ServiceInfo -Name $Name
    if ($null -eq $service) {
        return $false
    }
    if ($service.Status -eq 'Stopped') {
        return $false
    }

    Write-Info "Stopping service $Name..."
    $result = Invoke-NativeResult -FilePath (Join-Path $env:SystemRoot 'System32\sc.exe') -Arguments @('stop', $Name)
    if ($result.ExitCode -ne 0 -and $result.ExitCode -notin 1056, 1060, 1062) {
        throw "Could not stop service $Name. $($result.Output)"
    }

    if (-not (Wait-ServiceStatus -Name $Name -Status 'Stopped' -TimeoutSec 30)) {
        throw "Service $Name did not stop within 30 seconds."
    }

    [void](Wait-ForNoShadowProcess -TimeoutSec 10)
    return $true
}

function Start-ShadowService {
    param([string]$Name)

    $service = Get-ServiceInfo -Name $Name
    if ($null -eq $service) {
        throw "Service $Name does not exist."
    }
    if ($service.Status -eq 'Running') {
        return
    }

    Write-Info "Starting service $Name..."
    $result = Invoke-NativeResult -FilePath (Join-Path $env:SystemRoot 'System32\sc.exe') -Arguments @('start', $Name)
    if ($result.ExitCode -ne 0 -and $result.ExitCode -ne 1056) {
        throw "Could not start service $Name. $($result.Output)"
    }

    if (-not (Wait-ServiceStatus -Name $Name -Status 'Running' -TimeoutSec 30)) {
        $latest = Get-ServiceInfo -Name $Name
        $status = 'missing'
        if ($null -ne $latest) {
            $status = $latest.Status
        }
        throw "Service $Name did not reach Running. Current status: $status."
    }
}

function ConvertTo-ScStartMode {
    param([string]$StartType)

    if (-not $StartType) {
        throw 'A service start type is required.'
    }

    switch ($StartType.ToLowerInvariant()) {
        'auto' { return 'auto' }
        'automatic' { return 'auto' }
        'demand' { return 'demand' }
        'manual' { return 'demand' }
        'disabled' { return 'disabled' }
    }

    throw "Unknown service start type: $StartType"
}

function ConvertFrom-ScStartMode {
    param([string]$StartMode)

    switch ($StartMode) {
        'Auto' { return 'auto' }
        'Manual' { return 'demand' }
        'Disabled' { return 'disabled' }
    }

    return 'auto'
}

function ConvertTo-ServiceStartupType {
    param([string]$StartType)

    switch (ConvertTo-ScStartMode -StartType $StartType) {
        'auto' { return 'Automatic' }
        'demand' { return 'Manual' }
        'disabled' { return 'Disabled' }
    }
}

function Get-ServiceBinaryPath {
    param(
        [string]$ExePath,
        [string]$Arguments
    )

    $value = '"' + $ExePath + '"'
    if ($Arguments) {
        $value = $value + ' ' + $Arguments
    }
    return $value
}

function New-ShadowService {
    param(
        [string]$Name,
        [string]$ExePath,
        [string]$Arguments,
        [string]$StartType
    )

    Assert-ServiceName -Name $Name
    $binaryPath = Get-ServiceBinaryPath -ExePath $ExePath -Arguments $Arguments
    $startup = ConvertTo-ServiceStartupType -StartType $StartType

    Write-Info "Creating service $Name..."
    try {
        New-Service -Name $Name -BinaryPathName $binaryPath -DisplayName $Name -StartupType $startup -ErrorAction Stop | Out-Null
    }
    catch {
        throw "Could not create service $Name. $($_.Exception.Message)"
    }

    $description = Invoke-ScCommandLine -Arguments ('description "' + $Name + '" "Deep Packet Inspection circumvention utility"')
    if ($description.ExitCode -ne 0) {
        Write-Warn "The service description could not be set. $($description.Output)"
    }
}

function Set-ShadowService {
    param(
        [string]$Name,
        [string]$ExePath,
        [string]$Arguments,
        [string]$StartType
    )

    Assert-ServiceName -Name $Name
    $binaryPath = Get-ServiceBinaryPath -ExePath $ExePath -Arguments $Arguments
    $startMode = ConvertTo-ScStartMode -StartType $StartType
    $quotedBinary = '"' + ($binaryPath -replace '"', '\"') + '"'

    Write-Info "Updating service $Name..."
    $argumentLine = 'config "' + $Name + '" binPath= ' + $quotedBinary + ' start= ' + $startMode + ' DisplayName= "' + $Name + '"'
    $result = Invoke-ScCommandLine -Arguments $argumentLine
    if ($result.ExitCode -ne 0) {
        throw "Could not update service $Name. $($result.Output)"
    }

    $description = Invoke-ScCommandLine -Arguments ('description "' + $Name + '" "Deep Packet Inspection circumvention utility"')
    if ($description.ExitCode -ne 0) {
        Write-Warn "The service description could not be set. $($description.Output)"
    }
}

function Remove-ShadowService {
    param([string]$Name)

    $service = Get-ServiceInfo -Name $Name
    if ($null -eq $service) {
        return
    }

    [void](Stop-ShadowService -Name $Name)
    $sc = Join-Path $env:SystemRoot 'System32\sc.exe'
    Write-Info "Deleting service $Name..."
    $result = Invoke-NativeResult -FilePath $sc -Arguments @('delete', $Name)
    if ($result.ExitCode -ne 0 -and $result.ExitCode -notin 1060, 1072) {
        throw "Could not delete service $Name. $($result.Output)"
    }

    if (-not (Wait-ServiceStatus -Name $Name -Status 'Deleted' -TimeoutSec 30)) {
        throw "Service $Name is still registered after a delete request."
    }
}

function Test-ServiceOwnership {
    param(
        [string]$Name,
        [string]$ExePath
    )

    $service = Get-ServiceInfo -Name $Name
    if ($null -eq $service) {
        return $true
    }
    if (-not $service.PathName) {
        return $true
    }

    $launch = Get-ServiceLaunch -PathName $service.PathName
    if (-not $launch.ExePath) {
        return $false
    }

    try {
        return (Test-SamePath -First (Split-Path -Parent $launch.ExePath) -Second (Split-Path -Parent $ExePath))
    }
    catch {
        return $false
    }
}

function Update-ServiceConfiguration {
    param(
        [string]$ExePath,
        [string]$Name,
        [string]$Arguments,
        [string]$StartType,
        [bool]$Enabled,
        [bool]$StartAfter,
        [string]$OldName
    )

    if (-not $Enabled) {
        if ($OldName) {
            Remove-ShadowService -Name $OldName
        }
        return $false
    }

    Assert-ServiceName -Name $Name
    $existing = Get-ServiceInfo -Name $Name
    if ($null -ne $existing) {
        if (-not (Test-ServiceOwnership -Name $Name -ExePath $ExePath)) {
            $where = $existing.PathName
            if (-not $where) {
                $where = 'another folder'
            }
            throw "A service named $Name already exists at $where. Pass -ServiceName with a different name or remove that service first."
        }
        Set-ShadowService -Name $Name -ExePath $ExePath -Arguments $Arguments -StartType $StartType
    }
    else {
        New-ShadowService -Name $Name -ExePath $ExePath -Arguments $Arguments -StartType $StartType
    }

    if ($StartAfter) {
        Start-ShadowService -Name $Name
    }
    else {
        [void](Stop-ShadowService -Name $Name)
    }

    if ($OldName -and $OldName -ne $Name) {
        Remove-ShadowService -Name $OldName
    }

    $service = Get-ServiceInfo -Name $Name
    return ($null -ne $service -and $service.Status -eq 'Running')
}

function Restore-ShadowService {
    param(
        [string]$Name,
        [string]$ExePath,
        [string]$Arguments,
        [string]$StartType,
        [bool]$WasRunning
    )

    if (-not $Name) {
        return
    }

    try {
        if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
            Write-Warn "The service $Name could not be restored because $ExePath is missing."
            return
        }

        [void](Update-ServiceConfiguration -ExePath $ExePath -Name $Name -Arguments $Arguments -StartType $StartType -Enabled $true -StartAfter $WasRunning -OldName '')
        Write-Info "Service $Name was restored after the failed action."
    }
    catch {
        Write-Warn "The service $Name could not be restored: $($_.Exception.Message)"
    }
}

function Read-InstallMetadata {
    param([string]$Path)

    $file = Join-Path $Path $script:InstallMetadataName
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        return $null
    }

    try {
        return (Get-Content -LiteralPath $file -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json)
    }
    catch {
        Write-Warn "Could not read $file. The installer will use defaults."
        return $null
    }
}

function Get-InstallConfig {
    param($Metadata)

    $config = [pscustomobject]@{
        ServiceEnabled = $false
        ServiceName    = $script:DefaultServiceName
        ServiceArgs    = $script:DefaultServiceArgs
        StartType      = $script:DefaultStartType
    }

    if ($null -eq $Metadata) {
        return $config
    }

    $config.ServiceEnabled = $true
    if ($null -ne $Metadata.service) {
        if ($null -ne $Metadata.service.enabled) {
            if ($Metadata.service.enabled -is [bool]) {
                $config.ServiceEnabled = $Metadata.service.enabled
            }
            elseif ([string]$Metadata.service.enabled -match '^(?i)(true|1|yes)$') {
                $config.ServiceEnabled = $true
            }
            else {
                $config.ServiceEnabled = $false
            }
        }
        if ($Metadata.service.name) {
            $config.ServiceName = [string]$Metadata.service.name
        }
        if ($null -ne $Metadata.service.arguments) {
            $config.ServiceArgs = [string]$Metadata.service.arguments
        }
        if ($Metadata.service.startType) {
            $config.StartType = [string]$Metadata.service.startType
        }
    }

    return $config
}

function Get-InstalledState {
    param([string]$Path)

    if (-not $Path) {
        $Path = Get-DefaultInstallPath
    }

    $exePath = Join-Path $Path 'shadowdpi.exe'
    $metadataPath = Join-Path $Path $script:InstallMetadataName
    $exeExists = Test-Path -LiteralPath $exePath -PathType Leaf
    $metadata = Read-InstallMetadata -Path $Path
    $exists = $exeExists -or ($null -ne $metadata)

    $versionText = ''
    if ($null -ne $metadata -and $metadata.versionText) {
        $versionText = [string]$metadata.versionText
    }
    elseif ($exeExists) {
        $versionText = Get-ExeVersion -ExePath $exePath
    }

    $config = Get-InstallConfig -Metadata $metadata
    $service = Get-ServiceInfo -Name $config.ServiceName

    if ($null -eq $metadata) {
        $config.ServiceEnabled = ($null -ne $service)
        if ($null -ne $service) {
            $launch = Get-ServiceLaunch -PathName $service.PathName
            if ($launch.Arguments) {
                $config.ServiceArgs = $launch.Arguments
            }
            $config.StartType = ConvertFrom-ScStartMode -StartMode $service.StartMode
        }
    }

    return [pscustomobject]@{
        Path          = $Path
        Exists        = $exists
        ExePath       = $exePath
        ExeExists     = $exeExists
        MetadataPath  = $metadataPath
        Metadata      = $metadata
        VersionText   = $versionText
        Version       = (Get-VersionNumber -Text $versionText)
        ServiceEnabled = $config.ServiceEnabled
        ServiceName   = $config.ServiceName
        ServiceArgs   = $config.ServiceArgs
        StartType     = $config.StartType
        Service       = $service
    }
}

function Get-InstallPath {
    if ($script:BoundParameters.ContainsKey('InstallPath') -and $InstallPath) {
        return (Assert-SafeInstallPath -Path $InstallPath)
    }

    $defaultPath = Get-DefaultInstallPath
    $metadata = Read-InstallMetadata -Path $defaultPath
    if ($null -ne $metadata -and $metadata.installPath) {
        return (Assert-SafeInstallPath -Path ([string]$metadata.installPath))
    }

    if ($null -eq $metadata -and -not (Test-Path -LiteralPath (Join-Path $defaultPath 'shadowdpi.exe') -PathType Leaf)) {
        $names = @()
        if ($script:BoundParameters.ContainsKey('ServiceName') -and $ServiceName) {
            $names += $ServiceName
        }
        if ($names -notcontains $script:DefaultServiceName) {
            $names += $script:DefaultServiceName
        }

        foreach ($name in $names) {
            $service = Get-ServiceInfo -Name $name
            if ($null -eq $service) {
                continue
            }
            $launch = Get-ServiceLaunch -PathName $service.PathName
            if (-not $launch.ExePath) {
                continue
            }
            if ((Split-Path -Leaf $launch.ExePath) -ne 'shadowdpi.exe') {
                continue
            }
            try {
                return (Assert-SafeInstallPath -Path (Split-Path -Parent $launch.ExePath))
            }
            catch {
                Write-Warn "Ignoring service $name because its path is not safe: $($_.Exception.Message)"
            }
        }
    }

    return $defaultPath
}

function Get-SourceInfo {
    param([string]$Path)

    if ($Path) {
        $base = Resolve-FullPath -Path $Path
    }
    else {
        $base = $PSScriptRoot
    }

    $exePath = Join-Path $base 'shadowdpi.exe'
    $missing = @()
    foreach ($name in 'shadowdpi.exe', 'WinDivert.dll', 'WinDivert64.sys') {
        $candidate = Join-Path $base $name
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $missing += $name
        }
    }

    if ($missing.Count -gt 0) {
        throw "The release folder is incomplete. Missing from $base`: $($missing -join ', ')."
    }

    $versionText = Get-ExeVersion -ExePath $exePath
    return [pscustomobject]@{
        Path        = $base
        ExePath     = $exePath
        VersionText = $versionText
        Version     = (Get-VersionNumber -Text $versionText)
    }
}

function Test-SamePath {
    param(
        [string]$First,
        [string]$Second
    )

    $a = Get-CanonicalPath -Path $First
    $b = Get-CanonicalPath -Path $Second
    return $a.Equals($b, [StringComparison]::OrdinalIgnoreCase)
}

function Copy-InstallPayload {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [string]$ScriptPath
    )

    Write-Info "Installing files from $SourcePath"
    $canonicalSource = Get-CanonicalPath -Path $SourcePath
    $canonicalDestination = Get-CanonicalPath -Path $DestinationPath
    if ($canonicalDestination.StartsWith($canonicalSource + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "The install folder must not be inside the release folder: $DestinationPath"
    }

    if (-not (Test-Path -LiteralPath $DestinationPath -PathType Container)) {
        New-Item -ItemType Directory -Path $DestinationPath -Force | Out-Null
    }

    $files = @(
        'shadowdpi.exe',
        'WinDivert.dll',
        'WinDivert64.sys',
        'LICENSE',
        'NOTICE',
        'README.md',
        'install.ps1'
    )

    $payload = @()
    foreach ($name in $files) {
        $source = Join-Path $SourcePath $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            if ($name -eq 'install.ps1' -and $ScriptPath -and (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
                $source = $ScriptPath
            }
            else {
                continue
            }
        }

        $destination = Join-Path $DestinationPath $name
        if ((Test-SamePath -First $source -Second $destination)) {
            $payload += $name
            continue
        }

        try {
            Copy-Item -LiteralPath $source -Destination $destination -Force -ErrorAction Stop
            $payload += $name
        }
        catch {
            if ($_.Exception.Message -match '(?i)itself|same file') {
                $payload += $name
                continue
            }
            throw
        }
    }

    foreach ($name in 'docs', 'tools', 'resources') {
        $sourceDir = Join-Path $SourcePath $name
        if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
            continue
        }

        $destinationDir = Join-Path $DestinationPath $name
        if ((Test-SamePath -First $sourceDir -Second $destinationDir)) {
            foreach ($item in @(Get-ChildItem -LiteralPath $sourceDir -File -Recurse -Force)) {
                $payload += $name + '\' + $item.FullName.Substring($sourceDir.Length).TrimStart([char[]]@('\', '/'))
            }
            continue
        }
        if ((Test-PathWithin -Path (Get-CanonicalPath -Path $destinationDir) -Root (Get-CanonicalPath -Path $sourceDir))) {
            continue
        }

        if (-not (Test-Path -LiteralPath $destinationDir -PathType Container)) {
            New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
        }

        $items = @(Get-ChildItem -LiteralPath $sourceDir -Force)
        if ($items.Count -gt 0) {
            try {
                Copy-Item -Path (Join-Path $sourceDir '*') -Destination $destinationDir -Recurse -Force -ErrorAction Stop
            }
            catch {
                if ($_.Exception.Message -notmatch '(?i)itself|same file') {
                    throw
                }
            }
        }

        foreach ($item in @(Get-ChildItem -LiteralPath $sourceDir -File -Recurse -Force)) {
            $payload += $name + '\' + $item.FullName.Substring($sourceDir.Length).TrimStart([char[]]@('\', '/'))
        }
    }

    foreach ($name in 'shadowdpi.exe', 'WinDivert.dll', 'WinDivert64.sys') {
        $installed = Join-Path $DestinationPath $name
        if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) {
            throw "$name was not installed in $DestinationPath."
        }
    }

    return $payload
}

function Remove-StalePayload {
    param(
        [string]$Path,
        [object[]]$OldPayload,
        [string[]]$NewPayload
    )

    if (-not $OldPayload -or $OldPayload.Count -eq 0) {
        return
    }

    $root = Get-CanonicalPath -Path $Path
    $keep = @{}
    foreach ($item in $NewPayload) {
        $key = ([string]$item).Trim().Replace('/', '\').TrimStart([char[]]@('\'))
        if ($key) {
            $keep[$key.ToLowerInvariant()] = $true
        }
    }

    $removed = 0
    $directories = @{}
    foreach ($item in $OldPayload) {
        $relative = ([string]$item).Trim().Replace('/', '\').TrimStart([char[]]@('\'))
        if (-not $relative -or [IO.Path]::IsPathRooted($relative)) {
            continue
        }

        $segments = @($relative -split '\\' | Where-Object { $_ })
        if ($segments.Count -eq 0 -or $segments -contains '..') {
            continue
        }
        $relative = $segments -join '\'

        if ($keep.ContainsKey($relative.ToLowerInvariant())) {
            continue
        }

        $target = Join-Path $Path $relative
        try {
            if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
                continue
            }
            if (-not (Test-PathWithin -Path (Get-CanonicalPath -Path $target) -Root $root)) {
                Write-Warn "Skipping stale file outside the install folder: $target"
                continue
            }

            Remove-Item -LiteralPath $target -Force -ErrorAction Stop
            $removed++
        }
        catch {
            Write-Warn "Could not remove the stale file $target`: $($_.Exception.Message)"
            continue
        }

        $parent = Split-Path -Parent $relative
        while ($parent) {
            $directories[$parent] = $true
            $parent = Split-Path -Parent $parent
        }
    }

    foreach ($directory in @($directories.Keys | Sort-Object Length -Descending)) {
        $full = Join-Path $Path $directory
        if (-not (Test-Path -LiteralPath $full -PathType Container)) {
            continue
        }
        if (@(Get-ChildItem -LiteralPath $full -Force).Count -eq 0) {
            try {
                Remove-Item -LiteralPath $full -Force -ErrorAction Stop
            }
            catch {
            }
        }
    }

    if ($removed -gt 0) {
        Write-Info "Removed $removed stale file(s) from the previous release."
    }
}

function Write-InstallMetadata {
    param(
        [string]$Path,
        $Metadata,
        [string]$VersionText,
        [string]$SourcePath,
        [bool]$ServiceEnabled,
        [string]$ServiceName,
        [string]$ServiceArgs,
        [string]$StartType,
        [bool]$Running,
        [object[]]$Payload = $null
    )

    $now = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ', [Globalization.CultureInfo]::InvariantCulture)
    $installedUtc = $now
    if ($null -ne $Metadata -and $Metadata.installedUtc) {
        $installedUtc = [string]$Metadata.installedUtc
    }

    $payloadList = @()
    if ($null -ne $Payload) {
        $payloadList = @($Payload)
    }
    elseif ($null -ne $Metadata -and $null -ne $Metadata.payload) {
        $payloadList = @($Metadata.payload)
    }

    $data = [ordered]@{
        product       = 'ShadowDPI'
        schemaVersion = 1
        version       = (Get-VersionNumber -Text $VersionText)
        versionText   = $VersionText
        installPath   = $Path
        installedUtc  = $installedUtc
        updatedUtc    = $now
        sourcePath    = $SourcePath
        payload       = @($payloadList)
        service       = [ordered]@{
            enabled   = $ServiceEnabled
            name      = $ServiceName
            arguments = $ServiceArgs
            startType = $StartType
            running   = $Running
        }
    }

    $json = $data | ConvertTo-Json -Depth 5
    $file = Join-Path $Path $script:InstallMetadataName
    $temp = $file + '.tmp'
    try {
        [IO.File]::WriteAllText($temp, $json, (New-Object System.Text.UTF8Encoding($false)))
        Move-Item -LiteralPath $temp -Destination $file -Force -ErrorAction Stop
    }
    finally {
        if (Test-Path -LiteralPath $temp -PathType Leaf) {
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        }
    }
}

function Read-UrlList {
    param(
        [string[]]$InlineUrls,
        [string]$File
    )

    $lines = @()
    if ($InlineUrls) {
        $lines = @($InlineUrls)
    }
    else {
        if (-not $File) {
            $File = Join-Path $PSScriptRoot 'tools\scan-urls.txt'
        }
        if (-not (Test-Path -LiteralPath $File -PathType Leaf)) {
            throw "URL list was not found at $File"
        }
        $lines = @(Get-Content -LiteralPath $File -Encoding UTF8 -ErrorAction Stop)
    }

    $result = New-Object System.Collections.Generic.List[string]
    $seen = @{}

    foreach ($line in $lines) {
        if ($null -eq $line) {
            continue
        }

        $text = ([string]$line).Trim()
        if (-not $text) {
            continue
        }
        if ($text.StartsWith('#') -or $text.StartsWith(';')) {
            continue
        }

        if ($text -notmatch '^[a-zA-Z][a-zA-Z0-9+.-]*://') {
            $text = 'https://' + $text
        }

        $uri = $null
        if (-not [uri]::TryCreate($text, [UriKind]::Absolute, [ref]$uri)) {
            throw "invalid URL: $text"
        }
        if ($uri.Scheme -ne 'http' -and $uri.Scheme -ne 'https') {
            throw "only http and https URLs are supported: $text"
        }
        if (-not $uri.Host) {
            throw "URL has no host: $text"
        }

        $key = $text.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $result.Add($text)
        }
    }

    if ($result.Count -eq 0) {
        if ($InlineUrls) {
            throw 'no URLs to test'
        }
        throw "no URLs to test in $File"
    }

    return $result.ToArray()
}

function Get-ProbeVariants {
    param(
        [string]$Url,
        [string]$ProtocolMode
    )

    $uri = [uri]$Url
    if ($uri.Scheme -eq 'http') {
        return @('http')
    }

    switch ($ProtocolMode) {
        'auto' {
            return @('https')
        }
        'tls12' {
            return @('tls12')
        }
        'tls13' {
            return @('tls13')
        }
        'all' {
            return @('tls12', 'tls13')
        }
        default {
            throw "unknown protocol mode: $ProtocolMode"
        }
    }
}

function New-Candidate {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [int]$MinLevel
    )

    $safeArgs = @()
    if ($Arguments) {
        $safeArgs = @($Arguments)
    }

    return [pscustomobject]@{
        Name     = $Name
        Args     = $safeArgs
        MinLevel = $MinLevel
    }
}

function Get-Strategies {
    param(
        [string]$Level,
        [string[]]$DnsArgs,
        [string[]]$FakeSniHosts
    )

    $levelValue = @{
        quick    = 1
        standard = 2
        full     = 3
    }[$Level]

    $all = New-Object System.Collections.Generic.List[object]

    for ($preset = 1; $preset -le 9; $preset++) {
        $all.Add((New-Candidate "preset $preset" @("-$preset") 1))
    }

    foreach ($preset in 5, 9) {
        foreach ($ttl in 3, 5, 7) {
            $all.Add((New-Candidate "preset $preset, fake TTL $ttl" @("-$preset", '--fake-ttl', "$ttl") 2))
        }
    }

    $fragmentPairs = @(
        @(1, 1),
        @(1, 2),
        @(2, 1),
        @(2, 2),
        @(2, 40),
        @(40, 40)
    )
    foreach ($pair in $fragmentPairs) {
        $name = "split HTTP $($pair[0]), TLS $($pair[1]), reverse"
        $all.Add((New-Candidate $name @('-f', "$($pair[0])", '-e', "$($pair[1])", '--reverse-frag') 2))
    }

    $all.Add((New-Candidate 'split HTTP 2, TLS 2, native only' @('-f', '2', '-e', '2', '--native-frag') 2))
    $all.Add((New-Candidate 'split by SNI, reverse' @('-f', '2', '-e', '2', '--frag-by-sni', '--reverse-frag') 2))
    $all.Add((New-Candidate 'passive blocking and Host tricks' @('-p', '-r', '-s') 2))
    $all.Add((New-Candidate 'passive blocking, extra space' @('-p', '-a') 2))
    $all.Add((New-Candidate 'passive blocking, mixed-case Host' @('-p', '-r', '-s', '-m') 2))
    $all.Add((New-Candidate 'passive blocking, TLS split 2' @('-p', '-r', '-s', '-e', '2') 2))
    $all.Add((New-Candidate 'passive blocking only' @('-p') 2))
    $all.Add((New-Candidate 'QUIC blocking only' @('-q') 2))

    $all.Add((New-Candidate 'fake TTL 5 only' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '5') 2))
    $all.Add((New-Candidate 'fake TTL 3 only' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '3') 2))
    $all.Add((New-Candidate 'fake TTL 5, wrong SEQ' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '5', '--fake-wrong-seq') 2))
    $all.Add((New-Candidate 'fake TTL 5, wrong checksum' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '5', '--fake-wrong-checksum') 2))
    $all.Add((New-Candidate 'fake TTL 5, wrong SEQ and checksum' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '5', '--fake-wrong-seq', '--fake-wrong-checksum') 2))
    $all.Add((New-Candidate 'fake auto TTL only' @('-f', '2', '-e', '2', '--reverse-frag', '--auto-ttl') 2))
    $all.Add((New-Candidate 'fake TTL 5, resend 3' @('-f', '2', '-e', '2', '--reverse-frag', '--fake-ttl', '5', '--fake-resend', '3') 2))

    $all.Add((New-Candidate 'preset 5, split by SNI' @('-5', '--frag-by-sni') 2))
    $all.Add((New-Candidate 'preset 9, split by SNI' @('-9', '--frag-by-sni') 2))
    $all.Add((New-Candidate 'preset 5, HTTP 1, TLS 1' @('-5', '-f', '1', '-e', '1') 2))
    $all.Add((New-Candidate 'preset 5, HTTP 4, TLS 4' @('-5', '-f', '4', '-e', '4') 2))
    $all.Add((New-Candidate 'preset 9, HTTP 1, TLS 1' @('-9', '-f', '1', '-e', '1') 2))

    if ($levelValue -ge 3) {
        foreach ($preset in 5, 9) {
            for ($ttl = 1; $ttl -le 10; $ttl++) {
                $all.Add((New-Candidate "preset $preset, fake TTL $ttl" @("-$preset", '--fake-ttl', "$ttl") 3))
            }
        }

        for ($minTtl = 1; $minTtl -le 6; $minTtl++) {
            $all.Add((New-Candidate "preset 5, auto TTL, min TTL $minTtl" @('-5', '--auto-ttl', '1-4-10', '--min-ttl', "$minTtl") 3))
        }

        $autoRanges = @('1-2-6', '1-3-8', '2-4-10', '2-6-12', '1-4-20')
        foreach ($range in $autoRanges) {
            $all.Add((New-Candidate "preset 5, auto TTL $range" @('-5', '--auto-ttl', $range) 3))
        }

        for ($resend = 2; $resend -le 5; $resend++) {
            $all.Add((New-Candidate "preset 5, fake resend $resend" @('-5', '--fake-resend', "$resend") 3))
        }

        for ($count = 1; $count -le 3; $count++) {
            $all.Add((New-Candidate "preset 5, fake gen $count" @('-5', '--fake-gen', "$count") 3))
        }

        $extraPairs = @(
            @(1, 4),
            @(2, 16),
            @(4, 2),
            @(4, 4),
            @(8, 8),
            @(8, 16),
            @(16, 2),
            @(16, 16),
            @(16, 40),
            @(8, 40)
        )
        foreach ($pair in $extraPairs) {
            $name = "split HTTP $($pair[0]), TLS $($pair[1]), reverse"
            $all.Add((New-Candidate $name @('-f', "$($pair[0])", '-e', "$($pair[1])", '--reverse-frag') 3))
        }

        $all.Add((New-Candidate 'split by SNI, fake TTL 5' @('-f', '2', '-e', '2', '--frag-by-sni', '--reverse-frag', '--fake-ttl', '5') 3))
        $all.Add((New-Candidate 'Host tricks, TLS split 2, fake TTL 5' @('-p', '-r', '-s', '-e', '2', '--fake-ttl', '5', '--reverse-frag') 3))
        $all.Add((New-Candidate 'Host tricks, TLS split 2, auto TTL' @('-p', '-r', '-s', '-e', '2', '--auto-ttl', '--reverse-frag') 3))
        $all.Add((New-Candidate 'extra space, TLS split 2' @('-p', '-a', '-e', '2') 3))
        $all.Add((New-Candidate 'extra space, mixed-case Host, TLS split 2' @('-p', '-a', '-m', '-e', '2') 3))
        $all.Add((New-Candidate 'keep-alive, fake TTL 5' @('-f', '2', '-e', '2', '-n', '--reverse-frag', '--fake-ttl', '5') 3))
        $all.Add((New-Candidate 'preset 9, split by SNI, fake TTL 5' @('-9', '--frag-by-sni', '--fake-ttl', '5') 3))

        foreach ($hostName in $FakeSniHosts) {
            $all.Add((New-Candidate "preset 5, fake SNI $hostName" @('-5', '--fake-sni', $hostName) 3))
            $all.Add((New-Candidate "preset 9, fake SNI $hostName" @('-9', '--fake-sni', $hostName) 3))
        }
    }

    if ($DnsArgs.Count -gt 0) {
        $all.Add((New-Candidate 'DNS redirect only' @() 1))
        foreach ($candidate in $all) {
            $candidate.Args = @($candidate.Args + $DnsArgs)
        }
    }

    $result = New-Object System.Collections.Generic.List[object]
    $seen = @{}
    foreach ($candidate in $all) {
        if ($candidate.MinLevel -gt $levelValue) {
            continue
        }

        $key = ($candidate.Args -join ' ').Trim()
        if ($seen.ContainsKey($key)) {
            continue
        }
        $seen[$key] = $true
        $result.Add($candidate)
    }

    return $result.ToArray()
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Administrator {
    if (-not (Test-IsAdministrator)) {
        throw 'This script must run as Administrator. Right-click PowerShell and choose Run as administrator.'
    }
}

function Assert-NoRunningInstance {
    $processes = @(Get-Process -Name 'shadowdpi' -ErrorAction SilentlyContinue)
    if ($processes.Count -gt 0) {
        throw 'An instance of shadowdpi.exe is running. Close it before scanning.'
    }
}

function Assert-Dependencies {
    param([string]$ExePath)

    $directory = Split-Path -Parent $ExePath
    foreach ($name in 'WinDivert.dll', 'WinDivert64.sys') {
        $path = Join-Path $directory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$name was not found next to shadowdpi.exe in $directory"
        }
    }
}

function Get-ExeVersion {
    param([string]$ExePath)

    if ($script:VersionCache.ContainsKey($ExePath)) {
        return $script:VersionCache[$ExePath]
    }

    $versionText = ''

    try {
        $metadata = Read-InstallMetadata -Path (Split-Path -Parent $ExePath)
        if ($null -ne $metadata -and $metadata.versionText) {
            $versionText = [string]$metadata.versionText
        }
    }
    catch {
    }

    if (-not $versionText) {
        try {
            $product = [string](Get-Item -LiteralPath $ExePath -ErrorAction Stop).VersionInfo.ProductVersion
            if ($product) {
                $versionText = 'ShadowDPI ' + $product
            }
        }
        catch {
        }
    }

    if (-not $versionText -and (Test-IsAdministrator)) {
        try {
            $text = Invoke-NativeText -FilePath $ExePath -Arguments @('--version')
            $lines = @($text -split "`r?`n" | Where-Object { $_.Trim() })
            $line = $lines | Where-Object { $_ -match 'ShadowDPI' } | Select-Object -Last 1
            if ($line) {
                $versionText = ([string]$line).Trim()
            }
        }
        catch {
        }
    }

    if (-not $versionText) {
        $versionText = 'unknown'
    }

    $script:VersionCache[$ExePath] = $versionText
    return $versionText
}

function Test-DnsEndpoint {
    param(
        [string]$Address,
        [int]$Port
    )

    try {
        $parsed = $null
        $isV6 = $false
        if (-not [System.Net.IPAddress]::TryParse($Address, [ref]$parsed)) {
            throw 'not an IP address'
        }
        $isV6 = $parsed.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetworkV6
        $recordType = 'A'
        if ($isV6) {
            $recordType = 'AAAA'
        }

        if ($Port -eq 53 -and $null -ne (Get-Command Resolve-DnsName -ErrorAction SilentlyContinue)) {
            $records = @(Resolve-DnsName -Name example.com -Type $recordType -Server $Address -DnsOnly -ErrorAction Stop)
            if (@($records | Where-Object { $_.IPAddress }).Count -eq 0) {
                Write-Warn "DNS endpoint $Address`:$Port did not return an address for example.com"
            }
            return
        }

        $output = Invoke-NativeText -FilePath 'nslookup' -Arguments @("-type=$recordType", "-port=$Port", '-timeout=2', 'example.com', $Address)
        if ($output -match '(?i)(NXDOMAIN|SERVFAIL|REFUSED|timed out|request timed out)') {
            Write-Warn "DNS endpoint $Address`:$Port did not return an answer for example.com"
        }
    }
    catch {
        Write-Warn "could not test DNS endpoint $Address`:$Port"
    }
}

function Quote-Argument {
    param([string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append([string]::new([char]'\', (2 * $backslashes + 1)))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }

        if ($backslashes -gt 0) {
            [void]$builder.Append([string]::new([char]'\', $backslashes))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashes -gt 0) {
        [void]$builder.Append([string]::new([char]'\', (2 * $backslashes)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Get-CurlFailureReason {
    param([int]$Code)

    switch ($Code) {
        3 { return 'malformed URL' }
        5 { return 'could not resolve proxy' }
        6 { return 'could not resolve host' }
        7 { return 'could not connect' }
        8 { return 'invalid server reply' }
        9 { return 'access denied' }
        16 { return 'HTTP/2 error' }
        18 { return 'partial transfer' }
        23 { return 'write error' }
        28 { return 'operation timed out' }
        35 { return 'TLS connect error' }
        52 { return 'empty reply from server' }
        55 { return 'send error' }
        56 { return 'receive error' }
        58 { return 'local certificate error' }
        60 { return 'certificate verification failed' }
        77 { return 'certificate file problem' }
        92 { return 'HTTP/2 stream error' }
        default { return "curl exit code $Code" }
    }
}

function Test-RedirectAllowed {
    param(
        [string]$Location,
        [string]$OriginalUrl
    )

    if (-not $Location) {
        return $false
    }
    if ($Location -notmatch '^(?i)https?://') {
        return $false
    }

    try {
        $locationUri = [uri]$Location
        $originalUri = [uri]$OriginalUrl
    }
    catch {
        return $false
    }

    $locationHost = $locationUri.Host.ToLowerInvariant()
    $originalHost = $originalUri.Host.ToLowerInvariant()

    return ($locationHost -eq $originalHost -or $locationHost.EndsWith('.' + $originalHost, [StringComparison]::OrdinalIgnoreCase))
}

function Invoke-CurlProbe {
    param(
        [string]$Curl,
        [string]$Url,
        [string]$Variant,
        [int]$TimeoutSec,
        [string]$IpVersion,
        [string]$WorkRoot
    )

    $arguments = @(
        '--silent',
        '--show-error',
        '--http1.1',
        '--output', 'NUL',
        '--user-agent', $script:UserAgent,
        '--max-time', "$TimeoutSec",
        '--connect-timeout', "$([Math]::Min(5, $TimeoutSec))",
        '--write-out', '%{http_code}|%{redirect_url}'
    )

    if ($IpVersion -eq '4') {
        $arguments += '--ipv4'
    }
    elseif ($IpVersion -eq '6') {
        $arguments += '--ipv6'
    }

    if ($Variant -eq 'tls12') {
        $arguments += @('--tlsv1.2', '--tls-max', '1.2')
    }
    elseif ($Variant -eq 'tls13') {
        $arguments += '--tlsv1.3'
    }

    $arguments += $Url

    $stdoutFile = Join-Path $WorkRoot ('curl-out-' + [Guid]::NewGuid().ToString('N') + '.txt')
    $errorFile = Join-Path $WorkRoot ('curl-' + [Guid]::NewGuid().ToString('N') + '.err')
    $exitCode = -1
    $stdoutText = ''
    $stderrText = ''

    try {
        $argumentText = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
        $process = Start-Process -FilePath $Curl -ArgumentList $argumentText -PassThru -Wait -RedirectStandardOutput $stdoutFile -RedirectStandardError $errorFile -WindowStyle Hidden
        $exitCode = $process.ExitCode
    }
    catch {
        $stderrText = 'could not start curl.exe: ' + $_.Exception.Message
    }
    finally {
        if (Test-Path -LiteralPath $stdoutFile) {
            $stdoutText = [string](Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue)
            Remove-Item -LiteralPath $stdoutFile -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $errorFile) {
            $errorText = [string](Get-Content -LiteralPath $errorFile -Raw -ErrorAction SilentlyContinue)
            if ($errorText.Trim()) {
                $stderrText = $errorText
            }
            Remove-Item -LiteralPath $errorFile -Force -ErrorAction SilentlyContinue
        }
    }

    $stderrLine = ''
    $stderrLines = @($stderrText -split "`r?`n" | Where-Object { $_.Trim() })
    if ($stderrLines.Count -gt 0) {
        $stderrLine = ([string]$stderrLines[0]).Trim()
    }

    $text = $stdoutText.Trim()
    $httpCode = 0
    $location = ''
    $match = [regex]::Match($text, '(?m)^\s*(\d{3})\|(.*)$')
    if ($match.Success) {
        $httpCode = [int]$match.Groups[1].Value
        $location = $match.Groups[2].Value.Trim()
    }

    $ok = $false
    $status = 'fail'
    $detail = ''

    if ($exitCode -eq 1 -or $exitCode -eq 2 -or $exitCode -eq 4) {
        $status = 'unsupported'
        $detail = 'curl cannot run this probe'
    }
    elseif ($exitCode -ne 0) {
        $detail = Get-CurlFailureReason $exitCode
        if ($stderrLine) {
            $detail = $detail + ': ' + $stderrLine
        }
    }
    elseif ($httpCode -eq 0) {
        $detail = 'no HTTP response'
    }
    elseif ($httpCode -eq 400) {
        $status = 'http400'
        $detail = 'HTTP 400, the server may have received fake packets'
    }
    elseif ($httpCode -eq 403 -or $httpCode -eq 451 -or ($httpCode -ge 500 -and $httpCode -le 599)) {
        $status = 'blocked'
        $detail = "HTTP $httpCode, possible block page or server error"
    }
    elseif ($httpCode -ge 300 -and $httpCode -lt 400) {
        if (Test-RedirectAllowed -Location $location -OriginalUrl $Url) {
            $ok = $true
            $status = 'ok'
            $detail = "HTTP $httpCode redirect to $location"
        }
        else {
            $status = 'redirect'
            if ($location) {
                $detail = "suspicious redirect $httpCode to $location"
            }
            else {
                $detail = "suspicious redirect $httpCode without a Location header"
            }
        }
    }
    else {
        $ok = $true
        $status = 'ok'
        $detail = "HTTP $httpCode"
    }

    return [pscustomobject]@{
        Url      = $Url
        Variant  = $Variant
        Ok       = $ok
        Status   = $status
        ExitCode = $exitCode
        HttpCode = $httpCode
        Location = $location
        Detail   = $detail
        Attempts = 1
    }
}

function Invoke-ProbeWithRetries {
    param(
        [string]$Curl,
        [string]$Url,
        [string]$Variant,
        [int]$TimeoutSec,
        [string]$IpVersion,
        [string]$WorkRoot,
        [int]$Attempts
    )

    $last = $null
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        $last = Invoke-CurlProbe -Curl $Curl -Url $Url -Variant $Variant -TimeoutSec $TimeoutSec -IpVersion $IpVersion -WorkRoot $WorkRoot
        $last.Attempts = $attempt

        if ($last.Ok -or $last.Status -eq 'unsupported') {
            break
        }
        if ($attempt -lt $Attempts) {
            Start-Sleep -Milliseconds 150
        }
    }

    return $last
}

function Get-ProcessErrorText {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }

    $text = [string](Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue)
    $lines = @($text -split "`r?`n" | Where-Object { $_.Trim() })
    if ($lines.Count -gt 0) {
        $line = ([string]$lines[0]).Trim()
        if ($line -match '^(?i)error:\s*(.*)$') {
            return $matches[1]
        }
        return $line
    }
    return ''
}

function Invoke-ReadyCheck {
    param(
        [string]$ExePath,
        [string]$WorkRoot,
        [int]$TimeoutSec
    )

    $readyFile = Join-Path $WorkRoot 'preflight.ready'
    $stdoutFile = Join-Path $WorkRoot 'preflight.out'
    $stderrFile = Join-Path $WorkRoot 'preflight.err'

    foreach ($path in $readyFile, $stdoutFile, $stderrFile) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }

    $argumentText = '--debug-exit --ready-file ' + (Quote-Argument $readyFile)
    $process = Start-Process -FilePath $ExePath -ArgumentList $argumentText -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile -WindowStyle Hidden

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $readyFile -PathType Leaf) {
            $ready = $true
            break
        }

        $process.Refresh()
        if ($process.HasExited) {
            break
        }

        Start-Sleep -Milliseconds 100
    }

    $process.Refresh()
    $exitedOnItsOwn = $process.HasExited
    if (-not $exitedOnItsOwn) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
        }
        catch {
        }
    }
    [void]$process.WaitForExit(5000)

    if (-not $ready -and (Test-Path -LiteralPath $readyFile -PathType Leaf)) {
        $ready = $true
    }

    if (-not $ready) {
        $detail = Get-ProcessErrorText -Path $stderrFile
        if (-not $detail) {
            if ($exitedOnItsOwn) {
                $detail = 'shadowdpi.exe exited before the ready file appeared'
            }
            else {
                $detail = "ready file did not appear within $TimeoutSec seconds"
            }
        }
        throw "preflight failed: $detail"
    }
}

function Get-ProbeKey {
    param($Probe)

    return $Probe.Url + [char]31 + $Probe.Variant
}

function New-StrategyResult {
    param(
        $Strategy,
        [int]$Index,
        [string]$ExePath,
        [object[]]$ProbeResults,
        [object[]]$BaselineResults,
        [string]$ErrorText
    )

    $baselineMap = @{}
    foreach ($baseline in $BaselineResults) {
        $baselineMap[(Get-ProbeKey $baseline)] = $baseline
    }

    $targetTotal = @($BaselineResults | Where-Object { -not $_.Ok -and $_.Status -ne 'unsupported' }).Count
    $targetsPassed = 0
    $regressions = 0
    $passed = 0

    foreach ($probe in $ProbeResults) {
        if ($probe.Ok) {
            $passed++
        }

        $key = Get-ProbeKey $probe
        if (-not $baselineMap.ContainsKey($key)) {
            continue
        }

        $baseline = $baselineMap[$key]
        if ($baseline.Status -eq 'unsupported') {
            continue
        }

        if ($baseline.Ok) {
            if (-not $probe.Ok) {
                $regressions++
            }
        }
        elseif ($probe.Ok) {
            $targetsPassed++
        }
    }

    $verdict = 'fail'
    if ($ErrorText) {
        $verdict = 'error'
    }
    elseif (@($ProbeResults).Count -gt 0 -and @($ProbeResults | Where-Object { $_.Status -ne 'unsupported' }).Count -eq 0) {
        $verdict = 'error'
        $ErrorText = 'no probe could run with this curl build'
    }
    elseif ($targetTotal -gt 0) {
        if ($targetsPassed -eq $targetTotal -and $regressions -eq 0) {
            $verdict = 'pass'
        }
        elseif ($targetsPassed -gt 0) {
            $verdict = 'partial'
        }
        elseif ($regressions -gt 0) {
            $verdict = 'partial'
        }
    }
    elseif ($regressions -eq 0) {
        $verdict = 'pass'
    }
    else {
        $verdict = 'regression'
    }

    $argsText = ($Strategy.Args -join ' ').Trim()
    $commandLine = '"' + $ExePath + '"'
    if ($argsText) {
        $commandLine = $commandLine + ' ' + $argsText
    }

    return [pscustomobject]@{
        Index         = $Index
        Name          = $Strategy.Name
        Args          = @($Strategy.Args)
        CommandLine   = $commandLine
        Verdict       = $verdict
        TargetsPassed = $targetsPassed
        TargetTotal   = $targetTotal
        Regressions   = $regressions
        Passed        = $passed
        Total         = @($ProbeResults).Count
        Error         = $ErrorText
        Probes        = @($ProbeResults)
    }
}

function Invoke-Strategy {
    param(
        $Strategy,
        [int]$Index,
        [int]$TotalStrategies,
        [string]$ExePath,
        [string]$Curl,
        [object[]]$Probes,
        [object[]]$BaselineResults,
        [int]$TimeoutSec,
        [int]$StartupTimeoutSec,
        [int]$ProbeRepeats,
        [string]$IpVersion,
        [int]$SettleMs,
        [string]$WorkRoot
    )

    $readyFile = Join-Path $WorkRoot ("ready-{0}.txt" -f $Index)
    $stdoutFile = Join-Path $WorkRoot ("strategy-{0}.out" -f $Index)
    $stderrFile = Join-Path $WorkRoot ("strategy-{0}.err" -f $Index)

    foreach ($path in $readyFile, $stdoutFile, $stderrFile) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }

    $argumentList = @($Strategy.Args) + @('--ready-file', $readyFile)
    $argumentText = ($argumentList | ForEach-Object { Quote-Argument $_ }) -join ' '

    Write-Info ("[{0}/{1}] {2}" -f ($Index + 1), $TotalStrategies, $Strategy.Name)

    $process = $null
    $processStarted = $false
    $probeResults = @()
    $errorText = ''

    try {
        $process = Start-Process -FilePath $ExePath -ArgumentList $argumentText -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile -WindowStyle Hidden
        $script:ActiveProcess = $process
        $processStarted = $true
    }
    catch {
        $errorText = 'could not start shadowdpi.exe: ' + $_.Exception.Message
    }

    if ($processStarted) {
        $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSec)
        $ready = $false

        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $readyFile -PathType Leaf) {
                $ready = $true
                break
            }

            $process.Refresh()
            if ($process.HasExited) {
                break
            }

            Start-Sleep -Milliseconds 100
        }

        if (-not $ready) {
            $process.Refresh()
            $exitedOnItsOwn = $process.HasExited

            try {
                if (-not $exitedOnItsOwn) {
                    Stop-Process -Id $process.Id -Force -ErrorAction Stop
                }
                [void]$process.WaitForExit(5000)
            }
            catch {
            }

            $detail = Get-ProcessErrorText -Path $stderrFile
            if (-not $detail) {
                if ($exitedOnItsOwn) {
                    $detail = 'shadowdpi.exe exited before the filters opened'
                }
                else {
                    $detail = "ready file did not appear within $StartupTimeoutSec seconds"
                }
            }
            $errorText = $detail
        }
        else {
            if ($SettleMs -gt 0) {
                Start-Sleep -Milliseconds $SettleMs
            }

            foreach ($probe in $Probes) {
                $process.Refresh()
                if ($process.HasExited) {
                    $detail = Get-ProcessErrorText -Path $stderrFile
                    if (-not $detail) {
                        $detail = 'shadowdpi.exe exited during the probes'
                    }
                    $errorText = $detail
                    break
                }

                $result = Invoke-ProbeWithRetries -Curl $Curl -Url $probe.Url -Variant $probe.Variant -TimeoutSec $TimeoutSec -IpVersion $IpVersion -WorkRoot $WorkRoot -Attempts $ProbeRepeats
                $probeResults += $result

                $state = $result.Status
                if ($result.Ok) {
                    $state = 'ok'
                }
                Write-Verbose ("  {0} {1} {2}: {3}" -f $result.Variant, $result.Url, $state, $result.Detail)
            }

            $process.Refresh()
            if ($process.HasExited -and -not $errorText) {
                $detail = Get-ProcessErrorText -Path $stderrFile
                if (-not $detail) {
                    $detail = 'shadowdpi.exe exited during the probes'
                }
                $errorText = $detail
            }
        }
    }

    try {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
            }
            [void]$process.WaitForExit(5000)
        }
    }
    catch {
    }
    finally {
        $script:ActiveProcess = $null
    }

    $result = New-StrategyResult -Strategy $Strategy -Index $Index -ExePath $ExePath -ProbeResults $probeResults -BaselineResults $BaselineResults -ErrorText $errorText

    $color = 'DarkGray'
    switch ($result.Verdict) {
        'pass' { $color = 'Green' }
        'partial' { $color = 'Yellow' }
        'error' { $color = 'Red' }
        'regression' { $color = 'Red' }
    }

    $targetText = '-'
    if ($result.TargetTotal -gt 0) {
        $targetText = "$($result.TargetsPassed)/$($result.TargetTotal)"
    }
    Write-Host ("        {0,-10} targets {1,-7} regressions {2}" -f $result.Verdict, $targetText, $result.Regressions) -ForegroundColor $color

    if ($result.Error) {
        Write-Host ('        error: ' + $result.Error) -ForegroundColor Red
    }

    foreach ($failed in @($result.Probes | Where-Object { -not $_.Ok })) {
        Write-Host ("        {0} {1}: {2}" -f $failed.Variant, $failed.Url, $failed.Detail)
    }

    return $result
}

function Write-Baseline {
    param([object[]]$BaselineResults)
    Write-Info ''
    Write-Info 'Baseline (ShadowDPI stopped)'
    foreach ($probe in $BaselineResults) {
        $state = $probe.Status
        if ($probe.Ok) {
            $state = 'ok'
        }
        $stateText = $state.ToUpperInvariant()
        Write-Host ("  {0,-7} {1,-60} {2}" -f $probe.Variant, $probe.Url, ($stateText + ': ' + $probe.Detail))
    }
}

function Write-Results {
    param(
        [object[]]$Results
    )

    $rank = @{
        'pass'       = 0
        'partial'    = 1
        'fail'       = 2
        'regression' = 3
        'error'      = 4
    }

    $sorted = @($Results | Sort-Object @{ Expression = { $rank[$_.Verdict] } }, @{ Expression = { $_.TargetsPassed }; Descending = $true }, @{ Expression = { $_.Regressions } }, @{ Expression = { $_.Index } })

    Write-Info ''
    Write-Info 'Ranked results'
    Write-Info ('{0,4}  {1,-12}  {2,7}  {3,11}  {4}' -f 'Rank', 'Verdict', 'Targets', 'Regressions', 'Strategy')
    Write-Info ('{0,4}  {1,-12}  {2,7}  {3,11}  {4}' -f '----', '-------', '-------', '-----------', '--------')

    $position = 0
    foreach ($result in $sorted) {
        $position++
        $targetText = '-'
        if ($result.TargetTotal -gt 0) {
            $targetText = "$($result.TargetsPassed)/$($result.TargetTotal)"
        }
        $argsText = ($result.Args -join ' ').Trim()
        Write-Info ('{0,4}  {1,-12}  {2,7}  {3,11}  {4}' -f $position, $result.Verdict, $targetText, $result.Regressions, $argsText)
    }

    return $sorted
}

function Invoke-Scan {
    param(
        [ValidateSet('quick', 'standard', 'full')]
        [string]$Level = ''
    )

    $exitCode = 0
    $summary = $null
    $levelUsed = if ($Level) { $Level } else { $ScanLevel }

    try {
    $urls = @(Read-UrlList -InlineUrls $Urls -File $UrlFile)
    $exePath = Resolve-ExePath -Path $Exe

    $dnsArgs = @()

    if ($DnsAddr -and $script:BoundParameters.ContainsKey('DnsAddr') -and $DnsResolver -in 'cloudflare', 'google', 'system') {
        Write-Warn "The explicit DnsAddr overrides the $DnsResolver resolver choice."
    }

    if (-not $DnsAddr) {
        if ($DnsResolver -eq 'ask') {
            if ($DryRun) {
                Write-Info 'DNS: system resolver (prompt skipped in dry run)'
            }
            elseif ($DnsV6Addr) {
                Write-Info 'DNS: system resolver for IPv4 (the IPv6 resolver was supplied explicitly)'
            }
            elseif (Test-InteractiveHost -and -not $Quiet) {
                $choice = Read-DnsChoice -DefaultPort $DnsPort -DefaultV6Port $DnsV6Port
                if ($null -eq $choice) {
                    Write-Info 'DNS: system resolver'
                }
                elseif ($choice.IsV6) {
                    $DnsV6Addr = $choice.Address
                    $DnsV6Port = $choice.Port
                }
                else {
                    $DnsAddr = $choice.Address
                    $DnsPort = $choice.Port
                }
            }
            elseif ($Quiet) {
                Write-Info 'DNS: system resolver (prompt skipped by -Quiet)'
            }
            else {
                Write-Info 'DNS: system resolver (no prompt in a non-interactive session)'
            }
        }
        elseif ($DnsResolver -eq 'cloudflare') {
            $DnsAddr = '1.1.1.1'
        }
        elseif ($DnsResolver -eq 'google') {
            $DnsAddr = '8.8.8.8'
        }
    }

    if ($DnsAddr) {
        $dnsAddress = $null
        if (-not [System.Net.IPAddress]::TryParse($DnsAddr, [ref]$dnsAddress) -or $dnsAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
            throw "DnsAddr must be an IPv4 address: $DnsAddr"
        }
        $dnsArgs += @('--dns-addr', $DnsAddr, '--dns-port', "$DnsPort")
    }
    elseif ($script:BoundParameters.ContainsKey('DnsPort')) {
        throw 'DnsPort requires DnsAddr or a resolver that supplies an IPv4 address'
    }

    if ($DnsV6Addr) {
        $dnsV6Address = $null
        if (-not [System.Net.IPAddress]::TryParse($DnsV6Addr, [ref]$dnsV6Address) -or $dnsV6Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetworkV6) {
            throw "DnsV6Addr must be an IPv6 address: $DnsV6Addr"
        }
        $dnsArgs += @('--dnsv6-addr', $DnsV6Addr, '--dnsv6-port', "$DnsV6Port")
    }
    elseif ($script:BoundParameters.ContainsKey('DnsV6Port')) {
        throw 'DnsV6Port requires DnsV6Addr'
    }

    $fakeSniHosts = @()
    foreach ($url in $urls) {
        $hostName = ([uri]$url).Host
        $ipAddress = $null
        if ([System.Net.IPAddress]::TryParse($hostName, [ref]$ipAddress)) {
            continue
        }
        if ($hostName.Length -gt 253) {
            continue
        }
        if ($hostName -notmatch '^[A-Za-z0-9.-]+$') {
            continue
        }
        if ($fakeSniHosts -notcontains $hostName) {
            $fakeSniHosts += $hostName
        }
        if ($fakeSniHosts.Count -ge 3) {
            break
        }
    }

    $strategies = @(Get-Strategies -Level $levelUsed -DnsArgs $dnsArgs -FakeSniHosts $fakeSniHosts)

    if ($MaxStrategies -gt 0 -and $strategies.Count -gt $MaxStrategies) {
        Write-Warn "limiting the matrix from $($strategies.Count) to $MaxStrategies strategies"
        $strategies = @($strategies | Select-Object -First $MaxStrategies)
    }

    $probeRepeats = $Repeats
    if ($probeRepeats -eq 0) {
        if ($levelUsed -eq 'full') {
            $probeRepeats = 2
        }
        else {
            $probeRepeats = 1
        }
    }

    $probes = @()
    foreach ($url in $urls) {
        foreach ($variant in @(Get-ProbeVariants -Url $url -ProtocolMode $Protocols)) {
            $probes += [pscustomobject]@{
                Url     = $url
                Variant = $variant
            }
        }
    }

    if ($DryRun) {
        Write-Info "ShadowDPI: $exePath"
        Write-Info "URLs: $($urls.Count), probes: $($probes.Count), strategies: $($strategies.Count)"
        Write-Info "Scan level: $levelUsed, protocols: $Protocols, repeats: $probeRepeats, timeout: ${TimeoutSec}s"
        if ($dnsArgs.Count -gt 0) {
            Write-Info "DNS redirect: $($dnsArgs -join ' ')"
        }
        Write-Info ''
        $number = 0
        foreach ($strategy in $strategies) {
            $number++
            $argsText = ($strategy.Args -join ' ').Trim()
            Write-Info ("{0,4}. {1,-55} {2}" -f $number, $strategy.Name, $argsText)
        }
        return 0
    }

    Assert-Administrator
    Assert-Dependencies -ExePath $exePath

    $installedState = Get-InstalledState -Path (Get-InstallPath)
    if ($null -ne $installedState.Service -and $installedState.Service.Status -ne 'Stopped') {
        $script:ScanRestartService = $installedState.Service.Name
        [void](Stop-ShadowService -Name $script:ScanRestartService)
    }

    Assert-NoRunningInstance

    $jsonPath = ''
    if ($Json) {
        try {
            $jsonPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Json)
        }
        catch {
            throw "invalid JSON path: $Json"
        }
        $jsonDirectory = Split-Path -Parent $jsonPath
        if ($jsonDirectory -and -not (Test-Path -LiteralPath $jsonDirectory -PathType Container)) {
            throw "JSON directory does not exist: $jsonDirectory"
        }
    }

    $curlCommand = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -eq $curlCommand) {
        throw 'curl.exe was not found in PATH. Windows 10 and later include it.'
    }
    $curlPath = $curlCommand.Source
    $curlVersionText = Invoke-NativeText -FilePath $curlPath -Arguments @('--version')
    $curlVersion = ([string](@($curlVersionText -split "`r?`n" | Where-Object { $_.Trim() }) | Select-Object -First 1)).Trim()

    if ($dnsArgs.Count -gt 0) {
        if ($DnsAddr) {
            Test-DnsEndpoint -Address $DnsAddr -Port $DnsPort
        }
        elseif ($DnsV6Addr) {
            Test-DnsEndpoint -Address $DnsV6Addr -Port $DnsV6Port
        }
    }

    $exeVersion = Get-ExeVersion -ExePath $exePath

    $script:WorkRoot = Join-Path $env:TEMP ('shadowdpi-scan-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $script:WorkRoot -Force | Out-Null
    Invoke-ReadyCheck -ExePath $exePath -WorkRoot $script:WorkRoot -TimeoutSec $StartupTimeoutSec

    Write-Info "ShadowDPI: $exePath"
    Write-Info "Version: $exeVersion"
    Write-Info "curl: $curlVersion"
    Write-Info "URLs: $($urls.Count), probes: $($probes.Count), strategies: $($strategies.Count)"
    Write-Info "Scan level: $levelUsed, protocols: $Protocols, repeats: $probeRepeats, timeout: ${TimeoutSec}s"
    if ($dnsArgs.Count -gt 0) {
        Write-Info "DNS redirect: $($dnsArgs -join ' ')"
    }

    if (-not $NoDnsFlush) {
        Invoke-NativeText -FilePath 'ipconfig' -Arguments @('/flushdns') | Out-Null
    }

    $baselineResults = @()
    foreach ($probe in $probes) {
        $result = Invoke-ProbeWithRetries -Curl $curlPath -Url $probe.Url -Variant $probe.Variant -TimeoutSec $TimeoutSec -IpVersion $IpVersion -WorkRoot $script:WorkRoot -Attempts $probeRepeats
        $baselineResults += $result
    }

    Write-Baseline -BaselineResults $baselineResults

    $targets = @($baselineResults | Where-Object { -not $_.Ok -and $_.Status -ne 'unsupported' })
    $unsupported = @($baselineResults | Where-Object { $_.Status -eq 'unsupported' })

    if ($unsupported.Count -gt 0) {
        Write-Warn "$($unsupported.Count) probe(s) could not run with this curl build and were ignored."
    }
    if ($targets.Count -eq 0) {
        Write-Warn 'Every baseline probe succeeded. The matrix is still checked for regressions, but it cannot rank DPI circumvention on this connection.'
    }
    else {
        Write-Info "Blocked at baseline: $($targets.Count) probe(s)"
    }

    $results = @()
    for ($index = 0; $index -lt $strategies.Count; $index++) {
        $result = Invoke-Strategy -Strategy $strategies[$index] -Index $index -TotalStrategies $strategies.Count -ExePath $exePath -Curl $curlPath -Probes $probes -BaselineResults $baselineResults -TimeoutSec $TimeoutSec -StartupTimeoutSec $StartupTimeoutSec -ProbeRepeats $probeRepeats -IpVersion $IpVersion -SettleMs $SettleMs -WorkRoot $script:WorkRoot
        $results += $result
    }

    $sorted = @(Write-Results -Results $results)

    $recommended = $null
    if ($targets.Count -gt 0) {
        $recommended = $sorted | Where-Object { $_.Verdict -eq 'pass' } | Select-Object -First 1
    }

    Write-Info ''
    if ($recommended) {
        Write-Info 'Recommended command:'
        Write-Info ('  "{0}" {1}' -f $exePath, (($recommended.Args -join ' ').Trim()))
        if ($recommended.TargetTotal -gt 0) {
            Write-Info ("  Restored {0}/{1} baseline-blocked probes with {2} regressions." -f $recommended.TargetsPassed, $recommended.TargetTotal, $recommended.Regressions)
        }
    }
    elseif ($targets.Count -eq 0) {
        Write-Info 'No recommendation: the baseline already reached every URL.'
    }
    else {
        Write-Info 'No strategy restored every baseline-blocked probe without regressions.'
        Write-Info 'Try -ScanLevel full, a different DNS endpoint, or a URL list that matches your blocked sites.'
    }

    $summary = [pscustomobject]@{
        tool          = 'shadowdpi-scan'
        formatVersion = 1
        generatedUtc  = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ', [Globalization.CultureInfo]::InvariantCulture)
        shadowdpi     = [pscustomobject]@{
            path    = $exePath
            version = $exeVersion
        }
        curl          = $curlVersion
        scanLevel     = $levelUsed
        protocols     = $Protocols
        ipVersion     = $IpVersion
        timeoutSec    = $TimeoutSec
        repeats       = $probeRepeats
        dns           = [pscustomobject]@{
            args = @($dnsArgs)
        }
        urls              = @($urls)
        probes            = @($probes)
        targetsBlocked    = $targets.Count
        unsupportedProbes = $unsupported.Count
        baseline          = @($baselineResults)
        strategies        = @($sorted)
        recommended       = $null
    }

    if ($recommended) {
        $summary.recommended = [pscustomobject]@{
            name          = $recommended.Name
            args          = @($recommended.Args)
            commandLine   = $recommended.CommandLine
            targetsPassed = $recommended.TargetsPassed
            targetTotal   = $recommended.TargetTotal
            regressions   = $recommended.Regressions
        }
    }

    if ($Json) {
        $jsonText = $summary | ConvertTo-Json -Depth 10
        [IO.File]::WriteAllText($jsonPath, $jsonText, (New-Object System.Text.UTF8Encoding($false)))
        Write-Info ''
        Write-Info "JSON written to $jsonPath"
    }

    if ($targets.Count -eq 0) {
        $exitCode = 0
    }
    elseif ($recommended) {
        $exitCode = 0
    }
    else {
        $exitCode = 2
    }
}
catch {
    Write-Host ('error: ' + $_.Exception.Message) -ForegroundColor Red
    $exitCode = 1
}
finally {
    if ($null -ne $script:ActiveProcess) {
        try {
            $script:ActiveProcess.Refresh()
            if (-not $script:ActiveProcess.HasExited) {
                Stop-Process -Id $script:ActiveProcess.Id -Force -ErrorAction Stop
            }
        }
        catch {
        }
        $script:ActiveProcess = $null
    }

    if ($script:WorkRoot -and (Test-Path -LiteralPath $script:WorkRoot)) {
        if ($KeepArtifacts) {
            Write-Info "Artifacts kept in $script:WorkRoot"
        }
        else {
            Remove-Item -LiteralPath $script:WorkRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if ($script:ScanRestartService) {
        try {
            Start-ShadowService -Name $script:ScanRestartService
            Write-Info "Service $($script:ScanRestartService) started again."
        }
        catch {
            Write-Warn "The service $($script:ScanRestartService) could not be restarted: $($_.Exception.Message)"
        }
        $script:ScanRestartService = $null
    }
}

return $exitCode
}

function Write-InstallStatus {
    param($State)

    if (-not $State.Exists) {
        Write-Info 'ShadowDPI is not installed.'
        return
    }

    $version = $State.Version
    if (-not $version) {
        $version = 'unknown'
    }
    Write-Info "ShadowDPI $version is installed at $($State.Path)."

    if ($null -ne $State.Service) {
        $startText = $State.StartType
        switch ($State.StartType) {
            'auto' {
                $startText = 'automatic'
            }
            'demand' {
                $startText = 'manual'
            }
        }
        Write-Info "Service: $($State.Service.Name) ($($State.Service.Status), $startText)"
        Write-Info "Arguments: $($State.ServiceArgs)"
    }
    elseif ($State.ServiceEnabled) {
        Write-Info "Service: $($State.ServiceName) (not registered)"
    }
    else {
        Write-Info 'Service: not installed'
    }
}

function Invoke-MenuScan {
    Write-Info ''
    Write-Info 'Scan level'
    Write-Info '  1. Quick'
    Write-Info '  2. Standard (recommended)'
    Write-Info '  3. Full'

    $level = 'standard'
    while ($true) {
        $answer = [string](Read-Host 'Choice [2]')
        $answer = $answer.Trim()
        if (-not $answer -or $answer -eq '2') {
            $level = 'standard'
            break
        }
        if ($answer -eq '1') {
            $level = 'quick'
            break
        }
        if ($answer -eq '3') {
            $level = 'full'
            break
        }
        Write-Warn 'Enter 1, 2, or 3.'
    }

    return (Invoke-Scan -Level $level)
}

function Invoke-InstallAction {
    param(
        [ValidateSet('install', 'upgrade')]
        [string]$Mode,
        [switch]$AssumeYes
    )

    Assert-Administrator
    Assert-X64Windows

    $explicitServiceName = ''
    if ($script:BoundParameters.ContainsKey('ServiceName') -and $ServiceName) {
        $explicitServiceName = $ServiceName.Trim()
    }
    $explicitServiceArgs = $null
    if ($script:BoundParameters.ContainsKey('ServiceArgs')) {
        $explicitServiceArgs = $ServiceArgs.Trim()
    }

    $path = Get-InstallPath
    $state = Get-InstalledState -Path $path

    if ($Mode -eq 'upgrade' -and -not $state.Exists) {
        throw "ShadowDPI is not installed at $path. Run the installer with -Action install first."
    }

    if ($Mode -eq 'install' -and $state.Exists -and -not $AssumeYes -and -not $Force) {
        if (-not (Test-InteractiveHost)) {
            throw "ShadowDPI is already installed at $path. Use -Action upgrade or pass -Force."
        }
        if (-not (Read-YesNo -Prompt "ShadowDPI is already installed at $path. Reinstall it?" -Default $true)) {
            Write-Info 'Cancelled.'
            return 0
        }
    }

    $existing = $state.Exists
    if (-not $existing -and -not $Quiet -and -not $Force -and (Test-InteractiveHost)) {
        $entered = Read-ValueWithDefault -Prompt 'Install path' -Default $path
        $path = Assert-SafeInstallPath -Path (Resolve-FullPath -Path $entered)
        $state = Get-InstalledState -Path $path
        $existing = $state.Exists
        if ($existing) {
            Write-Info "An existing installation was found at $path. Its service settings will be preserved."
        }
    }

    $source = Get-SourceInfo -Path $SourcePath
    if ($existing -and (Test-SamePath -First $source.Path -Second $path)) {
        if ($Mode -eq 'upgrade') {
            throw "The source folder is the installed folder. Extract the new release and run install.ps1 from there to upgrade."
        }
        throw "The source folder is the installed folder. Run install.ps1 from a fresh release folder to reinstall."
    }

    Write-Info ''
    Write-Info 'ShadowDPI installer'
    if ($existing) {
        Write-Info "Installed: $($state.VersionText)"
    }
    else {
        Write-Info 'Installed: none'
    }
    Write-Info "Source:    $($source.VersionText)"

    $serviceEnabled = $true
    $serviceName = $script:DefaultServiceName
    $serviceArgs = $script:DefaultServiceArgs
    $startType = $script:DefaultStartType
    $startAfter = $true
    $oldServiceName = ''
    $oldServiceWasRunning = $false

    if ($existing) {
        $serviceEnabled = $state.ServiceEnabled
        $serviceName = $state.ServiceName
        $serviceArgs = $state.ServiceArgs
        $startType = $state.StartType
        if ($null -ne $state.Service) {
            $serviceEnabled = $true
            $oldServiceName = $state.Service.Name
            $oldServiceWasRunning = ($state.Service.Status -eq 'Running')
            $startAfter = $oldServiceWasRunning
        }
        elseif ($serviceEnabled) {
            $startAfter = $true
        }
        else {
            $startAfter = $false
        }
    }

    if ($explicitServiceName) {
        $serviceName = $explicitServiceName
        $serviceEnabled = $true
    }
    if ($null -ne $explicitServiceArgs) {
        $serviceArgs = $explicitServiceArgs
    }
    if ($NoService -and -not $existing) {
        $serviceEnabled = $false
        $startAfter = $false
    }
    if ($NoStart) {
        $startAfter = $false
    }

    if (-not $existing -and -not $Quiet -and -not $Force -and (Test-InteractiveHost)) {
        if (-not $NoService) {
            $serviceEnabled = Read-YesNo -Prompt 'Install and start a Windows service?' -Default $serviceEnabled
        }
        else {
            $serviceEnabled = $false
        }

        if ($serviceEnabled) {
            if (-not $script:BoundParameters.ContainsKey('ServiceName')) {
                $serviceName = Read-ValueWithDefault -Prompt 'Service name' -Default $serviceName
            }
            if (-not $script:BoundParameters.ContainsKey('ServiceArgs')) {
                $serviceArgs = Read-ValueWithDefault -Prompt 'Service arguments' -Default $serviceArgs
            }
            if (Read-YesNo -Prompt 'Start the service automatically at boot?' -Default ($startType -eq 'auto')) {
                $startType = 'auto'
            }
            else {
                $startType = 'demand'
            }
            if (-not $NoStart) {
                $startAfter = Read-YesNo -Prompt 'Start the service after install?' -Default $true
            }
        }
        else {
            $startAfter = $false
        }
    }

    $startType = ConvertTo-ScStartMode -StartType $startType
    if ($serviceEnabled) {
        Assert-ServiceName -Name $serviceName
    }
    if ($serviceArgs -match "`r|`n") {
        throw 'Service arguments must be a single line.'
    }

    $newServiceExisted = $false
    if ($serviceEnabled) {
        $newServiceExisted = $null -ne (Get-ServiceInfo -Name $serviceName)
        if (-not (Test-ServiceOwnership -Name $serviceName -ExePath (Join-Path $path 'shadowdpi.exe'))) {
            $conflict = Get-ServiceInfo -Name $serviceName
            $conflictPath = $conflict.PathName
            $launch = Get-ServiceLaunch -PathName $conflict.PathName
            if ($launch.ExePath) {
                $conflictPath = $launch.ExePath
            }
            throw "A service named $serviceName already exists at $conflictPath. Pass -ServiceName with a different name or remove that service first."
        }
    }

    if ($oldServiceName -and $oldServiceName -ne $serviceName) {
        Write-Warn "The service name is changing from $oldServiceName to $serviceName."
    }

    $completed = $false
    try {
        if ($oldServiceName) {
            $oldWasRunning = Stop-ShadowService -Name $oldServiceName
            if ($oldWasRunning -and -not $NoStart) {
                $startAfter = $true
            }
        }

        [void](Wait-ForNoShadowProcess -TimeoutSec 10 -ThrowIfFound)
        $payload = @(Copy-InstallPayload -SourcePath $source.Path -DestinationPath $path -ScriptPath $script:ScriptPath)
        Remove-StalePayload -Path $path -OldPayload $state.Metadata.payload -NewPayload $payload

        $serviceExe = Join-Path $path 'shadowdpi.exe'
        $running = Update-ServiceConfiguration -ExePath $serviceExe -Name $serviceName -Arguments $serviceArgs -StartType $startType -Enabled $serviceEnabled -StartAfter $startAfter -OldName $oldServiceName

        Write-InstallMetadata -Path $path -Metadata $state.Metadata -VersionText $source.VersionText -SourcePath $source.Path -ServiceEnabled $serviceEnabled -ServiceName $serviceName -ServiceArgs $serviceArgs -StartType $startType -Running $running -Payload $payload
        $completed = $true
    }
    catch {
        if (-not $completed) {
            if ($serviceEnabled -and -not $newServiceExisted -and $null -ne (Get-ServiceInfo -Name $serviceName)) {
                try {
                    Remove-ShadowService -Name $serviceName
                }
                catch {
                    Write-Warn "The new service $serviceName could not be removed after the failure."
                }
            }
            Restore-ShadowService -Name $oldServiceName -ExePath (Join-Path $path 'shadowdpi.exe') -Arguments $state.ServiceArgs -StartType $state.StartType -WasRunning $oldServiceWasRunning
        }
        throw
    }

    Write-Info ''
    Write-Info "ShadowDPI $($source.Version) is installed at $path."
    if ($serviceEnabled) {
        $service = Get-ServiceInfo -Name $serviceName
        if ($null -ne $service) {
            Write-Info "Service: $serviceName ($($service.Status))"
        }
        else {
            Write-Info "Service: $serviceName (not registered)"
        }
    }
    else {
        Write-Info 'Service: not installed'
    }

    $defaultPath = Get-DefaultInstallPath
    if (-not (Test-SamePath -First $path -Second $defaultPath)) {
        Write-Info "Custom install path: pass -InstallPath '$path' to later actions."
    }

    return 0
}

function Invoke-ReconfigureAction {
    Assert-Administrator
    Assert-X64Windows

    $explicitServiceName = ''
    if ($script:BoundParameters.ContainsKey('ServiceName') -and $ServiceName) {
        $explicitServiceName = $ServiceName.Trim()
    }
    $explicitServiceArgs = $null
    if ($script:BoundParameters.ContainsKey('ServiceArgs')) {
        $explicitServiceArgs = $ServiceArgs.Trim()
    }

    $path = Get-InstallPath
    $state = Get-InstalledState -Path $path
    if (-not $state.Exists) {
        throw "ShadowDPI is not installed at $path. Run the installer with -Action install first."
    }

    $oldServiceName = ''
    $wasRunning = $false
    if ($null -ne $state.Service) {
        $oldServiceName = $state.Service.Name
        $wasRunning = ($state.Service.Status -eq 'Running')
    }

    $serviceEnabled = $state.ServiceEnabled
    if ($null -ne $state.Service) {
        $serviceEnabled = $true
    }
    $serviceName = $state.ServiceName
    $serviceArgs = $state.ServiceArgs
    $startType = $state.StartType
    $startAfter = $wasRunning

    if ($explicitServiceName) {
        $serviceName = $explicitServiceName
        $serviceEnabled = $true
    }
    if ($null -ne $explicitServiceArgs) {
        $serviceArgs = $explicitServiceArgs
    }
    if ($NoService) {
        $serviceEnabled = $false
        $startAfter = $false
    }
    if ($NoStart) {
        $startAfter = $false
    }

    if (-not $NoService -and -not $Quiet -and -not $Force -and (Test-InteractiveHost)) {
        Write-Info ''
        Write-InstallStatus -State $state
        Write-Info ''

        $serviceEnabled = Read-YesNo -Prompt 'Run ShadowDPI as a Windows service?' -Default $serviceEnabled
        if ($serviceEnabled) {
            if (-not $script:BoundParameters.ContainsKey('ServiceName')) {
                $serviceName = Read-ValueWithDefault -Prompt 'Service name' -Default $serviceName
            }
            if (-not $script:BoundParameters.ContainsKey('ServiceArgs')) {
                $serviceArgs = Read-ValueWithDefault -Prompt 'Service arguments' -Default $serviceArgs
            }
            if (Read-YesNo -Prompt 'Start the service automatically at boot?' -Default ($startType -eq 'auto')) {
                $startType = 'auto'
            }
            else {
                $startType = 'demand'
            }
            if (-not $NoStart) {
                $startAfter = Read-YesNo -Prompt 'Start the service now?' -Default $wasRunning
            }
        }
        else {
            $startAfter = $false
            if ($oldServiceName -and -not (Read-YesNo -Prompt "Remove the existing service $oldServiceName?" -Default $true)) {
                $serviceEnabled = $true
            }
        }
    }

    $startType = ConvertTo-ScStartMode -StartType $startType
    if ($serviceEnabled) {
        Assert-ServiceName -Name $serviceName
    }
    if ($serviceArgs -match "`r|`n") {
        throw 'Service arguments must be a single line.'
    }

    $newServiceExisted = $false
    if ($serviceEnabled) {
        $newServiceExisted = $null -ne (Get-ServiceInfo -Name $serviceName)
        if (-not (Test-ServiceOwnership -Name $serviceName -ExePath (Join-Path $path 'shadowdpi.exe'))) {
            $conflict = Get-ServiceInfo -Name $serviceName
            $conflictPath = $conflict.PathName
            $launch = Get-ServiceLaunch -PathName $conflict.PathName
            if ($launch.ExePath) {
                $conflictPath = $launch.ExePath
            }
            throw "A service named $serviceName already exists at $conflictPath. Pass -ServiceName with a different name or remove that service first."
        }
    }

    $completed = $false
    try {
        if ($oldServiceName) {
            [void](Stop-ShadowService -Name $oldServiceName)
        }

        $serviceExe = Join-Path $path 'shadowdpi.exe'
        $running = Update-ServiceConfiguration -ExePath $serviceExe -Name $serviceName -Arguments $serviceArgs -StartType $startType -Enabled $serviceEnabled -StartAfter $startAfter -OldName $oldServiceName

        $versionText = $state.VersionText
        if (-not $versionText -and (Test-Path -LiteralPath $serviceExe -PathType Leaf)) {
            $versionText = Get-ExeVersion -ExePath $serviceExe
        }

        $sourceText = $path
        if ($null -ne $state.Metadata -and $state.Metadata.sourcePath) {
            $sourceText = [string]$state.Metadata.sourcePath
        }

        Write-InstallMetadata -Path $path -Metadata $state.Metadata -VersionText $versionText -SourcePath $sourceText -ServiceEnabled $serviceEnabled -ServiceName $serviceName -ServiceArgs $serviceArgs -StartType $startType -Running $running
        $completed = $true
    }
    catch {
        if (-not $completed) {
            if ($serviceEnabled -and -not $newServiceExisted -and $null -ne (Get-ServiceInfo -Name $serviceName)) {
                try {
                    Remove-ShadowService -Name $serviceName
                }
                catch {
                    Write-Warn "The new service $serviceName could not be removed after the failure."
                }
            }
            Restore-ShadowService -Name $oldServiceName -ExePath (Join-Path $path 'shadowdpi.exe') -Arguments $state.ServiceArgs -StartType $state.StartType -WasRunning $wasRunning
        }
        throw
    }

    Write-Info ''
    Write-Info 'ShadowDPI configuration updated.'
    if ($serviceEnabled) {
        $service = Get-ServiceInfo -Name $serviceName
        if ($null -ne $service) {
            Write-Info "Service: $serviceName ($($service.Status))"
        }
        else {
            Write-Info "Service: $serviceName (not registered)"
        }
    }
    else {
        Write-Info 'Service: not installed'
    }
    return 0
}

function Invoke-UninstallAction {
    Assert-Administrator

    $path = Get-InstallPath
    $state = Get-InstalledState -Path $path
    $serviceName = $state.ServiceName
    if ($null -ne $state.Service) {
        $serviceName = $state.Service.Name
    }

    if (-not $state.Exists -and $null -eq $state.Service) {
        Write-Info "ShadowDPI is not installed at $path."
        return 0
    }

    if (-not $Force) {
        if (-not (Test-InteractiveHost)) {
            throw 'Uninstall from a non-interactive session requires -Force.'
        }
        if (-not (Read-YesNo -Prompt "Remove ShadowDPI from $path and delete service $serviceName?" -Default $false)) {
            Write-Info 'Cancelled.'
            return 0
        }
    }

    $safePath = $null
    $hasPayload = $false
    if (Test-Path -LiteralPath $path -PathType Container) {
        $safePath = Assert-SafeInstallPath -Path $path
        $hasPayload = (Test-Path -LiteralPath (Join-Path $safePath 'shadowdpi.exe') -PathType Leaf) -or (Test-Path -LiteralPath (Join-Path $safePath $script:InstallMetadataName) -PathType Leaf)
    }

    $removedSomething = ($null -ne $state.Service)
    if ($null -ne $state.Service) {
        Remove-ShadowService -Name $serviceName
    }

    if ($null -ne $safePath) {
        if ($hasPayload) {
            [void](Wait-ForNoShadowProcess -TimeoutSec 10 -ThrowIfFound)
        }

        $current = (Get-Location).ProviderPath
        if (Test-PathWithin -Path $current -Root $safePath) {
            $parent = Split-Path -Parent $safePath
            [IO.Directory]::SetCurrentDirectory($parent)
            Set-Location -LiteralPath $parent
        }

        if (-not $hasPayload) {
            Write-Warn "Skipping $safePath because it does not contain shadowdpi.exe or $($script:InstallMetadataName)."
        }
        else {
            Write-Info "Removing $safePath..."
            try {
                Remove-Item -LiteralPath $safePath -Recurse -Force -ErrorAction Stop
            }
            catch {
                throw "Could not remove all files from $safePath. Close Explorer windows and console instances, then run uninstall again. $($_.Exception.Message)"
            }
            $removedSomething = $true
        }
    }

    if ($removedSomething) {
        Write-Info 'ShadowDPI was removed.'
    }
    else {
        Write-Info 'Nothing was removed.'
    }
    return 0
}

function Invoke-InteractiveMenu {
    if (-not (Test-InteractiveHost)) {
        throw 'The interactive menu requires a console. Use -Action install, upgrade, reconfigure, uninstall, or scan.'
    }

    $menuExitCode = 0
    while ($true) {
        $path = Get-InstallPath
        $state = Get-InstalledState -Path $path

        Write-Info ''
        Write-Info 'ShadowDPI installer'
        Write-Info '=================='
        Write-InstallStatus -State $state
        if (-not (Test-IsAdministrator)) {
            Write-Warn 'This session is not elevated. Install, service, and scan actions need Administrator rights.'
        }
        Write-Info ''

        if ($state.Exists) {
            Write-Info '  1. Reinstall'
            Write-Info '  2. Upgrade from this folder'
            Write-Info '  3. Reconfigure'
            Write-Info '  4. Uninstall'
            Write-Info '  5. Scan for a working strategy'
            Write-Info '  6. Exit'
        }
        else {
            Write-Info '  1. Install'
            Write-Info '  2. Scan for a working strategy'
            Write-Info '  3. Exit'
        }

        $choice = [string](Read-Host 'Choice')
        $choice = $choice.Trim()

        $exit = $false
        $didAction = $false
        try {
            if ($state.Exists) {
                switch ($choice) {
                    '1' {
                        [void](Invoke-InstallAction -Mode install -AssumeYes)
                        $didAction = $true
                    }
                    '2' {
                        [void](Invoke-InstallAction -Mode upgrade -AssumeYes)
                        $didAction = $true
                    }
                    '3' {
                        [void](Invoke-ReconfigureAction)
                        $didAction = $true
                    }
                    '4' {
                        [void](Invoke-UninstallAction)
                        $didAction = $true
                    }
                    '5' {
                        [void](Invoke-MenuScan)
                        $didAction = $true
                    }
                    '6' {
                        $exit = $true
                    }
                    default {
                        Write-Warn 'Enter 1, 2, 3, 4, 5, or 6.'
                    }
                }
            }
            else {
                switch ($choice) {
                    '1' {
                        [void](Invoke-InstallAction -Mode install -AssumeYes)
                        $didAction = $true
                    }
                    '2' {
                        [void](Invoke-MenuScan)
                        $didAction = $true
                    }
                    '3' {
                        $exit = $true
                    }
                    default {
                        Write-Warn 'Enter 1, 2, or 3.'
                    }
                }
            }
            if ($didAction) {
                $menuExitCode = 0
            }
        }
        catch {
            Write-Host ('error: ' + $_.Exception.Message) -ForegroundColor Red
            $menuExitCode = 1
        }

        if ($exit) {
            return $menuExitCode
        }

        [void](Read-Host 'Press Enter to continue')
    }
}

$exitCode = 0
try {
    switch ($Action) {
        'menu' {
            $exitCode = Invoke-InteractiveMenu
        }
        'install' {
            $exitCode = Invoke-InstallAction -Mode install
        }
        'upgrade' {
            $exitCode = Invoke-InstallAction -Mode upgrade
        }
        'reconfigure' {
            $exitCode = Invoke-ReconfigureAction
        }
        'uninstall' {
            $exitCode = Invoke-UninstallAction
        }
        'scan' {
            $exitCode = Invoke-Scan
        }
    }
}
catch {
    Write-Host ('error: ' + $_.Exception.Message) -ForegroundColor Red
    $exitCode = 1
}

exit $exitCode
