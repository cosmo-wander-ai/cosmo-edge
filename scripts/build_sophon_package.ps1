param(
    [string]$BaseImage = $(if ($env:SOPHON_BASE_IMAGE) { $env:SOPHON_BASE_IMAGE } else { "stream_dev:0.2" }),
    [string]$ImageTar = $env:SOPHON_STREAM_DEV_TAR,
    [string]$DfssUrl = $(if ($env:SOPHON_STREAM_DEV_DFSS_URL) { $env:SOPHON_STREAM_DEV_DFSS_URL } else { "open@sophgo.com:/sophon-stream/docker/stream_dev_22.04.tar" }),
    [string]$CacheDir = $(if ($env:SOPHON_DOCKER_CACHE_DIR) { $env:SOPHON_DOCKER_CACHE_DIR } else { Join-Path $env:USERPROFILE ".cache\cosmo\sophon" }),
    [string]$PipIndexUrl = $(if ($env:SOPHON_PIP_INDEX_URL) { $env:SOPHON_PIP_INDEX_URL } else { "https://pypi.tuna.tsinghua.edu.cn/simple" })
)

$ErrorActionPreference = "Stop"

# Automatically restore Linux .so symlinks to local binary copies on Windows before building
Write-Host "Restoring shared library symlinks for Windows host..."
& (Join-Path $PSScriptRoot "restore_symlinks.ps1")


function Test-Command {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-DockerImage {
    param([string]$Image)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        docker image inspect $Image *> $null
        return $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

if (-not (Test-Command "docker")) {
    throw "docker is required but was not found in PATH."
}

if (-not (Test-DockerImage $BaseImage)) {
    Write-Host "Sophon base image not found locally: $BaseImage"

    if (-not $ImageTar) {
        New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
        $ImageTar = Join-Path $CacheDir (Split-Path $DfssUrl -Leaf)

        if (-not (Test-Path -LiteralPath $ImageTar)) {
            try {
                Write-Host "Installing or upgrading dfss..."
                if ($PipIndexUrl) {
                    python -m pip install --user --upgrade -i $PipIndexUrl dfss
                } else {
                    python -m pip install --user --upgrade dfss
                }

                Write-Host "Downloading Sophon stream dev image:"
                Write-Host "  $DfssUrl"
                Push-Location $CacheDir
                try {
                    python -m dfss "--url=$DfssUrl"
                    if ($LASTEXITCODE -ne 0) {
                        throw "dfss failed with exit code $LASTEXITCODE"
                    }
                } finally {
                    Pop-Location
                }
            } catch {
                Write-Error @"
Failed to download Sophon stream dev image with dfss.
The Sophon dfss servers may be unreachable from this network.

You can either:
  1. Download stream_dev_22.04.tar manually, then run:
     `$env:SOPHON_STREAM_DEV_TAR="C:\path\to\stream_dev_22.04.tar"; .\scripts\build_sophon_package.ps1
  2. Load the image yourself with:
     docker load -i C:\path\to\stream_dev_22.04.tar
     .\scripts\build_sophon_package.ps1
"@
                throw
            }
        }
    }

    if (-not (Test-Path -LiteralPath $ImageTar)) {
        throw "Sophon image tar was not found: $ImageTar"
    }

    Write-Host "Loading Sophon base image from $ImageTar..."
    docker load -i $ImageTar

    if (-not (Test-DockerImage $BaseImage)) {
        throw "docker load completed, but $BaseImage is still unavailable. Set SOPHON_BASE_IMAGE to the tag shown by 'docker images' and rerun this script."
    }
} else {
    Write-Host "Sophon base image already exists: $BaseImage"
}

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $ProjectRoot
try {
    docker compose -f docker-compose.sophon.yml up --build
} finally {
    Pop-Location
}
