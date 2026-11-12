# RC3.1 Security Incident Report & Credential Remediation Plan

**Date:** 2026-11-12  
**Incident Classification:** P0 Security Incident — Database Credential Exposure  
**Status:** Remediated in Active Branches / History Remediation Plan Prepared  
**Severity:** High (Credential committed to branch; local loopback scope)  

---

## 1. Incident Description & Scope

During the documentation of the multi-bot regression matrix in milestone RC3, an unredacted connection string for `PlayerbotsDatabaseInfo` containing a MySQL credential hash was accidentally included in the markdown report:
`RC3_SQUID_REGRESSION_RESULTS.md` (Line 51).

This report was committed and pushed to remote branch `perf/3000-active-bots-rc3` in:
- `https://github.com/Corfirean/azerothcore-wotlk-coa` (Commit `57d2b76fe`)
- `https://github.com/Corfirean/mod-coa-playerbots` (Commit `842f5a4`)

### Asset & Exposure Assessment:
- **Exposed Service**: MySQL Database connection string (`acore` user).
- **Network Scope**: The connection string specifies target host `127.0.0.1` on non-default port `64187`.
- **Target Instance**: The local performance test harness and local development server (`CoA Server 2`). It is NOT an externally accessible public production host or internet-facing endpoint.
- **Exposure Window**: Present in branch `perf/3000-active-bots-rc3` from 13:28 UTC until immediate remediation in milestone RC3.1.

---

## 2. Immediate Remediation Actions Taken

1. **Active Working Copy Redaction**:
   - Both working trees (`candidate-core-20261028` and `release-bots-20261029`) have had line 51 updated to:
     ```text
     PlayerbotsDatabaseInfo = "127.0.0.1;64187;acore;<redacted>;acore_playerbots"
     ```
   - All subsequent RC3.1 reports and configurations enforce strict token redaction.
2. **Scanner Verification**:
   - Automated secret-scanning across all tracked files in both repositories confirmed zero remaining instances of the exposed credential in working branches `perf/3000-active-bots-rc3-1`.
3. **Operational Policy Enforcement**:
   - Strict adherence to rule: Never output credential plaintext to console logs, agent transcripts, build logs, or markdown documentation.

---

## 3. Credential Rotation Instructions (Local Development Environment)

Because the credential was published to a remote branch, it must be considered compromised and rotated.

### Rotation Procedure:
1. Log into local MySQL management console on port 64187 (or local development instance):
   ```sql
   ALTER USER 'acore'@'127.0.0.1' IDENTIFIED BY '<NEW_STRONG_PASSWORD>';
   ALTER USER 'acore'@'localhost' IDENTIFIED BY '<NEW_STRONG_PASSWORD>';
   FLUSH PRIVILEGES;
   ```
2. Update local configuration files (kept outside git version control or within ignored paths):
   - `configs/worldserver.conf` (`LoginDatabaseInfo`, `WorldDatabaseInfo`, `CharacterDatabaseInfo`)
   - `configs/modules/playerbots.conf` (`PlayerbotsDatabaseInfo`)
   - `private-client.ini` (test harness client configuration)
3. Verify server connection:
   - Run `worldserver.exe -c configs/worldserver.conf` and confirm clean pool initialization for `acore_auth`, `acore_characters`, `acore_world`, and `acore_playerbots`.

---

## 4. Git History Remediation Plan

To permanently eradicate the commit from git history on remote branches, the following procedure should be executed by repository maintainers after explicit authorization:

> [!WARNING]
> Rewriting git history alters commit SHAs. Anyone who has pulled `perf/3000-active-bots-rc3` will need to rebase or reset their local tracking branch.

### Maintenance Steps:
1. Use `git-filter-repo` on a fresh clone of both repositories:
   ```bash
   pip install git-filter-repo
   git filter-repo --replace-text <(echo "<EXPOSED_HASH>==><redacted>")
   ```
2. Force-push the rewritten branch `perf/3000-active-bots-rc3`:
   ```bash
   git push origin perf/3000-active-bots-rc3 --force
   ```
3. Alternatively, if branch `perf/3000-active-bots-rc3` is superseded by `perf/3000-active-bots-rc3-1`:
   - Delete remote branch `perf/3000-active-bots-rc3` from GitHub:
     ```bash
     git push origin --delete perf/3000-active-bots-rc3
     ```
   - Direct all reviews and integration to `perf/3000-active-bots-rc3-1`.

---

## 5. Security Acceptance Status

- [x] Unredacted credential removed from all active files.
- [x] Automated scanner confirmed zero secrets in RC3.1 working tree.
- [x] Scope bounded to local loopback (127.0.0.1) development fixture.
- [x] Clear rotation instructions documented without printing credentials.
- [x] Git history remediation plan prepared.
- **Residual Risk**: Present in commit `57d2b76fe` / `842f5a4` on `perf/3000-active-bots-rc3` until remote branch deletion or filter-repo rebase by maintainer.
