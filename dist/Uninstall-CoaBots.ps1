param(
    [string]$RepackPath = "C:\games\CoA-Repack"
)

$ErrorActionPreference = "Stop"
$CoreDir = Join-Path $RepackPath "Core"
$TargetExe = Join-Path $CoreDir "worldserver.exe"
$BackupExe = Join-Path $CoreDir "worldserver.exe.orig"

if (Test-Path $BackupExe) {
    Write-Host "Restoring original worldserver.exe..."
    Copy-Item $BackupExe $TargetExe -Force
    Remove-Item $BackupExe
    Write-Host "Restored. Bot character data in the characters DB is left as-is (bot accounts stay, unused)." -ForegroundColor Green
} else {
    Write-Host "No worldserver.exe.orig backup found -- nothing to restore. Remove worldserver.exe manually if needed." -ForegroundColor Yellow
}
