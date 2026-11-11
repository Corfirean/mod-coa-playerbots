from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes as w
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import statistics
import subprocess
import threading
import time

PREFIX = 'CoaPerf'
HIDDEN = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
SCRIPT_SHA256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')
    temp.replace(path)


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def verify_native_features(folder, options):
    if options.get('World.Performance.DirectRankLookup'):
        with (folder / 'console.log').open('rb') as console:
            if not any(b'Direct spell rank lookup enabled after validating ' in line for line in console):
                raise RuntimeError('Selected world did not activate validated direct spell rank lookup')


def verify_native_schedule(snapshot, options):
    gauges = snapshot.get('gauges', {})
    if options.get('World.Performance.InterleaveWorldObjects') and not gauges.get('interleaved_world_maps', 0):
        raise RuntimeError('Selected world did not activate interleaved world object updates')
    if options.get('CoaBots.Spells.CacheLearnedRanks') and gauges.get('learned_rank_cache_enabled') != 1:
        raise RuntimeError('Selected world does not support or activate learned rank caching')
    far = options.get('CoaBots.Scheduling.FarIdleIntervalMs', 0)
    if far and options.get('CoaBots.Scheduling.Enable') and options.get('CoaBots.Scheduling.Adaptive'):
        if gauges.get('far_idle_interval_ms') != min(2000, max(500, far)):
            raise RuntimeError('Selected world did not activate the requested far idle interval')


def event(root, name, **fields):
    row = dict(utc=dt.datetime.now(dt.timezone.utc).isoformat(), event=name, **fields)
    with (root / 'events.jsonl').open('a', encoding='utf-8') as out:
        out.write(json.dumps(row, ensure_ascii=False) + '\n')
    print(json.dumps(row, ensure_ascii=False), flush=True)


def set_value(text, key, value):
    replacement = key + ' = ' + str(value)
    pattern = r'(?m)^' + re.escape(key) + r'\s*=.*$'
    return re.sub(pattern, lambda _: replacement, text) if re.search(pattern, text) else text + '\n' + replacement + '\n'


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def powershell(command):
    command = '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); ' + command
    result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True,
                            encoding='utf-8', errors='replace', creationflags=HIDDEN)
    if result.returncode:
        raise RuntimeError('Host inventory failed')
    return result.stdout.strip()


def digest(path):
    with path.open('rb') as src:
        return hashlib.file_digest(src, 'sha256').hexdigest()


def contaminating_process(name):
    name = name.lower()
    return name in {'cl.exe', 'ninja.exe', 'msbuild.exe'} or bool(
        re.fullmatch(r'worldserver(?:[-_].*)?\.exe', name))


