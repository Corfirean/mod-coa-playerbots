"""Imports the dungeon data tables of jrad7/mod-dungeon-clear (AGPL-3.0-or-later) into mod-coa-playerbots.

    python import_dc.py <path to a mod-dungeon-clear checkout> <module/src/dungeon directory>

What it does
  * copies the pure data headers/sources (boss roster, wings, nav penalties, route hints, event builder) with the include paths rewritten;
  * copies every Data/Events/<Dungeon>Events.cpp, keeping the data functions (Register<X>Events / Roster / Wings / Route) and constants as they are
    and replacing the body of every helper that talks to the mod-playerbots engine (PlayerbotAI, AiObjectContext, Dc* utilities) with a stub:
    a Conditional predicate reads "false" (the event never fires until it is implemented in our engine), a hook reads "Blocked";
  * writes the stub list to stubs.txt so the remaining work is visible.

The upstream code stays the source of the numbers (coordinates, GameObject/creature entries, boss orders); the engine that runs them is ours
(dungeon/DungeonEventExecutor.*). Re-running the script regenerates the imported files; nothing hand-written lives in them.
"""
import re
import sys
from pathlib import Path

SRC = Path(sys.argv[1]) / 'src/Ai/Dungeon/DungeonClear'
DST = Path(sys.argv[2])

HEADER_NOTE = ('// Imported from jrad7/mod-dungeon-clear (AGPL-3.0-or-later) by tools/dungeon-import/import_dc.py; engine-bound helpers are stubbed.\n'
               '// Do not edit by hand: re-run the importer. Implementations of the stubs live in dungeon/DungeonConditions.cpp and DungeonHooks.cpp.\n')

FORBIDDEN = re.compile(r'PlayerbotAI|GET_PLAYERBOT_AI|DcMovement|DcStatus|DcLeader|botAI|DcRaidMuster|DcBreath|DcFlight|DcRawPath|DcEngage|DcCombat|'
                       r'DcDoor|DcPull|DcPoolExit|DcUbrs|DcToc|DcKara(?!LibraryPit::(MAP|HOOK))|LongRangePathfinder|DungeonPathFollower|DungeonEventExecutor|'
                       r'Playerbots\.h|AI_VALUE|GetAiObjectContext|DcAnchor|DcSuppression|DcRaidMusterDecision|DcFormGate|DcPlayerbotCompat')

# helpers that still reach into engine internals the compat layer does not model
FORCE_STUB = {('BlackrockSpireEvents.cpp', 'StampedWithin'), ('StratholmeEvents.cpp', 'StrInTrapAlleyPit')}

INCLUDE_MAP = {
    'DcDifficulty.h': 'dungeon/DcDifficulty.h',
    'DcDifficultyGate.h': 'dungeon/DcDifficultyGate.h',
    'DungeonBossInfo.h': 'dungeon/DungeonBossInfo.h',
    'WaypointHint.h': 'dungeon/WaypointHint.h',
    'DungeonEventRegistry.h': 'dungeon/DungeonEventRegistry.h',
    'BossRosterRegistry.h': 'dungeon/BossRosterRegistry.h',
    'DungeonRosterBuilders.h': 'dungeon/DungeonRosterBuilders.h',
    'DungeonWingRegistry.h': 'dungeon/DungeonWingRegistry.h',
    'DungeonClearRouteRegistry.h': 'dungeon/DungeonClearRouteRegistry.h',
    'DcNavPenaltyRegistry.h': 'dungeon/DcNavPenaltyRegistry.h',
    'DungeonEventTables.h': 'dungeon/DungeonEventTables.h',
    'BossSpawnIndex.h': 'dungeon/BossSpawnIndex.h',
    'CreatureSpawnEntry.h': 'dungeon/CreatureSpawnEntry.h',
    'DcBossOrdering.h': 'dungeon/DcBossOrdering.h',
    'RoomAggroRegistry.h': 'dungeon/RoomAggroRegistry.h',
    'DcNeverTargetRegistry.h': 'dungeon/DcNeverTargetRegistry.h',
    'SealedEncounterRegistry.h': 'dungeon/SealedEncounterRegistry.h',
    'DcFactionEntrySwapRegistry.h': 'dungeon/DcFactionEntrySwapRegistry.h',
    'DcEventDoorRegistry.h': 'dungeon/DcEventDoorRegistry.h',
    'BossPullbackRegistry.h': 'dungeon/BossPullbackRegistry.h',
    'FightInPlaceRegistry.h': 'dungeon/FightInPlaceRegistry.h',
    'HealLeashRegistry.h': 'dungeon/HealLeashRegistry.h',
    'RouteSweepRegistry.h': 'dungeon/RouteSweepRegistry.h',
    'DcCombatPurgeRegistry.h': 'dungeon/DcCombatPurgeRegistry.h',
    'DcSocialQuarantineRegistry.h': 'dungeon/DcSocialQuarantineRegistry.h',
}


