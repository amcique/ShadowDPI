#requires -Version 5.1
<#
.SYNOPSIS
Tests ShadowDPI option combinations against a list of URLs.

.DESCRIPTION
Starts one ShadowDPI instance per candidate strategy, waits for the filters to
open through --ready-file, probes every URL with curl.exe, and reports which
strategies restore access. The model is a baseline comparison: probes that fail
without ShadowDPI are targets, and a strategy passes only when every target is
reachable and no baseline-working probe regresses.

The script does not follow redirects. A redirect to another host is treated as
a suspicious DPI response, and HTTP 400 is treated as a sign that fake packets
reached the server.

.PARAMETER Urls
One or more URLs to test. Overrides the URL file.

.PARAMETER UrlFile
A text file with one URL per line. Blank lines and lines starting with # or ;
are ignored. A bare host is treated as https://host/.

.PARAMETER Exe
Path to shadowdpi.exe. By default the script looks next to the tools folder,
then in the usual build output directories.

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

.PARAMETER DnsAddr
IPv4 resolver for --dns-addr. When set, every strategy also redirects DNS and
an extra "DNS redirect only" strategy is added.

.PARAMETER DnsPort
Port for DnsAddr. Defaults to 53.

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
.\tools\blockcheck.ps1 -ScanLevel quick

.EXAMPLE
.\tools\blockcheck.ps1 -Urls https://example.com/ -ScanLevel full -Json result.json

.EXAMPLE
.\tools\blockcheck.ps1 -UrlFile .\blocked.txt -DnsAddr 77.88.8.8 -DnsPort 1253
#>
[CmdletBinding()]
param(
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

$script:UserAgent = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36'
$script:WorkRoot = $null
$script:ActiveProcess = $null

function Write-Info {
    param([string]$Message)
    Write-Host $Message
}

function Write-Warn {
    param([string]$Message)
    Write-Host ('warning: ' + $Message) -ForegroundColor Yellow
}

function Resolve-ExePath {
    param([string]$Path)

    if ($Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "shadowdpi.exe was not found at $Path"
        }
        return (Resolve-Path -LiteralPath $Path).Path
    }

    $candidates = @(
        (Join-Path $PSScriptRoot '..\shadowdpi.exe'),
        (Join-Path $PSScriptRoot '..\build\msvc-x64\Release\shadowdpi.exe'),
        (Join-Path $PSScriptRoot '..\build\msvc-x64\Debug\shadowdpi.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'shadowdpi.exe was not found. Pass -Exe or unpack the release next to the tools folder.'
}

function Invoke-NativeText {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    # Windows PowerShell 5.1 wraps native stderr in an ErrorRecord, which would
    # become terminating under ErrorActionPreference=Stop. Keep stderr in the
    # returned text instead.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        return (& $FilePath @Arguments 2>&1 | Out-String)
    }
    finally {
        $ErrorActionPreference = $previous
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
            $File = Join-Path $PSScriptRoot 'blockcheck-urls.txt'
        }
        if (-not (Test-Path -LiteralPath $File -PathType Leaf)) {
            throw "URL list was not found at $File"
        }
        $lines = @(Get-Content -LiteralPath $File -ErrorAction Stop)
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

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'This script must run as Administrator. Right-click PowerShell and choose Run as administrator.'
    }
}

function Assert-NoRunningInstance {
    $service = Get-Service -Name 'ShadowDPI' -ErrorAction SilentlyContinue
    if ($null -ne $service -and $service.Status -eq 'Running') {
        throw 'The ShadowDPI service is running. Stop it with "sc stop ShadowDPI" before scanning.'
    }

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

    try {
        $text = Invoke-NativeText -FilePath $ExePath -Arguments @('--version')
        $lines = @($text -split "`r?`n" | Where-Object { $_.Trim() })
        $line = $lines | Where-Object { $_ -match 'ShadowDPI' } | Select-Object -Last 1
        if ($line) {
            return ([string]$line).Trim()
        }
    }
    catch {
        return 'unknown'
    }

    return 'unknown'
}

function Test-DnsEndpoint {
    param(
        [string]$Address,
        [int]$Port
    )

    try {
        $output = Invoke-NativeText -FilePath 'nslookup' -Arguments @('-type=A', "-port=$Port", '-timeout=2', 'example.com', $Address)
        if ($output -notmatch '(?m)^Name:') {
            Write-Warn "DNS endpoint $Address`:$Port did not return an answer for example.com"
        }
    }
    catch {
        Write-Warn "could not test DNS endpoint $Address`:$Port"
    }
}

