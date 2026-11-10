[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameExe,
    [Parameter(Mandatory = $true)][string]$UserDirectory
)

# Launch only: never copy a loaded DLL, stop a process, or supply login details.
$ErrorActionPreference = 'Stop'
$gamePath = (Resolve-Path -LiteralPath $GameExe).ProviderPath
$userPath = (Resolve-Path -LiteralPath $UserDirectory).ProviderPath
if ((Split-Path -Leaf $gamePath) -ine 'nwmain.exe') {
    throw 'GameExe must identify the NWN client nwmain.exe.'
}
if (-not (Test-Path -LiteralPath $userPath -PathType Container)) {
    throw 'UserDirectory must be an existing NWN user directory.'
}
if ($userPath.Contains('"')) { throw 'UserDirectory cannot contain a quote.' }
if (Get-Process -Name nwmain -ErrorAction SilentlyContinue) {
    throw 'Close the running NWN client before starting the diagnostic client.'
}
$installedDll = Join-Path (Split-Path -Parent $gamePath) 'version.dll'
$builtDll = Join-Path $PSScriptRoot 'version.dll'
if (-not (Test-Path -LiteralPath $installedDll -PathType Leaf)) {
    throw 'The renderer version.dll is not installed beside this client.'
}
$installedHash = (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash
if ((Test-Path -LiteralPath $builtDll -PathType Leaf) -and
    $installedHash -ne (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash) {
    throw 'Installed renderer differs from this checkout build. Install the verified build while NWN is stopped.'
}
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $gamePath
$start.WorkingDirectory = Split-Path -Parent $gamePath
$start.Arguments = '-userdirectory "' + $userPath + '"'
$start.UseShellExecute = $false
$start.EnvironmentVariables['NWN_DRAW_PROBE'] = '1'
$start.EnvironmentVariables['NWN_SHADOWMAP_LOG'] = '1'
$client = [System.Diagnostics.Process]::Start($start)
Write-Output "Started drawing probe client PID $($client.Id); renderer SHA256 $installedHash"
Write-Output "Log: $(Join-Path $start.WorkingDirectory 'nwn_shadowmap_win.log')"