def rewrite_includes(text: str) -> str:
    def repl(m):
        base = m.group(1).split('/')[-1]
        if base in INCLUDE_MAP:
            return f'#include "{INCLUDE_MAP[base]}"'
        return ''  # any other playerbots include is dropped; DcCompat.h supplies the few types still needed
    text = re.sub(r'#include "(Ai/Dungeon/DungeonClear/[^"]+)"[^\n]*', repl, text)
    text = re.sub(r'#include "(Playerbots\.h|PlayerbotAI\.h|AiObjectContext\.h)"[^\n]*', '', text)
    return text


def copy_plain(rel: str, dst_name: str):
    text = (SRC / rel).read_text(encoding='utf-8')
    text = rewrite_includes(text)
    # the include of the file's own header (same directory in the original) becomes a dungeon/ path
    text = re.sub(r'#include "([A-Za-z]+\.h)"', lambda m: f'#include "{INCLUDE_MAP.get(m.group(1), m.group(1))}"', text, count=1) if dst_name.endswith('.cpp') else text
    if '#include "dungeon/DcCompat.h"' not in text:
        first_inc = text.find('#include')
        text = text[:first_inc] + '#include "dungeon/DcCompat.h"\n' + text[first_inc:]
    (DST / dst_name).write_text(text, encoding='utf-8', newline='\n')


# ---------------------------------------------------------------------------------------------------------------------------------------------
# A small C++ scanner: finds top-level function definitions (also inside namespaces) so their bodies can be replaced.
# ---------------------------------------------------------------------------------------------------------------------------------------------

def skip_literal(s: str, i: int) -> int:
    """i is at a quote or the start of a comment; returns the index after it."""
    if s.startswith('//', i):
        j = s.find('\n', i)
        return len(s) if j < 0 else j
    if s.startswith('/*', i):
        j = s.find('*/', i + 2)
        return len(s) if j < 0 else j + 2
    q = s[i]
    if q == 'R' and s.startswith('R"', i):
        m = re.match(r'R"([^(]*)\(', s[i:])
        if m:
            end = s.find(')' + m.group(1) + '"', i)
            return end + len(m.group(1)) + 2
    j = i + 1
    while j < len(s):
        if s[j] == '\\':
            j += 2
            continue
        if s[j] == q:
            return j + 1
        j += 1
    return len(s)


def match_close(s: str, i: int, open_c: str, close_c: str) -> int:
    depth = 0
    while i < len(s):
        c = s[i]
        if s.startswith('//', i) or s.startswith('/*', i) or c in '"\'':
            if c == "'" and i > 0 and s[i - 1].isalnum():  # digit separator 1'000
                i += 1
                continue
            i = skip_literal(s, i)
            continue
        if c == open_c:
            depth += 1
        elif c == close_c:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return len(s) - 1


