#!/usr/bin/env python3
"""
DBM construct to semantic mechanic mappings.

Translates DBM SpecialWarning and Announce constructs into server-side semantic
mechanics for Playerbots tactical callouts.
"""

from typing import Dict, Any, List, Optional

# Semantic mechanic types supported by the system
SEMANTIC_TYPES = {
    "Interrupt",
    "Dispel",
    "MoveFromGround",
    "SpreadOut",
    "StackTogether",
    "TankInDanger",
    "KillAdds",
    "TargetSwitch",
    "Defensive",
    "RunAway",
    "PhaseChange",
    "IncomingAoE",
    "EnemyCaster",
    "Soak",
    "AvoidFront",
    "AvoidBehind",
    "StopDamage",
    "BurstNow",
    "GroupDamage",
    "TankSwap",
}

# Construct mappings: DBM API -> (mechanicType, priority, roleMask, dialogueBeat)
CONSTRUCT_MAPPINGS: Dict[str, Dict[str, Any]] = {
    "NewSpecialWarningInterrupt": {
        "mechanicType": "Interrupt",
        "priority": "Critical",
        "roleMask": ["Tank", "Dps"],
        "dialogueBeat": "Interrupt",
    },
    "NewSpecialWarningDispel": {
        "mechanicType": "Dispel",
        "priority": "High",
        "roleMask": ["Healer"],
        "dialogueBeat": "Dispel",
    },
    "NewSpecialWarningGTFO": {
        "mechanicType": "MoveFromGround",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "MoveFromGround",
    },
    "NewSpecialWarningMove": {
        "mechanicType": "MoveFromGround",
        "priority": "High",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "MoveFromGround",
    },
    "NewSpecialWarningRun": {
        "mechanicType": "RunAway",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "RunAway",
    },
    "NewSpecialWarningMoveAway": {
        "mechanicType": "SpreadOut",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "SpreadOut",
    },
    "NewSpecialWarningSpread": {
        "mechanicType": "SpreadOut",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "SpreadOut",
    },
    "NewSpecialWarningClose": {
        "mechanicType": "SpreadOut",
        "priority": "High",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "SpreadOut",
    },
    "NewSpecialWarningStack": {
        "mechanicType": "StackTogether",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "StackTogether",
    },
    "NewSpecialWarningTaunt": {
        "mechanicType": "TankSwap",
        "priority": "Critical",
        "roleMask": ["Tank"],
        "dialogueBeat": "TankSwap",
    },
    "NewSpecialWarningDefensive": {
        "mechanicType": "Defensive",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer"],
        "dialogueBeat": "Defensive",
    },
    "NewSpecialWarningSwitch": {
        "mechanicType": "TargetSwitch",
        "priority": "High",
        "roleMask": ["Dps"],
        "dialogueBeat": "TargetSwitch",
    },
    "NewSpecialWarningReflect": {
        "mechanicType": "StopDamage",
        "priority": "Critical",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "StopDamage",
    },
    "NewAddsLeftAnnounce": {
        "mechanicType": "KillAdds",
        "priority": "High",
        "roleMask": ["Dps"],
        "dialogueBeat": "KillAdds",
    },
    "NewPhaseAnnounce": {
        "mechanicType": "PhaseChange",
        "priority": "Normal",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "PhaseChange",
    },
    "NewStageAnnounce": {
        "mechanicType": "PhaseChange",
        "priority": "Normal",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "PhaseChange",
    },
    "NewPrePhaseAnnounce": {
        "mechanicType": "PhaseChange",
        "priority": "Normal",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "PhaseChange",
    },
    "NewSpecialWarningSpell": {
        "mechanicType": "IncomingAoE",
        "priority": "High",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "IncomingAoE",
    },
    "NewSpecialWarningYou": {
        "mechanicType": "RunAway",
        "priority": "High",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "RunAway",
    },
    "NewSpecialWarningTarget": {
        "mechanicType": "TargetSwitch",
        "priority": "Normal",
        "roleMask": ["Dps"],
        "dialogueBeat": "TargetSwitch",
    },
    "NewSpecialWarningMoveTo": {
        "mechanicType": "RunAway",
        "priority": "High",
        "roleMask": ["Tank", "Healer", "Dps"],
        "dialogueBeat": "RunAway",
    },
}

def resolve_role_mask(raw_role_str: Optional[str], default_roles: List[str]) -> List[str]:
    """Parse DBM role hint string into normalized role list."""
    if not raw_role_str:
        return default_roles
    
    clean = raw_role_str.replace('"', '').replace("'", '').strip()
    roles = []
    lower = clean.lower()
    if "tank" in lower:
        roles.append("Tank")
    if "healer" in lower or "magicdispeller" in lower or "cursedispeller" in lower:
        roles.append("Healer")
    if "dps" in lower or "hasinterrupt" in lower or "melee" in lower or "ranged" in lower:
        roles.append("Dps")
        
    return roles if roles else default_roles
