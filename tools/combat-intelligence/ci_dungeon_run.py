"""Bot groups through dungeons in auto-dungeon mode, with a record of how each run went.

python -I ci_dungeon_run.py scout    config.json     one run of the fixed team per dungeon: which dungeons the team can play at all
python -I ci_dungeon_run.py campaign config.json     every specialization in its role slot, several runs, in different dungeons

config.json (paths are the sandbox's; credentials are read, never printed):
{
  "credentials": "C:/.../ra-account.json", "ra_port": 3469, "out": "C:/.../dungeon-results/<label>", "world_log_dir": "C:/.../training-live",
  "dungeons": "C:/.../dungeons.json",
  "lanes": [ {"tank": 106035, "healer": 106036, "dps": [106037, 106038], "support": 106039} , ... ],     # fixed teams; one lane = one group at a time
  "actors": {"29": 106057, ...},                                                                      # class -> dedicated actor under test
  "scout_dungeons": ["uk", "nexus"],  "campaign_dungeons": ["uk", "nexus", "an"],
  "attempts": 2, "max_seconds": 600, "poll_seconds": 5, "stuck_seconds": 300, "max_wipes": 3, "difficulty": 0
}

A run: the group is formed with the tank as leader, brought to full health, taken to the dungeon entrance and set to auto-dungeon (`botcmd train dauto`);
the harness polls `botcmd train dstat` and ends the run when the dungeon is cleared, the group wiped `max_wipes` times, nothing moved for `stuck_seconds`,
or `max_seconds` passed. The record per run: outcome, seconds, bosses down/total, wipes, deaths per member, damage/healing/damage taken per member,
lowest health and power seen, time in combat, the mobs met (level, health), the tank's last positions and the last auto-dungeon log lines. Nothing here
heals, buffs or resurrects anybody during a run, and nothing decides about a rotation: it measures.
"""
import json
import math
import re
import sys
import threading
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_ra import Ra  # noqa: E402
from build_spec_matrix import spec_table  # noqa: E402

OUTSIDE = (0, -9449.0, 64.0, 56.0, 0.0)  # Elwynn Forest: where a finished group is sent so the next run starts from a neutral place
ROLE_NAMES = {0: 'dps', 1: 'tank', 2: 'healer', 3: 'support'}


def dstat(ra, leader):
    text = ra.run(f'botcmd train dstat {leader}')
    m = re.search(r'DSTAT (\{.*\})', text)
    return json.loads(m.group(1)) if m else None


def newest_world_log(directory):
    logs = sorted(Path(directory).glob('world-ci-*.log'), key=lambda p: p.stat().st_mtime)
    return logs[-1] if logs else None