def process_scope(s: str, stubs: list, file_name: str, hooks: list = None) -> str:
    hooks = hooks if hooks is not None else []
    """Walks one scope (file level or the inside of a namespace) and returns the rewritten text."""
    out = []
    i = 0
    n = len(s)
    seg_start = 0  # start of the current declaration (after the last ';' or '}' at this level)
    while i < n:
        c = s[i]
        if s.startswith('//', i) or s.startswith('/*', i) or c in '"\'':
            if c == "'" and i > 0 and s[i - 1].isalnum():
                i += 1
                continue
            i = skip_literal(s, i)
            continue
        if c == '#':  # preprocessor line: its own segment
            j = s.find('\n', i)
            j = n if j < 0 else j
            while j > 0 and s[j - 1] == '\\':
                j = s.find('\n', j + 1)
                j = n if j < 0 else j
            i = j
            continue
        if c == ';':
            i += 1
            out.append(s[seg_start:i])
            seg_start = i
            continue
        if c == '{':
            head = s[seg_start:i]
            head_code = re.sub(r'//[^\n]*|/\*.*?\*/', '', head, flags=re.S).strip()
            end = match_close(s, i, '{', '}')
            body = s[i:end + 1]
            ns = re.search(r'(^|\s)namespace(\s+\w+)?\s*$', head_code)
            is_type = re.search(r'(^|\s)(struct|class|enum(\s+class)?|union)\b', head_code) and '(' not in head_code
            is_func = '(' in head_code and ')' in head_code and not is_type and not ns and not re.search(r'=\s*(\[|\{)?\s*$', head_code) \
                and not re.match(r'(if|for|while|switch|else|do|try|catch)\b', head_code)
            if ns:
                inner = process_scope(s[i + 1:end], stubs, file_name, hooks)
                out.append(s[seg_start:i + 1] + inner + '}')
            elif is_func:
                name_m = re.search(r'([A-Za-z_]\w*)\s*\([^()]*(\([^()]*\)[^()]*)*\)\s*(const)?\s*(noexcept)?\s*$', head_code, re.S)
                name = name_m.group(1) if name_m else '?'
                keep = bool(re.match(r'Register\w*(Events|Roster|Wings|Route|Routes)$', name)) or not FORBIDDEN.search(head + body)
                if re.match(r'Register\w*Hooks$', name):
                    hooks.append(name)
                    keep = True
                if (file_name, name) in FORCE_STUB:
                    keep = False
                if not keep and '&' in head_code.split(name)[0]:
                    i = end + 1  # a function returning a reference to an engine type cannot be stubbed: drop it
                    seg_start = i
                    stubs.append(f'{file_name}: {name}  (dropped)')
                    continue
                if keep:
                    out.append(s[seg_start:end + 1])
                else:
                    ret = head_code.split(name)[0].replace('static', '').replace('inline', '').strip()
                    if re.search(r'\bbool\b', ret):
                        stub = '{ return false; }'
                    elif 'ObjectiveArriveResult' in ret:
                        stub = '{ return ObjectiveArriveResult::Blocked; }'
                    elif re.search(r'\bvoid\b', ret):
                        stub = '{ }'
                    elif ret.endswith('*'):
                        stub = '{ return nullptr; }'
                    elif re.search(r'\b(u?int\d+|float|double|size_t)\b', ret):
                        stub = '{ return 0; }'
                    else:
                        stub = '{ return {}; }'
                    stubs.append(f'{file_name}: {name}  ({ret or "?"})')
                    out.append(s[seg_start:i] + stub)
            else:
                out.append(s[seg_start:end + 1])
            i = end + 1
            seg_start = i
            continue
        i += 1
    out.append(s[seg_start:])
    return ''.join(out)


def convert_events(path: Path, stubs: list, hooks: list = None) -> str:
    text = path.read_text(encoding='utf-8')
    text = rewrite_includes(text)
    # keep the leading comment block and includes, then process everything after the last include
    last_inc = max((m.end() for m in re.finditer(r'#include [^\n]*\n', text)), default=0)
    head, rest = text[:last_inc], text[last_inc:]
    rest = process_scope(rest, stubs, path.name, hooks)
    return head.replace('#include', '#include', 1) + rest


