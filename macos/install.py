"""Per-user macOS layout and launchd management. Never uses sudo."""
import argparse
import os
from pathlib import Path
import plistlib
import shlex
import shutil
import subprocess
import sys
import venv

from bridge import atomic_json, publish_identity, read_json

LABEL = 'com.codex-pet.bridge'
OWNER = 'codex-pet-v0.6b-phase1'
MARKER = '.codex-pet-install.json'


def layout(home):
    home = Path(home).absolute()
    support = home / 'Library/Application Support/Codex Pet'
    return dict(home=home, support=support, runtime=support / 'runtime',
                cli=home / '.local/bin/codex-pet',
                plist=home / 'Library/LaunchAgents' / (LABEL + '.plist'))


def safe_path(path):
    # Do not follow user-created symlinks into other installations/directories.
    for item in (path, *path.parents):
        if item.is_symlink():
            raise RuntimeError('Refusing symlink in install path: ' + str(item))


def launcher(paths):
    return ('#!/bin/sh\n# ' + OWNER + '\nexec ' +
            shlex.quote(str(paths['runtime'] / 'venv/bin/python3')) + ' ' +
            shlex.quote(str(paths['runtime'] / 'codex_pet.py')) + ' "$@"\n')


def launch_config(paths):
    return dict(Label=LABEL,
                ProgramArguments=[str(paths['runtime'] / 'venv/bin/python3'),
                                  str(paths['runtime'] / 'codex_pet.py'), 'bridge'],
                RunAtLoad=True, KeepAlive=True, ThrottleInterval=10,
                WorkingDirectory=str(paths['support']),
                EnvironmentVariables={'HOME': str(paths['home']), 'PYTHONUNBUFFERED': '1',
                                      'PYTHONDONTWRITEBYTECODE': '1'},
                StandardOutPath=str(paths['support'] / 'bridge.log'),
                StandardErrorPath=str(paths['support'] / 'bridge-error.log'),
                ProcessType='Background', Umask=0o077)


def launchctl(*args, check=True):
    return subprocess.run(['/bin/launchctl', *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def provision(runtime):
    venv.EnvBuilder(with_pip=True).create(runtime / 'venv')
    subprocess.run([str(runtime / 'venv/bin/python3'), '-m', 'pip',
                    'install', '--disable-pip-version-check', 'pyserial==3.5'], check=True)


def owned(paths):
    return read_json(paths['support'] / MARKER) == {'owner': OWNER}


def stop_service():
    target = 'gui/' + str(os.getuid()) + '/' + LABEL
    result = launchctl('bootout', target, check=False)
    if result.returncode and launchctl('print', target, check=False).returncode == 0:
        raise RuntimeError('Could not stop Codex Pet LaunchAgent; files retained.')


def check_targets(paths, installed):
    for key in ('support', 'runtime', 'cli', 'plist'):
        safe_path(paths[key])
    safe_path(paths['support'] / MARKER)
    for name in ('bridge-identity.json', 'state.json', 'bridge.log', 'bridge-error.log'):
        safe_path(paths['support'] / name)
    for name in ('bridge.py', 'codex_pet.py', 'codex_pet_setup.py', 'venv',
                 'venv/bin', 'venv/lib', 'venv/pyvenv.cfg'):
        safe_path(paths['runtime'] / name)
    if installed:
        if paths['cli'].exists() and paths['cli'].read_text() != launcher(paths):
            raise RuntimeError('CLI was replaced; refusing to overwrite/remove it.')
        if paths['plist'].exists():
            with paths['plist'].open('rb') as stream:
                if plistlib.load(stream) != launch_config(paths):
                    raise RuntimeError('LaunchAgent was replaced; refusing to overwrite/remove it.')
    else:
        for path in (paths['runtime'], paths['cli'], paths['plist'],
                     paths['support'] / MARKER, paths['support'] / 'bridge-identity.json'):
            if path.exists():
                raise RuntimeError('Unowned install target exists: ' + str(path))


def install(home, source, manage_service=True, provisioner=provision):
    paths = layout(home)
    for key in ('support', 'runtime', 'cli', 'plist'):
        safe_path(paths[key])
    safe_path(paths['support'] / MARKER)
    installed = owned(paths)
    check_targets(paths, installed)
    # Fail before mutation if a competing process already owns the bridge port.
    if manage_service and not installed:
        import socket
        with socket.socket() as probe:
            probe.bind(('0.0.0.0', 8788))
    paths['support'].mkdir(parents=True, exist_ok=True)
    # Mark before creating owned targets, so even an interrupted install is retryable.
    atomic_json(paths['support'] / MARKER, {'owner': OWNER})
    # Validate the Mac identity before touching an existing runtime.
    publish_identity(paths['support'])
    if manage_service and installed:
        stop_service()
    paths['runtime'].mkdir(exist_ok=True)
    provisioner(paths['runtime'])
    for name in ('bridge.py', 'codex_pet.py'):
        shutil.copyfile(Path(source) / 'macos' / name, paths['runtime'] / name)
    shutil.copyfile(Path(source) / 'scripts/codex_pet_setup.py', paths['runtime'] / 'codex_pet_setup.py')
    paths['cli'].parent.mkdir(parents=True, exist_ok=True)
    paths['cli'].write_text(launcher(paths))
    paths['cli'].chmod(0o755)
    paths['plist'].parent.mkdir(parents=True, exist_ok=True)
    paths['plist'].write_bytes(plistlib.dumps(launch_config(paths)))
    if manage_service:
        domain = 'gui/' + str(os.getuid())
        launchctl('enable', domain + '/' + LABEL)
        launchctl('bootstrap', domain, str(paths['plist']))
    return paths


def uninstall(home, manage_service=True):
    paths = layout(home)
    safe_path(paths['support'])
    safe_path(paths['support'] / MARKER)
    if not owned(paths):
        # An absent installation is a successful no-op. Never touch another bridge.
        return False
    check_targets(paths, True)
    if manage_service:
        stop_service()
    for key in ('cli', 'plist'):
        paths[key].unlink(missing_ok=True)
    if paths['runtime'].exists():
        shutil.rmtree(paths['runtime'])
    (paths['support'] / 'bridge-identity.json').unlink(missing_ok=True)
    # Keep user state and logs. Keep ownership marker to permit safe reinstall.
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'uninstall'))
    parser.add_argument('--home', type=Path, help='Test/staging home; requires --no-launchd')
    parser.add_argument('--no-launchd', action='store_true', help='Generate/remove files only (staging)')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This installer requires macOS.')
    if os.getuid() == 0:
        parser.error('Run as your normal logged-in user, without sudo.')
    if args.home and not args.no_launchd:
        parser.error('--home requires --no-launchd')
    home = args.home or Path.home()
    if args.action == 'install':
        paths = install(home, Path(__file__).resolve().parents[1], not args.no_launchd)
        print('Installed. Run: ' + shlex.quote(str(paths['cli'])) + ' setup')
        print('Add ~/.local/bin to PATH to use: codex-pet setup')
    else:
        uninstall(home, not args.no_launchd)
        print('Codex Pet uninstalled. Local state/logs retained in Application Support/Codex Pet.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('Codex Pet install: ' + str(error), file=sys.stderr)
        sys.exit(1)
