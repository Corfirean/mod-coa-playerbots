param(
    [string]$RepackPath = "C:\games\CoA-Repack",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$PackageRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Manifest = Get-Content (Join-Path $PackageRoot "release.json") -Raw | ConvertFrom-Json

function Fail($msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

$RepackReleaseFile = Join-Path $RepackPath "RELEASE.json"
if (-not (Test-Path $RepackReleaseFile)) {
    Fail "No RELEASE.json found at '$RepackReleaseFile'. -RepackPath must point at a CoA-Repack root (the folder containing Core\ and RELEASE.json)."
}
$RepackRelease = Get-Content $RepackReleaseFile -Raw | ConvertFrom-Json

if ($RepackRelease.sourceRevision -ne $Manifest.builtForRepack.sourceRevision) {
    $msg = "This patch was built for repack sourceRevision '$($Manifest.builtForRepack.sourceRevision)', " +
           "but the target repack reports '$($RepackRelease.sourceRevision)'. Bots may not work correctly " +
           "on a different repack release."
    if ($Force) {
        Write-Host "WARNING: $msg (continuing anyway, -Force given)" -ForegroundColor Yellow
    } else {
        Fail "$msg Get the matching patch release, or re-run with -Force to install anyway at your own risk."
    }
}

$CoreDir = Join-Path $RepackPath "Core"
$TargetExe = Join-Path $CoreDir "worldserver.exe"
$BackupExe = Join-Path $CoreDir "worldserver.exe.orig"
if (-not (Test-Path $TargetExe)) {
    Fail "'$TargetExe' not found."
}
if (Get-Process -Name "worldserver" -ErrorAction SilentlyContinue) {
    Fail "worldserver.exe is currently running -- stop the server first (this would kick everyone and the file is locked while it runs)."
}

$CurrentHash = (Get-FileHash $TargetExe -Algorithm SHA256).Hash.ToLower()
$IsOriginal = $Manifest.originalWorldserverSha256 -contains $CurrentHash
$IsAlreadyPatched = $Manifest.patchedWorldserverSha256 -contains $CurrentHash

if (-not $IsOriginal -and -not $IsAlreadyPatched -and -not $Force) {
    Fail ("'$TargetExe' (SHA256 $CurrentHash) matches neither the expected original repack binary " +
          "nor a known build of this patch. It may already be modified by something else. " +
          "Re-run with -Force to overwrite anyway.")
}

if ($IsAlreadyPatched) {
    Write-Host "This exact patch build is already installed -- reapplying (repair/no-op)." -ForegroundColor Cyan
} elseif (-not (Test-Path $BackupExe)) {
    Write-Host "Backing up original worldserver.exe -> worldserver.exe.orig"
    Copy-Item $TargetExe $BackupExe
}

Write-Host "Installing worldserver.exe..."
Copy-Item (Join-Path $PackageRoot "bin\worldserver.exe") $TargetExe -Force

$ModulesConfDir = Join-Path $CoreDir "configs\modules"
New-Item -ItemType Directory -Force -Path $ModulesConfDir | Out-Null

Write-Host "Installing mod_coa_playerbots.conf.dist..."
Copy-Item (Join-Path $PackageRoot "conf\mod_coa_playerbots.conf.dist") (Join-Path $ModulesConfDir "mod_coa_playerbots.conf.dist") -Force

$RealConf = Join-Path $ModulesConfDir "mod_coa_playerbots.conf"
if (-not (Test-Path $RealConf)) {
    Write-Host "No existing mod_coa_playerbots.conf -- creating one from the template."
    Copy-Item (Join-Path $ModulesConfDir "mod_coa_playerbots.conf.dist") $RealConf
} else {
    Write-Host "Existing mod_coa_playerbots.conf left untouched -- diff it against the .dist for any new keys." -ForegroundColor Yellow
}

$ReferenceDir = Join-Path $CoreDir "reference"
New-Item -ItemType Directory -Force -Path $ReferenceDir | Out-Null
Write-Host "Installing talent build data..."
Copy-Item (Join-Path $PackageRoot "reference\ascensionsidekick-level-builds.json") (Join-Path $ReferenceDir "ascensionsidekick-level-builds.json") -Force

$AddonDest = Join-Path $RepackPath "Client\Interface\AddOns\CoABotUI"
$ClientDir = Join-Path $RepackPath "Client"
if (Test-Path $ClientDir) {
    Write-Host "Installing client addon to $AddonDest..."
    New-Item -ItemType Directory -Force -Path (Split-Path $AddonDest -Parent) | Out-Null
    Copy-Item (Join-Path $PackageRoot "addon\CoABotUI") $AddonDest -Recurse -Force
} else {
    Write-Host "No Client\ folder found under the repack -- skipping addon install. Copy dist\addon\CoABotUI into your WoW client's Interface\AddOns\ manually." -ForegroundColor Yellow
}

# Apply any bundled SQL migrations the core needs (idempotent -- CREATE TABLE IF NOT EXISTS style).
$MysqlExe = Join-Path $RepackPath "mysql\bin\mysql.exe"
$WorldConf = Join-Path $CoreDir "configs\worldserver.conf"
$SqlFiles = Get-ChildItem (Join-Path $PackageRoot "sql") -Filter "*.sql" -ErrorAction SilentlyContinue
if ($SqlFiles -and (Test-Path $MysqlExe) -and (Test-Path $WorldConf)) {
    $dbLine = Select-String -Path $WorldConf -Pattern '^\s*CharacterDatabaseInfo\s*=\s*"([^"]+)"' | Select-Object -First 1
    if ($dbLine) {
        $parts = $dbLine.Matches[0].Groups[1].Value -split ';'
        # AzerothCore format: hostname;port;user;password;database
        $dbHost, $dbPort, $dbUser, $dbPass, $dbName = $parts
        foreach ($f in $SqlFiles) {
            Write-Host "Applying core DB migration $($f.Name)..."
            & $MysqlExe "-h$dbHost" "-P$dbPort" "-u$dbUser" "-p$dbPass" $dbName -e "source $($f.FullName)"
        }
    } else {
        Write-Host "Could not find CharacterDatabaseInfo in worldserver.conf -- apply dist\sql\*.sql to the characters DB manually." -ForegroundColor Yellow
    }
} else {
    Write-Host "Skipping DB migrations (mysql client, worldserver.conf, or no bundled .sql files found) -- apply dist\sql\*.sql to the characters DB manually if the server fails to start with a 'Could not prepare statements' error." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Done. Start the server, then create a bot population with:" -ForegroundColor Green
Write-Host "    .botcmd spawnleveled 500"
Write-Host "from GM chat or the RA console, and set CoaBots.AutoLoginOnStartup = 1 in mod_coa_playerbots.conf" -ForegroundColor Green
Write-Host "so bots log back in automatically on future restarts." -ForegroundColor Green
