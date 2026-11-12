# RC3.2 Security Gate: Credential Revocation & Residual Risk Assessment

**Date:** 2026-11-12  
**Target:** Comprehensive Credential Revocation & Remote History Audit  
**Core Branch:** `perf/3000-active-bots-rc3-2` (`C:\games\source\candidate-core-20261028`)  
**CoA Bots Branch:** `perf/3000-active-bots-rc3-2` (`C:\games\source\release-bots-20261029`)  
**Security Gate Status:** **PARTIAL (ACTION REQUIRED — OWNER ROTATION & REMOTE PURGE PENDING)**  

---

## 1. Executive Summary & Forensic Findings

During the RC3 delivery, a diagnostic report (`RC3_SQUID_REGRESSION_RESULTS.md`, line 51) inadvertently published a database connection string containing an active 48-character authentication credential:
```text
PlayerbotsDatabaseInfo = "127.0.0.1;64187;acore;<redacted>;acore_playerbots"
```

Our forensic audit performed under **RC3.2** determined the following factual findings:

1. **Origin of Secret**:
   - The credential originates from `C:\games\CoA Server 2\Core\configs\worldserver.conf` (`LoginDatabaseInfo`, `WorldDatabaseInfo`, `CharacterDatabaseInfo`, `PlayerbotsDatabaseInfo`).
   - The benchmarking automation (`benchmark.py`, line 705–720) cloned the configuration and data directory from `C:\games\CoA Server 2` to create the disposable test fixture (`C:\games\source\bot-performance-20261102`), rebinding the port to loopback `127.0.0.1:64187`.
   - The credential is the active authentication password for user `acore` in the host's local MySQL database (`C:\games\CoA Server 2\mysql`).

2. **Network Scope & Exposure Boundary**:
   - Both the source instance (`C:\games\CoA Server 2\mysql\my.ini`) and the benchmark instance bind strictly to `127.0.0.1` (`bind-address=127.0.0.1`).
   - The database server is not bound to public interfaces and is not directly accessible over the external network.

3. **Status of Rotation**:
   - The secret remains active in the host's `C:\games\CoA Server 2\Core\configs\worldserver.conf`.
   - In accordance with safety constraints, **the agent cannot rotate the credentials of the user's primary/host server installation without explicit authorization**, as doing so could disrupt external services or active local environments.
   - Therefore, per the qualification rules, **the Security Gate CANNOT be marked PASS until the owner executes rotation.**

4. **Git History Exposure**:
   - In the active working tree (`perf/3000-active-bots-rc3-1` and `perf/3000-active-bots-rc3-2`), all occurrences are redacted to `<redacted>`.
   - However, the secret remains present in the historical commit objects of published branches:
     - `https://github.com/Corfirean/azerothcore-wotlk-coa` (branch `perf/3000-active-bots-rc3`, commit `57d2b76fefba06ab2e62e521cd32e878558d196e`).
     - `https://github.com/Corfirean/mod-coa-playerbots` (branch `perf/3000-active-bots-rc3`, commit `842f5a470762043c56118b306a28dd7355108ebb`).

---

## 2. Action Required Checklist (For Server Owner)

To achieve full `SECURITY PASS`, the server owner must execute two remediation steps:

### Action 1: Local MySQL Credential Rotation
Execute the following commands in the local MySQL instance on `C:\games\CoA Server 2`:

```sql
-- Connect via mysql client
ALTER USER 'acore'@'localhost' IDENTIFIED BY '<NEW_SECURE_PASSWORD>';
ALTER USER 'acore'@'127.0.0.1' IDENTIFIED BY '<NEW_SECURE_PASSWORD>';
FLUSH PRIVILEGES;
```

Update `C:\games\CoA Server 2\Core\configs\worldserver.conf` and `authserver.conf`:
```ini
LoginDatabaseInfo     = "127.0.0.1;3307;acore;<NEW_SECURE_PASSWORD>;acore_auth"
WorldDatabaseInfo     = "127.0.0.1;3307;acore;<NEW_SECURE_PASSWORD>;acore_world"
CharacterDatabaseInfo = "127.0.0.1;3307;acore;<NEW_SECURE_PASSWORD>;acore_characters"
PlayerbotsDatabaseInfo= "127.0.0.1;3307;acore;<NEW_SECURE_PASSWORD>;acore_playerbots"
```

### Action 2: Remote GitHub Branch Purge / History Scrub
Because `perf/3000-active-bots-rc3` is an intermediate development branch and NOT the production `coa-bots` or `master` branch:

**Recommended Option A (Immediate Branch Deletion on Remote):**
Since RC3 is completely superseded by RC3.1 and RC3.2, delete the obsolete RC3 branch from GitHub:
```powershell
git -C C:\games\source\candidate-core-20261028 push origin --delete perf/3000-active-bots-rc3
git -C C:\games\source\release-bots-20261029 push origin --delete perf/3000-active-bots-rc3
```

**Option B (History Rewrite via `git-filter-repo`):**
If historical retention of the branch is required:
```powershell
# In a fresh clone or backup:
pip install git-filter-repo
git-filter-repo --replace-text <(echo "regex:127\.0\.0\.1;64187;acore;[0-9a-fA-F]+;acore_playerbots==>127.0.0.1;64187;acore;<redacted>;acore_playerbots") --force
git push origin perf/3000-active-bots-rc3 --force
```
*Note on Impact*: Option B will rewrite commit hashes on branch `perf/3000-active-bots-rc3`. Other contributors would need to re-fetch the branch.

---

## 3. Residual Risk Analysis

| Risk Vector | Exposure Level | Mitigation State | Residual Risk |
|---|---|---|---|
| **Remote Exploitation** | Low | Server binds to `127.0.0.1`; not externally routable | Minimal |
| **Local Privilege Escalation** | Medium | Local user with access to localhost port 3307 | Addressed upon Action 1 completion |
| **Public GitHub Scraping** | High | Secret exists in commit `57d2b76fe` and `842f5a4` | Action Required (Delete or scrub remote RC3 branch) |

**Conclusion:** Active working trees and branches `perf/3000-active-bots-rc3-1` and `perf/3000-active-bots-rc3-2` are 100% clean of plaintext secrets. Full security closure is pending completion of owner actions above.
