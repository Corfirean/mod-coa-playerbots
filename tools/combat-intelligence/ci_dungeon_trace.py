"""Sample one specialization's bot every two seconds while a group plays a dungeon, to see what it does between the fights.

python -I ci_dungeon_trace.py <config.json> <spec-id> <dungeon-key> <seconds> <out.jsonl> [lane-index]

Uses the same group as the campaign (the spec in its role slot, the fixed team around it) and the same preparation (spec, live gear, a four-minute warm-up that is not
sampled when `warmup` is true in the config). Every sample is one line: seconds since the start, the bot's health/power, whether it is in combat, moving, casting,
its target and distance to the leader, the AI state string, and what the group is fighting. Nothing is decided here; it only records.
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_dungeon_run as d  # noqa: E402
from build_spec_matrix import spec_table  # noqa: E402


def main(argv):
    cfg = json.load(open(argv[0], encoding='utf-8'))
    spec_id, key, seconds, out = int(argv[1]), argv[2], int(argv[3]), Path(argv[4])
    lane_index = int(argv[5]) if len(argv) > 5 else 0
    dungeon = d.load_dungeons(cfg['dungeons'], [key])[0]
    actors = {int(k): v for k, v in cfg['actors'].items()}
    cls, spec, role, name = next(s for s in spec_table() if s[1] == spec_id)
    team = cfg['lanes'][lane_index]
    lane = d.Lane(cfg, lane_index, team)
    guid = actors[cls]
    if role == 'Tank':
        members = [guid, team['healer'], *team['dps'], team['support']]
    elif role == 'Healer':
        members = [team['tank'], guid, *team['dps'], team['support']]
    else:
        members = [team['tank'], team['healer'], guid, team['dps'][0], team['dps'][1]]
    prep = [f'botcmd learnspec {guid} {spec}', f'botcmd train livegear {guid} new']
    if cfg.get('warmup', True):
        lane.run({'label': 'trace-warmup', 'members': members, 'dungeon': dungeon, 'prep': prep, 'meta': {'warmup': True}, 'max_seconds': 240})
        prep = []
    ra = lane.ra
    leader = members[0]
    for g in members:
        ra.run(f'botcmd spawnbot {g}')
    for g in members:
        ra.run(f'botcmd train ungroup {g}')
    ra.run(f'botcmd train dgroup {leader} ' + ' '.join(str(g) for g in members[1:]))
    ra.run(f'botcmd train ddiff {leader} 0')
    for cmd in prep:
        ra.run(cmd)
    ra.run(f'botcmd train dheal {leader}')
    x, y, z, o = dungeon['pos']
    ra.run(f'botcmd train dgo {leader} {dungeon["map"]} {x} {y} {z} {o} all')
    time.sleep(10)
    ra.run(f'botcmd train dheal {leader}')
    ra.run(f'botcmd train dauto {leader} on')
    index = members.index(guid)
    began = time.time()
    with open(out, 'w', encoding='utf-8') as f:
        while time.time() - began < seconds:
            time.sleep(2)
            s = d.dstat(ra, leader)
            if not s:
                continue
            me = s['members'][index]
            row = {'t': round(time.time() - began, 1), 'name': me['name'], 'hp': me['hp'], 'pw': me['pw'], 'alive': me['alive'], 'cmb': me['cmb'], 'moving': me['moving'],
                   'cast': me.get('cast'), 'tgt': me.get('tgt'), 'dist': round(me['d'], 1), 'td': round(me['td'], 1) if me.get('td') is not None else None, 'dbg': me.get('dbg'), 'dmg': me.get('dmg'), 'groupCmb': s['inCombat'],
                   'mobs': [(m['name'][:14], m['hp'], m.get('on')) for m in s.get('mobs', [])][:4], 'kills': me.get('kills'), 'spells': me.get('spells')}
            f.write(json.dumps(row) + '\n')
    ra.run(f'botcmd train dauto {leader} off')
    ra.run(f'botcmd train dgo {leader} 0 -9449.0 64.0 56.0 0.0 all')
    time.sleep(3)
    ra.run(f'botcmd train dfree {leader}')


if __name__ == '__main__':
    main(sys.argv[1:])
