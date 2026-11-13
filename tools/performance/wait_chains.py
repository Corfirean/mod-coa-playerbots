import ctypes
from ctypes import wintypes as w
import os


class StallWatch:
    def __init__(self):
        self.previous = None
        self.idle_since = None
        self.next_capture = 0
        self.captures = 0

    def update(self, elapsed, cpu_seconds):
        previous, self.previous = self.previous, (elapsed, cpu_seconds)
        if previous is None:
            return False
        duration = elapsed - previous[0]
        cpu = cpu_seconds - previous[1]
        if not 0 < duration <= 2.5 or not 0 <= cpu <= .05 * duration:
            self.idle_since = None
            return False
        if self.idle_since is None:
            self.idle_since = previous[0]
        if elapsed - self.idle_since < 2 or elapsed < self.next_capture or self.captures >= 6:
            return False
        self.captures += 1
        self.next_capture = elapsed + 30
        return True


class LockInfo(ctypes.Structure):
    _fields_ = [('name', w.WCHAR * 128), ('timeout', ctypes.c_longlong), ('alertable', w.BOOL)]


class ThreadInfo(ctypes.Structure):
    _fields_ = [('pid', w.DWORD), ('tid', w.DWORD), ('wait_time', w.DWORD), ('context_switches', w.DWORD)]


class NodeData(ctypes.Union):
    _fields_ = [('lock', LockInfo), ('thread', ThreadInfo)]


class Node(ctypes.Structure):
    _fields_ = [('kind', w.DWORD), ('status', w.DWORD), ('data', NodeData)]


KINDS = ['unknown', 'critical_section', 'send_message', 'mutex', 'alpc', 'com', 'thread_wait',
         'process_wait', 'thread', 'com_activation', 'unknown', 'socket_io', 'smb_io']
STATUSES = ['unknown', 'no_access', 'running', 'blocked', 'pid_only', 'pid_only_rpcss', 'owned',
            'not_owned', 'abandoned', 'unknown', 'error']


def capture(owned_pid, thread_ids):
    if os.name != 'nt' or owned_pid <= 0:
        raise ValueError('Wait-chain capture requires a valid owned Windows process')
    api = ctypes.WinDLL('advapi32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenThreadWaitChainSession.argtypes = [w.DWORD, ctypes.c_void_p]
    api.OpenThreadWaitChainSession.restype = w.HANDLE
    api.CloseThreadWaitChainSession.argtypes = [w.HANDLE]
    api.GetThreadWaitChain.argtypes = [w.HANDLE, ctypes.c_size_t, w.DWORD, w.DWORD,
                                     ctypes.POINTER(w.DWORD), ctypes.POINTER(Node), ctypes.POINTER(w.BOOL)]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.GetProcessIdOfThread.argtypes = [w.HANDLE]
    kernel.GetProcessIdOfThread.restype = w.DWORD
    kernel.CloseHandle.argtypes = [w.HANDLE]
    session = api.OpenThreadWaitChainSession(0, None)
    if not session:
        return dict(error=ctypes.get_last_error(), threads=[])
    rows = []
    try:
        for tid in list(dict.fromkeys(thread_ids))[:16]:
            row = dict(tid=tid, nodes=[])
            rows.append(row)
            handle = kernel.OpenThread(0x800, False, tid)
            if not handle:
                row['error'] = ctypes.get_last_error()
                continue
            try:
                if kernel.GetProcessIdOfThread(handle) != owned_pid:
                    row['error'] = 'thread_owner_changed'
                    continue
                count, cycle, nodes = w.DWORD(16), w.BOOL(), (Node * 16)()
                success = api.GetThreadWaitChain(session, 0, 0xc, tid, ctypes.byref(count), nodes, ctypes.byref(cycle))
                error = 0 if success else ctypes.get_last_error()
                row.update(error=error, cycle=bool(cycle.value), reported_nodes=count.value)
                if not success and error not in [234, 565]:
                    continue
                for node in nodes[:min(16, count.value)]:
                    value = dict(kind=KINDS[node.kind] if node.kind < len(KINDS) else str(node.kind),
                                 status=STATUSES[node.status] if node.status < len(STATUSES) else str(node.status))
                    if node.kind == 8:
                        info = node.data.thread
                        value.update(pid=info.pid, tid=info.tid, wait_time=info.wait_time,
                                     context_switches=info.context_switches)
                    else:
                        value['name'] = node.data.lock.name
                    row['nodes'].append(value)
            finally:
                kernel.CloseHandle(handle)
        return dict(threads=rows, limitations='Read-only WCT with existing privileges; unsupported waits may appear running.')
    finally:
        api.CloseThreadWaitChainSession(session)
