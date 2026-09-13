param([Parameter(Mandatory)][string]$Installer)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$installDir = Join-Path $projectRoot 'build\installer-test'
$registryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8DFA1442-A417-446B-A0F4-B5B0F68319B4}_is1'
if (Test-Path $registryPath) { throw 'AudioSync is already installed; use a clean environment for installer tests.' }
if (Get-Process audiosync -ErrorAction SilentlyContinue) { throw 'Exit AudioSync before testing the installer.' }
if (Test-Path -LiteralPath $installDir) { throw 'The installer-test directory already exists; inspect it before retrying.' }
$setup = (Resolve-Path -LiteralPath $Installer).Path

try {
    foreach ($attempt in 1..2) {
        $process = Start-Process -FilePath $setup -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS',
            "/DIR=`"$installDir`""
        ) -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Installer exited with $($process.ExitCode)." }
        if (-not (Test-Path "$installDir\audiosync.exe")) { throw 'The app was not installed.' }
        if (-not (Test-Path "$installDir\README.md")) { throw 'The CLI documentation is missing.' }
        if (-not (Test-Path "$installDir\docs\cli.md")) { throw 'The CLI reference is missing.' }
        if (-not (Test-Path "$installDir\docs\adapters.md")) { throw 'The adapter guide is missing.' }
        if (-not (Test-Path "$installDir\docs\development.md")) { throw 'The development guide is missing.' }
        if (-not (Test-Path $registryPath)) { throw 'The uninstaller was not registered.' }
        $expected = (Get-FileHash "$projectRoot\build\release\audiosync.exe").Hash
        if ((Get-FileHash "$installDir\audiosync.exe").Hash -ne $expected) { throw 'The installed binary differs from the build.' }
    }
}
finally {
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
        $process = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Uninstaller exited with $($process.ExitCode)." }
    }
}
if (Test-Path "$installDir\audiosync.exe") { throw 'Uninstall left the app executable behind.' }
if (Test-Path $registryPath) { throw 'Uninstall left its registry entry behind.' }
Write-Host 'PASS: install, reinstall, binary integrity, and uninstall'