def live_competitors(owned_pid):
    class ProcessEntry(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD),
                    ('heap', ctypes.c_size_t), ('module', w.DWORD), ('threads', w.DWORD),
                    ('parent', w.DWORD), ('priority', w.LONG), ('flags', w.DWORD),
                    ('name', w.WCHAR * 260)]

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Process32FirstW.argtypes = [w.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [w.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = ProcessEntry()
        entry.size = ctypes.sizeof(entry)
        found = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        competitors = []
        while found:
            if entry.pid != owned_pid and contaminating_process(entry.name):
                competitors.append(dict(name=entry.name, pid=entry.pid))
            found = kernel.Process32NextW(snapshot, ctypes.byref(entry))
        if ctypes.get_last_error() != 18:
            raise ctypes.WinError(ctypes.get_last_error())
        return competitors
    finally:
        kernel.CloseHandle(snapshot)


def process_running(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return ctypes.get_last_error() not in [87, 1168]
    try:
        code = w.DWORD()
        return not kernel.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value == 259
    finally:
        kernel.CloseHandle(handle)


def cached_runtime_file(root, source, destination):
    cache = root / 'runtime-cache'
    cache.mkdir(exist_ok=True)
    identity = digest(source)
    stored = cache / (identity + '-' + source.name)
    if not stored.exists():
        shutil.copy2(source, stored)
    elif digest(stored) != identity:
        raise RuntimeError('Immutable runtime cache checksum mismatch')
    try:
        os.link(stored, destination)
    except OSError:
        shutil.copy2(stored, destination)


def source_identity(path):
    result = subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'], capture_output=True, text=True, creationflags=HIDDEN)
    diff = subprocess.run(['git', '-C', str(path), 'diff', '--no-ext-diff', 'HEAD'], capture_output=True, creationflags=HIDDEN)
    fingerprint = hashlib.sha256()
    source = path / 'module/src' if (path / 'module/src').is_dir() else path / 'src'
    for file in sorted(source.rglob('*')):
        if file.is_file() and file.suffix in ['.cpp', '.h', '.hpp', '.inl']:
            fingerprint.update(str(file.relative_to(source)).replace('\\', '/').encode())
            fingerprint.update(bytes.fromhex(digest(file)))
    return dict(path=str(path.resolve()), sha=result.stdout.strip() if result.returncode == 0 else None,
                tracked_diff_sha256=hashlib.sha256(diff.stdout).hexdigest() if diff.returncode == 0 else None,
                source_files_sha256=fingerprint.hexdigest())


def thread_resources(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)

    class Entry(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('id', w.DWORD), ('owner', w.DWORD),
                    ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]

    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Thread32First.argtypes = [w.HANDLE, ctypes.POINTER(Entry)]
    kernel.Thread32Next.argtypes = [w.HANDLE, ctypes.POINTER(Entry)]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.GetThreadTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4
    kernel.CloseHandle.argtypes = [w.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(4, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        return []
    rows = []
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        found = kernel.Thread32First(snapshot, ctypes.byref(entry))
        while found:
            if entry.owner == pid:
                handle = kernel.OpenThread(0x0800, False, entry.id)
                if handle:
                    try:
                        times = [w.FILETIME() for _ in range(4)]
                        if kernel.GetThreadTimes(handle, *(ctypes.byref(t) for t in times)):
                            values = [(t.dwHighDateTime << 32) | t.dwLowDateTime for t in times]
                            rows.append(dict(id=entry.id, creation=values[0], cpu_seconds=sum(values[2:]) / 1e7))
                    finally:
                        kernel.CloseHandle(handle)
            found = kernel.Thread32Next(snapshot, ctypes.byref(entry))
        return sorted(rows, key=lambda row: row['creation'])
    finally:
        kernel.CloseHandle(snapshot)


def sanitized_config(text):
    lines = []
    for line in text.splitlines():
        match = re.match(r'^\s*([^#=]+?)\s*=\s*(.*)$', line)
        if match and re.search('databaseinfo|password|secret|token|credential', match[1], re.I):
            line = match[1] + ' = "<redacted>"'
        lines.append(line)
    return '\n'.join(lines) + '\n'


def inventory():
    return dict(cpu=powershell('Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json'),
                computer=powershell('Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory | ConvertTo-Json'),
                os=powershell('Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json'),
                disks=powershell('Get-CimInstance Win32_DiskDrive | Select-Object Model,Size,InterfaceType | ConvertTo-Json'),
                power=powershell('powercfg /getactivescheme'))


def safe_remove(root, path):
    root, path = root.resolve(), path.resolve()
    if not (root / '.disposable-benchmark').is_file() or not path.is_relative_to(root) or path == root:
        raise RuntimeError('Refusing removal outside the marked fixture')
    if path.exists():
        shutil.rmtree(path)


def resources(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4

    class Memory(ctypes.Structure):
        _fields_ = [('cb', w.DWORD), ('faults', w.DWORD)] + [(key, ctypes.c_size_t) for key in
                    ('peak_ws', 'ws', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]

    class IO(ctypes.Structure):
        _fields_ = [(key, ctypes.c_ulonglong) for key in
                    ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]

    class GlobalMemory(ctypes.Structure):
        _fields_ = [('length', w.DWORD), ('load', w.DWORD)] + [(key, ctypes.c_ulonglong) for key in
                    ('total', 'available', 'total_page', 'available_page', 'total_virtual', 'available_virtual', 'extended')]

    mem_api = ctypes.WinDLL('psapi').GetProcessMemoryInfo
    mem_api.argtypes = [w.HANDLE, ctypes.POINTER(Memory), w.DWORD]
    kernel.GetProcessIoCounters.argtypes = [w.HANDLE, ctypes.POINTER(IO)]
    handle = kernel.OpenProcess(0x1000 | 0x10, False, pid)
    if not handle:
        raise RuntimeError('Cannot read process resource counters')
    try:
        times = [w.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)):
            raise RuntimeError('Cannot read process CPU time')
        mem = Memory()
        mem.cb = ctypes.sizeof(mem)
        if not mem_api(handle, ctypes.byref(mem), mem.cb):
            raise RuntimeError('Cannot read process memory')
        io = IO()
        kernel.GetProcessIoCounters(handle, ctypes.byref(io))
        system = GlobalMemory()
        system.length = ctypes.sizeof(system)
        kernel.GlobalMemoryStatusEx(ctypes.byref(system))
        system_times = [w.FILETIME() for _ in range(3)]
        kernel.GetSystemTimes.argtypes = [ctypes.POINTER(w.FILETIME)] * 3
        busy_cpu = None
        if kernel.GetSystemTimes(*(ctypes.byref(value) for value in system_times)):
            values = [(value.dwHighDateTime << 32) | value.dwLowDateTime for value in system_times]
            busy_cpu = (values[1] + values[2] - values[0]) / 1e7
        return dict(cpu_seconds=sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 1e7,
                    system_busy_cpu_seconds=busy_cpu,
                    private_bytes=mem.private, working_set_bytes=mem.ws, read_bytes=io.read_bytes,
                    write_bytes=io.write_bytes, available_ram_bytes=system.available, total_ram_bytes=system.total)
    finally:
        kernel.CloseHandle(handle)


class Fixture:
    def __init__(self, root):
        self.root = root.resolve()
        if not (root / '.disposable-benchmark').is_file():
            raise RuntimeError('Fixture is not explicitly marked disposable')
        self.settings = read_json((root / 'fixture.json'))
        self.mysql_bin = Path(self.settings['mysql_bin'])
        self.client = root / 'private-client.ini'
        self.db = None
        self.db_pid = None
        self.verified_database = False

    def sql(self, query):
        result = subprocess.run([str(self.mysql_bin / 'mysql.exe'), '--defaults-file=' + str(self.client),
                                 '--batch', '--skip-column-names'], input=query.encode(), capture_output=True,
                                timeout=120, creationflags=HIDDEN)
        if result.returncode:
            raise RuntimeError('Fixture SQL failed; database diagnostics remain private')
        return result.stdout.decode(errors='replace').strip()

    def start_db(self):
        if self.db:
            raise RuntimeError('Fixture database already owned by this runner')
        self.db = subprocess.Popen([str(self.mysql_bin / 'mysqld.exe'), '--defaults-file=' + str(self.root / 'my.ini')],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=HIDDEN)
        until = time.monotonic() + 180
        while time.monotonic() < until:
            if self.db.poll() is not None:
                raise RuntimeError('Fixture MySQL exited during startup')
            try:
                if self.sql('SELECT 1;') == '1':
                    actual_data = Path(self.sql('SELECT @@datadir;')).resolve()
                    if actual_data != (self.root / 'data').resolve():
                        raise ValueError('Fixture port points to a different data directory')
                    self.db_pid = int((self.root / 'mysql.pid').read_text(encoding='utf-8-sig').strip())
                    self.verified_database = True
                    return
            except RuntimeError:
                pass
            time.sleep(1)
        raise RuntimeError('Fixture MySQL readiness timed out')

    def stop_db(self):
        if not self.db:
            return
        if not self.verified_database:
            self.db.wait(timeout=30)
            self.db = None
            self.db_pid = None
            return
        if self.db.poll() is None:
            self.sql('SHUTDOWN;')
            self.db.wait(timeout=300)
        if self.db.returncode != 0:
            raise RuntimeError('Fixture MySQL did not stop cleanly')
        self.db = None
        self.db_pid = None
        self.verified_database = False

    def restore(self):
        if self.db:
            raise RuntimeError('Cannot restore while database is running')
        if not (self.root / 'seed-data' / 'mysql.ibd').is_file():
            raise RuntimeError('A clean seed snapshot is required')
        safe_remove(self.root, self.root / 'data')
        shutil.copytree(self.root / 'seed-data', self.root / 'data')


class World:
    def __init__(self, fixture, binary, folder, population, options, scenario='distributed', instrumentation='full'):
        self.fixture, self.folder = fixture, folder
        folder.mkdir(parents=True, exist_ok=False)
        self.lock, self.text = threading.Lock(), ''
        self.input_lock = threading.Lock()
        self.last_progress = time.monotonic()
        self.low_ram_since = None
        self.next_competitor_check = 0
        self.last_metric_stamp = None
        self.last_tick_status = None
        runtime = folder / 'runtime'
        config_dir = runtime / 'configs'
        shutil.copytree(fixture.root / 'configs', config_dir)
        for dll in binary.parent.glob('*.dll'):
            cached_runtime_file(fixture.root, dll, runtime / dll.name)
        for dll in Path(fixture.settings['runtime_dlls']).glob('*.dll'):
            if not (runtime / dll.name).exists():
                cached_runtime_file(fixture.root, dll, runtime / dll.name)
        self.binary = runtime / 'worldserver.exe'
        cached_runtime_file(fixture.root, binary, self.binary)
        config = (config_dir / 'worldserver.conf').read_text(encoding='utf-8-sig')
        for key, value in {'LogsDir': '"' + (folder / 'logs').as_posix() + '"', 'WorldServerPort': free_port(),
                           'BindIP': '"127.0.0.1"', 'Ra.Enable': 0, 'SOAP.Enabled': 0, 'Console.Enable': 1,
                           'Updates.EnableDatabases': 0, 'MaxCoreStuckTime': 0, 'Log.Async.Enable': 1,
                           'AiPlayerbot.Enabled': 0}.items():
            config = set_value(config, key, value)
        (folder / 'logs').mkdir()
        (config_dir / 'worldserver.conf').write_text(config, encoding='utf-8')
        bot_conf = config_dir / 'modules/mod_coa_playerbots.conf'
        bots = bot_conf.read_text(encoding='utf-8-sig')
        seed = read_json((fixture.root / 'seed-population.json')) if (fixture.root / 'seed-population.json').exists() else None
        anchor = seed['characters'][0] if seed else [0, 0, 0, 0, 0, 0, 0, 0]
        for key, value in {'CoaBots.Enable': 1, 'CoaBots.AutoLoginOnStartup': int(population > 0),
                           'CoaBots.AutoLogin.MaxCount': population, 'CoaBots.AutoLogin.BatchSize': 10,
                           'CoaBots.AutoLogin.BatchIntervalMs': 500, 'CoaBots.RandomSpawn.AccountPrefix': '"' + PREFIX + '"',
                           'CoaBots.RandomSpawn.MaxCount': 3000, 'CoaBots.RandomSpawn.AutoLogin': 0,
                           'CoaBots.RandomSpawn.BatchSize': 5, 'CoaBots.RandomSpawn.BatchIntervalMs': 100,
                           'CoaBots.World.Enable': 1, 'CoaBots.WorldBrain.Enable': 1,
                           'CoaBots.Economy.Enable': 1, 'CoaBots.BGFill.Enable': 1, 'CoaBots.LfgFill.Enable': 1,
                           'BotAI.DynamicEncounters.Enable': 1,
                           'CoaBots.Scheduling.Enable': 0, 'CoaBots.Scheduling.Adaptive': 0,
                           'CoaBots.Budgets.Enable': 0, 'CoaBots.Navigation.ReuseValidatedPath': 0,
                           'CoaBots.Perf.ProfileCombat': 0, 'World.Performance.EntitySampleRate': 0,
                           'CoaBots.Benchmark.PreserveLoginPosition': 1,
                           'CoaBots.Benchmark.Root': '"' + fixture.root.as_posix() + '"',
                           'CoaBots.Benchmark.Scenario': '"' + scenario + '"', 'CoaBots.Benchmark.MaxOnline': population,
                           'CoaBots.Benchmark.AnchorMap': int(anchor[4]), 'CoaBots.Benchmark.AnchorX': float(anchor[5]),
                           'CoaBots.Benchmark.AnchorY': float(anchor[6]), 'CoaBots.Benchmark.AnchorZ': float(anchor[7]),
                           'CoaBots.Perf.Enable': int(instrumentation != 'reference'),
                           'CoaBots.Benchmark.ReferenceMetrics': int(instrumentation == 'reference'),
                           'CoaBots.Perf.OutputDir': '"' + (folder / 'metrics').as_posix() + '"',
                           **options}.items():
            bots = set_value(bots, key, value)
        bot_conf.write_text(bots, encoding='utf-8')
        env = {k: v for k, v in os.environ.items() if not k.startswith('AC_')}
        env.update(AC_AI_PLAYERBOT_ENABLED='0', AC_UPDATES_ENABLE_DATABASES='0', AC_PLAYERBOTS_UPDATES_ENABLE_DATABASES='0')
        self.output = (folder / 'console.log').open('ab', buffering=0)
        self.proc = subprocess.Popen([str(self.binary), '-c', 'configs/worldserver.conf'], cwd=runtime,
                                     env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, creationflags=HIDDEN)
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        while data := self.proc.stdout.read1(65536):
            self.output.write(data)
            with self.lock:
                self.text = (self.text + data.decode(errors='replace'))[-1000000:]

    def command(self, text):
        if self.proc.poll() is not None:
            raise RuntimeError('World exited unexpectedly: ' + str(self.proc.returncode))
        with self.input_lock:
            self.proc.stdin.write((text + '\n').encode())
            self.proc.stdin.flush()

    def check_stop(self):
        if (self.fixture.root / 'STOP').exists():
            raise RuntimeError('Benchmark stop requested')
        if time.monotonic() >= self.next_competitor_check:
            self.next_competitor_check = time.monotonic() + 10
            competitors = live_competitors(self.proc.pid)
            if competitors:
                write_json(self.folder / 'host-contamination.json', dict(processes=competitors))
                raise RuntimeError('Another world or build started during the benchmark')
        if self.proc.poll() is None:
            memory = resources(self.proc.pid)
            if memory['available_ram_bytes'] < memory['total_ram_bytes'] * .1:
                self.low_ram_since = self.low_ram_since or time.monotonic()
                if time.monotonic() - self.low_ram_since > 30:
                    write_json(self.folder / 'low-memory.json', memory)
                    raise RuntimeError('Less than 10 percent RAM available for 30 seconds')
            else:
                self.low_ram_since = None
            metrics = list((self.folder / 'metrics').glob('metrics-*.json'))
            with self.lock:
                statuses = re.findall(r'CoaBotPerf enabled=[01] epoch=(\d+) samples=(\d+)', self.text)
            if statuses and statuses[-1] != self.last_tick_status:
                self.last_tick_status = statuses[-1]
                self.last_progress = time.monotonic()
            if metrics:
                newest = max(file.stat().st_mtime_ns for file in metrics)
                if newest != self.last_metric_stamp:
                    self.last_metric_stamp = newest
                    self.last_progress = time.monotonic()
                if time.monotonic() - self.last_progress > 30:
                    write_json(self.folder / 'stalled-world.json', dict(resources=memory,
                               threads=thread_resources(self.proc.pid), seconds_without_metrics=time.monotonic() - self.last_progress))
                    raise RuntimeError('No metric progress for 30 seconds')

    def reset_metrics(self):
        with self.lock:
            self.text = ''
        self.command('botcmd perf reset')
        until = time.monotonic() + 30
        while time.monotonic() < until:
            with self.lock:
                match = re.search(r'CoaBotPerf enabled=[01] epoch=(\d+)', self.text)
            if match:
                return int(match[1])
            time.sleep(.1)
        raise RuntimeError('Metric reset did not acknowledge')

    def verify_reference_mode(self):
        until = time.monotonic() + 10
        while time.monotonic() < until:
            self.check_stop()
            self.command('botcmd perf status')
            with self.lock:
                if re.search(r'CoaBotPerf enabled=0 epoch=\d+ samples=\d+ reference=1', self.text):
                    return
            time.sleep(.5)
        raise RuntimeError('Selected binary does not acknowledge the isolated reference mode')

    def dump_metrics(self, expected_epoch=None):
        directory = self.folder / 'metrics'
        before = set(directory.glob('metrics-*.json'))
        self.command('botcmd perf dump')
        until = time.monotonic() + 30
        while time.monotonic() < until:
            for path in directory.glob('metrics-*.json'):
                if path in before:
                    continue
                try:
                    value = read_json(path)
                    if expected_epoch is None or value['epoch'] == expected_epoch:
                        return value
                    before.add(path)
                except (OSError, json.JSONDecodeError):
                    pass
            time.sleep(.2)
            self.command('botcmd perf dump')
        raise RuntimeError('Metric dump did not complete')

    def await_ready(self):
        until = time.monotonic() + 600
        while time.monotonic() < until:
            self.check_stop()
            if self.proc.poll() is not None:
                raise RuntimeError('World startup failed; inspect console.log')
            with self.lock:
                if 'AC> ' in self.text:
                    return
                if 'AzerothCore rev.' in self.text and ('Ready...' in self.text or 'World initialized' in self.text):
                    return
                if 'server is now ready' in self.text.lower():
                    return
            time.sleep(1)
        raise RuntimeError('World readiness timed out')

    def online(self):
        return int(self.fixture.sql("SELECT COUNT(*) FROM acore_characters.characters c JOIN acore_auth.account a ON a.id=c.account "
                                    "WHERE a.username LIKE '" + PREFIX + "%' AND c.online=1;"))

    def await_population(self, target):
        started = time.monotonic()
        until = started + max(900, target / 5)
        while time.monotonic() < until:
            self.check_stop()
            self.command('botcmd perf status')
            if self.online() == target:
                paths = list((self.folder / 'metrics').glob('metrics-*.json'))
                if paths:
                    latest = max(paths, key=lambda path: int(path.stem.split('-')[-1]))
                    try:
                        data = read_json(latest)
                        if data['gauges'].get('online_in_world') == target:
                            return time.monotonic() - started
                    except (OSError, json.JSONDecodeError):
                        pass
            time.sleep(5)
        raise RuntimeError('Population did not reach target')

    def stop(self):
        started = time.monotonic()
        forced = False
        if self.proc.poll() is None:
            self.command('server shutdown 1s 0')
            previous = None
            unchanged_since = time.monotonic()
            diagnosed = False
            while self.proc.poll() is None:
                try:
                    current = resources(self.proc.pid)
                except RuntimeError:
                    if self.proc.poll() is not None:
                        break
                    raise
                database = resources(self.fixture.db_pid)
                progress = (current['write_bytes'], self.output.tell(), database['write_bytes'])
                if progress != previous:
                    unchanged_since = time.monotonic()
                    previous = progress
                if time.monotonic() - started >= 300 and not diagnosed:
                    latest = sorted((self.folder / 'metrics').glob('metrics-*.json'), key=lambda file: file.stat().st_mtime_ns)
                    queues = read_json(latest[-1]).get('gauges', {}) if latest else {}
                    write_json(self.folder / 'slow-shutdown.json', dict(resources=current, database=database,
                               threads=thread_resources(self.proc.pid), last_gauges=queues,
                               seconds=time.monotonic() - started))
                    diagnosed = True
                if time.monotonic() - unchanged_since >= 600:
                    self.proc.terminate()
                    self.proc.wait(timeout=30)
                    forced = True
                    break
                time.sleep(1)
        self.reader.join(timeout=10)
        self.output.close()
        result = dict(seconds=time.monotonic() - started, exit_code=self.proc.returncode, forced=forced)
        write_json(self.folder / 'shutdown.json', result)
        return result


def compare_checkpoints(expected, actual):
    errors = []
    by_guid = {row['guid']: row for row in actual['players']}
    for before in expected['players']:
        after = by_guid.get(before['guid'])
        if after is None:
            errors.append(dict(guid=before['guid'], field='missing_player'))
            continue
        for field in ['level', 'class', 'race', 'spec', 'xp', 'money', 'map']:
            if before[field] != after[field]:
                errors.append(dict(guid=before['guid'], field=field, before=before[field], after=after[field]))
        for field in ['x', 'y', 'z']:
            if abs(before[field] - after[field]) > .05:
                errors.append(dict(guid=before['guid'], field=field, before=before[field], after=after[field]))
        for field in ['quests', 'items']:
            if sorted(before[field]) != sorted(after[field]):
                errors.append(dict(guid=before['guid'], field=field))
    return errors


def verify_persistence(fixture, binary, folder, options):
    checkpoint_file = folder / 'metrics/checkpoint.json'
    if not checkpoint_file.is_file():
        raise RuntimeError('The native pre-logout checkpoint is missing')
    expected = read_json(checkpoint_file)
    if not expected['players']:
        write_json(folder / 'persistence.json', dict(status='not_applicable', sample_count=0))
        return
    ids = ','.join(str(int(row['guid'])) for row in expected['players'])
    ownership = int(fixture.sql("SELECT COUNT(*) FROM acore_characters.characters c JOIN acore_auth.account a ON a.id=c.account "
                                "WHERE c.guid IN (" + ids + ") AND a.username LIKE '" + PREFIX + "%';"))
    if ownership != len(expected['players']):
        raise RuntimeError('Checkpoint contains characters outside the benchmark population')
    saved = fixture.sql('SELECT guid,level,xp,money,map,position_x,position_y,position_z FROM acore_characters.characters WHERE guid IN (' + ids + ') ORDER BY guid;')
    write_json(folder / 'persisted-characters.json', [line.split('\t') for line in saved.splitlines()])
    verification_options = {**options, 'CoaBots.Benchmark.SuspendOnLogin': 1,
                            'CoaBots.Economy.Enable': 0, 'CoaBots.World.Enable': 0, 'CoaBots.WorldBrain.Enable': 0,
                            'CoaBots.BGFill.Enable': 0, 'CoaBots.LfgFill.Enable': 0, 'BotAI.DynamicEncounters.Enable': 0}
    readback = World(fixture, binary, folder / 'persistence-relogin', 0, verification_options)
    try:
        readback.await_ready()
        for row in expected['players']:
            readback.command('botcmd spawnbot ' + str(row['guid']))
        readback.await_population(len(expected['players']))
        stopped = readback.stop()
        if stopped['exit_code'] != 0 or stopped['forced']:
            raise RuntimeError('Persistence readback did not stop cleanly')
        actual = read_json((readback.folder / 'metrics/checkpoint.json'))
        errors = compare_checkpoints(expected, actual)
        write_json(folder / 'persistence.json', dict(status='passed' if not errors else 'failed',
                   sample_count=len(expected['players']), differences=errors,
                   exclusions=['dead_or_in_transit_actors', 'buyback_conjured_and_expiring_items'],
                   readback='GM-hidden suspended actors; background scenarios disabled only for this readback'))
        if errors:
            raise RuntimeError('Persistence readback differs from the native checkpoint')
    finally:
        if readback.proc.poll() is None:
            readback.stop()


def prepare(args):
    root, source = args.root.resolve(), args.source.resolve()
    if root.exists():
        raise RuntimeError('Preparation requires a new fixture directory')
    running = powershell('Get-CimInstance Win32_Process | Where-Object Name -eq "mysqld.exe" | Select-Object ProcessId,ExecutablePath | ConvertTo-Json')
    if running:
        raise RuntimeError('Stop source MySQL cleanly before copying its data directory')
    root.mkdir(parents=True)
    (root / '.disposable-benchmark').write_text('Isolated CoA performance fixture\n')
    event(root, 'copying_clean_database')
    shutil.copytree(source / 'mysql/data', root / 'data')
    shutil.copytree(source / 'Core/configs', root / 'configs')
    port = free_port()
    client = (source / 'mysql/admin-client.ini').read_text(encoding='utf-8-sig')
    client = set_value(client, 'port', port)
    (root / 'private-client.ini').write_text(client, encoding='utf-8')
    ini = f'[mysqld]\nbasedir="{(source / "mysql").as_posix()}"\ndatadir="{(root / "data").as_posix()}"\nport={port}\nbind-address=127.0.0.1\nmysqlx=0\nskip-log-bin\ninnodb_buffer_pool_size=256M\nmax_allowed_packet=128M\nlog-error="{(root / "mysql-error.log").as_posix()}"\npid-file="{(root / "mysql.pid").as_posix()}"\n'
    (root / 'my.ini').write_text(ini)
    conf = (root / 'configs/worldserver.conf').read_text(encoding='utf-8-sig')
    for label in ('Login', 'World', 'Character', 'Playerbots'):
        key = label + 'DatabaseInfo'
        pattern = r'(?m)^' + key + r'\s*=\s*"([^"]*)"'
        match = re.search(pattern, conf)
        if match:
            fields = match[1].split(';')
            fields[0], fields[1] = '127.0.0.1', str(port)
            conf = set_value(conf, key, '"' + ';'.join(fields) + '"')
    conf = set_value(conf, 'DataDir', '"' + (source / 'Data').as_posix() + '"')
    (root / 'configs/worldserver.conf').write_text(conf, encoding='utf-8')
    for dist in (root / 'configs/modules').glob('*.conf.dist'):
        target = dist.with_suffix('')
        if not target.exists():
            shutil.copy2(dist, target)
    write_json(root / 'fixture.json', dict(mysql_bin=str(source / 'mysql/bin'), runtime_dlls=str(source / 'Core'), mysql_port=port))
    write_json(root / 'host.json', inventory())
    event(root, 'fixture_prepared', port=port)


def run(args):
    import msvcrt
    if not (args.root / '.disposable-benchmark').is_file():
        raise RuntimeError('Fixture marker is missing')
    with (args.root / '.runner.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise RuntimeError('Another runner owns this fixture') from None
        idle_deadline = time.monotonic() + args.wait_for_idle
        while True:
            raw = powershell('Get-CimInstance Win32_Process | Select-Object Name,ProcessId,ExecutablePath,CommandLine | ConvertTo-Json')
            processes = json.loads(raw) if raw else []
            if isinstance(processes, dict):
                processes = [processes]
            processes = [process for process in processes if process_running(process['ProcessId'])]
            competitors = [process for process in processes if contaminating_process(process['Name'])]
            if args.seed or not competitors or time.monotonic() >= idle_deadline:
                break
            event(args.root, 'waiting_for_idle', processes=[dict(name=p['Name'], pid=p['ProcessId']) for p in competitors])
            if (args.root / 'STOP').exists():
                raise RuntimeError('Stop requested while waiting for an idle host')
            time.sleep(min(5, max(0, idle_deadline - time.monotonic())))
        root_text = args.root.resolve().as_posix().lower()
        for process in processes:
            executable = (process.get('ExecutablePath') or '').replace('\\', '/').lower()
            command = (process.get('CommandLine') or '').replace('\\', '/').lower()
            if executable.startswith(root_text + '/') or root_text + '/my.ini' in command:
                raise RuntimeError('A process still owns files in this fixture')
            if not args.seed and contaminating_process(process['Name']):
                raise RuntimeError('Another build or world would contaminate this measurement: '
                                   + process['Name'] + ' PID ' + str(process['ProcessId']))
        try:
            run_owned(args)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def scenario_coverage(scenario, snapshots):
    """Coverage is independent of timing; queued requests do not prove native content ran."""
    requirements = {
        'combat': {'benchmark_combat_pct': 20},
        'dense-combat': {'benchmark_dense_combatants': 200},
        'cluster': {'benchmark_clustered': 300},
        'group-content': {'benchmark_dungeons': 5, 'benchmark_battlegrounds': 2},
        'observer': {'benchmark_synthetic_observer': 1},
        'navigation': {'benchmark_actors': 10},
    }.get(scenario, {})
    rows = [snapshot.get('gauges', {}) for snapshot in snapshots]
    checks = {}
    for name, minimum in requirements.items():
        values = [row.get(name, 0) for row in rows]
        fraction = sum(value >= minimum for value in values) / len(values) if values else 0
        checks[name] = dict(required=minimum, minimum=min(values, default=0),
                            maximum=max(values, default=0), fraction_met=fraction, passed=fraction >= .9)
    return dict(scenario=scenario, snapshots=len(rows), checks=checks,
                passed=all(check['passed'] for check in checks.values()),
                limitations=['Synthetic observer does not validate real-client latency or visible motion.']
                if scenario == 'observer' else
                ['Ten suspended diagnostic actors; not a load or behavior qualification.']
                if scenario == 'navigation' else
                ['GM-hidden stationary density actors; active quest scans and real-client visibility remain unqualified.']
                if scenario == 'cluster' else
                ['Native combat against synthetic durable enemies; not a raid or battleground correctness test.']
                if scenario == 'dense-combat' else [])


def validate_options(options):
    protected = {'CoaBots.Perf.Enable', 'CoaBots.Perf.OutputDir', 'CoaBots.Enable',
                 'CoaBots.AutoLoginOnStartup', 'CoaBots.AutoLogin.MaxCount', 'CoaBots.RandomSpawn.AccountPrefix',
                 'CoaBots.RandomSpawn.MaxCount', 'CoaBots.RandomSpawn.AutoLogin', 'CoaBots.World.Enable',
                 'CoaBots.WorldBrain.Enable', 'CoaBots.Economy.Enable', 'CoaBots.BGFill.Enable',
                 'CoaBots.LfgFill.Enable', 'BotAI.DynamicEncounters.Enable'}
    invalid = [key for key in options if key in protected or key.startswith('CoaBots.Benchmark.')]
    if invalid:
        raise ValueError('Options cannot override managed population, behavior or fixture settings: ' + ', '.join(invalid))


def run_owned(args):
    fixture = Fixture(args.root)
    world = None
    cpu_profile = None
    folder = args.root / 'runs' / args.label
    if folder.exists():
        raise RuntimeError('Run label already exists')
    options = dict(read_json(args.options)) if args.options else {}
    validate_options(options)
    if args.instrumentation == 'reference' and (options.get('CoaBots.Perf.ProfileCombat') or options.get('World.Performance.EntitySampleRate')):
        raise ValueError('Reference overhead runs cannot enable diagnostic CPU profiles')
    if args.restore:
        fixture.restore()
    try:
        fixture.start_db()
        frozen_file = args.binary.parent / 'build-manifest.json'
        frozen = read_json(frozen_file) if frozen_file.exists() else None
        binary_sha = digest(args.binary)
        if frozen and frozen['binary_sha256'] != binary_sha:
            raise RuntimeError('Frozen binary differs from its build manifest')
        world = World(fixture, args.binary, folder, args.population, options, args.scenario, args.instrumentation)
        write_json(folder / 'manifest.json', dict(binary_sha256=binary_sha, binary=str(args.binary.resolve()),
                   benchmark_script_sha256=SCRIPT_SHA256, build=frozen,
                   population=args.population, warmup=args.warmup, seconds=args.seconds, label=args.label,
                   restored_seed=bool(args.restore),
                   seed_manifest_sha256=digest(args.root / 'seed-population.json') if (args.root / 'seed-population.json').exists() else None,
                   scenario=args.scenario, options=options, host=inventory(),
                   instrumentation=args.instrumentation,
                   external_cpu_profile=bool(args.stack_profiler),
                   host_observed_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                   randomness='Native random engines are not seeded by this runner; independent repeats are required.',
                   module_source=frozen['module'] if frozen else source_identity(Path(__file__).resolve().parents[2]),
                   core_source=frozen['core'] if frozen else source_identity(args.core_source) if args.core_source else None))
        write_json(folder / 'runtime-binaries.json', {file.name: digest(file) for file in (folder / 'runtime').iterdir()
                   if file.suffix.lower() in ['.exe', '.dll']})
        write_json(folder / 'mysql-settings.json', dict(binary_sha256=digest(fixture.mysql_bin / 'mysqld.exe'),
                   settings=fixture.sql("SHOW VARIABLES WHERE Variable_name IN ('version','innodb_buffer_pool_size',"
                                        "'innodb_flush_log_at_trx_commit','sync_binlog','innodb_io_capacity',"
                                        "'innodb_io_capacity_max','innodb_read_io_threads','innodb_write_io_threads',"
                                        "'max_connections','transaction_isolation');")))
        public_configs = folder / 'effective-configs'
        public_configs.mkdir()
        for config_file in (folder / 'runtime/configs').rglob('*.conf'):
            destination = public_configs / config_file.relative_to(folder / 'runtime/configs')
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(sanitized_config(config_file.read_text(encoding='utf-8-sig')), encoding='utf-8')
        event(args.root, 'world_started', label=args.label, pid=world.proc.pid)
        world.await_ready()
        verify_native_features(folder, options)
        if args.instrumentation == 'reference':
            world.verify_reference_mode()
        if args.seed:
            count = int(fixture.sql("SELECT COUNT(*) FROM acore_characters.characters c JOIN acore_auth.account a ON a.id=c.account WHERE a.username LIKE '" + PREFIX + "%';"))
            if count < 3000:
                world.command('botcmd spawnleveled ' + str(3000 - count))
            until = time.monotonic() + 3600
            while count < 3000 and time.monotonic() < until:
                time.sleep(5)
                world.command('botcmd perf status')
                count = int(fixture.sql("SELECT COUNT(*) FROM acore_characters.characters c JOIN acore_auth.account a ON a.id=c.account WHERE a.username LIKE '" + PREFIX + "%';"))
                event(args.root, 'seeding', count=count)
            if count < 3000:
                raise RuntimeError('Seeding failed')
        login_seconds = 0 if args.seed else world.await_population(args.population)
        if not args.seed and args.instrumentation != 'reference' and (
                options.get('CoaBots.Spells.CacheLearnedRanks') or options.get('CoaBots.Scheduling.FarIdleIntervalMs') or
                (args.population and options.get('World.Performance.InterleaveWorldObjects'))):
            verify_native_schedule(world.dump_metrics(), options)
        event(args.root, 'population_ready', label=args.label, online=args.population, login_seconds=login_seconds)
        until = time.monotonic() + args.warmup
        while time.monotonic() < until:
            world.check_stop()
            world.command('botcmd perf status')
            time.sleep(min(10, max(.01, until - time.monotonic())))
        epoch = None if args.seed else world.reset_metrics()
        initial_metrics = None if args.seed else world.dump_metrics(epoch)
        if initial_metrics and args.scenario not in ['distributed', 'login', 'save']:
            until_ready = time.monotonic() + 30
            while initial_metrics['gauges'].get('benchmark_ready') != 1 and time.monotonic() < until_ready:
                time.sleep(1)
                initial_metrics = world.dump_metrics(epoch)
        if initial_metrics:
            if initial_metrics['epoch'] != epoch:
                raise RuntimeError('Metric epoch mismatch after reset')
            write_json(folder / 'measurement-initial.json', initial_metrics)
            if args.scenario not in ['distributed', 'login', 'save'] and initial_metrics['gauges'].get('benchmark_ready') != 1:
                raise RuntimeError('Scenario driver did not initialize')
        started = time.monotonic()
        next_sample = started
        if args.stack_profiler:
            from stack_profile import StackCapture
            cpu_profile = StackCapture(world.proc, world.binary, args.stack_profiler,
                                       folder / 'private-cpu/world.sleepy', args.stack_seconds,
                                       args.binary.parent)
        low_since = None
        last_metric_progress = started
        latest_metric = None
        samples = []
        with (folder / 'samples.jsonl').open('w', encoding='utf-8') as output:
            while time.monotonic() - started < args.seconds:
                world.check_stop()
                if world.proc.poll() is not None:
                    raise RuntimeError('World exited during measurement')
                row = dict(t=time.monotonic() - started, world=resources(world.proc.pid), mysql=resources(fixture.db_pid))
                row['world_threads'] = thread_resources(world.proc.pid)
                row['disk_free_bytes'] = shutil.disk_usage(args.root).free
                if row['disk_free_bytes'] < 5 * 1024 ** 3:
                    raise RuntimeError('Less than 5 GiB disk space remains for safe persistence')
                if row['world']['available_ram_bytes'] < row['world']['total_ram_bytes'] * .1:
                    low_since = low_since or time.monotonic()
                    if time.monotonic() - low_since > 30:
                        raise RuntimeError('Less than 10 percent RAM available for 30 seconds')
                else:
                    low_since = None
                samples.append(row)
                output.write(json.dumps(row) + '\n')
                output.flush()
                if len(samples) % 10 == 0:
                    world.command('botcmd perf status')
                    database_status = fixture.sql("SHOW GLOBAL STATUS WHERE Variable_name IN ('Queries','Questions','Threads_running','Threads_connected','Slow_queries','Innodb_buffer_pool_reads','Innodb_buffer_pool_read_requests','Innodb_row_lock_waits');")
                    with (folder / 'mysql-status.jsonl').open('a', encoding='utf-8') as db_output:
                        db_output.write(json.dumps(dict(t=row['t'], status=database_status)) + '\n')
                metrics = list((folder / 'metrics').glob('metrics-*.json'))
                if metrics:
                    newest = max(path.stat().st_mtime_ns for path in metrics)
                    if newest != latest_metric:
                        latest_metric = newest
                        last_metric_progress = time.monotonic()
                    if time.monotonic() - last_metric_progress > 30:
                        raise RuntimeError('No metric progress for 30 seconds')
                next_sample += 1
                if next_sample < time.monotonic() - 1:
                    next_sample = time.monotonic() + 1
                time.sleep(max(0, next_sample - time.monotonic()))
        metrics = None if args.seed else world.dump_metrics(epoch)
        if metrics:
            if metrics['epoch'] != epoch:
                raise RuntimeError('Metric epoch changed during measurement')
            write_json(folder / 'measurement-final.json', metrics)
        if cpu_profile:
            write_json(folder / 'cpu-profile.json', cpu_profile.finish(check=world.check_stop))
        world.command('botcmd worldstats')
        shutdown = world.stop()
        world = None
        if args.seed:
            characters = [line.split('\t') for line in fixture.sql(
                "SELECT c.guid,c.class,c.race,c.level,c.map,c.position_x,c.position_y,c.position_z,c.online "
                "FROM acore_characters.characters c JOIN acore_auth.account a ON a.id=c.account "
                "WHERE a.username LIKE '" + PREFIX + "%' ORDER BY c.guid;").splitlines()]
            if len(characters) != 3000 or any(int(row[8]) for row in characters):
                raise RuntimeError('Seed must contain exactly 3000 cleanly logged-out benchmark characters')
            write_json(folder / 'seed-population.json', dict(count=len(characters), characters=characters,
                       shutdown=shutdown))
        coverage = None
        if metrics:
            snapshots = []
            for path in (folder / 'metrics').glob('metrics-*.json'):
                snapshot = read_json(path)
                if snapshot['epoch'] == epoch and initial_metrics['elapsed_us'] <= snapshot['elapsed_us'] <= metrics['elapsed_us']:
                    snapshots.append(snapshot)
            coverage = scenario_coverage(args.scenario, snapshots)
            write_json(folder / 'scenario-coverage.json', coverage)
        if args.verify_persistence:
            verify_persistence(fixture, args.binary, folder, options)
        write_json(folder / 'summary.json', dict(status='measured', login_seconds=login_seconds, shutdown=shutdown,
                   scenario_coverage=coverage,
                   sample_count=len(samples), median_private_bytes=statistics.median(x['world']['private_bytes'] for x in samples),
                   histograms={key: {k: v for k, v in value.items() if k != 'bins'}
                               for key, value in metrics['histograms'].items()} if metrics else {}))
        event(args.root, 'run_complete', label=args.label, shutdown=shutdown)
        if shutdown['exit_code'] != 0 or shutdown['forced']:
            raise RuntimeError('Measured world did not complete a clean shutdown')
        if coverage and not coverage['passed']:
            raise RuntimeError('Scenario coverage insufficient; timing results are diagnostic only')
    except BaseException as exc:
        folder.mkdir(parents=True, exist_ok=True)
        write_json(folder / 'failure.json', dict(type=type(exc).__name__, message=str(exc)))
        event(args.root, 'run_failed', label=args.label, error=str(exc))
        raise
    finally:
        try:
            try:
                if cpu_profile and cpu_profile.proc and cpu_profile.proc.poll() is None:
                    cpu_profile.finish()
            finally:
                if world:
                    world.stop()
        finally:
            if world is None or world.proc.poll() is not None:
                fixture.stop_db()
            else:
                event(args.root, 'cleanup_requires_attention', label=args.label,
                      reason='World still alive; its database was preserved')
    if args.seed:
        if (args.root / 'seed-data').exists():
            raise RuntimeError('Seed snapshot already exists')
        shutil.copytree(args.root / 'data', args.root / 'seed-data')
        shutil.copy2(folder / 'seed-population.json', args.root / 'seed-population.json')
        event(args.root, 'seed_snapshot_saved')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--source', type=Path, required=True)
    prep.add_argument('--root', type=Path, required=True)
    case = commands.add_parser('run')
    case.add_argument('--root', type=Path, required=True)
    case.add_argument('--binary', type=Path, required=True)
    case.add_argument('--label', required=True)
    case.add_argument('--population', type=int, choices=[0, 500, 1000, 1500, 2000, 2500, 3000], required=True)
    case.add_argument('--warmup', type=int, default=600)
    case.add_argument('--seconds', type=int, default=900)
    case.add_argument('--options', type=Path)
    case.add_argument('--core-source', type=Path)
    case.add_argument('--scenario', choices=['distributed', 'observer', 'combat', 'dense-combat', 'cluster',
                                           'group-content', 'navigation', 'login', 'save'], default='distributed')
    case.add_argument('--restore', action='store_true')
    case.add_argument('--seed', action='store_true')
    case.add_argument('--verify-persistence', action='store_true')
    case.add_argument('--instrumentation', choices=['full', 'reference'], default='full')
    case.add_argument('--stack-profiler', type=Path)
    case.add_argument('--stack-seconds', type=int, default=60)
    case.add_argument('--wait-for-idle', type=int, default=0)
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args)
    else:
        if args.seconds < 1 or args.warmup < 0 or args.wait_for_idle < 0 or not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
            parser.error('Invalid run duration or label')
        if args.scenario == 'save':
            args.verify_persistence = True
        if args.instrumentation == 'reference' and (args.verify_persistence or args.seed or args.scenario != 'distributed'):
            parser.error('Reference instrumentation is restricted to distributed overhead runs without persistence readback')
        if args.stack_profiler and (args.instrumentation == 'reference' or args.stack_seconds < 1 or args.stack_seconds > args.seconds):
            parser.error('Stack profiles require full mode and a positive capture duration within the diagnostic measurement')
        run(args)


if __name__ == '__main__':
    main()
