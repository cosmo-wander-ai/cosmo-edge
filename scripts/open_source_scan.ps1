param(
    [string]$Root = ".",
    [switch]$IncludeThirdParty,
    [int]$MaxMatches = 80
)

$ErrorActionPreference = "Stop"

function Add-Arg {
    param([System.Collections.Generic.List[string]]$Args, [string]$Value)
    $Args.Add($Value) | Out-Null
}

function Add-ExcludeGlob {
    param([System.Collections.Generic.List[string]]$Args, [string]$Glob)
    Add-Arg $Args "--glob"
    Add-Arg $Args "!$Glob"
}

function New-BaseArgs {
    param([switch]$Files)

    $argsList = [System.Collections.Generic.List[string]]::new()

    if ($Files) {
        Add-Arg $argsList "--files"
    } else {
        Add-Arg $argsList "--line-number"
    }

    Add-Arg $argsList "--hidden"
    Add-Arg $argsList "--no-messages"
    Add-Arg $argsList "--color"
    Add-Arg $argsList "never"

    Add-ExcludeGlob $argsList ".git/**"
    Add-ExcludeGlob $argsList "**/.git/**"
    Add-ExcludeGlob $argsList "node_modules/**"
    Add-ExcludeGlob $argsList "**/node_modules/**"
    Add-ExcludeGlob $argsList "docs/.vitepress/dist/**"
    Add-ExcludeGlob $argsList "**/docs/.vitepress/dist/**"
    Add-ExcludeGlob $argsList "build*/**"
    Add-ExcludeGlob $argsList "**/build*/**"
    Add-ExcludeGlob $argsList "package-lock.json"
    Add-ExcludeGlob $argsList "**/package-lock.json"
    Add-ExcludeGlob $argsList "**/*.png"
    Add-ExcludeGlob $argsList "**/*.jpg"
    Add-ExcludeGlob $argsList "**/*.jpeg"
    Add-ExcludeGlob $argsList "**/*.gif"
    Add-ExcludeGlob $argsList "**/*.mp4"
    Add-ExcludeGlob $argsList "**/*.avi"
    Add-ExcludeGlob $argsList "**/*.mov"
    Add-ExcludeGlob $argsList "**/*.zip"
    Add-ExcludeGlob $argsList "**/*.tar"
    Add-ExcludeGlob $argsList "**/*.gz"
    Add-ExcludeGlob $argsList "**/*.7z"
    Add-ExcludeGlob $argsList "**/*.so"
    Add-ExcludeGlob $argsList "**/*.dll"
    Add-ExcludeGlob $argsList "**/*.exe"
    Add-ExcludeGlob $argsList "**/*.min.js"

    if (-not $IncludeThirdParty) {
        Add-ExcludeGlob $argsList "3rd/**"
        Add-ExcludeGlob $argsList "**/3rd/**"
    }

    return $argsList
}

function Write-Matches {
    param([string[]]$Matches)

    if ($Matches.Count -eq 0) {
        Write-Host "No matches"
        return
    }

    $Matches | Select-Object -First $MaxMatches
    if ($Matches.Count -gt $MaxMatches) {
        Write-Host "... truncated: $($Matches.Count - $MaxMatches) additional matches. Re-run with -MaxMatches for a larger sample."
    }
}

function Test-IncludedPath {
    param([string]$Path)

    $normalized = $Path -replace "\\", "/"
    $normalized = $normalized -replace "^\./", ""

    if ($normalized -match "(^|/)(\.git|node_modules)(/|$)") {
        return $false
    }

    if ($normalized -eq "package-lock.json" -or $normalized -match "/package-lock\.json$") {
        return $false
    }

    if ($normalized -match "(^|/)docs/\.vitepress/dist(/|$)") {
        return $false
    }

    if ($normalized -match "(^|/)build[^/]*/") {
        return $false
    }

    if (-not $IncludeThirdParty -and $normalized -match "(^|/)3rd(/|$)") {
        return $false
    }

    return $true
}

function Select-IncludedMatches {
    param([string[]]$Matches)

    @($Matches | Where-Object {
        $candidatePath = ($_ -split ":", 2)[0]
        Test-IncludedPath $candidatePath
    })
}

function Invoke-Scan {
    param([string]$Title, [string]$Pattern)

    Write-Host ""
    Write-Host "== $Title =="

    $argsList = New-BaseArgs
    $rawMatches = @(& rg @argsList "--" $Pattern $Root)
    $matches = Select-IncludedMatches -Matches $rawMatches
    if ($LASTEXITCODE -eq 1) {
        Write-Host "No matches"
    } elseif ($LASTEXITCODE -ne 0) {
        throw "rg failed for scan: $Title"
    } else {
        Write-Matches $matches
    }
}

function Invoke-FileScan {
    param([string]$Title, [string]$Pattern)

    Write-Host ""
    Write-Host "== $Title =="

    $argsList = New-BaseArgs -Files
    $files = @(& rg @argsList $Root | Where-Object { $_ -match $Pattern })
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 1) {
        throw "rg failed for file scan: $Title"
    }

    Write-Matches $files
}

if (-not (Get-Command rg -ErrorAction SilentlyContinue)) {
    throw "ripgrep (rg) is required. Install rg or run equivalent manual scans."
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
Write-Host "Open-source release scan root: $resolvedRoot"
Write-Host "Include third-party directory: $IncludeThirdParty"
Write-Host "Maximum matches per category: $MaxMatches"
Write-Host "Review every match manually. This script reports candidates; it does not prove a file is safe or unsafe."

Invoke-Scan "Secrets and credentials" "(?i)(api[_-]?key|access[_-]?key|secret|token|passwd|password|private[_-]?key|BEGIN (RSA|OPENSSH|EC|DSA)? ?PRIVATE KEY|authorization:|bearer )"
Invoke-Scan "Private IP addresses" "\b(10\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[0-1])\.[0-9]{1,3}\.[0-9]{1,3}|192\.168\.[0-9]{1,3}\.[0-9]{1,3})\b"
Invoke-Scan "Device serial number hints" "(?i)(device\s*sn|deviceSn|serialNumber|serial[_-]?number|SN[:=]|aibox::)"
Invoke-Scan "Private or internal wording" "(?i)(internal[-_ ]only|private[-_ ]only|confidential|customer|客户|项目现场|内部|私有)"
Invoke-Scan "Private model/package keywords" "(?i)(private.*(model|package|artifact)|model.*(download|url|http)|weight|bmodel|onnx|artifact)"
Invoke-Scan "HTTP URLs for review" 'https?://[^\s"''<>]+'
Invoke-FileScan "Sensitive file names" "(?i)(^|[\\/])\.env(\.|$)|id_rsa|id_dsa|\.pem$|\.p12$|\.pfx$"

Write-Host ""
Write-Host "Done. Record reviewed findings in docs/project/sensitive-data-review.md or the release checklist."
