#!/usr/bin/env python3
"""Interactive USB setup. Credentials stay in memory, never argv/files/logs."""
import argparse
import getpass
import json
import ipaddress
import platform
from pathlib import Path
import re
import socket
import subprocess
import warnings
import sys
import time


HELP_ACK = 'OK HELP STATUS WIFI <ssid-hex> <password-hex-or-> BRIDGE <host:port> CLEAR-WIFI CANCEL SAVE REBOOT'


class AcknowledgementTimeout(RuntimeError):
    pass


def discover_port(list_ports, explicit=None):
    if explicit:
        return explicit
    ports = [p for p in list_ports.comports()
             if p.vid in (0x303A, 0x10C4, 0x1A86, 0x0403)
             or 'usbmodem' in p.device or 'ttyACM' in p.device]
    if not ports:
        raise RuntimeError('No USB serial device found; connect a data USB cable or use --port.')
    if len(ports) == 1:
        return ports[0].device
    for index, port in enumerate(ports, 1):
        print(f'{index}: {port.device} ({port.description})')
    try:
        index = int(input('Device number: '))
        if 1 <= index <= len(ports):
            return ports[index - 1].device
    except ValueError:
        pass
    raise RuntimeError('Invalid device number.')


def exchange(port, command, expected, timeout=5, handshake=False):
    # Do not send a new probe if a previous handshake used the remaining window.
    if timeout <= 0:
        raise AcknowledgementTimeout('USB setup handshake deadline expired.')
    port.write((('\n' if handshake else '') + command + '\n').encode('ascii'))
    port.flush()
    deadline = time.monotonic() + timeout
    pending = b''
    while time.monotonic() < deadline:
        # readline() may return a partial line on a serial read timeout.
        pending += port.readline()
        if not pending.endswith(b'\n'):
            pending = pending[-512:]
            continue
        response, pending = pending, b''
        # Never echo unexpected data (including another firmware's credential logs).
        if response.rstrip(b'\r\n') == expected.encode('ascii'):
            return
        if command == 'STATUS' and response.rstrip(b'\r\n') in (
                b'INFO wifi-saved=yes', b'INFO wifi-saved=no',
                b'INFO wifi-connected=yes', b'INFO wifi-connected=no'):
            print(response.decode('ascii').strip())
        if response.startswith(b'ERR '):
            if handshake:
                continue  # Discard a previous session's incomplete line/error.
            if response.startswith(b'ERR SAVE '):
                raise RuntimeError('Pairing save or legacy-key cleanup failed. Re-run setup with the intended settings. Device has not rebooted.')
            raise RuntimeError('Device rejected the request; changes may be pending. Re-run setup or use CANCEL.')
    # Report only the operation, never its arguments (WIFI carries secrets).
    operation = command.partition(' ')[0]
    raise AcknowledgementTimeout(f'No expected acknowledgement for {operation}. Hold BtnB for 2s until USB SETUP; close serial monitors and retry.')


def connect_setup(port, timeout=45):
    """Retry only the non-persistent handshake, keeping the serial port open.

    Opening can reset an already prepared board. Physical BtnB entry is still
    required after a normal boot; HELP never enables setup or reads credentials.
    """
    deadline = time.monotonic() + timeout
    prompted = False
    while time.monotonic() < deadline:
        try:
            exchange(port, 'HELP', HELP_ACK,
                     timeout=min(2, max(0, deadline - time.monotonic())), handshake=True)
            exchange(port, 'CANCEL', 'OK CANCEL',
                     timeout=min(2, max(0, deadline - time.monotonic())), handshake=True)
            return
        except AcknowledgementTimeout:
            if not prompted:
                print('Waiting for USB SETUP. Opening USB may have reset the board. '
                      'Hold BtnB for 2 seconds now; keep this helper running (45s connection window).')
                prompted = True
    raise AcknowledgementTimeout('USB setup handshake timed out. Verify USB SETUP, the port and data cable; close serial monitors and retry.')


def setup_port(serial, device):
    # Set idle line state BEFORE open. StickS3 uses hardware USB Serial/JTAG,
    # where DTR/RTS control reset/download rather than announcing a CDC reader.
    # Drivers may still glitch on open, so connect_setup tolerates a reset.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = device
    return port


def wifi_command(ssid, password):
    ssid_bytes, password_bytes = ssid.encode('utf-8'), password.encode('utf-8')
    if not 1 <= len(ssid_bytes) <= 32 or b'\0' in ssid_bytes:
        raise ValueError('SSID must be 1–32 UTF-8 bytes, without NUL.')
    if len(password_bytes) not in (0,) and not 8 <= len(password_bytes) <= 63:
        raise ValueError('Password must be empty or 8–63 UTF-8 bytes.')
    if b'\0' in password_bytes:
        raise ValueError('Password cannot contain NUL.')
    return f'WIFI {ssid_bytes.hex()} {password_bytes.hex() or "-"}'


def bridge_command(value):
    value = value.lower()
    host, separator, port = value.rpartition(':')
    labels = host.split('.')
    if (not separator or not host or len(host) > 253 or
            any(not label or len(label) > 63 or label.startswith('-') or label.endswith('-')
                or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in label)
                for label in labels) or not port.isascii() or not port.isdigit() or
            port.startswith('0') or not 1 <= int(port) <= 65535):
        raise ValueError('Bridge must be DNS hostname/IPv4:port (1–65535).')
    return 'BRIDGE ' + value


def local_command(*args):
    """Read public network identity only; never query passwords or Keychain."""
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=5, check=False)
        return result.stdout.rstrip('\r\n') if result.returncode == 0 else ''
    except (OSError, subprocess.TimeoutExpired):
        return ''


