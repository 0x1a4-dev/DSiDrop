param(
    [string]$DevkitPro = 'C:\devkitPro'
)
$ErrorActionPreference = 'Stop'
$ndsRoot = $PSScriptRoot
$shellPath = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $shellPath)) {
    throw "devkitPro was not found at $DevkitPro. Install the current nds-dev package with libnds 2.x/Calico."
}
$env:ROMS_FINDER_NDS_DIR = $ndsRoot
& $shellPath -lc 'cd "$(cygpath -u "$ROMS_FINDER_NDS_DIR")" && make -j2'
if ($LASTEXITCODE -ne 0) { throw 'NDS build failed.' }

$distPath = Join-Path $ndsRoot 'dist'
$appHeader = Get-Content -LiteralPath (Join-Path $ndsRoot 'include\app.h') -Raw
if ($appHeader -notmatch '#define APP_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"') {
    throw 'APP_VERSION is missing from include/app.h.'
}
$appVersion = $Matches[1]
New-Item -ItemType Directory -Path $distPath -Force | Out-Null
$builtAppPath = Join-Path $ndsRoot 'DSiDrop.nds'
$distAppPath = Join-Path $distPath 'DSiDrop.nds'
Copy-Item -LiteralPath $builtAppPath -Destination (Join-Path $distPath "DSiDrop-$appVersion.nds") -Force
Copy-Item -LiteralPath $builtAppPath -Destination $distAppPath -Force
$webDownloadPath = Join-Path (Split-Path -Parent $ndsRoot) 'public\downloads'
New-Item -ItemType Directory -Path $webDownloadPath -Force | Out-Null
Copy-Item -LiteralPath $builtAppPath -Destination (Join-Path $webDownloadPath 'DSiDrop.nds') -Force
$legacyDownloadPath = Join-Path $webDownloadPath 'DSiDrop-DSi.zip'
if (Test-Path -LiteralPath $legacyDownloadPath -PathType Leaf) {
    Remove-Item -LiteralPath $legacyDownloadPath
}
$hash = (Get-FileHash -LiteralPath $distAppPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $distPath 'DSiDrop-SHA256SUMS.txt') -Value "$hash  DSiDrop.nds" -Encoding ascii
Write-Output "NDS: $distAppPath"
Write-Output "Download: $(Join-Path $webDownloadPath 'DSiDrop.nds')"
Write-Output "SHA256: $hash"