class Lane:
    """One fixed team and one console connection; runs one group at a time."""

    def __init__(self, config, index, team):
        self.config, self.index, self.team = config, index, team
        self.ra = Ra(config['credentials'], port=config.get('ra_port', 3469))
        self.run_max = None

    def reconnect(self):
        self.ra.close()
        self.ra = Ra(self.config['credentials'], port=self.config.get('ra_port', 3469))

    def log_tail(self, offset, names):
        # Several lanes write to one world log: keep the auto-dungeon lines that name a bot of this group.
        log = newest_world_log(self.config['world_log_dir'])
        if not log:
            return []
        with open(log, 'rb') as f:
            f.seek(offset)
            data = f.read().decode('utf-8', errors='replace')
        return [line for line in data.splitlines() if 'AutoDungeon' in line and any(f"'{n}'" in line or n in line for n in names)]

    def log_offset(self):
        log = newest_world_log(self.config['world_log_dir'])
        return log.stat().st_size if log else 0

    def run(self, spec):
        """spec: {'label', 'members': [guid...] (first is the leader), 'dungeon': {...}, 'prep': [console commands], 'meta': {...}}"""
        ra, cfg = self.ra, self.config
        self.run_max = spec.get('max_seconds')
        leader, members = spec['members'][0], spec['members'][1:]
        dungeon = spec['dungeon']
        record = {'label': spec['label'], 'dungeon': dungeon['key'], 'map': dungeon['map'], 'meta': spec.get('meta', {}), 'members': spec['members'],
                  'started': time.strftime('%Y-%m-%dT%H:%M:%S'), 'events': []}
        offset = self.log_offset()
        t_begin = time.time()
        try:
            for g in spec['members']:
                ra.run(f'botcmd spawnbot {g}')
            time.sleep(2)
            for cmd in spec.get('prep', []):
                ra.run(cmd)
            for g in spec['members']:
                ra.run(f'botcmd train ungroup {g}')
            reply = ra.run(f'botcmd train dgroup {leader} ' + ' '.join(str(g) for g in members) + (' locked' if cfg.get('lock_gear', False) else ''))
            if 'dgroup' not in reply:
                raise RuntimeError('dgroup failed: ' + reply[:200])
            ra.run(f'botcmd train ddiff {leader} {cfg.get("difficulty", 0)}')
            ra.run(f'botcmd train dheal {leader}')
            if cfg.get('settle_seconds', 0):
                # A bot that was just re-equipped lost its buffs and its AI state (train livegear resets both): the group stands in a quiet place first,
                # not at the dungeon entrance where packs would attack an idle group.
                ra.run(f'botcmd train dgo {leader} {OUTSIDE[0]} {OUTSIDE[1]} {OUTSIDE[2]} {OUTSIDE[3]} {OUTSIDE[4]} all')
                time.sleep(cfg['settle_seconds'])
                ra.run(f'botcmd train dheal {leader}')
            x, y, z, o = dungeon['pos']
            for attempt in range(3):
                ra.run(f'botcmd train dgo {leader} {dungeon["map"]} {x} {y} {z} {o} all')
                time.sleep(8)
                s = dstat(ra, leader)
                if s and s['map'] == dungeon['map'] and all(m['map'] == dungeon['map'] for m in s['members']):
                    break
                ra.run(f'botcmd train dheal {leader}')
            else:
                raise RuntimeError('group did not arrive in the dungeon')
            time.sleep(2)
            ra.run(f'botcmd train dheal {leader}')
            ra.run(f'botcmd train dauto {leader} on')
            outcome = self.watch(leader, record, dungeon)
        except Exception as exc:  # a failed set-up is a result too
            outcome = 'ERROR'
            record['error'] = ''.join(traceback.format_exception_only(type(exc), exc)).strip()
            try:
                self.reconnect()
                ra = self.ra
            except Exception:
                pass
        record['outcome'] = outcome
        record['seconds'] = round(time.time() - t_begin, 1)
        record['autoDungeonLog'] = self.log_tail(offset, list(record.get('members_result', {}).keys()))[-12:]
        try:
            ra.run(f'botcmd train dauto {leader} off')
            ra.run(f'botcmd train dheal {leader}')
            ra.run(f'botcmd train dgo {leader} {OUTSIDE[0]} {OUTSIDE[1]} {OUTSIDE[2]} {OUTSIDE[3]} {OUTSIDE[4]} all')
            time.sleep(3)
            ra.run(f'botcmd train dfree {leader}')
        except Exception:
            pass
        return record

    def watch(self, leader, record, dungeon):
        ra, cfg = self.ra, self.config
        poll, max_seconds = cfg.get('poll_seconds', 5), self.run_max or cfg.get('max_seconds', 600)
        stuck_seconds, max_wipes = cfg.get('stuck_seconds', 300), cfg.get('max_wipes', 3)
        began = time.time()
        members = {}
        mobs = {}
        wipes = 0
        was_wiped = False
        combat_s = 0.0
        active_s = 0.0        # polls in which the group's damage to creatures grew: a real fight
        stalemate_s = 0.0     # time in combat with no damage dealt for 30 s or longer (a mob nobody reaches or can hurt)
        quiet_since = None
        last_damage = 0
        stalemate_noted = False
        last_move_at, last_pos = began, None
        last_boss, boss_times = 0, []
        trail = []
        final = None
        while True:
            time.sleep(poll)
            now = time.time()
            s = dstat(ra, leader)
            if not s:
                continue
            final = s
            t = now - began
            alive = 0
            for m in s['members']:
                r = members.setdefault(m['name'], {'role': ROLE_NAMES.get(m['role'], str(m['role'])), 'deaths': 0, 'alive': True, 'minHp': 100, 'minPw': 100})
                if r['alive'] and not m['alive']:
                    r['deaths'] += 1
                    record['events'].append({'t': round(t), 'event': 'death', 'who': m['name'], 'role': r['role']})
                r['alive'] = m['alive']
                if m['alive']:
                    alive += 1
                    r['minHp'] = min(r['minHp'], m['hp'])
                    r['minPw'] = min(r['minPw'], m['pw'])
            if alive == 0 and not was_wiped:
                wipes += 1
                was_wiped = True
                record['events'].append({'t': round(t), 'event': 'wipe', 'bossDone': s.get('bossDone')})
            elif alive > 0:
                was_wiped = alive == 0
            if s['inCombat']:
                combat_s += poll
            damage_now = sum(m.get('dmg', 0) for m in s['members'])
            if damage_now > last_damage:
                active_s += poll
                quiet_since = None
                stalemate_noted = False
            elif s['inCombat']:
                quiet_since = quiet_since or now
                if now - quiet_since >= 30:
                    stalemate_s += poll
                    if now - quiet_since >= 120 and not stalemate_noted:
                        stalemate_noted = True
                        record['events'].append({'t': round(t), 'event': 'stalemate', 'mobs': [(m['name'][:20], m['hp'], m.get('on')) for m in s.get('mobs', [])][:3]})
            else:
                quiet_since = None
            last_damage = damage_now
            for mob in s.get('mobs', []):
                e = mobs.setdefault(mob['name'], {'lvl': mob.get('lvl'), 'maxhp': mob.get('maxhp', 0)})
                e['maxhp'] = max(e['maxhp'], mob.get('maxhp', 0))
            tank = s['members'][0]
            pos = (tank['x'], tank['y'])
            if last_pos is None or math.hypot(pos[0] - last_pos[0], pos[1] - last_pos[1]) > 10 or s['inCombat']:
                last_pos, last_move_at = pos, now
            if s.get('bossDone', 0) > last_boss:
                last_boss = s['bossDone']
                boss_times.append(round(t))
                record['events'].append({'t': round(t), 'event': 'boss', 'done': last_boss, 'total': s.get('bossTotal')})
            if int(t) % 30 < poll:
                trail.append({'t': round(t), 'x': round(pos[0]), 'y': round(pos[1]), 'cmb': s['inCombat'], 'alive': alive,
                              'hp': [m['hp'] for m in s['members']], 'boss': s.get('bossDone')})
            outcome = None
            if s.get('bossTotal') and s.get('bossDone') == s.get('bossTotal'):
                outcome = 'CLEAR'
            elif not s['auto']:
                outcome = 'AUTO_OFF'
            elif wipes >= max_wipes:
                outcome = 'WIPES'
            elif now - last_move_at > stuck_seconds:
                outcome = 'STUCK'
                record['tankDebug'] = tank.get('dbg')
            elif t > max_seconds:
                outcome = 'TIMEOUT'
            if outcome:
                break
        record['members_result'] = {}
        for m in final['members']:
            r = members[m['name']]
            record['members_result'][m['name']] = {
                'role': r['role'], 'level': m.get('lvl'), 'deaths': r['deaths'], 'minHp': r['minHp'], 'minPw': r['minPw'],
                'damage': m.get('dmg', 0), 'healing': m.get('heal', 0), 'taken': m.get('taken', 0),
                'kills': m.get('kills', 0), 'eliteKills': m.get('elite', 0), 'spells': m.get('spells', [])}
        record['wipes'] = wipes
        record['bossDone'], record['bossTotal'] = final.get('bossDone'), final.get('bossTotal')
        record['bossTimes'] = boss_times
        record['combatSeconds'] = round(combat_s)
        record['activeSeconds'] = round(active_s)
        record['stalemateSeconds'] = round(stalemate_s)
        record['mobs'] = mobs
        record['trail'] = trail
        record['tankPos'] = [round(final['members'][0]['x']), round(final['members'][0]['y'])]
        return outcome