def current_wifi():
    if platform.system() != 'Darwin':
        return None
    hardware = local_command('/usr/sbin/networksetup', '-listallhardwareports')
    devices = re.findall(r'Hardware Port: (?:Wi-Fi|AirPort)\nDevice: ([^\n]+)', hardware)
    for device in devices:
        value = local_command('/usr/sbin/networksetup', '-getairportnetwork', device)
        prefix = 'Current Wi-Fi Network: '
        if value.startswith(prefix):
            ssid = value[len(prefix):]
            if ssid and ssid.lower() not in ('<redacted>', 'redacted', '<unknown>', 'unknown'):
                return ssid
    return None


def local_identity(identity_file=None, bridge=None):
    """Package contract: a dedicated public identity file, never a credentials file."""
    computer = (local_command('/usr/sbin/scutil', '--get', 'ComputerName')
                if platform.system() == 'Darwin' else '') or socket.gethostname()
    path = (Path(identity_file) if identity_file else
            Path.home() / 'Library/Application Support/Codex Pet/bridge-identity.json')
    endpoint = None
    source = 'macOS local hostname; assumes bridge port 8788'
    if not bridge and (identity_file or path.is_file()):
        try:
            # Bounded and allowlisted: do not print file contents or parse other package settings.
            with path.open(encoding='utf-8') as stream:
                raw = stream.read(4097)
            if len(raw) > 4096:
                raise ValueError()
            data = json.loads(raw)
            endpoint = data['endpoint']
            if not isinstance(endpoint, str):
                raise ValueError()
            endpoint = bridge_command(endpoint)[7:]
            source = 'local bridge package identity'
            host, _, port = endpoint.rpartition(':')
            try:
                ipaddress.IPv4Address(host)
                address_only = True
            except ipaddress.AddressValueError:
                address_only = host == 'localhost'
            if address_only and platform.system() == 'Darwin':
                hostname = local_command('/usr/sbin/scutil', '--get', 'LocalHostName')
                if hostname:
                    endpoint = hostname + '.local:' + port
                    source += '; stable macOS hostname'

        except (OSError, ValueError, KeyError, TypeError):
            raise ValueError('Invalid public bridge identity file; expected {"endpoint":"computer.local:8788"}.') from None
    if bridge:
        endpoint, source = bridge, 'explicit diagnostic override'
    if not endpoint and platform.system() == 'Darwin':
        hostname = local_command('/usr/sbin/scutil', '--get', 'LocalHostName')
        if hostname:
            endpoint = hostname + '.local:8788'
    if not endpoint:
        raise ValueError('Local bridge identity unavailable. Install the bridge identity file or use --bridge host:port for diagnostics.')
    endpoint = bridge_command(endpoint)[7:]
    return computer, endpoint, source


def secure_password(prompt):
    # getpass otherwise falls back to echoed stdin when no secure terminal exists.
    with warnings.catch_warnings():
        warnings.simplefilter('error', getpass.GetPassWarning)
        try:
            return getpass.getpass(prompt)
        except getpass.GetPassWarning:
            raise RuntimeError('A secure terminal is required to enter the Wi-Fi password.') from None


def configure(port, identity_file=None, bridge=None):
    connect_setup(port)
    computer, endpoint, source = local_identity(identity_file, bridge)
    ssid = current_wifi()
    if ssid is None:
        print('Current Wi-Fi SSID unavailable (macOS may restrict access).')
        ssid = input('Current computer Wi-Fi SSID (2.4 GHz network required by Pet): ')
    wifi_command(ssid, '')  # Validate before confirmation or asking for a password.
    print(f'Computer: {computer!r}')
    print(f'Bridge: {endpoint} ({source})')
    print(f'Current Wi-Fi: {ssid!r}')
    print('This replaces the single current pairing. Verify this computer runs the bridge and Wi-Fi is correct.')
    if input('Pair Codex Pet with this computer and Wi-Fi? [y/N]: ').strip().lower() not in ('y', 'yes'):
        raise RuntimeError('Pairing cancelled; nothing written.')
    password = secure_password('Wi-Fi password (blank = open network): ')
    if password != secure_password('Repeat password: '):
        raise ValueError('Passwords do not match; nothing written.')
    wifi = wifi_command(ssid, password)
    # Interactive prompts can take arbitrarily long. Revalidate the live link
    # and discard stale staging before transmitting any credential bytes.
    # Keep this port open; never retry WIFI/SAVE after an ambiguous timeout.
    connect_setup(port)
    exchange(port, wifi, 'OK WIFI staged')
    exchange(port, bridge_command(endpoint), 'OK BRIDGE staged')
    exchange(port, 'SAVE', 'OK SAVE')
    exchange(port, 'REBOOT', 'OK REBOOT')
    print('Pairing saved. Device is rebooting; check DEVICE/ONLINE after Wi-Fi connects.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Explicit serial port, e.g. /dev/cu.usbmodem... or COM3')
    parser.add_argument('--identity-file', help='Dedicated public bridge package identity JSON (endpoint only)')
    parser.add_argument('--bridge', help='Diagnostic endpoint override; normally detected from the local package/macOS')
    args = parser.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.exit(1, 'Install pyserial first: python3 -m pip install pyserial\n')
    print('Connect USB and hold BtnB for 2 seconds until USB SETUP. Close any serial monitor.')
    try:
        device = discover_port(list_ports, args.port)
        with setup_port(serial, device) as port:
            configure(port, args.identity_file, args.bridge)
    except (RuntimeError, ValueError, OSError, serial.SerialException) as error:
        # Serial exceptions may contain OS details but never include commands.
        print(f'Setup failed: {error}', file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print('\nSetup cancelled. Unsaved staged changes are discarded on reset.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