def main():
    DST.mkdir(parents=True, exist_ok=True)
    (DST / 'events').mkdir(exist_ok=True)

    plain = [
        ('Util/DcDifficulty.h', 'DcDifficulty.h'),
        ('Util/DcDifficulty.cpp', 'DcDifficulty.cpp'),
        ('Data/DcDifficultyGate.h', 'DcDifficultyGate.h'),
        ('Data/DungeonBossInfo.h', 'DungeonBossInfo.h'),
        ('Data/WaypointHint.h', 'WaypointHint.h'),
        ('Data/DungeonEventRegistry.h', 'DungeonEventRegistry.h'),
        ('Data/DungeonEventRegistry.cpp', 'DungeonEventRegistry.cpp'),
        ('Overrides/BossRosterRegistry.h', 'BossRosterRegistry.h'),
        ('Overrides/BossRosterRegistry.cpp', 'BossRosterRegistry.cpp'),
        ('Data/Events/DungeonRosterBuilders.h', 'DungeonRosterBuilders.h'),
        ('Data/DungeonWingRegistry.h', 'DungeonWingRegistry.h'),
        ('Data/DungeonWingRegistry.cpp', 'DungeonWingRegistry.cpp'),
        ('Data/DungeonClearRouteRegistry.h', 'DungeonClearRouteRegistry.h'),
        ('Data/DungeonClearRouteRegistry.cpp', 'DungeonClearRouteRegistry.cpp'),
        ('Data/DcNavPenaltyRegistry.h', 'DcNavPenaltyRegistry.h'),
        ('Data/DcNavPenaltyRegistry.cpp', 'DcNavPenaltyRegistry.cpp'),
        ('Data/BossSpawnIndex.h', 'BossSpawnIndex.h'),
        ('Data/BossSpawnIndex.cpp', 'BossSpawnIndex.cpp'),
        ('Data/CreatureSpawnEntry.h', 'CreatureSpawnEntry.h'),
        ('Data/RoomAggroRegistry.h', 'RoomAggroRegistry.h'),
        ('Data/RoomAggroRegistry.cpp', 'RoomAggroRegistry.cpp'),
        ('Data/DcNeverTargetRegistry.h', 'DcNeverTargetRegistry.h'),
        ('Data/DcNeverTargetRegistry.cpp', 'DcNeverTargetRegistry.cpp'),
        ('Data/SealedEncounterRegistry.h', 'SealedEncounterRegistry.h'),
        ('Data/SealedEncounterRegistry.cpp', 'SealedEncounterRegistry.cpp'),
        ('Data/BossPullbackRegistry.h', 'BossPullbackRegistry.h'),
        ('Data/BossPullbackRegistry.cpp', 'BossPullbackRegistry.cpp'),
        ('Data/FightInPlaceRegistry.h', 'FightInPlaceRegistry.h'),
        ('Data/FightInPlaceRegistry.cpp', 'FightInPlaceRegistry.cpp'),
        ('Data/HealLeashRegistry.h', 'HealLeashRegistry.h'),
        ('Data/HealLeashRegistry.cpp', 'HealLeashRegistry.cpp'),
        ('Data/RouteSweepRegistry.h', 'RouteSweepRegistry.h'),
        ('Data/RouteSweepRegistry.cpp', 'RouteSweepRegistry.cpp'),
        ('Data/DcCombatPurgeRegistry.h', 'DcCombatPurgeRegistry.h'),
        ('Data/DcCombatPurgeRegistry.cpp', 'DcCombatPurgeRegistry.cpp'),
        ('Data/DcSocialQuarantineRegistry.h', 'DcSocialQuarantineRegistry.h'),
        ('Data/DcSocialQuarantineRegistry.cpp', 'DcSocialQuarantineRegistry.cpp'),
        ('Data/DcFactionEntrySwapRegistry.h', 'DcFactionEntrySwapRegistry.h'),
        ('Data/DcEventDoorRegistry.h', 'DcEventDoorRegistry.h'),
        ('Util/DcBossOrdering.h', 'DcBossOrdering.h'),
    ]
    for rel, name in plain:
        copy_plain(rel, name)

    stubs: list = []
    hooks: list = []
    for f in sorted((SRC / 'Data/Events').glob('*Events.cpp')):
        text = convert_events(f, stubs, hooks)
        if '#include "dungeon/DcCompat.h"' not in text:
            k = text.find('#include')
            text = text[:k] + '#include "dungeon/DcCompat.h"\n' + text[k:]
        (DST / 'events' / f.name).write_text(HEADER_NOTE + text, encoding='utf-8', newline='\n')
    (DST / 'HookRegistrations.inc').write_text(''.join('DC_HOOK_APPENDER(' + h + ')' + chr(10) for h in sorted(set(hooks))), encoding='utf-8', newline=chr(10))
    tables = convert_events(SRC / 'Data/Events/DungeonEventTables.h', stubs)
    (DST / 'DungeonEventTables.h').write_text(HEADER_NOTE + tables, encoding='utf-8', newline='\n')
    (DST / 'stubs.txt').write_text('\n'.join(stubs) + '\n', encoding='utf-8')
    print(f'{len(stubs)} engine-bound helpers stubbed (see stubs.txt)')


main()