def load_dungeons(path, keys):
    table = {d['key']: d for d in json.load(open(path, encoding='utf-8'))}
    return [table[k] for k in keys]


def save(out_dir, record):
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    (Path(out_dir) / f'{record["label"]}.json').write_text(json.dumps(record, indent=1), encoding='utf-8')


def scout(config):
    dungeons = load_dungeons(config['dungeons'], config['scout_dungeons'])
    jobs = list(dungeons)
    lock = threading.Lock()

    def worker(lane):
        while True:
            with lock:
                if not jobs:
                    return
                dungeon = jobs.pop(0)
            t = lane.team
            members = [t['tank'], t['healer'], *t['dps'], t['support']]
            rec = lane.run({'label': f'scout-{dungeon["key"]}', 'members': members, 'dungeon': dungeon, 'meta': {'scout': True}})
            save(config['out'], rec)
            print(f'{dungeon["key"]:8} {rec["outcome"]:8} {rec["seconds"]:6.0f}s boss {rec.get("bossDone")}/{rec.get("bossTotal")} wipes {rec.get("wipes")}', flush=True)

    lanes = [Lane(config, i, team) for i, team in enumerate(config['lanes'])]
    threads = [threading.Thread(target=worker, args=(lane,)) for lane in lanes]
    for th in threads:
        th.start()
    for th in threads:
        th.join()


