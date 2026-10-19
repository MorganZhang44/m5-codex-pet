"""Package tests: temporary homes, mocked launchd/pip/scutil, no privileged writes."""
import contextlib
import io
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'macos'))
import bridge
import codex_pet
import install

FIELDS = {'state', 'last_result', 'usage_5h_percent', 'usage_7d_percent',
          'usage_observed_at', 'usage_stale', 'usage_5h_resets_at', 'usage_7d_resets_at'}


def fake_provision(runtime):
    target = runtime / 'venv/bin/python3'
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        target.symlink_to(sys.executable)


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/private/tmp' if Path('/private/tmp').exists() else None)
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name) / "Mac user's $test home"
        self.paths = install.layout(self.home)

    def install(self, service=False):
        with patch.object(install.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, 'Test-Mac\n', '')):
            return install.install(self.home, ROOT, service, fake_provision)

    def test_layout_config_idempotence_and_scoped_uninstall(self):
        paths = self.install()
        with paths['plist'].open('rb') as stream:
            config = plistlib.load(stream)
        self.assertEqual(config['Label'], 'com.codex-pet.bridge')
        self.assertEqual(config['ProgramArguments'], [str(paths['runtime'] / 'venv/bin/python3'),
                                                     str(paths['runtime'] / 'codex_pet.py'), 'bridge'])
        self.assertEqual(config['EnvironmentVariables']['HOME'], str(self.home))
        self.assertTrue(config['RunAtLoad'] and config['KeepAlive'])
        self.assertEqual(json.loads((paths['support'] / 'bridge-identity.json').read_text()),
                         {'endpoint': 'test-mac.local:8788'})
        self.assertEqual({p.name for p in paths['runtime'].iterdir()},
                         {'venv', 'bridge.py', 'codex_pet.py', 'codex_pet_setup.py'})
        self.assertEqual((paths['runtime'] / 'codex_pet_setup.py').read_bytes(),
                         (ROOT / 'scripts/codex_pet_setup.py').read_bytes())
        # A shell path containing spaces, apostrophes and dollar signs executes correctly.
        result = subprocess.run([str(paths['cli']), 'setup', '--help'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--identity-file', result.stdout)
        self.install()
        unrelated = paths['support'] / 'user-notes.txt'
        unrelated.write_text('keep')
        (paths['support'] / 'state.json').write_text('{}')
        (paths['support'] / 'bridge.log').write_text('keep logs')
        self.assertTrue(install.uninstall(self.home, False))
        install.uninstall(self.home, False)
        self.assertFalse(paths['runtime'].exists())
        self.assertFalse(paths['cli'].exists())
        self.assertFalse(paths['plist'].exists())
        self.assertFalse((paths['support'] / 'bridge-identity.json').exists())
        self.assertEqual(unrelated.read_text(), 'keep')
        self.assertTrue((paths['support'] / 'state.json').exists())
        self.assertTrue((paths['support'] / 'bridge.log').exists())
        self.install()

    def test_launchd_dispatch(self):
        with patch.object(install, 'launchctl') as ctl, patch('socket.socket'):
            self.install(True)
            self.assertEqual([c.args[0] for c in ctl.call_args_list], ['enable', 'bootstrap'])
            ctl.reset_mock()
            ctl.return_value.returncode = 0
            self.install(True)
            self.assertEqual([c.args[0] for c in ctl.call_args_list], ['bootout', 'enable', 'bootstrap'])
            ctl.reset_mock()
            install.uninstall(self.home)
            self.assertEqual(ctl.call_args.args, ('bootout', 'gui/' + str(os.getuid()) + '/' + install.LABEL))

    def test_stop_failure_retains_runtime(self):
        self.install()
        with patch.object(install, 'launchctl', side_effect=[subprocess.CompletedProcess([], 1),
                                                           subprocess.CompletedProcess([], 0)]):
            with self.assertRaises(RuntimeError):
                install.uninstall(self.home)
        self.assertTrue(self.paths['runtime'].exists())

    def test_failed_provision_is_retryable(self):
        with patch.object(install, 'publish_identity'), self.assertRaises(RuntimeError):
            install.install(self.home, ROOT, False,
                            provisioner=Mock(side_effect=RuntimeError('dependency failure')))
        self.assertTrue(install.owned(self.paths))
        self.install()
        self.assertTrue(self.paths['cli'].exists())

    def test_dependency_provision_command(self):
        with patch.object(install.venv, 'EnvBuilder') as builder, patch.object(install.subprocess, 'run') as run:
            install.provision(self.paths['runtime'])
        builder.assert_called_once_with(with_pip=True)
        builder.return_value.create.assert_called_once_with(self.paths['runtime'] / 'venv')
        self.assertEqual(run.call_args.args[0], [str(self.paths['runtime'] / 'venv/bin/python3'),
                         '-m', 'pip', 'install', '--disable-pip-version-check', 'pyserial==3.5'])

    def test_no_unowned_collision_or_symlink_writes(self):
        self.paths['cli'].parent.mkdir(parents=True)
        self.paths['cli'].write_text('other tool')
        with self.assertRaises(RuntimeError):
            self.install()
        self.assertFalse(install.uninstall(self.home, False))
        self.assertEqual(self.paths['cli'].read_text(), 'other tool')
        self.paths['cli'].unlink()
        elsewhere = Path(self.temp.name) / 'elsewhere'
        elsewhere.mkdir()
        self.paths['support'].parent.mkdir(parents=True)
        self.paths['support'].symlink_to(elsewhere)
        with self.assertRaises(RuntimeError):
            self.install()
        with self.assertRaises(RuntimeError):
            install.uninstall(self.home, False)
        self.assertEqual(list(elsewhere.iterdir()), [])

    def test_replaced_owned_targets_are_retained(self):
        paths = self.install()
        paths['plist'].write_bytes(plistlib.dumps({'Label': 'unrelated'}))
        with self.assertRaises(RuntimeError):
            install.uninstall(self.home, False)
        with self.assertRaises(RuntimeError):
            self.install()
        self.assertTrue(paths['runtime'].exists())

    def test_identity_validation(self):
        self.paths['support'].mkdir(parents=True)
        for value in ('', '../unsafe', 'bad.local', '-bad', 'a' * 64):
            with patch.object(install.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, value, '')):
                with self.assertRaises(ValueError):
                    install.publish_identity(self.paths['support'])
        self.assertFalse((self.paths['support'] / 'bridge-identity.json').exists())

    def test_cli_setup_dispatch(self):
        with patch.object(codex_pet.subprocess, 'call', return_value=7) as run:
            self.assertEqual(codex_pet.main(['setup', '--port', '/dev/test', '--bridge', 'mac.local:8788']), 7)
            self.assertEqual(run.call_args.args[0], [sys.executable,
                             str(ROOT / 'macos/codex_pet_setup.py'), '--port', '/dev/test',
                             '--bridge', 'mac.local:8788'])

    def test_cli_state_lifecycle_and_corrupt_fallback(self):
        support = self.paths['support']
        support.mkdir(parents=True)
        with patch.object(codex_pet, 'support_path', return_value=support), patch.object(codex_pet.time, 'time', return_value=100):
            self.assertEqual(codex_pet.main(['state', 'FAILED', '--ttl', '20']), 0)
            source = bridge.LocalSource(support, self.home / '.codex', clock=lambda: 101)
            self.assertEqual((source.snapshot()['state'], source.snapshot()['last_result']), ('FAILED', 'FAILED'))
            codex_pet.main(['state', 'RUNNING', '--ttl', '1'])
            source.clock = lambda: 102
            self.assertEqual((source.snapshot()['state'], source.snapshot()['last_result']), ('SLEEP', 'FAILED'))
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                codex_pet.main(['state', 'RUNNING', '--ttl', '0'])
        (support / 'state.json').write_text('invalid')
        self.assertEqual(source.snapshot()['state'], 'SLEEP')
        self.assertIsNone(source.snapshot()['last_result'])

    def test_cli_case_insensitive_canonical_storage(self):
        self.paths['support'].mkdir(parents=True)
        with patch.object(codex_pet, 'support_path', return_value=self.paths['support']):
            for value in ('running', 'success', 'fAiLeD', 'sleep', 'RUNNING'):
                self.assertEqual(codex_pet.main(['state', value]), 0)
                stored = bridge.read_json(self.paths['support'] / 'state.json')
                self.assertEqual(stored['state'], value.upper())
                if value.upper() in bridge.RESULTS:
                    self.assertEqual(stored['last_result'], value.upper())
            before = (self.paths['support'] / 'state.json').read_bytes()
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                codex_pet.main(['state', 'unknown'])
            self.assertEqual((self.paths['support'] / 'state.json').read_bytes(), before)


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.now = 1760000000
        self.source = bridge.LocalSource(self.home, self.home / '.codex', clock=lambda: self.now)

    def event(self, used=42.5):
        return {'type': 'event_msg', 'timestamp': '2025-10-09T08:53:20Z',
                'payload': {'type': 'token_count', 'rate_limits': {
                    'primary': {'used_percent': used, 'window_minutes': 300, 'resets_at': 1760003600},
                    'secondary': {'used_percent': 68, 'window_minutes': 10080, 'resets_at': None}}}}

    def write_event(self, event):
        path = self.source.sessions / '2025/10/09/test.jsonl'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(event) + '\n')
        return path

    def test_empty_eight_field_contract(self):
        reply = self.source.snapshot()
        self.assertEqual(set(reply), FIELDS)
        self.assertEqual(reply['state'], 'SLEEP')
        self.assertTrue(reply['usage_stale'])
        for key in FIELDS - {'state', 'usage_stale'}:
            self.assertIsNone(reply[key])
        self.assertLess(len(json.dumps(reply)), 512)

    def test_local_usage_staleness_and_no_private_reads(self):
        self.write_event(self.event())
        # Neither file may be opened by the runtime, even if malformed/private.
        (self.home / '.codex/auth.json').write_text('do not read')
        (self.home / '.codex/config.toml').write_text('do not read')
        original = Path.open
        opened = []
        def record(path, *args, **kwargs):
            opened.append(path)
            return original(path, *args, **kwargs)
        with patch.object(Path, 'open', record):
            reply = self.source.snapshot()
        self.assertFalse(any(p.name in ('auth.json', 'config.toml') for p in opened))
        self.assertEqual(reply['usage_5h_percent'], 42.5)
        self.assertEqual(reply['usage_7d_percent'], 68)
        self.assertEqual(reply['usage_5h_resets_at'], '2025-10-09T09:53:20Z')
        self.assertIsNone(reply['usage_7d_resets_at'])
        self.assertFalse(reply['usage_stale'])
        self.now += 601
        self.assertTrue(self.source.snapshot()['usage_stale'])
        self.assertEqual(self.source.snapshot()['usage_5h_percent'], 42.5)

    def test_malformed_unknown_and_partial_events(self):
        for event in ([], {}, {'type': 'event_msg', 'payload': []}, self.event(float('nan'))):
            result = bridge.usage_event(event)
            if result:
                self.assertIsNone(result[1]['5h'][0])
        event = self.event(True)
        event['payload']['rate_limits']['secondary']['window_minutes'] = 60
        self.assertIsNone(bridge.usage_event(event))
        path = self.write_event(self.event())
        with path.open('a') as stream:
            stream.write('{partial log line')
        self.assertEqual(self.source.snapshot()['usage_5h_percent'], 42.5)

    def test_http_schema_and_no_mutation_routes(self):
        # Exercise the real HTTP parser/handler with an in-memory connection:
        # no socket permissions, launchd, or privileged ports needed by tests.
        with patch.object(bridge, 'ThreadingHTTPServer') as make_server:
            bridge.server(self.source)
            address, handler = make_server.call_args.args
        self.assertEqual(address, ('0.0.0.0', 8788))
        class Connection:
            def __init__(self, request):
                self.request, self.response = io.BytesIO(request), b''
            def makefile(self, *args):
                return self.request
            def sendall(self, data):
                self.response += data
        def request(method, path):
            connection = Connection(f'{method} {path} HTTP/1.0\r\nHost: localhost\r\n\r\n'.encode())
            handler(connection, ('127.0.0.1', 12345), Mock())
            return connection.response
        headers, body = request('GET', '/status').split(b'\r\n\r\n', 1)
        self.assertIn(b'200 OK', headers)
        self.assertIn(b'Content-Type: application/json', headers)
        self.assertEqual(set(json.loads(body)), FIELDS)
        for path in ('/', '/status?other=1', '/state'):
            self.assertIn(b'404 Not Found', request('GET', path))
        self.assertIn(b'501 Unsupported method', request('POST', '/status'))

    def lifecycle(self, kind, turn='one', **extra):
        return {'type': 'event_msg', 'timestamp': bridge.reset_time(self.now),
                'payload': dict(type=kind, turn_id=turn, **extra)}

    def append(self, path, event):
        with path.open('a') as stream:
            stream.write(json.dumps(event) + '\n')
        self.now += bridge.SCAN_INTERVAL

    def test_automatic_boundaries_progress_and_restart(self):
        path = self.write_event(self.lifecycle('task_started'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        # Start stays in memory even after it leaves the bounded tail.
        for _ in range(3):
            self.append(path, self.lifecycle('agent_message', message='x' * 100000))
            self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        self.append(path, self.lifecycle('task_complete'))
        self.assertEqual(self.source.snapshot()['state'], 'SUCCESS')
        restarted = bridge.LocalSource(self.home, self.home / '.codex', clock=lambda: self.now)
        self.assertEqual(restarted.snapshot()['last_result'], 'SUCCESS')
        self.now += bridge.RESULT_TTL
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.assertEqual(self.source.snapshot()['last_result'], 'SUCCESS')

    def test_cancellation_and_failure_are_not_guessed(self):
        path = self.write_event(self.lifecycle('task_started'))
        self.source.snapshot()
        for kind in ('error', 'stream_error', 'exec_command_end'):
            self.append(path, self.lifecycle(kind, message='failed', exit_code=1))
            self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        self.append(path, self.lifecycle('turn_aborted', reason='interrupted'))
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.assertIsNone(self.source.snapshot()['last_result'])
        self.append(path, self.lifecycle('task_started', turn='two'))
        self.append(path, self.lifecycle('task_complete', turn='two', error={'message': 'terminal error'}))
        self.assertEqual(self.source.snapshot()['state'], 'FAILED')
        self.assertEqual(self.source.snapshot()['last_result'], 'FAILED')

    def test_concurrent_turn_matching_and_inactivity_lease(self):
        first = self.write_event(self.lifecycle('task_started'))
        second = first.with_name('other.jsonl')
        second.write_text(json.dumps(self.lifecycle('task_started', turn='two')) + '\n')
        self.source.snapshot()
        self.append(first, self.lifecycle('task_complete'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        self.assertEqual(self.source.snapshot()['last_result'], 'SUCCESS')
        self.append(second, self.lifecycle('task_complete', turn='wrong'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        self.now += bridge.RUNNING_TTL
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.assertEqual(self.source.snapshot()['last_result'], 'SUCCESS')

    def test_partial_append_truncation_and_explicit_precedence(self):
        path = self.write_event(self.lifecycle('task_started'))
        self.source.snapshot()
        terminal = json.dumps(self.lifecycle('task_complete')).encode()
        with path.open('ab') as stream:
            stream.write(terminal[:30])
        self.now += bridge.SCAN_INTERVAL
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        with path.open('ab') as stream:
            stream.write(terminal[30:] + b'\n')
        self.now += bridge.SCAN_INTERVAL
        self.assertEqual(self.source.snapshot()['state'], 'SUCCESS')
        bridge.atomic_json(self.home / 'state.json', {'state': 'SLEEP', 'expires_at': self.now + 5})
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.now += 6
        self.assertEqual(self.source.snapshot()['state'], 'SUCCESS')
        path.write_text('{}\n')
        self.now += bridge.RESULT_TTL
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')

    def test_unknown_future_missing_and_symlink_signals(self):
        for event in ([], {}, self.lifecycle('turn.started'),
                      self.lifecycle('task_complete', error='bad'),
                      self.lifecycle('task_started', turn=[])):
            result = bridge.lifecycle_event(event)
            self.assertTrue(result is None or result[2] == 'SLEEP')
        future = self.lifecycle('task_started')
        future['timestamp'] = bridge.reset_time(self.now + 120)
        path = self.write_event(future)
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        path.unlink()
        private = self.home / 'private.jsonl'
        private.write_text(json.dumps(self.lifecycle('task_started')) + '\n')
        path.symlink_to(private)
        self.now += bridge.SCAN_INTERVAL
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        path.unlink()
        private.unlink()
        # Older boundaries without turn IDs still work within their session.
        self.write_event(self.lifecycle('task_started', turn=None))
        self.now += bridge.SCAN_INTERVAL
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')

    def test_bounded_tail_gap_and_eviction_fail_safe(self):
        path = self.write_event(self.lifecycle('task_started'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        # Missing data may include a terminal boundary: do not retain RUNNING.
        self.append(path, self.lifecycle('agent_message', message='x' * (bridge.READ_LIMIT + 10)))
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.append(path, self.lifecycle('token_count'))
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        # A new explicit start restores detection after the skipped region.
        self.append(path, self.lifecycle('task_started', turn='new'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        os.utime(path, (self.now - 1, self.now - 1))
        for index in range(20):
            other = path.with_name(str(index) + '.jsonl')
            other.write_text('{}\n')
            os.utime(other, (self.now + index, self.now + index))
        self.now += bridge.SCAN_INTERVAL
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.assertNotIn(path, self.source.files)

    def test_history_and_cancellation_across_turns(self):
        path = self.write_event(self.lifecycle('task_complete'))
        self.source.snapshot()
        self.append(path, self.lifecycle('task_started', turn='next'))
        self.assertEqual(self.source.snapshot()['state'], 'RUNNING')
        self.assertEqual(self.source.snapshot()['last_result'], 'SUCCESS')
        self.append(path, self.lifecycle('turn_aborted', turn='next'))
        self.assertEqual(self.source.snapshot()['state'], 'SLEEP')
        self.assertEqual(self.source.snapshot()['last_result'], 'SUCCESS')


if __name__ == '__main__':
    unittest.main()
