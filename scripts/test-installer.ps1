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
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        $matches = @($userPath -split ';' | Where-Object { $_ -eq $installDir })
        if ($matches.Count -ne 1) { throw 'The installer did not add exactly one PATH entry.' }
        & "$installDir\audiosync.exe" sync -d
        if ($LASTEXITCODE) { throw 'Detached sync could not start.' }
        $deadline = (Get-Date).AddSeconds(10)
        while ((Get-Process audiosync -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if (Get-Process audiosync -ErrorAction SilentlyContinue) { throw 'Detached sync did not finish.' }
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
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$matches = @($userPath -split ';' | Where-Object { $_ -eq $installDir })
if ($matches.Count) { throw 'Uninstall left its PATH entry behind.' }
Write-Host 'PASS: install, PATH, reinstall, binary integrity, and uninstall'
