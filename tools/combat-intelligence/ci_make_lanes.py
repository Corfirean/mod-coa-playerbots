"""Create fixed teams ("lanes") of dedicated bots for parallel dungeon runs.

python -I ci_make_lanes.py <credentials> <count> <out.json> [port]

Each lane is five dedicated training actors in the same specializations as the sandbox's first team (tank Fortitude 52, healer Vizier 101, damage dealers
23 and 50, support Wind 13), equipped by the live gear-up of their level. The first lane is the existing team 106035-106039 and is not recreated.
Writes {"lanes": [{"tank": g, "healer": g, "dps": [g, g], "support": g}, ...]} and prints nothing secret.
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_ra import Ra  # noqa: E402

TEAM = [('tank', 29, 52), ('healer', 29, 101), ('dps', 19, 23), ('dps', 28, 50), ('support', 16, 13)]
FIRST = {'tank': 106035, 'healer': 106036, 'dps': [106037, 106038], 'support': 106039}


def main(argv):
    credentials, count, out = argv[0], int(argv[1]), argv[2]
    port = int(argv[3]) if len(argv) > 3 else 3469
    ra = Ra(credentials, port=port)
    lanes = [FIRST]
    for _ in range(count - 1):
        lane = {'dps': []}
        for slot, cls, spec in TEAM:
            text = ra.run(f'botcmd train actor {cls}')
            m = re.search(r'guid=(\d+)', text)
            if not m:
                raise SystemExit('actor creation failed: ' + text[:200])
            guid = int(m.group(1))
            ra.run(f'botcmd spawnbot {guid}')
            time.sleep(3)
            ra.run(f'botcmd learnspec {guid} {spec}')
            ra.run(f'botcmd train livegear {guid} new')
            if slot == 'dps':
                lane['dps'].append(guid)
            else:
                lane[slot] = guid
        lanes.append(lane)
        print(lane, flush=True)
    Path(out).write_text(json.dumps({'lanes': lanes}, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1:])
