"""Remote-console client for the combat-intelligence test runs.

Credentials come from a JSON file ({"user": ..., "password": ...}); they are never printed. The helper only sends console commands
and returns text - it makes no decisions about rotations.
"""
import json
import socket
import time


class Ra:
    def __init__(self, credentials_file, host='127.0.0.1', port=3469, timeout=120):
        c = json.load(open(credentials_file, encoding='utf-8'))
        self.s = socket.create_connection((host, port), timeout=30)
        self.s.settimeout(timeout)
        self._read(b'Username: ')
        self.s.sendall(c['user'].encode() + b'\r\n')
        self._read(b'Password: ')
        self.s.sendall(c['password'].encode() + b'\r\n')
        self._read(b'AC>')

    def _read(self, marker):
        out = b''
        while marker not in out:
            part = self.s.recv(65536)
            if not part:
                raise RuntimeError('remote console closed')
            out += part
        return out.decode(errors='replace')

    def run(self, command):
        self.s.sendall(command.encode() + b'\r\n')
        return self._read(b'AC>').replace('AC>', '').strip()

    def wait_until(self, command, predicate, every=5.0, limit=1800.0):
        """Repeat a console command until predicate(text) is true; raise after `limit` seconds."""
        started = time.time()
        while time.time() - started < limit:
            text = self.run(command)
            if predicate(text):
                return text
            time.sleep(every)
        raise TimeoutError(f'condition not met within {limit:.0f}s for: {command}')

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass
