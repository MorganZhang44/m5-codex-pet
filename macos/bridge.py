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
SCAN_INTERVAL = 2
READ_LIMIT = 262144
RUNNING_TTL = 900  # Inactivity lease, never evidence of failure.
RESULT_TTL = 30


def lifecycle_event(event):
    """Project only explicit persisted turn boundaries, never transcript content."""
    if not isinstance(event, dict) or event.get('type') != 'event_msg':
        return None
    payload = event.get('payload')
    observed = timestamp(event.get('timestamp'))
    if not isinstance(payload, dict) or observed is None:
        return None
    kind = payload.get('type')
    if kind not in ('task_started', 'task_complete', 'turn_aborted'):
        return None
    turn = payload.get('turn_id')
    if turn is not None and (not isinstance(turn, str) or not turn):
        return None
    state = {'task_started': 'RUNNING', 'task_complete': 'SUCCESS',
             'turn_aborted': 'SLEEP'}[kind]
    # Newer Codex versions persist a terminal ErrorEvent on task_complete.
    # Older versions may omit errors entirely; standalone error events are not
    # reliably terminal and must not manufacture a failed result.
    if kind == 'task_complete' and payload.get('error') is not None:
        error = payload['error']
        state = 'FAILED' if isinstance(error, dict) and isinstance(error.get('message'), str) else 'SLEEP'
    return observed, turn, state


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
        self.files = {}
        self.last_terminal = None
        self.last_result = None

    def scan_sessions(self):
        # Only session JSONL, never auth.json, config, databases or credentials.
        try:
            paths = []
            for path in self.sessions.glob('**/*.jsonl'):
                parents = (self.sessions / parent for parent in path.relative_to(self.sessions).parents)
                if not path.is_symlink() and not any(parent.is_symlink() for parent in parents):
                    paths.append(path)
            paths = sorted(paths, key=lambda p: p.stat().st_mtime, reverse=True)[:20]
        except OSError:
            return
        self.files = {p: value for p, value in self.files.items() if p in paths}
        latest = None
        for path in paths:
            try:
                with path.open('rb') as stream:
                    stat = path.stat()
                    size = stream.seek(0, 2)
                    cached = self.files.get(path)
                    if (cached is None or cached['identity'] != (stat.st_dev, stat.st_ino)
                            or size < cached['offset'] or size - cached['offset'] > READ_LIMIT):
                        cached = dict(identity=(stat.st_dev, stat.st_ino),
                                      offset=max(0, size - READ_LIMIT), active=None)
                        self.files[path] = cached
                    stream.seek(cached['offset'])
                    skip_first = cached['offset'] and cached.get('partial_start', True)
                    cached['partial_start'] = False
                    data = stream.read(READ_LIMIT)
                    # Leave an incomplete append for the next poll.
                    end = data.rfind(b'\n') + 1
                    cached['offset'] = stream.tell() - len(data) + end
                    lines = data[:end].splitlines()
                    if skip_first:
                        lines = lines[1:]  # Drop the bounded tail's first fragment.
                for line in lines:
                    try:
                        event = json.loads(line)
                    except (ValueError, UnicodeError):
                        continue
                    usage = usage_event(event)
                    if usage and usage[0] <= self.clock() + 60 and (latest is None or usage[0] > latest[0]):
                        latest = usage
                    boundary = lifecycle_event(event)
                    observed = timestamp(event.get('timestamp')) if isinstance(event, dict) else None
                    if observed is None or observed > self.clock() + 60:
                        continue
                    if boundary:
                        when, turn, state = boundary
                        active = cached['active']
                        if state == 'RUNNING':
                            cached['active'] = (when, turn)
                        elif active is None or turn is None or active[1] is None or turn == active[1]:
                            cached['active'] = None
                            terminal = (when, state)
                            if self.last_terminal is None or when >= self.last_terminal[0]:
                                self.last_terminal = terminal
                            if state in RESULTS and (self.last_result is None or when >= self.last_result[0]):
                                self.last_result = terminal
                    elif (cached['active'] and isinstance(event, dict)
                          and event.get('type') == 'event_msg'):
                        # Structured progress renews an already observed start;
                        # activity alone can never start a task.
                        when, turn = cached['active']
                        cached['active'] = (max(when, observed), turn)
            except OSError:
                continue
        if latest and (self.usage is None or latest[0] >= self.usage[0]):
            self.usage = latest

    def snapshot(self):
        with self.lock:
            now = self.clock()
            if now >= self.next_scan:
                self.scan_sessions()
                self.next_scan = now + SCAN_INTERVAL
            state = read_json(self.support / 'state.json')
            state = state if isinstance(state, dict) else {}
            current = state.get('state')
            expires = state.get('expires_at')
            explicit = current in STATES and finite(expires) and now < expires
            if not explicit:
                current = 'SLEEP'
                if any(value['active'] and now - value['active'][0] < RUNNING_TTL
                       for value in self.files.values()):
                    current = 'RUNNING'
                elif self.last_terminal and now - self.last_terminal[0] < RESULT_TTL:
                    current = self.last_terminal[1]
            last = state.get('last_result')
            updated = state.get('updated_at')
            if (self.last_result and not explicit and
                    (last not in RESULTS or not finite(updated) or self.last_result[0] >= updated)):
                last = self.last_result[1]
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