function Quote-Argument {
    param([string]$Value)

    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '(\\*)"', '$1$1\"') + '"'
    }
    return $Value
}

function Get-CurlFailureReason {
    param([int]$Code)

    switch ($Code) {
        1 { return 'unsupported protocol' }
        2 { return 'curl option or initialization failure' }
        3 { return 'malformed URL' }
        4 { return 'feature not supported by this curl build' }
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

    return ($locationHost -eq $originalHost -or $locationHost.EndsWith('.' + $originalHost))
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
            $stderrText = [string](Get-Content -LiteralPath $errorFile -Raw -ErrorAction SilentlyContinue)
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
                $result = Invoke-ProbeWithRetries -Curl $Curl -Url $probe.Url -Variant $probe.Variant -TimeoutSec $TimeoutSec -IpVersion $IpVersion -WorkRoot $WorkRoot -Attempts $ProbeRepeats
                $probeResults += $result

                $state = $result.Status
                if ($result.Ok) {
                    $state = 'ok'
                }
                Write-Verbose ("  {0} {1} {2}: {3}" -f $result.Variant, $result.Url, $state, $result.Detail)
            }

            $process.Refresh()
            if ($process.HasExited) {
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

$exitCode = 0
$summary = $null

try {
    $urls = @(Read-UrlList -InlineUrls $Urls -File $UrlFile)
    $exePath = Resolve-ExePath -Path $Exe

    $dnsArgs = @()
    if ($DnsAddr) {
        $dnsAddress = $null
        if (-not [System.Net.IPAddress]::TryParse($DnsAddr, [ref]$dnsAddress) -or $dnsAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
            throw "DnsAddr must be an IPv4 address: $DnsAddr"
        }
        $dnsArgs += @('--dns-addr', $DnsAddr, '--dns-port', "$DnsPort")
    }
    elseif ($PSBoundParameters.ContainsKey('DnsPort')) {
        throw 'DnsPort requires DnsAddr'
    }

    if ($DnsV6Addr) {
        $dnsV6Address = $null
        if (-not [System.Net.IPAddress]::TryParse($DnsV6Addr, [ref]$dnsV6Address) -or $dnsV6Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetworkV6) {
            throw "DnsV6Addr must be an IPv6 address: $DnsV6Addr"
        }
        $dnsArgs += @('--dnsv6-addr', $DnsV6Addr, '--dnsv6-port', "$DnsV6Port")
    }
    elseif ($PSBoundParameters.ContainsKey('DnsV6Port')) {
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

    $strategies = @(Get-Strategies -Level $ScanLevel -DnsArgs $dnsArgs -FakeSniHosts $fakeSniHosts)

    if ($MaxStrategies -gt 0 -and $strategies.Count -gt $MaxStrategies) {
        Write-Warn "limiting the matrix from $($strategies.Count) to $MaxStrategies strategies"
        $strategies = @($strategies | Select-Object -First $MaxStrategies)
    }

    $probeRepeats = $Repeats
    if ($probeRepeats -eq 0) {
        if ($ScanLevel -eq 'full') {
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
        Write-Info "Scan level: $ScanLevel, protocols: $Protocols, repeats: $probeRepeats, timeout: ${TimeoutSec}s"
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
        exit 0
    }

    Assert-Administrator
    Assert-Dependencies -ExePath $exePath
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

    if ($dnsArgs.Count -gt 0 -and $DnsAddr) {
        Test-DnsEndpoint -Address $DnsAddr -Port $DnsPort
    }

    $exeVersion = Get-ExeVersion -ExePath $exePath

    $script:WorkRoot = Join-Path $env:TEMP ('shadowdpi-blockcheck-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $script:WorkRoot -Force | Out-Null
    Invoke-ReadyCheck -ExePath $exePath -WorkRoot $script:WorkRoot -TimeoutSec $StartupTimeoutSec

    Write-Info "ShadowDPI: $exePath"
    Write-Info "Version: $exeVersion"
    Write-Info "curl: $curlVersion"
    Write-Info "URLs: $($urls.Count), probes: $($probes.Count), strategies: $($strategies.Count)"
    Write-Info "Scan level: $ScanLevel, protocols: $Protocols, repeats: $probeRepeats, timeout: ${TimeoutSec}s"
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
        tool          = 'shadowdpi-blockcheck'
        formatVersion = 1
        generatedUtc  = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        shadowdpi     = [pscustomobject]@{
            path    = $exePath
            version = $exeVersion
        }
        curl          = $curlVersion
        scanLevel     = $ScanLevel
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
}

exit $exitCode
