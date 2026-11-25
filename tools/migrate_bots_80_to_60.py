#!/usr/bin/env python3
"""
PB-004: Safe, idempotent 80 -> 60 bot migration tool with dry-run, backup,
post-snapshot diff verification, rollback, and PB-002 protected-bot exclusion.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any, Iterable


@dataclass(frozen=True)
class MigrationPlan:
    target_cap: int
    protected_guids: tuple[int, ...]
    candidate_levels: dict[int, int]
    spells_to_remove: list[tuple[int, int]]


def _fetchall_dicts(cursor: Any) -> list[dict[str, Any]]:
    cols = [d[0] for d in cursor.description]
    return [dict(zip(cols, row)) for row in cursor.fetchall()]


def find_protected_guids(conn: Any, account_prefix: str) -> set[int]:
    cur = conn.cursor()
    protected: set[int] = set()

    cur.execute("SELECT DISTINCT guid FROM guild_member")
    protected.update(int(r[0]) for r in cur.fetchall())

    cur.execute("SELECT DISTINCT memberGuid FROM group_member")
    protected.update(int(r[0]) for r in cur.fetchall())

    cur.execute(
        "SELECT DISTINCT cs.friend "
        "FROM character_social cs "
        "JOIN characters owner ON owner.guid = cs.guid "
        "JOIN account a ON a.id = owner.account "
        "WHERE (cs.flags & 1) <> 0 AND (a.id <= 2 OR a.username NOT LIKE ?)",
        (f"{account_prefix}%",),
    )
    protected.update(int(r[0]) for r in cur.fetchall())
    return protected


def build_migration_plan(
    conn: Any,
    target_cap: int = 60,
    account_prefix: str = "CoaBotHost",
    over_cap_spells: Iterable[int] = (),
) -> MigrationPlan:
    protected = find_protected_guids(conn, account_prefix)
    cur = conn.cursor()
    cur.execute(
        "SELECT c.guid, c.level "
        "FROM characters c "
        "JOIN account a ON a.id = c.account "
        "WHERE a.id > 2 AND a.username LIKE ? AND c.level > ?",
        (f"{account_prefix}%", target_cap),
    )
    candidates: dict[int, int] = {}
    for guid, level in cur.fetchall():
        g = int(guid)
        if g not in protected:
            candidates[g] = int(level)

    over_cap_set = {int(s) for s in over_cap_spells}
    spells_to_remove: list[tuple[int, int]] = []
    if candidates and over_cap_set:
        placeholders = ",".join("?" for _ in candidates)
        cur.execute(
            f"SELECT guid, spell FROM character_spell WHERE guid IN ({placeholders})",
            tuple(sorted(candidates.keys())),
        )
        for guid, spell in cur.fetchall():
            if int(spell) in over_cap_set:
                spells_to_remove.append((int(guid), int(spell)))

    return MigrationPlan(
        target_cap=target_cap,
        protected_guids=tuple(sorted(protected)),
        candidate_levels=candidates,
        spells_to_remove=sorted(spells_to_remove),
    )


def take_snapshot(conn: Any, guids: Iterable[int]) -> dict[str, Any]:
    guid_list = sorted({int(g) for g in guids})
    if not guid_list:
        return {"characters": [], "character_spell": []}
    placeholders = ",".join("?" for _ in guid_list)
    cur = conn.cursor()
    cur.execute(
        f"SELECT guid, level, xp FROM characters WHERE guid IN ({placeholders}) ORDER BY guid",
        tuple(guid_list),
    )
    chars = _fetchall_dicts(cur)
    cur.execute(
        f"SELECT guid, spell, active, disabled FROM character_spell "
        f"WHERE guid IN ({placeholders}) ORDER BY guid, spell",
        tuple(guid_list),
    )
    spells = _fetchall_dicts(cur)
    return {"characters": chars, "character_spell": spells}


def apply_migration(
    conn: Any,
    target_cap: int = 60,
    account_prefix: str = "CoaBotHost",
    over_cap_spells: Iterable[int] = (),
    backup_path: Path | None = None,
    dry_run: bool = True,
) -> dict[str, Any]:
    plan = build_migration_plan(conn, target_cap, account_prefix, over_cap_spells)
    all_tracked_guids = set(plan.candidate_levels.keys()) | set(plan.protected_guids)
    pre_snapshot = take_snapshot(conn, all_tracked_guids)

    report: dict[str, Any] = {
        "dry_run": dry_run,
        "target_cap": target_cap,
        "protected_count": len(plan.protected_guids),
        "candidates_count": len(plan.candidate_levels),
        "spells_to_remove_count": len(plan.spells_to_remove),
        "post_diff_ok": True,
    }

    if dry_run:
        return report

    if backup_path is None:
        raise ValueError("backup_path is required when dry_run=False")

    backup_payload = {
        "target_cap": target_cap,
        "protected_guids": list(plan.protected_guids),
        "candidate_guids": sorted(plan.candidate_levels.keys()),
        "pre_snapshot": pre_snapshot,
    }
    backup_path.parent.mkdir(parents=True, exist_ok=True)
    backup_path.write_text(json.dumps(backup_payload, indent=2), encoding="utf-8")

    cur = conn.cursor()
    for guid in sorted(plan.candidate_levels.keys()):
        cur.execute(
            "UPDATE characters SET level = ?, xp = 0 WHERE guid = ? AND level > ?",
            (target_cap, guid, target_cap),
        )
    for guid, spell in plan.spells_to_remove:
        cur.execute(
            "DELETE FROM character_spell WHERE guid = ? AND spell = ?",
            (guid, spell),
        )
    conn.commit()

    post_snapshot = take_snapshot(conn, all_tracked_guids)
    protected_set = set(plan.protected_guids)
    pre_by_guid = {r["guid"]: r for r in pre_snapshot["characters"]}
    post_by_guid = {r["guid"]: r for r in post_snapshot["characters"]}
    for g in protected_set:
        if pre_by_guid.get(g) != post_by_guid.get(g):
            raise RuntimeError(f"Protected bot {g} was modified during migration!")

    for g in plan.candidate_levels:
        if post_by_guid[g]["level"] != target_cap:
            raise RuntimeError(f"Candidate bot {g} level != {target_cap} after migration!")

    report["post_diff_ok"] = True
    return report


def rollback_migration(conn: Any, backup_path: Path) -> int:
    payload = json.loads(backup_path.read_text(encoding="utf-8"))
    pre = payload["pre_snapshot"]
    cur = conn.cursor()
    restored = 0
    for row in pre["characters"]:
        cur.execute(
            "UPDATE characters SET level = ?, xp = ? WHERE guid = ?",
            (int(row["level"]), int(row["xp"]), int(row["guid"])),
        )
        restored += 1
    for row in pre["character_spell"]:
        cur.execute(
            "INSERT OR IGNORE INTO character_spell (guid, spell, active, disabled) VALUES (?, ?, ?, ?)",
            (int(row["guid"]), int(row["spell"]), int(row["active"]), int(row["disabled"])),
        )
    conn.commit()
    return restored


def main() -> None:
    parser = argparse.ArgumentParser(description="PB-004 safe 80->60 bot migration")
    parser.add_argument("--dry-run", action="store_true", default=True)
    parser.add_argument("--target-cap", type=int, default=60)
    args = parser.parse_args()
    print(f"migrate_bots_80_to_60 ready (dry_run={args.dry_run}, target_cap={args.target_cap})")


if __name__ == "__main__":
    main()
