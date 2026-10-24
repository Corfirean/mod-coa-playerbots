"""Pull real dungeon packs with a bot group and record how the fight goes (descriptive, not a paired test).

python -I ci_pack_probe.py probe.json

probe.json
{
  "credentials": "C:/.../ra-account.json", "ra_port": 3469, "out": "C:/.../pack-probe-label.json",
  "leader": 106039,                                   # inert bot that stands like a player who has not touched the keyboard
  "members": [106035, 106036, 106037, 106038],        # the group (tank, healer, damage dealers)
  "map": 289,
  "packs": [{"name": "caster pack", "stage": [216, 171, 110, 0], "pull_range": 45}],
  "poll_seconds": 2, "timeout_seconds": 120
}

For each pack the group is put at `stage`, revived and at full health, the nearest enemy is pulled onto the leader (`train dpull`) and the group's
state is polled with `train dstat` until combat ends or the timeout is reached. The record per pack: seconds in combat, deaths, lowest health of every
member, lowest mana, enemies seen, whether it ended by victory, wipe or timeout. Only the group's own behaviour is under test: the probe never
heals, buffs or resurrects anybody during the fight.
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_ra import Ra  # noqa: E402


def dstat(ra, leader):
    text = ra.run(f'botcmd train dstat {leader}')
    m = re.search(r'DSTAT (\{.*\})', text)
    return json.loads(m.group(1)) if m else None


def main(path):
    cfg = json.load(open(path, encoding='utf-8'))
    ra = Ra(cfg['credentials'], port=cfg.get('ra_port', 3469))
    leader, members = cfg['leader'], cfg['members']
    for g in [leader] + members:
        ra.run(f'botcmd spawnbot {g}')
    time.sleep(15)
    ra.run(f'botcmd suspend {leader}')
    ra.run('botcmd train dgroup %d %s' % (leader, ' '.join(str(g) for g in members)))
    results = []
    for pack in cfg['packs']:
        x, y, z, o = pack['stage']
        ra.run(f"botcmd train dgo {leader} {cfg['map']} {x} {y} {z} {o} all")
        time.sleep(8)
        pre = dstat(ra, leader)
        reply = ra.run(f"botcmd train dpull {leader} {pack.get('pull_range', 45)}")
        record = {'pack': pack['name'], 'pull': reply, 'start': time.time(), 'members': {}, 'mobs': set(), 'ended': 'timeout'}
        began = time.time()
        in_combat_seen = False
        while time.time() - began < cfg.get('timeout_seconds', 120):
            time.sleep(cfg.get('poll_seconds', 2))
            s = dstat(ra, leader)
            if not s:
                continue
            for m in s['members']:
                r = record['members'].setdefault(m['name'], {'minHp': 100, 'minPower': 100, 'died': False, 'role': m['role']})
                r['minHp'] = min(r['minHp'], m['hp'])
                r['minPower'] = min(r['minPower'], m['pw'])
                if not m['alive']:
                    r['died'] = True
            for mob in s.get('mobs', []):
                record['mobs'].add(mob['name'])
            in_combat_seen = in_combat_seen or s['inCombat']
            if all(not m['alive'] for m in s['members'] if m['map'] == s['map']):
                record['ended'] = 'wipe'
                break
            if in_combat_seen and not s['inCombat'] and not s.get('mobs'):
                record['ended'] = 'victory'
                break
        record['seconds'] = round(time.time() - began, 1)
        record['mobs'] = sorted(record['mobs'])
        record['deaths'] = sum(1 for r in record['members'].values() if r['died'])
        results.append(record)
        # put everybody back on their feet before the next pack (console `revive` takes the character name, quoted)
        for name in list(record['members']):
            ra.run(f'revive "{name}"')
        time.sleep(3)
    ra.run(f'botcmd train dfree {leader}')
    ra.run(f'botcmd resume {leader}')
    Path(cfg['out']).write_text(json.dumps(results, indent=1, default=list), encoding='utf-8')
    print('wrote', cfg['out'])


if __name__ == '__main__':
    main(sys.argv[1])
