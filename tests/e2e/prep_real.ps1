# Copies one real pixelglyphlib font and the pxgl files it names into tests\tmp\reallib (same pxfm/xx and pxgl/xx layout).
# The source library is only read, never written.
param(
    [string]$Source = "C:\Users\jnsGEzD3\Documents\GitHub\pixelglyphlib",
    [string]$Dest = "tests\tmp\reallib",
    [string]$Hash = "b86206e5d4408e60dd8174f29050daa7"
)
$ErrorActionPreference = "Stop"
if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
$xx = $Hash.Substring(0, 2)
$pxfmDir = Join-Path $Dest "pxfm\$xx"
New-Item -ItemType Directory -Force $pxfmDir | Out-Null
$pxfm = Join-Path $Source "pxfm\$xx\$Hash.pxfm"
Copy-Item $pxfm $pxfmDir
$hashes = Get-Content $pxfm | ForEach-Object { $f = $_ -split "`t"; if ($f.Count -ge 5 -and $f[0] -ieq "glyph") { $f[4].ToLower() } } | Sort-Object -Unique
foreach ($h in $hashes) {
    $d = Join-Path $Dest "pxgl\$($h.Substring(0, 2))"
    New-Item -ItemType Directory -Force $d | Out-Null
    Copy-Item (Join-Path $Source "pxgl\$($h.Substring(0, 2))\$h.pxgl") $d
}
Write-Host "real lib: $($hashes.Count) pxgl files copied to $Dest"
