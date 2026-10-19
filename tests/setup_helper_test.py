"""No hardware, serial dependency, or real credentials needed."""
import contextlib
import importlib.util
import io
import unittest
import tempfile
import subprocess
from pathlib import Path
from unittest.mock import Mock, patch

import sys
sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('setup_helper', Path(__file__).resolve().parents[1] / 'scripts/codex_pet_setup.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class Port:
    def __init__(self, reject=False, silent=False, reject_command=None):
        self.commands = []
        self.response = b''
        self.reject, self.silent = reject, silent
        self.reject_command = reject_command

    def write(self, data):
        command = data.decode().strip()
        self.commands.append(command)
        name = command.split()[0]
        answers = {'HELP': 'OK HELP STATUS WIFI <ssid-hex> <password-hex-or-> BRIDGE <host:port> CLEAR-WIFI CANCEL SAVE REBOOT',
                   'STATUS': 'OK STATUS pending=no', 'WIFI': 'OK WIFI staged',
                   'BRIDGE': 'OK BRIDGE staged'}
        self.response = (('ERR rejected' if self.reject or name == self.reject_command else answers.get(name, 'OK ' + name)) + '\n').encode()

    def flush(self):
        pass

    def readline(self):
        if self.silent:
            return b''
        response, self.response = self.response, b''
        return response


class HelperTest(unittest.TestCase):
    def test_validation(self):
        self.assertEqual(helper.wifi_command('test net', ''), 'WIFI 74657374206e6574 -')
        self.assertEqual(helper.bridge_command('MAC.local:8788'), 'BRIDGE mac.local:8788')
        for ssid, password in [('', ''), ('a' * 33, ''), ('test', 'short'), ('test', 'x' * 64), ('a\0', '')]:
            with self.assertRaises(ValueError):
                helper.wifi_command(ssid, password)
        for value in ['auto', '', 'host:0', 'host:65536', 'host:080', 'http://host:80', '-bad:80', 'host:80\nSAVE']:
            with self.assertRaises(ValueError):
                helper.bridge_command(value)

    def test_success(self):
        port = Port()
        with patch.object(helper, 'local_identity', return_value=('Test Mac', 'mac.local:8788', 'package')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', return_value='y'), patch.object(helper.getpass, 'getpass', return_value='test-only-pass'), contextlib.redirect_stdout(io.StringIO()) as output:
            helper.configure(port)
        self.assertEqual([c.split()[0] for c in port.commands], ['HELP', 'CANCEL', 'HELP', 'CANCEL', 'WIFI', 'BRIDGE', 'SAVE', 'REBOOT'])
        self.assertIn('Test Mac', output.getvalue())
        self.assertIn('mac.local:8788', output.getvalue())
        self.assertIn('test net', output.getvalue())
        self.assertNotIn('test-only-pass', output.getvalue())
        self.assertNotIn('test-only-pass'.encode().hex(), output.getvalue())

    def test_failed_save_never_reboots(self):
        port = Port(reject_command='SAVE')
        with patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'package')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', return_value='y'), patch.object(helper.getpass, 'getpass', return_value='test-only-pass'), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(RuntimeError):
                helper.configure(port)
        self.assertIn('SAVE', port.commands)
        self.assertEqual(port.commands.count('SAVE'), 1)
        self.assertNotIn('REBOOT', port.commands)

    def test_ssid_fallback_and_confirmation_before_password(self):
        port = Port()
        events = []
        def answer(prompt):
            events.append('ssid' if 'SSID' in prompt else 'confirm')
            return 'test net' if 'SSID' in prompt else 'yes'
        def password(prompt):
            events.append('password')
            return 'test-only-pass'
        with patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'local')), patch.object(helper, 'current_wifi', return_value=None), patch('builtins.input', side_effect=answer), patch.object(helper.getpass, 'getpass', side_effect=password), contextlib.redirect_stdout(io.StringIO()):
            helper.configure(port)
        self.assertEqual(events, ['ssid', 'confirm', 'password', 'password'])
        self.assertIn('WIFI 74657374206e6574 746573742d6f6e6c792d70617373', port.commands)

    def test_cancel_or_password_mismatch_never_stages_credentials(self):
        for answers, passwords in [(['no'], []), (['yes'], ['test-pass-a', 'test-pass-b'])]:
            port = Port()
            with patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'local')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', side_effect=answers), patch.object(helper.getpass, 'getpass', side_effect=passwords), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises((RuntimeError, ValueError)):
                    helper.configure(port)
            self.assertEqual(port.commands, ['HELP', 'CANCEL'])

    def test_line_state_is_set_before_open(self):
        events = []
        class SerialPort:
            def __setattr__(self, name, value):
                events.append((name, value))
            def __enter__(self):
                events.append(('open', None))
                return self
            def __exit__(self, *args):
                events.append(('close', None))
        serial = Mock()
        serial.Serial.return_value = SerialPort()
        with helper.setup_port(serial, 'test-port'):
            pass
        serial.Serial.assert_called_once_with(port=None, baudrate=115200, timeout=0.25, write_timeout=3)
        self.assertEqual(events, [('dtr', False), ('rts', False), ('port', 'test-port'),
                                  ('open', None), ('close', None)])

    def test_open_reset_recovers_on_same_port(self):
        events, now = [], [0]
        class SerialPort(Port):
            def __enter__(self):
                events.append(('open', self.dtr, self.rts))
                self.in_setup = False  # Driver glitch despite idle line states.
                return self
            def __exit__(self, *args):
                events.append(('close',))
            def write(self, data):
                self.assert_safe_probe(data)
                super().write(data)
                if not self.in_setup:
                    self.response = b''
            def assert_safe_probe(self, data):
                if data not in (b'\nHELP\n', b'\nCANCEL\n'):
                    raise AssertionError('Only handshake probes during recovery')
            def readline(self):
                now[0] += 0.25
                if now[0] >= 3:
                    self.in_setup = True  # User completes physical BtnB hold.
                return super().readline()
        serial = Mock()
        serial.Serial.return_value = port = SerialPort()
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), contextlib.redirect_stdout(io.StringIO()) as output:
            with helper.setup_port(serial, 'test-port') as opened:
                helper.connect_setup(opened)
        self.assertEqual(events, [('open', False, False), ('close',)])
        self.assertEqual(port.commands, ['HELP', 'HELP', 'HELP', 'CANCEL'])
        self.assertIn('Hold BtnB', output.getvalue())

    def test_already_in_setup_connects_without_reset_or_delay(self):
        port = Port()
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.connect_setup(port)
        self.assertEqual(port.commands, ['HELP', 'CANCEL'])
        self.assertEqual(output.getvalue(), '')

    def test_help_at_deadline_does_not_send_cancel_or_credentials(self):
        port, now = Port(), [0]
        read = port.readline
        def final_read():
            now[0] = 5
            return read()
        port.readline = final_read
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(helper.AcknowledgementTimeout):
                helper.connect_setup(port, timeout=5)
        self.assertEqual(port.commands, ['HELP'])

    def test_handshake_retries_boot_and_cancel_loss(self):
        # Model an open-induced normal boot, a later physical BtnB entry,
        # then a reset losing CANCEL. Only HELP/CANCEL may be retried.
        port = Port()
        calls = []
        now = [0]
        write = port.write
        def boot_write(data):
            command = data.decode().strip()
            calls.append(command)
            # Lost probes while booting, and loss of setup after first HELP.
            if len(calls) in (1, 3):
                port.response = b''
                return
            write(data)
        read = port.readline
        def boot_read():
            now[0] += 0.25
            return read()
        port.write, port.readline = boot_write, boot_read
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), contextlib.redirect_stdout(io.StringIO()) as output:
            helper.connect_setup(port)
        self.assertEqual(calls, ['HELP', 'HELP', 'CANCEL', 'HELP', 'CANCEL'])
        self.assertIn('Hold BtnB', output.getvalue())
        self.assertEqual(output.getvalue().count('Hold BtnB'), 1)

    def test_handshake_timeout_precedes_identity_and_password(self):
        with patch.object(helper, 'connect_setup', side_effect=helper.AcknowledgementTimeout('timeout')), patch.object(helper, 'local_identity') as identity, patch.object(helper, 'secure_password') as password:
            with self.assertRaises(helper.AcknowledgementTimeout):
                helper.configure(Port())
            identity.assert_not_called()
            password.assert_not_called()

    def test_reset_during_password_entry_is_recovered_before_wifi(self):
        port = Port()
        now = [0]
        resetting = [False]
        original_write, original_read = port.write, port.readline
        def password(prompt):
            resetting[0] = True
            return 'test-only-pass'
        def write(data):
            # A reset at the prompt loses the first new probe. Recovery must
            # use HELP, not transmit or replay credential-bearing commands.
            if resetting[0]:
                self.assertEqual(data, b'\nHELP\n')
                port.commands.append('HELP')
                port.response = b''
                resetting[0] = False
            else:
                original_write(data)
        def read():
            now[0] += 0.25
            return original_read()
        port.write, port.readline = write, read
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'local')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', return_value='yes'), patch.object(helper, 'secure_password', side_effect=password), contextlib.redirect_stdout(io.StringIO()):
            helper.configure(port)
        self.assertEqual([c.split()[0] for c in port.commands],
                         ['HELP', 'CANCEL', 'HELP', 'HELP', 'CANCEL', 'WIFI', 'BRIDGE', 'SAVE', 'REBOOT'])

    def test_lost_readiness_after_password_never_sends_credentials(self):
        port = Port()
        with patch.object(helper, 'connect_setup', side_effect=[None, helper.AcknowledgementTimeout('timeout')]) as connect, patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'local')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', return_value='yes'), patch.object(helper, 'secure_password', return_value='test-only-pass') as password, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(helper.AcknowledgementTimeout):
                helper.configure(port)
        self.assertEqual(connect.call_count, 2)
        self.assertEqual(password.call_count, 2)
        self.assertEqual(port.commands, [])

    def test_wifi_ack_loss_never_retries_or_saves_and_error_has_no_secret(self):
        port = Port()
        original_write, original_read = port.write, port.readline
        now = [0]
        def write(data):
            original_write(data)
            if data.startswith(b'WIFI '):
                port.response = b''
        def read():
            now[0] += 0.25
            return original_read()
        port.write, port.readline = write, read
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), patch.object(helper, 'local_identity', return_value=('Mac', 'mac.local:8788', 'local')), patch.object(helper, 'current_wifi', return_value='test net'), patch('builtins.input', return_value='yes'), patch.object(helper, 'secure_password', return_value='test-only-pass'), contextlib.redirect_stdout(io.StringIO()) as output:
            with self.assertRaises(helper.AcknowledgementTimeout) as error:
                helper.configure(port)
        self.assertEqual([c.split()[0] for c in port.commands], ['HELP', 'CANCEL', 'HELP', 'CANCEL', 'WIFI'])
        self.assertIn('for WIFI', str(error.exception))
        for secret in ('test-only-pass', 'test-only-pass'.encode().hex()):
            self.assertNotIn(secret, str(error.exception) + output.getvalue())

    def test_handshake_window_is_bounded(self):
        port = Port(silent=True)
        now = [0]
        def read():
            now[0] += 0.25
            return b''
        port.readline = read
        with patch.object(helper.time, 'monotonic', side_effect=lambda: now[0]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(helper.AcknowledgementTimeout):
                helper.connect_setup(port, timeout=5)
        self.assertEqual(now[0], 5)
        self.assertEqual(port.commands, ['HELP', 'HELP', 'HELP'])

    def test_handshake_ignores_stale_errors_and_fragmented_lines(self):
        port = Port()
        replies = [b'private unexpected data\n', b'ERR invalid-line\n',
                   b'OK READY codex-pet-usb-v1; use HELP\n',
                   helper.HELP_ACK[:20].encode(), b'', helper.HELP_ACK[20:].encode() + b'\r\n',
                   (helper.HELP_ACK + '\n').encode(), b'ERR unknown-command\n', b'OK CAN', b'CEL\n']
        port.readline = lambda: replies.pop(0) if replies else b''
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.connect_setup(port)
        self.assertEqual(port.commands, ['HELP', 'CANCEL'])
        self.assertEqual(output.getvalue(), '')

    def test_no_insecure_password_fallback(self):
        def insecure(prompt):
            import warnings
            warnings.warn('no tty', helper.getpass.GetPassWarning)
            self.fail('must stop before reading echoed stdin')
        with patch.object(helper.getpass, 'getpass', side_effect=insecure):
            with self.assertRaises(RuntimeError):
                helper.secure_password('Password: ')

    def test_macos_wifi_detection(self):
        hardware = 'Hardware Port: Ethernet\nDevice: en0\n\nHardware Port: Wi-Fi\nDevice: en1'
        with patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', side_effect=[hardware, 'Current Wi-Fi Network: test net ']) as command:
            self.assertEqual(helper.current_wifi(), 'test net ')
            self.assertEqual(command.call_args.args, ('/usr/sbin/networksetup', '-getairportnetwork', 'en1'))
        for response in ['You are not associated with an AirPort network.', 'Current Wi-Fi Network: <redacted>', '']:
            with patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', side_effect=[hardware, response]):
                self.assertIsNone(helper.current_wifi())
        with patch.object(helper.platform, 'system', return_value='Linux'):
            self.assertIsNone(helper.current_wifi())

    def test_local_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            identity = Path(directory) / 'bridge-identity.json'
            identity.write_text('{"endpoint":"Test-Mac.local:9123"}')
            with patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', return_value='Test Mac'):
                computer, endpoint, source = helper.local_identity(identity)
                self.assertEqual((computer, endpoint), ('Test Mac', 'test-mac.local:9123'))
                self.assertIn('package', source)
            identity.write_text('{"endpoint":"192.0.2.5:9123"}')
            with patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', side_effect=['Test Mac', 'stable-mac']):
                self.assertEqual(helper.local_identity(identity)[1], 'stable-mac.local:9123')
            identity.write_text('invalid-json')
            with patch.object(helper.platform, 'system', return_value='Linux'), patch.object(helper.socket, 'gethostname', return_value='test-host'):
                self.assertEqual(helper.local_identity(identity, 'diagnostic.local:8788')[1], 'diagnostic.local:8788')
            for content in ['{}', '{"endpoint":"auto"}', '{"endpoint":123}', '[]', 'x' * 4097]:
                identity.write_text(content)
                with patch.object(helper.platform, 'system', return_value='Linux'), patch.object(helper.socket, 'gethostname', return_value='test-host'), self.assertRaises(ValueError):
                    helper.local_identity(identity)
        with patch.object(helper.Path, 'is_file', return_value=False), patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', side_effect=['Test Mac', 'test-mac']):
            self.assertEqual(helper.local_identity()[1], 'test-mac.local:8788')
        with patch.object(helper.Path, 'is_file', return_value=False), patch.object(helper.platform, 'system', return_value='Darwin'), patch.object(helper, 'local_command', return_value=''):
            with self.assertRaises(ValueError):
                helper.local_identity()

    def test_public_commands_only_and_bounded_timeout(self):
        with patch.object(helper.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, 'test net \n', '')) as run:
            self.assertEqual(helper.local_command('public-command'), 'test net ')
            self.assertEqual(run.call_args.kwargs['timeout'], 5)
        with patch.object(helper.subprocess, 'run', side_effect=OSError):
            self.assertEqual(helper.local_command('missing'), '')

    def test_failure_never_reboots(self):
        for port in [Port(reject=True), Port(silent=True)]:
            with self.assertRaises(RuntimeError):
                helper.exchange(port, 'SAVE', 'OK SAVE', timeout=0.01)
            self.assertNotIn('REBOOT', port.commands)

    def test_wrong_ack_is_failure(self):
        with self.assertRaises(RuntimeError):
            helper.exchange(Port(), 'SAVE', 'OK REBOOT', timeout=0.01)


if __name__ == '__main__':
    unittest.main()