def campaign(config):
    """One task per specialization: the actor is re-specced and re-equipped once, plays a warm-up run that is not counted (a bot that was just re-equipped lost its
    buffs and its AI state and died in its first fight in 29 of 33 runs, against 14 of 196 for the fixed team), then plays its counted runs in different dungeons."""
    specs = [s for s in spec_table() if s[0] >= 12]
    # The dungeon of a run follows the spec's place in the full list, so a re-run of a few specs plays the dungeons of the full campaign.
    indexed = [(gi, s) for gi, s in enumerate(specs) if not config.get('only_specs') or s[1] in config['only_specs']]
    dungeons = load_dungeons(config['dungeons'], config['campaign_dungeons'])
    actors = {int(k): v for k, v in config['actors'].items()}
    attempts = config.get('attempts', 2)
    out_dir = Path(config['out'])
    done = {p.stem for p in out_dir.glob('*.json')} if out_dir.exists() else set()
    tasks = []
    for i, (cls, spec, role, name) in indexed:
        runs = []
        for attempt in range(attempts):
            dungeon = dungeons[(i + attempt) % len(dungeons)]  # consecutive dungeons: every spec sees both halves of the list
            label = f'spec{spec:03d}-{name.replace(" ", "")}-a{attempt + 1}-{dungeon["key"]}'
            if label not in done:
                runs.append({'attempt': attempt + 1, 'dungeon': dungeon, 'label': label})
        if runs:
            tasks.append({'cls': cls, 'spec': spec, 'role': role, 'name': name, 'runs': runs, 'index': i})
    lock = threading.Lock()
    in_flight = set()

    def next_task():
        with lock:
            for t in tasks:
                if actors[t['cls']] not in in_flight:
                    tasks.remove(t)
                    in_flight.add(actors[t['cls']])
                    return t
        return None

    def worker(lane):
        t = lane.team
        while True:
            task = next_task()
            if task is None:
                with lock:
                    if not tasks:
                        return
                time.sleep(5)
                continue
            guid = actors[task['cls']]
            if task['role'] == 'Tank':
                members = [guid, t['healer'], *t['dps'], t['support']]
            elif task['role'] == 'Healer':
                members = [t['tank'], guid, *t['dps'], t['support']]
            else:
                others = [t['dps'][0], t['dps'][1], t['support']]
                members = [t['tank'], t['healer'], guid, *others[:2]]
            prep = [f'botcmd learnspec {guid} {task["spec"]}', f'botcmd train livegear {guid} new']
            first = task['runs'][0]
            warm = lane.run({'label': 'warmup-' + first['label'], 'members': members, 'dungeon': first['dungeon'], 'prep': prep,
                             'meta': {'warmup': True}, 'max_seconds': config.get('warmup_seconds', 240)})
            print(f'{warm["label"]:48} {warm["outcome"]:8} {warm["seconds"]:6.0f}s (warm-up, not counted)', flush=True)
            for run in task['runs']:
                meta = {'cls': task['cls'], 'spec': task['spec'], 'role': task['role'], 'name': task['name'], 'attempt': run['attempt']}
                rec = lane.run({'label': run['label'], 'members': members, 'dungeon': run['dungeon'], 'prep': [], 'meta': meta})
                save(out_dir, rec)
                print(f'{run["label"]:48} {rec["outcome"]:8} {rec["seconds"]:6.0f}s boss {rec.get("bossDone")}/{rec.get("bossTotal")} wipes {rec.get("wipes")}', flush=True)
            with lock:
                in_flight.discard(guid)

    lanes = [Lane(config, i, team) for i, team in enumerate(config['lanes'])]
    threads = [threading.Thread(target=worker, args=(lane,)) for lane in lanes]
    for th in threads:
        th.start()
    for th in threads:
        th.join()


if __name__ == '__main__':
    mode, path = sys.argv[1], sys.argv[2]
    cfg = json.load(open(path, encoding='utf-8'))
    {'scout': scout, 'campaign': campaign}[mode](cfg)
