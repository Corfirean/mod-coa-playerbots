"""Run a plan of training episodes on a running sandbox world and store normalised results.

python -I ci_plan_run.py plan.json

plan.json
{
  "label": "baseline-heal",                      # result folder name under "out"
  "out": "C:/.../combat-intelligence-results",
  "credentials": "C:/.../ra-account.json",
  "ra_port": 3469,
  "spawn": [106035, 106036],                     # bots to log in first (a restarted world has none online)
  "binary": "C:/.../worldserver-play.exe",       # recorded (sha256) in the manifest, not launched here
  "git_revision": "966e054",                      # recorded as given
  "module_conf": "C:/.../mod_coa_playerbots.conf", # optional; default: configs/modules/mod_coa_playerbots.conf next to the credentials file.
                                                  # Only its CoaBots.* settings are recorded (ci_compare checks them between the two runs)
  "episodes": [
     {"scenario": "HEAL_RAMP", "candidate": 105988, "party": [105913, 105945, 105986, 105915],
      "seconds": 60, "seeds": [1001, 1002, 1003],
      "before": ["botcmd learnspec 105988 101"],      # optional console commands run before every seed of this episode
      "name": "vizier"}                               # optional: result folder name instead of <scenario>-<candidate>
  ]
}

For every (episode, seed) the runner sends `botcmd train party ...` and `botcmd train start <candidate> <scenario> <seconds> <seed>`, waits for the
session to finish, then copies summary.json (plus the decision/event files when "keep_traces" is true) into <out>/<label>/. Nothing here picks
or changes a rotation: the plan decides what is measured, an analyst (human or AI agent) decides what to change.
"""
import hashlib
import json
import re
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_ra import Ra  # noqa: E402


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def session_line(status_text, candidate, scenario):
    for line in status_text.splitlines():
        if f'bot={candidate} ' in line and f'scenario={scenario}' in line:
            return line
    return None


def coabots_settings(plan):
    """The CoaBots.* key/value pairs of the module config the world runs with (nothing else is read: the file may sit next to credentials)."""
    path = Path(plan['module_conf']) if plan.get('module_conf') else Path(plan['credentials']).parent / 'configs/modules/mod_coa_playerbots.conf'
    if not path.exists():
        return None
    settings = {}
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        line = line.strip()
        if line.startswith('CoaBots.') and '=' in line:
            key, value = line.split('=', 1)
            settings[key.strip()] = value.split('#')[0].strip()
    return settings


def main(plan_path):
    plan = json.load(open(plan_path, encoding='utf-8'))
    out = Path(plan['out']) / plan['label']
    out.mkdir(parents=True, exist_ok=True)
    ra = Ra(plan['credentials'], port=plan.get('ra_port', 3469))
    manifest = {
        'label': plan['label'],
        'started': time.strftime('%Y-%m-%dT%H:%M:%S'),
        'git_revision': plan.get('git_revision'),
        'binary': plan.get('binary'),
        'binary_sha256': sha256(plan['binary']) if plan.get('binary') and Path(plan['binary']).exists() else None,
        'episodes': [],
    }
    # What ci_compare needs to tell that two runs are the same experiment: the core revision of the running world and its CoaBots.* settings.
    m = re.search(r'rev\. (\w+)', ra.run('server info'))
    manifest['core_revision'] = m.group(1) if m else None
    manifest['config'] = coabots_settings(plan)
    for guid in plan.get('spawn', []):
        ra.run(f'botcmd spawnbot {guid}')
    if plan.get('spawn'):
        time.sleep(15)
    for ep in plan['episodes']:
        for seed in ep['seeds']:
            ra.run('botcmd train stop')
            for command in ep.get('before', []):
                ra.run(command)
                time.sleep(2)
            if ep.get('party'):
                ra.run('botcmd train party ' + ' '.join(str(g) for g in ep['party']))
            reply = ra.run(f"botcmd train start {ep['candidate']} {ep['scenario']} {ep['seconds']} {seed}")
            m = re.search(r'session=(\S+)', reply)
            if not m:
                manifest['episodes'].append({'scenario': ep['scenario'], 'seed': seed, 'error': reply})
                continue
            session = m.group(1)
            status = ra.wait_until('botcmd train status', lambda t: session in t and 'state=finished' in t.split(session, 1)[1].split('\n', 1)[0],
                                   every=5, limit=ep['seconds'] * 3 + 600)
            line = next(l for l in status.splitlines() if session in l)
            folder = Path(re.search(r'output=(.+)$', line).group(1).strip())
            dest = out / f"{ep.get('name') or ep['scenario'] + '-' + str(ep['candidate'])}-{seed}"
            dest.mkdir(exist_ok=True)
            shutil.copy2(folder / 'summary.json', dest / 'summary.json')
            for extra in ('abilities.json', 'threat.json', 'healing.json', 'resources.json', 'support.json', 'mechanics.json'):
                if (folder / extra).exists():
                    shutil.copy2(folder / extra, dest / extra)
            if plan.get('keep_traces'):
                for extra in ('events.jsonl', 'decisions.jsonl'):
                    if (folder / extra).exists():
                        shutil.copy2(folder / extra, dest / extra)
            manifest['episodes'].append({'scenario': ep['scenario'], 'candidate': ep['candidate'], 'seed': seed, 'session': session,
                                         'folder': str(dest)})
    manifest['finished'] = time.strftime('%Y-%m-%dT%H:%M:%S')
    json.dump(manifest, open(out / 'manifest.json', 'w', encoding='utf-8'), indent=1)
    ra.close()
    print('wrote', out / 'manifest.json')


if __name__ == '__main__':
    main(sys.argv[1])
