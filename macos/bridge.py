"""Small read-only LAN status service. Python standard library only."""
import datetime as dt
import json
import math
from pathlib import Path
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATES = ('SLEEP', 'RUNNING', 'SUCCESS', 'FAILED')
RESULTS = ('SUCCESS', 'FAILED')
USAGE_TTL = 600
SCAN_INTERVAL = 30


def publish_identity(support):
    result = subprocess.run(['/usr/sbin/scutil', '--get', 'LocalHostName'],
                            capture_output=True, text=True, timeout=5, check=True)
    hostname = result.stdout.strip().lower()
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', hostname):
        raise ValueError('macOS LocalHostName must be a valid DNS label; check Sharing settings.')
    endpoint = hostname + '.local:8788'
    atomic_json(Path(support) / 'bridge-identity.json', {'endpoint': endpoint})
    return endpoint


def atomic_json(path, value):
    """Publish a complete file; callers own the parent directory."""
    import os
    import tempfile
    path = Path(path)
    fd, name = tempfile.mkstemp(prefix='.' + path.name, dir=str(path.parent))
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, allow_nan=False)
            stream.write('\n')
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def read_json(path):
    try:
        with Path(path).open() as stream:
            return json.loads(stream.read(4097))
    except (OSError, ValueError, UnicodeError):
        return None


def finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def timestamp(value):
    try:
        if not isinstance(value, str):
            return None
        parsed = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
        return parsed.timestamp() if parsed.tzinfo else None
    except (ValueError, OverflowError):
        return None


def reset_time(value):
    if not finite(value):
        return None
    try:
        return dt.datetime.fromtimestamp(value, dt.timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z')
    except (ValueError, OverflowError, OSError):
        return None


def usage_event(event):
    """Only recognize Codex token_count rate_limits; never derive quotas from tokens."""
    if not isinstance(event, dict) or event.get('type') != 'event_msg':
        return None
    payload = event.get('payload')
    if not isinstance(payload, dict) or payload.get('type') != 'token_count':
        return None
    limits = payload.get('rate_limits')
    observed = timestamp(event.get('timestamp'))
    if not isinstance(limits, dict) or observed is None:
        return None
    values = {}
    for name, minutes, field in [('primary', 300, '5h'), ('secondary', 10080, '7d')]:
        window = limits.get(name)
        # Other products/windows must not be mislabeled as Codex 5h/7d usage.
        if not isinstance(window, dict) or window.get('window_minutes') != minutes:
            values[field] = (None, None)
            continue
        used = window.get('used_percent')
        values[field] = (used if finite(used) and 0 <= used <= 100 else None,
                         reset_time(window.get('resets_at')))
    if all(value[0] is None for value in values.values()):
        return None
    return observed, values


class LocalSource:
    def __init__(self, support, codex_home, clock=time.time):
        self.support = Path(support)
        self.sessions = Path(codex_home) / 'sessions'
        self.clock = clock
        self.lock = threading.Lock()
        self.next_scan = 0
        self.usage = None

    def scan_usage(self):
        # Only session JSONL, never auth.json, config, databases or credentials.
        try:
            paths = sorted((p for p in self.sessions.glob('**/*.jsonl') if not p.is_symlink()),
                           key=lambda p: p.stat().st_mtime, reverse=True)[:20]
        except OSError:
            return
        latest = None
        for path in paths:
            try:
                with path.open('rb') as stream:
                    size = stream.seek(0, 2)
                    stream.seek(max(0, size - 262144))
                    if size > 262144:
                        stream.readline()  # Drop partial first line.
                    lines = stream.read(262144).splitlines()
                for line in lines:
                    try:
                        usage = usage_event(json.loads(line))
                    except (ValueError, UnicodeError):
                        continue
                    if usage and usage[0] <= self.clock() + 60 and (latest is None or usage[0] > latest[0]):
                        latest = usage
            except OSError:
                continue
        if latest and (self.usage is None or latest[0] >= self.usage[0]):
            self.usage = latest

    def snapshot(self):
        with self.lock:
            now = self.clock()
            if now >= self.next_scan:
                self.scan_usage()
                self.next_scan = now + SCAN_INTERVAL
            state = read_json(self.support / 'state.json')
            state = state if isinstance(state, dict) else {}
            current = state.get('state')
            expires = state.get('expires_at')
            if current not in STATES or not finite(expires) or now >= expires:
                current = 'SLEEP'
            last = state.get('last_result')
            reply = dict(state=current, last_result=last if last in RESULTS else None,
                         usage_5h_percent=None, usage_7d_percent=None,
                         usage_observed_at=None, usage_stale=True,
                         usage_5h_resets_at=None, usage_7d_resets_at=None)
            if self.usage:
                observed, values = self.usage
                reply['usage_observed_at'] = observed
                reply['usage_stale'] = now - observed > USAGE_TTL
                for field in ('5h', '7d'):
                    reply['usage_' + field + '_percent'] = values[field][0]
                    reply['usage_' + field + '_resets_at'] = values[field][1]
            return reply


def server(source, host='0.0.0.0', port=8788):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != '/status':
                self.send_error(404)
                return
            body = json.dumps(source.snapshot(), separators=(',', ':'), allow_nan=False).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass  # No client-supplied request contents in launchd logs.

    return ThreadingHTTPServer((host, port), Handler)
