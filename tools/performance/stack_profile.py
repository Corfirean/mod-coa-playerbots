"""Optional user-mode stack capture, restricted to a process spawned by this controller."""
import argparse
import ctypes
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import time
import winreg
import zipfile


class StackCapture:
    def __init__(self, target, executable, profiler, output, seconds, symbols):
        self.target, self.output = target, Path(output)
        self.proc, self.desktop = None, None
        self.previous = {}
        self.closed = False
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        if not isinstance(target, subprocess.Popen) or target.poll() is not None:
            raise RuntimeError('Stack capture requires a live child process owned by this controller')
        self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.kernel.OpenProcess.restype = w.HANDLE
        self.kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        handle = self.kernel.OpenProcess(0x1000, False, target.pid)
        try:
            name, size = ctypes.create_unicode_buffer(32768), w.DWORD(32768)
            if not handle or not self.kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)):
                raise RuntimeError('Cannot verify the child executable before profiling')
            if not os.path.samefile(name.value, executable):
                raise RuntimeError('Child executable does not match the owned target')
        finally:
            if handle:
                self.kernel.CloseHandle(handle)
        if self.output.exists():
            raise RuntimeError('CPU profile output already exists')
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.registry_path = r'Software\codersnotes.com\Very Sleepy'
        self.settings = {'UseSymbolServer': (winreg.REG_DWORD, 0),
                         'SaveMinidump': (winreg.REG_DWORD, 0xffffffff),
                         'SymbolSearchPath': (winreg.REG_SZ, str(Path(symbols).resolve()))}
        self.user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, w.LPVOID, w.DWORD, w.DWORD, w.LPVOID]
        self.user.CreateDesktopW.restype = w.HANDLE
        self.user.CloseDesktop.argtypes = [w.HANDLE]
        desktop_name = 'CoaProfiler_' + str(os.getpid()) + '_' + str(time.monotonic_ns())
        self.desktop = self.user.CreateDesktopW(desktop_name, None, None, 0, 0x000f01ff, None)
        if not self.desktop:
            raise RuntimeError('Cannot create an inactive desktop for the profiler UI')
        try:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, self.registry_path, 0,
                                   winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as key:
                for name, (kind, value) in self.settings.items():
                    try:
                        self.previous[name] = winreg.QueryValueEx(key, name)
                    except FileNotFoundError:
                        self.previous[name] = None
                    winreg.SetValueEx(key, name, 0, kind, value)
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            startup.lpDesktop = 'winsta0\\' + desktop_name
            self.log = self.output.with_suffix('.profiler.log').open('wb')
            self.proc = subprocess.Popen([str(Path(profiler).resolve()), '/a', str(target.pid), '/t', str(seconds),
                                          '/o', str(self.output.resolve()), '/q'],
                                         cwd=Path(profiler).resolve().parent, startupinfo=startup,
                                         creationflags=subprocess.CREATE_NO_WINDOW,
                                         stdout=self.log, stderr=subprocess.STDOUT)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            if self.previous:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.registry_path, 0,
                                    winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as key:
                    for name, old in self.previous.items():
                        try:
                            current, kind = winreg.QueryValueEx(key, name)
                        except FileNotFoundError:
                            continue
                        expected_kind, expected_value = self.settings[name]
                        if current != expected_value or kind != expected_kind:
                            continue
                        if old is None:
                            winreg.DeleteValue(key, name)
                        else:
                            value, previous_kind = old
                            winreg.SetValueEx(key, name, 0, previous_kind, value)
        finally:
            if hasattr(self, 'log'):
                self.log.close()
            if self.desktop:
                self.user.CloseDesktop(self.desktop)
                self.desktop = None

    def finish(self, check=None, timeout=240):
        started = time.monotonic()
        try:
            while self.proc.poll() is None:
                if check:
                    check()
                if time.monotonic() - started > timeout:
                    raise RuntimeError('Stack profiler did not finish within its diagnostic time limit')
                time.sleep(1)
            if self.proc.returncode or not self.output.exists():
                raise RuntimeError('Stack profiler failed: exit ' + str(self.proc.returncode))
            with zipfile.ZipFile(self.output) as archive:
                if any(name.lower().endswith('.dmp') for name in archive.namelist()):
                    raise RuntimeError('Unexpected minidump in CPU capture; keep this file private')
                return dict(exit_code=self.proc.returncode, files=archive.namelist(),
                            stats=archive.read('Stats.txt').decode('utf-8-sig'))
        finally:
            if self.proc.poll() is not None:
                self.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--smoke', action='store_true', required=True)
    parser.add_argument('--profiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    args = parser.parse_args()
    target = subprocess.Popen([sys.executable, '-c',
                               'import time\nend=time.monotonic()+90\nx=1\nwhile time.monotonic()<end:\n x=(x*13+17)%10000019'],
                              creationflags=subprocess.CREATE_NO_WINDOW)
    capture = None
    try:
        capture = StackCapture(target, sys.executable, args.profiler, args.output, 3, args.symbols)
        print(capture.finish(timeout=60))
    finally:
        if capture and capture.proc and capture.proc.poll() is None:
            capture.proc.terminate()
            capture.proc.wait(timeout=10)
            capture.close()
        if target.poll() is None:
            target.terminate()
            target.wait(timeout=10)


if __name__ == '__main__':
    main()
