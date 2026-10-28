#!/usr/bin/env python3
"""
Tolerant DBM Lua Parser.

Extracts boss metadata, spell mechanics, timers, and role masks from DBM boss modules.
Ignores UI elements, frames, sounds, options, and client chat commands.
"""

import re
import os
from typing import Dict, Any, List, Optional, Tuple, Set
from mappings import CONSTRUCT_MAPPINGS, resolve_role_mask

class DbmParser:
    def __init__(self):
        self.warnings_logged: List[str] = []

    def strip_comments_preserve_lines(self, text: str) -> str:
        """Remove single and multi-line comments while preserving line count."""
        def replace_multiline(match):
            return '\n' * match.group(0).count('\n')
        text = re.sub(r'--\[\[.*?\]\]', replace_multiline, text, flags=re.DOTALL)
        lines = []
        for line in text.splitlines():
            line_no_comment = re.split(r'--', line)[0]
            lines.append(line_no_comment)
        return '\n'.join(lines)

    def parse_file(self, file_path: str, package_name: str) -> Optional[Dict[str, Any]]:
        """Parse a single DBM boss Lua file."""
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception as e:
            self.warnings_logged.append(f"Failed to read {file_path}: {e}")
            return None

        clean_text = self.strip_comments_preserve_lines(content)
        lines = clean_text.splitlines()

        # 1. Detect module declaration: DBM:NewMod("Name", "Package", ...)
        mod_match = re.search(r'DBM:NewMod\s*\(\s*["\']([^"\']+)["\'](?:\s*,\s*["\']([^"\']+)["\'])?', clean_text)
        if not mod_match:
            return None

        mod_name = mod_match.group(1)
        pkg_name = mod_match.group(2) or package_name

        # 2. Extract creature IDs: mod:SetCreatureID(36612, ...)
        creature_ids = []
        for match in re.finditer(r'mod:SetCreatureID\s*\(\s*([0-9,\s]+)\)', clean_text):
            ids_str = match.group(1)
            for part in ids_str.split(','):
                part = part.strip()
                if part.isdigit():
                    creature_ids.append(int(part))

        # 3. Extract encounter ID: mod:SetEncounterID(845)
        encounter_id = 0
        enc_match = re.search(r'mod:SetEncounterID\s*\(\s*(\d+)\)', clean_text)
        if enc_match:
            encounter_id = int(enc_match.group(1))

        # 4. Extract registered combat events for spells:
        # e.g. "SPELL_CAST_START 69057 70826 ...", "SPELL_AURA_APPLIED 69076"
        spell_events: Dict[int, str] = {}
        for event_match in re.finditer(r'RegisterEvents(?:InCombat)?\s*\(\s*([^)]+)\)', clean_text):
            event_args = event_match.group(1)
            string_literals = re.findall(r'["\']([^"\']+)["\']', event_args)
            for lit in string_literals:
                parts = lit.split()
                if parts:
                    event_type = parts[0]
                    for spell_str in parts[1:]:
                        if spell_str.isdigit():
                            spell_events[int(spell_str)] = event_type

        # 5. Extract timers: mod:NewCDTimer(sec, spellId, ...)
        spell_timers: Dict[int, int] = {}
        timer_pattern = re.compile(
            r'mod:New(?:CD|Next|Cast|BuffActive|Var)Timer\s*\(\s*(?:["\']v?(\d+)[^"\']*["\']|(\d+(?:\.\d+)?))\s*,\s*(\d+)',
            re.MULTILINE
        )
        for match in timer_pattern.finditer(clean_text):
            dur_str = match.group(1) or match.group(2)
            spell_str = match.group(3)
            try:
                duration_sec = float(dur_str)
                spell_id = int(spell_str)
                spell_timers[spell_id] = int(duration_sec * 1000)
            except ValueError:
                pass

        # 6. Extract SpecialWarnings and Announces
        mechanics: List[Dict[str, Any]] = []
        seen_keys: Set[str] = set()

        # Iterate over all lines to preserve line numbers
        for line_idx, line in enumerate(lines, 1):
            line_clean = line.strip()
            if not line_clean:
                continue

            for construct_name, mapping in CONSTRUCT_MAPPINGS.items():
                # Matches: mod:NewSpecialWarningInterrupt(72007, "HasInterrupt", ...)
                pattern = rf'mod:{construct_name}\s*\(\s*([^,\)]+)(?:,\s*([^,\)]+))?'
                match = re.search(pattern, line_clean)
                if not match:
                    continue

                arg1 = match.group(1).strip()
                arg2 = match.group(2).strip() if match.group(2) else None

                # Find spell ID (either arg1 or arg2)
                spell_id = None
                if arg1.isdigit():
                    spell_id = int(arg1)
                elif arg2 and arg2.isdigit():
                    spell_id = int(arg2)

                if not spell_id or spell_id == 0:
                    continue

                # Role mask resolution
                raw_role = arg2 if arg1.isdigit() else None
                roles = resolve_role_mask(raw_role, mapping["roleMask"])

                mechanic_type = mapping["mechanicType"]
                key = f"{mod_name}_{spell_id}_{mechanic_type}"

                if key in seen_keys:
                    continue
                seen_keys.add(key)

                trigger_ev = spell_events.get(spell_id, "SPELL_CAST_START" if mechanic_type == "Interrupt" else "SPELL_AURA_APPLIED")
                timer_ms = spell_timers.get(spell_id, 0)

                rec = {
                    "key": key,
                    "encounterId": encounter_id,
                    "bossEntry": creature_ids[0] if creature_ids else 0,
                    "spellId": spell_id,
                    "mechanicType": mechanic_type,
                    "dialogueBeat": mapping["dialogueBeat"],
                    "roleMask": roles,
                    "priority": mapping["priority"],
                    "source": "DBM",
                    "confidence": "Imported",
                    "phase": 0,
                    "estimatedTimerMs": timer_ms,
                    "triggerEvent": trigger_ev,
                    "disabled": False,
                    "replacementMechanicId": "",
                    "provenance": {
                        "package": pkg_name,
                        "module": os.path.basename(file_path),
                        "line": line_idx,
                        "construct": construct_name
                    },
                    "notes": f"Imported from {construct_name} in {os.path.basename(file_path)}"
                }
                mechanics.append(rec)

        return {
            "module": mod_name,
            "package": pkg_name,
            "file": file_path,
            "encounterId": encounter_id,
            "creatureIds": creature_ids,
            "mechanics": mechanics
        }
