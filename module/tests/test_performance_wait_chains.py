import ctypes
from ctypes import wintypes as w
import importlib.util
import os
from pathlib import Path
import threading
import time
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/performance/wait_chains.py'
SPEC = importlib.util.spec_from_file_location('coa_wait_chains', SCRIPT)
waits = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(waits)


class StallWatchTests(unittest.TestCase):
    def test_requires_sustained_low_cpu_and_limits_captures(self):
        watch = waits.StallWatch()
        self.assertFalse(watch.update(0, 20))
        self.assertFalse(watch.update(1, 20))
        self.assertTrue(watch.update(2, 20))
        self.assertFalse(watch.update(3, 20))
        for elapsed in range(4, 250):
            watch.update(elapsed, 20)
        self.assertEqual(watch.captures, 6)

    def test_sampling_gaps_or_active_cpu_reset_the_idle_window(self):
        watch = waits.StallWatch()
        for elapsed, cpu in [(0, 1), (1, 1), (2, 2), (3, 2), (8, 2), (9, 2)]:
            self.assertFalse(watch.update(elapsed, cpu))
        self.assertTrue(watch.update(10, 2))


@unittest.skipUnless(os.name == 'nt', 'Windows wait-chain API')
class WaitChainTests(unittest.TestCase):
    def test_native_abi_and_ownership_guard(self):
        self.assertEqual(ctypes.sizeof(waits.Node), 280)
        self.assertEqual(waits.Node.data.offset, 8)
        kernel = ctypes.WinDLL('kernel32')
        tid = kernel.GetCurrentThreadId()
        result = waits.capture(os.getpid() + 1, [tid, tid])
        self.assertEqual(len(result['threads']), 1)
        self.assertEqual(result['threads'][0]['error'], 'thread_owner_changed')
        self.assertEqual(result['threads'][0]['nodes'], [])

    def test_observes_an_owned_blocked_mutex_without_changing_privileges(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateMutexW.argtypes = [w.LPVOID, w.BOOL, w.LPCWSTR]
        kernel.CreateMutexW.restype = w.HANDLE
        kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        kernel.ReleaseMutex.argtypes = [w.HANDLE]
        kernel.CloseHandle.argtypes = [w.HANDLE]
        mutex = kernel.CreateMutexW(None, True, None)
        self.assertTrue(mutex)
        ready = threading.Event()

        def worker():
            ready.set()
            if kernel.WaitForSingleObject(mutex, 5000) == 0:
                kernel.ReleaseMutex(mutex)

        thread = threading.Thread(target=worker)
        thread.start()
        try:
            self.assertTrue(ready.wait(2))
            deadline = time.monotonic() + 2
            while True:
                result = waits.capture(os.getpid(), [thread.native_id])
                if result.get('error') in [5, 50]:
                    self.skipTest('WCT is unavailable with current privileges')
                row = result['threads'][0]
                if row.get('error') in [5, 50]:
                    self.skipTest('WCT is unavailable with current privileges')
                if any(node['kind'] == 'mutex' for node in row['nodes']) or time.monotonic() >= deadline:
                    break
                time.sleep(.01)
            self.assertEqual(row['error'], 0)
            self.assertTrue(any(node['kind'] == 'mutex' for node in row['nodes']))
            self.assertEqual(row['nodes'][0]['pid'], os.getpid())
        finally:
            kernel.ReleaseMutex(mutex)
            thread.join(6)
            kernel.CloseHandle(mutex)
        self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
