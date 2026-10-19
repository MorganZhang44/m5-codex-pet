#!/usr/bin/env python3
"""Codex Pet: USB pairing and the independent local status bridge."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

from bridge import LocalSource, STATES, atomic_json, publish_identity, read_json, server


def support_path():
    return Path.home() / 'Library/Application Support/Codex Pet'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('setup', help='Pair this Mac and Wi-Fi over USB', add_help=False)
    commands.add_parser('status', help='Read the running local bridge')
    state = commands.add_parser('state', help='Set local pet state (explicit integration/fallback)')
    state.add_argument('value', type=str.upper, choices=STATES)
    state.add_argument('--ttl', type=int, default=900, help='Seconds until automatic state resumes (1–86400; default 900)')
    commands.add_parser('bridge', help='Run bridge (normally managed by launchd)')
    # Pass setup arguments, including --help, unchanged to the original helper.
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments and arguments[0] == 'setup':
        return subprocess.call([sys.executable, str(Path(__file__).with_name('codex_pet_setup.py'))] + arguments[1:])
    args = parser.parse_args(arguments)
    support = support_path()
    if args.command == 'status':
        with urllib.request.urlopen('http://127.0.0.1:8788/status', timeout=5) as response:
            print(json.dumps(json.load(response), indent=2))
    elif args.command == 'state':
        if not 1 <= args.ttl <= 86400:
            parser.error('--ttl must be between 1 and 86400')
        previous = read_json(support / 'state.json')
        last = previous.get('last_result') if isinstance(previous, dict) else None
        if last not in ('SUCCESS', 'FAILED'):
            last = None
        if args.value in ('SUCCESS', 'FAILED'):
            last = args.value
        atomic_json(support / 'state.json', dict(state=args.value, last_result=last,
                                               updated_at=time.time(), expires_at=time.time() + args.ttl))
    elif args.command == 'bridge':
        publish_identity(support)
        source = LocalSource(support, Path(os.environ.get('CODEX_HOME', str(Path.home() / '.codex'))))
        with server(source) as httpd:
            httpd.serve_forever()
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print('Codex Pet: ' + str(error), file=sys.stderr)
        sys.exit(1)
