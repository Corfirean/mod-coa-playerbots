#!/usr/bin/env python3
"""
Validation, Cross-Referencing, Conflict Detection, and Override Merging.

Elevates confidence of imported mechanics to 'Verified' if found in Core C++ scripts.
Applies manual CoA overrides (setting source='Manual', confidence='Authoritative').
Detects contradictory mechanics (e.g. Spread vs Stack) marking them as 'Conflict'.
"""

import os
import re
import json
from typing import Dict, Any, List, Set, Tuple

class MechanicValidator:
    def __init__(self, core_scripts_dir: str = None, overrides_path: str = None):
        self.core_scripts_dir = core_scripts_dir
        self.overrides_path = overrides_path
        self.core_spell_ids: Set[int] = set()
        self.core_creature_ids: Set[int] = set()
        self.overrides: Dict[str, Any] = {}
        
        if self.core_scripts_dir and os.path.exists(self.core_scripts_dir):
            self._index_core_scripts()
        if self.overrides_path and os.path.exists(self.overrides_path):
            self._load_overrides()

    def _index_core_scripts(self):
        """Index spell and creature IDs referenced in AzerothCore / CoA C++ scripts."""
        spell_re = re.compile(r'\b(?:SPELL_|SpellId|SpellID|spellId|spell|DoCast|CastSpell)\w*\s*[=\(,]\s*(\d{3,6})\b')
        creature_re = re.compile(r'\b(?:NPC_|CREATURE_|Entry|entry)\w*\s*[=\(,]\s*(\d{3,6})\b')

        for root, _, files in os.walk(self.core_scripts_dir):
            for file in files:
                if file.endswith(('.cpp', '.h')):
                    fpath = os.path.join(root, file)
                    try:
                        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
                            text = f.read()
                            for m in spell_re.finditer(text):
                                self.core_spell_ids.add(int(m.group(1)))
                            for m in creature_re.finditer(text):
                                self.core_creature_ids.add(int(m.group(1)))
                    except Exception:
                        pass

    def _load_overrides(self):
        """Load manual CoA overrides."""
        try:
            with open(self.overrides_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
                if isinstance(data, list):
                    for rec in data:
                        if "key" in rec:
                            self.overrides[rec["key"]] = rec
                elif isinstance(data, dict):
                    if "overrides" in data:
                        for rec in data["overrides"]:
                            if "key" in rec:
                                self.overrides[rec["key"]] = rec
                    else:
                        self.overrides = data
        except Exception as e:
            print(f"Warning: Failed to load overrides from {self.overrides_path}: {e}")

    def validate_and_merge(self, raw_mechanics: List[Dict[str, Any]]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
        """Validate, detect conflicts, cross-reference against core, and apply overrides."""
        verified_count = 0
        conflict_count = 0
        override_count = 0
        disabled_count = 0

        # Group by (encounterId, spellId) to detect conflicts
        grouped: Dict[Tuple[int, int], List[Dict[str, Any]]] = {}
        for m in raw_mechanics:
            k = (m.get("encounterId", 0), m["spellId"])
            grouped.setdefault(k, []).append(m)

        conflicting_keys: Set[str] = set()
        for (enc_id, spell_id), recs in grouped.items():
            types = {r["mechanicType"] for r in recs}
            # Contradictory mechanics
            if ("SpreadOut" in types and "StackTogether" in types) or ("RunAway" in types and "StackTogether" in types):
                for r in recs:
                    conflicting_keys.add(r["key"])

        processed: List[Dict[str, Any]] = []

        for m in raw_mechanics:
            rec = dict(m)
            key = rec["key"]
            spell_id = rec["spellId"]
            boss_entry = rec.get("bossEntry", 0)

            # 1. Conflict Check
            if key in conflicting_keys:
                rec["confidence"] = "Conflict"
                rec["notes"] += " [CONFLICT: Contradictory mechanic requirements detected]"
                conflict_count += 1

            # 2. Core Cross-Reference
            elif self.core_spell_ids and spell_id in self.core_spell_ids:
                rec["confidence"] = "Verified"
                rec["notes"] += " [Verified against core server scripts]"
                verified_count += 1
            elif self.core_creature_ids and boss_entry in self.core_creature_ids:
                rec["confidence"] = "Verified"
                rec["notes"] += " [Verified creature in core server scripts]"
                verified_count += 1

            # 3. Apply Manual Override (Highest Precedence)
            if key in self.overrides or str(spell_id) in self.overrides:
                ov = self.overrides.get(key) or self.overrides.get(str(spell_id))
                for field in ["mechanicType", "priority", "roleMask", "dialogueBeat", "disabled", "replacementMechanicId", "notes", "phase"]:
                    if field in ov:
                        rec[field] = ov[field]
                rec["source"] = "Manual"
                rec["confidence"] = "Authoritative"
                rec["notes"] += " [Applied Manual CoA Override]"
                override_count += 1

            if rec.get("disabled", False):
                disabled_count += 1

            processed.append(rec)

        stats = {
            "totalRecords": len(processed),
            "verifiedCount": verified_count,
            "conflictCount": conflict_count,
            "overrideCount": override_count,
            "disabledCount": disabled_count,
            "uniqueSpells": len({r["spellId"] for r in processed if "spellId" in r}),
            "uniqueEncounters": len({r.get("encounterId", 0) for r in processed if r.get("encounterId", 0) > 0}),
        }

        # Breakdown by mechanic type
        type_breakdown = {}
        for r in processed:
            t = r["mechanicType"]
            type_breakdown[t] = type_breakdown.get(t, 0) + 1
        stats["typeBreakdown"] = type_breakdown

        return processed, stats
