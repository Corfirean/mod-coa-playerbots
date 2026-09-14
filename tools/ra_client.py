import socket
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HOST = "127.0.0.1"
PORT = 3443
USER = "local"
PASS = "local"


def recv_all(sock, timeout=1.5):
    sock.settimeout(timeout)
    chunks = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except socket.timeout:
        pass
    return b"".join(chunks).decode("utf-8", errors="replace")


def connect():
    # AzerothCore's RA listener refuses a new login for a few seconds after a
    # recent connection from the same address (observed ~15-20s backoff empirically,
    # not documented) -- reconnecting faster than that gets "Authentication failed"
    # even with the right credentials. Retry with backoff instead of failing outright.
    last_exc = None
    for attempt, delay in enumerate([0, 5, 10, 20]):
        if delay:
            time.sleep(delay)
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            # RASession::Start() (RASession.cpp) waits up to 1.0s (10x100ms) checking for
            # unsolicited bytes it would treat as a telnet negotiation subsequence, before
            # ever sending "Authentication Required\r\nUsername: ". A first-recv timeout
            # close to that same 1.0s is a real race: if our recv() times out (empty) and
            # we send the username microseconds before the server's own prompt finally
            # goes out, the server treats our username bytes AS negotiation data, eats
            # them, and authentication silently desyncs from there (observed live: a
            # delayed "Authentication failed" shows up attached to the first real command
            # instead of the login banner). Comfortably outlasting that 1.0s window avoids
            # ever racing it.
            recv_all(s, 2.5)  # username prompt
            s.sendall((USER + "\r\n").encode())
            recv_all(s, 1.0)  # password prompt
            s.sendall((PASS + "\r\n").encode())
            banner = recv_all(s, 1.5)
            if "failed" in banner.lower():
                s.close()
                last_exc = RuntimeError(banner.strip())
                continue
            return s
        except (ConnectionAbortedError, ConnectionResetError, OSError) as exc:
            last_exc = exc
    raise RuntimeError(f"RA login failed after retries: {last_exc}")


def main():
    commands = sys.argv[1:]
    if not commands:
        print("usage: ra_client.py <command> [more commands...]")
        return

    s = connect()

    for cmd in commands:
        s.sendall((cmd + "\r\n").encode())
        time.sleep(0.3)
        out = recv_all(s, 2.0)
        print(f"--- {cmd} ---")
        print(out)

    s.close()


if __name__ == "__main__":
    main()
