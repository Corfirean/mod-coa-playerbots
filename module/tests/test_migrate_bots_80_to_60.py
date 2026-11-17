from pathlib import Path
import sqlite3
import sys
import tempfile

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import migrate_bots_80_to_60 as mig


def create_mock_db() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.executescript(
        """
        CREATE TABLE account (id INTEGER PRIMARY KEY, username TEXT NOT NULL);
        CREATE TABLE characters (guid INTEGER PRIMARY KEY, account INTEGER NOT NULL, level INTEGER NOT NULL, xp INTEGER NOT NULL);
        CREATE TABLE guild_member (guildid INTEGER NOT NULL, guid INTEGER NOT NULL);
        CREATE TABLE group_member (guid INTEGER NOT NULL, memberGuid INTEGER NOT NULL);
        CREATE TABLE character_social (guid INTEGER NOT NULL, friend INTEGER NOT NULL, flags INTEGER NOT NULL);
        CREATE TABLE character_spell (guid INTEGER NOT NULL, spell INTEGER NOT NULL, active INTEGER NOT NULL, disabled INTEGER NOT NULL, PRIMARY KEY (guid, spell));

        INSERT INTO account VALUES (1, 'ADMIN'), (2, 'PLAYER1'), (3, 'CoaBotHost1');
        INSERT INTO characters VALUES
            (10, 2, 80, 500),   -- real player
            (100, 3, 80, 123),  -- unprotected bot (should migrate 80->60)
            (101, 3, 80, 456),  -- guilded bot (protected)
            (102, 3, 80, 789),  -- grouped bot (protected)
            (103, 3, 80, 321),  -- real player's friend (protected)
            (104, 3, 45, 99);   -- already below cap

        INSERT INTO guild_member VALUES (1, 101);
        INSERT INTO group_member VALUES (1, 102);
        INSERT INTO character_social VALUES (10, 103, 1);

        INSERT INTO character_spell VALUES
            (100, 6603, 1, 0),   -- Attack (baseline, must stay)
            (100, 47436, 1, 0),  -- Over-cap level 80 spell (must strip)
            (101, 47436, 1, 0);  -- Protected bot spell (must stay)
        """
    )
    return conn


def main() -> None:
    conn = create_mock_db()
    over_cap = {47436}

    # 1. Dry-run makes zero DB changes.
    dry = mig.apply_migration(conn, target_cap=60, over_cap_spells=over_cap, dry_run=True)
    assert dry["dry_run"] is True
    assert dry["candidates_count"] == 1
    assert dry["protected_count"] == 3
    assert dry["spells_to_remove_count"] == 1
    assert conn.execute("SELECT level FROM characters WHERE guid = 100").fetchone()[0] == 80

    # 2. Apply on copy DB with backup + post-snapshot diff.
    with tempfile.TemporaryDirectory() as tmp:
        backup = Path(tmp) / "backup.json"
        res = mig.apply_migration(conn, target_cap=60, over_cap_spells=over_cap, backup_path=backup, dry_run=False)
        assert res["post_diff_ok"] is True
        assert backup.exists()

        # Verify unprotected bot migrated, baseline spell kept, over-cap spell removed.
        assert conn.execute("SELECT level FROM characters WHERE guid = 100").fetchone()[0] == 60
        assert conn.execute("SELECT 1 FROM character_spell WHERE guid = 100 AND spell = 6603").fetchone() is not None
        assert conn.execute("SELECT 1 FROM character_spell WHERE guid = 100 AND spell = 47436").fetchone() is None

        # Verify all 3 protected bots + already-below-cap bot + real player untouched.
        for g in (10, 101, 102, 103):
            assert conn.execute("SELECT level FROM characters WHERE guid = ?", (g,)).fetchone()[0] == 80
        assert conn.execute("SELECT 1 FROM character_spell WHERE guid = 101 AND spell = 47436").fetchone() is not None
        assert conn.execute("SELECT level FROM characters WHERE guid = 104").fetchone()[0] == 45

        # 3. Idempotency: second run finds 0 candidates.
        res2 = mig.apply_migration(conn, target_cap=60, over_cap_spells=over_cap, backup_path=backup, dry_run=False)
        assert res2["candidates_count"] == 0
        assert res2["spells_to_remove_count"] == 0

        # 4. Rollback restores exact pre-migration state.
        # Note: backup was overwritten on res2 (empty); test rollback with a fresh migration on a fresh DB!
    conn2 = create_mock_db()
    with tempfile.TemporaryDirectory() as tmp2:
        backup2 = Path(tmp2) / "backup2.json"
        mig.apply_migration(conn2, target_cap=60, over_cap_spells=over_cap, backup_path=backup2, dry_run=False)
        restored = mig.rollback_migration(conn2, backup2)
        assert restored >= 1
        assert conn2.execute("SELECT level, xp FROM characters WHERE guid = 100").fetchone() == (80, 123)
        assert conn2.execute("SELECT 1 FROM character_spell WHERE guid = 100 AND spell = 47436").fetchone() is not None

    print("PB-004 migration tests: dry-run, backup, post-snapshot diff, protected-bot exclusion, idempotency, and rollback passed.")


if __name__ == "__main__":
    main()
