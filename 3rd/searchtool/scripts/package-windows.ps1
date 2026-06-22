param(
    [string]$QtPrefix = "",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build"
$Exe = Join-Path $BuildDir "$Config\AiboxSearchTool.exe"
$DistDir = Join-Path $Root "dist"

if (!(Test-Path $Exe)) {
    throw "Executable not found: $Exe. Run scripts\build-windows.ps1 first."
}

if (Test-Path $DistDir) {
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir | Out-Null
Copy-Item -LiteralPath $Exe -Destination $DistDir

$deploy = "windeployqt"
if ($QtPrefix -ne "") {
    $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path $candidate) {
        $deploy = $candidate
    }
}

Write-Host "Deploy: $deploy $DistDir\AiboxSearchTool.exe"
& $deploy (Join-Path $DistDir "AiboxSearchTool.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "Done. Dist:"
Write-Host "  $DistDir"
