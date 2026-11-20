[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$Version = '0.5.0',

    [string]$Output = (Join-Path $PSScriptRoot '..\..\build\msi-out')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $SourceDir).Path
foreach ($name in @('shadowdpi.exe', 'shadowdpi-gui.exe', 'WinDivert.dll',
                    'WinDivert64.sys', 'install.ps1')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) {
        throw "Missing required payload file: $name"
    }
}

$project = Join-Path $PSScriptRoot 'ShadowDPI.wixproj'
$env:DOTNET_ROLL_FORWARD = 'Major'
dotnet build $project `
    -t:Rebuild `
    -p:SourceDir="$source" `
    -p:Version="$Version" `
    -o "$Output" `
    --nologo
if ($LASTEXITCODE -ne 0) {
    throw "WiX build failed with exit code $LASTEXITCODE"
}

$msi = Join-Path $Output "shadowdpi-$Version-x64.msi"
if (-not (Test-Path -LiteralPath $msi -PathType Leaf)) {
    throw "Expected MSI was not produced: $msi"
}
Write-Output $msi
