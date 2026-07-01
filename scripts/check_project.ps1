$ErrorActionPreference = "Stop"

$ProjectDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$UserProfile = $env:USERPROFILE
if (-not $UserProfile) {
    $UserProfile = [Environment]::GetFolderPath("UserProfile")
}

$PlatformIoScripts = Join-Path $UserProfile ".platformio\penv\Scripts"
$PlatformIoPython = Join-Path $PlatformIoScripts "python.exe"
$PlatformIoPio = Join-Path $PlatformIoScripts "pio.exe"

if (-not (Test-Path -LiteralPath $PlatformIoPython)) {
    throw "PlatformIO bundled Python was not found: $PlatformIoPython"
}

if (-not (Test-Path -LiteralPath $PlatformIoPio)) {
    throw "PlatformIO executable was not found: $PlatformIoPio"
}

$env:PATH = "$PlatformIoScripts;$env:PATH"

Push-Location $ProjectDir
try {
    & $PlatformIoPython "scripts\check_project.py"
    $ExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

exit $ExitCode
