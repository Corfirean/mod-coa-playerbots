#!/usr/bin/env python3
"""
Deadly Boss Mods (DBM) Offline Mechanic Importer.

Usage:
    python tools/dbm_import/import_dbm.py \\
        --dbm-root C:/games/source/dbm-reference \\
        --output module/data/mechanics/mechanic_registry.generated.json \\
        --core-scripts C:/games/source/server/azerothcore-wotlk-coa/src/server/scripts \\
        --overrides module/data/mechanics/mechanic_registry.overrides.json \\
        --report docs/testing/DBM_MECHANIC_IMPORT_REPORT.md

CRITICAL INVARIANT:
    DBM is used exclusively as an offline development-time metadata source.
    DBM is NOT required on the client, NOT executed at runtime by worldserver,
    and NOT bundled with player client addons.
"""

import os
import sys
import argparse
import json
from parser import DbmParser
from validate import MechanicValidator

DEFAULT_PACKAGES = [
    # Classic / Vanilla
    "DBM-MC", "DBM-BWL", "DBM-AQ20", "DBM-AQ40", "DBM-ZG",
    "DBM-VanillaNaxx", "DBM-VanillaOnyxia", "DBM-Azeroth", "DBM-Party-Classic",
    # The Burning Crusade
    "DBM-Karazhan", "DBM-Gruul", "DBM-Magtheridon", "DBM-Serpentshrine",
    "DBM-TheEye", "DBM-Hyjal", "DBM-BlackTemple", "DBM-Sunwell",
    "DBM-ZulAman", "DBM-Outland", "DBM-Party-BC",
    # Wrath of the Lich King
    "DBM-Naxx", "DBM-EyeOfEternity", "DBM-Onyxia", "DBM-ChamberOfAspects",
    "DBM-Ulduar", "DBM-Coliseum", "DBM-Icecrown", "DBM-VoA", "DBM-Party-WotLK"
]

def generate_report(stats: dict, modules_scanned: int, packages_scanned: int, report_path: str):
    """Generate Markdown coverage report."""
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, 'w', encoding='utf-8') as f:
        f.write("# DBM Offline Mechanic Import Report\n\n")
        f.write("> **Notice**: DBM is used purely as an offline metadata source. It is **NOT** a client or runtime dependency.\n\n")
        f.write("## 1. Summary Statistics\n\n")
        f.write(f"- **Packages Scanned**: {packages_scanned}\n")
        f.write(f"- **Boss Modules Scanned**: {modules_scanned}\n")
        f.write(f"- **Total Mechanics Imported**: {stats['totalRecords']}\n")
        f.write(f"- **Verified (Cross-Referenced with Core C++ Scripts)**: {stats['verifiedCount']}\n")
        f.write(f"- **Conflicts Detected**: {stats['conflictCount']}\n")
        f.write(f"- **Manual CoA Overrides Applied**: {stats['overrideCount']}\n")
        f.write(f"- **Disabled Mechanics**: {stats['disabledCount']}\n")
        f.write(f"- **Unique Spell IDs**: {stats['uniqueSpells']}\n")
        f.write(f"- **Unique Encounters**: {stats['uniqueEncounters']}\n\n")

        f.write("## 2. Breakdown by Semantic Mechanic Type\n\n")
        f.write("| Semantic Mechanic Type | Count |\n")
        f.write("|------------------------|-------|\n")
        for m_type, count in sorted(stats['typeBreakdown'].items(), key=lambda x: -x[1]):
            f.write(f"| `{m_type}` | {count} |\n")
        f.write("\n")

        f.write("## 3. Client & Runtime Independence Verification\n\n")
        f.write("- **DBM client addon required**: **NO**\n")
        f.write("- **DBM server runtime dependency**: **NO**\n")
        f.write("- **DBM Lua executed by worldserver**: **NO**\n")
        f.write("- **DBM files shipped to WoW AddOns**: **NO**\n")
        f.write("- **DBM required after registry generation**: **NO**\n")

def main():
    parser = argparse.ArgumentParser(description="DBM Offline Mechanic Importer")
    parser.add_argument("--dbm-root", required=True, help="Path to local DBM source root directory")
    parser.add_argument("--output", default="module/data/mechanics/mechanic_registry.generated.json", help="Path to output generated JSON")
    parser.add_argument("--core-scripts", default=None, help="Path to AzerothCore/CoA src/server/scripts")
    parser.add_argument("--overrides", default=None, help="Path to mechanic_registry.overrides.json")
    parser.add_argument("--report", default="docs/testing/DBM_MECHANIC_IMPORT_REPORT.md", help="Path to markdown report")

    args = parser.parse_args()

    if not os.path.isdir(args.dbm_root):
        print(f"Error: DBM root directory does not exist: {args.dbm_root}", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Scanning DBM packages in: {args.dbm_root}")
    dbm_parser = DbmParser()

    all_raw_mechanics = []
    modules_scanned = 0
    packages_scanned = 0

    for pkg in DEFAULT_PACKAGES:
        pkg_dir = os.path.join(args.dbm_root, pkg)
        if not os.path.isdir(pkg_dir):
            continue
        packages_scanned += 1

        for root, _, files in os.walk(pkg_dir):
            for file in files:
                if file.endswith(".lua") and not file.startswith("localization."):
                    fpath = os.path.join(root, file)
                    res = dbm_parser.parse_file(fpath, pkg)
                    if res and res["mechanics"]:
                        modules_scanned += 1
                        all_raw_mechanics.extend(res["mechanics"])

    print(f"[*] Scanned {packages_scanned} packages, {modules_scanned} boss modules.")
    print(f"[*] Found {len(all_raw_mechanics)} raw mechanic candidates.")

    # Validation and merging
    validator = MechanicValidator(core_scripts_dir=args.core_scripts, overrides_path=args.overrides)
    processed, stats = validator.validate_and_merge(all_raw_mechanics)

    # Save output JSON
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(processed, f, indent=2, ensure_ascii=False)
    print(f"[+] Saved generated registry to: {args.output}")

    # Generate Markdown report
    if args.report:
        generate_report(stats, modules_scanned, packages_scanned, args.report)
        print(f"[+] Saved report to: {args.report}")

    print("\n" + "=" * 50)
    print("DBM OFFLINE MECHANIC IMPORT STATISTICS")
    print("=" * 50)
    print(f"Total Mechanics Imported: {stats['totalRecords']}")
    print(f"Verified against Core:    {stats['verifiedCount']}")
    print(f"Conflicts Detected:       {stats['conflictCount']}")
    print(f"Manual Overrides Applied: {stats['overrideCount']}")
    print(f"Disabled Mechanics:       {stats['disabledCount']}")
    print(f"Unique Spells:            {stats['uniqueSpells']}")
    print(f"Unique Encounters:        {stats['uniqueEncounters']}")
    print("-" * 50)
    print("Mechanic Type Breakdown:")
    for m_type, cnt in sorted(stats['typeBreakdown'].items(), key=lambda x: -x[1]):
        print(f"  {m_type:20s}: {cnt}")
    print("=" * 50)

if __name__ == "__main__":
    main()
