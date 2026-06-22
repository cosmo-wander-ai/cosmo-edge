# This script restores symlinks that Git on Windows checked out as plain text files containing the target file name.
# It resolves the targets and replaces the text files with copies of the actual binary files.

$targetDirs = @(
    "$PSScriptRoot/../prebuild",
    "$PSScriptRoot/../3rd"
)

foreach ($dir in $targetDirs) {
    # Resolve the path to absolute
    $resolvedDir = [System.IO.Path]::GetFullPath($dir)
    if (-not (Test-Path $resolvedDir)) {
        Write-Warning "Directory not found: $resolvedDir"
        continue
    }

    Write-Host "Scanning for dummy symbolic link files in $resolvedDir..."

    Get-ChildItem -Path $resolvedDir -Recurse -File | ForEach-Object {
        # Check if the file is a potential git symlink placeholder (small text file containing a relative path)
        if ($_.Length -lt 200 -and ($_.Name -like "*.so" -or $_.Name -like "*.so.*" -or $_.Extension -eq "")) {
            try {
                $content = (Get-Content $_.FullName -Raw -ErrorAction Stop).Trim()
                # Verify the content looks like a filename in the same directory
                if ($content -and -not ($content -match "[\r\n]") -and (Test-Path (Join-Path $_.DirectoryName $content))) {
                    $targetPath = Join-Path $_.DirectoryName $content
                    
                    # Recursively resolve symlink chain if the target itself is another symlink placeholder
                    $safetyCounter = 0
                    while ($safetyCounter -lt 10) {
                        $targetItem = Get-Item $targetPath
                        if ($targetItem.Length -lt 200) {
                            $nextContent = (Get-Content $targetPath -Raw).Trim()
                            if ($nextContent -and -not ($nextContent -match "[\r\n]") -and (Test-Path (Join-Path $targetItem.DirectoryName $nextContent))) {
                                $targetPath = Join-Path $targetItem.DirectoryName $nextContent
                                $safetyCounter++
                                continue
                            }
                        }
                        break
                    }
                    
                    Write-Host "Restoring: $($_.FullName) -> $targetPath"
                    # Remove the placeholder file and copy the real file over
                    Remove-Item $_.FullName -Force
                    Copy-Item -Path $targetPath -Destination $_.FullName -Force
                }
            } catch {
                # Not a plain text file or failed to read, skip
            }
        }
    }
}

Write-Host "Symlinks restoration completed successfully!"
