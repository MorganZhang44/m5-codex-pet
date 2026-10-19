"""Per-user macOS layout and launchd management. Never uses sudo."""
import argparse
import errno
import os
from pathlib import Path
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import socket
import stat
import time
import venv

from bridge import atomic_json, publish_identity, read_json

LABEL = 'com.codex-pet.bridge'
LEGACY_LABEL = 'org.codex-pet.status'
OWNER = 'codex-pet-v0.6b-phase1'
MARKER = '.codex-pet-install.json'
ZSH_PATH_BLOCK = b'''# >>> codex-pet PATH >>>
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) export PATH="$HOME/.local/bin:$PATH" ;;
esac
# <<< codex-pet PATH <<<
'''


def zsh_path_update(home, zdotdir=None):
    """Plan an append only; never evaluate or rewrite existing shell content."""
    directory = Path(zdotdir) if zdotdir else Path(home)
    if not directory.is_absolute():
        raise RuntimeError('ZDOTDIR must be absolute to configure zsh PATH safely.')
    path = directory / '.zshrc'
    safe_path(path)
    if path.exists() and not path.is_file():
        raise RuntimeError('Refusing non-regular zsh configuration: ' + str(path))
    content = path.read_bytes() if path.exists() else b''
    if ZSH_PATH_BLOCK in content:
        return path, b''
    if b'# >>> codex-pet PATH >>>' in content or b'# <<< codex-pet PATH <<<' in content:
        raise RuntimeError('Codex Pet PATH block was modified; check ' + str(path))
    return path, (b'\n' if content and not content.endswith(b'\n') else b'') + ZSH_PATH_BLOCK


def ensure_zsh_path(home, zdotdir=None):
    path, addition = zsh_path_update(home, zdotdir)
    if not addition:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    # Append preserves existing bytes, permissions and inode; reject symlinks
    # again at open time rather than following a replaced configuration file.
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'ab') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise RuntimeError('Refusing non-regular zsh configuration: ' + str(path))
        stream.write(addition)


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
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)


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


def port_free():
    with socket.socket() as probe:
        # Match HTTPServer's bind policy: old connections in TIME_WAIT must not
        # block upgrades, while a live listener still prevents this bind.
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind(('0.0.0.0', 8788))
        except OSError as error:
            if error.errno != errno.EADDRINUSE:
                raise
            return False
    return True


def wait_for_bridge_port():
    for _ in range(20):
        if port_free():
            return True
        time.sleep(0.25)
    return False


def legacy_listener(home, target):
    """Return stable identity evidence, or False when any metadata is ambiguous."""
    # Read only listener/job/process metadata. Never open the legacy checkout or
    # its configuration, and never include command/environment data in errors.
    listeners = subprocess.run(['/usr/sbin/lsof', '-nP', '-t', '-iTCP:8788',
                                '-sTCP:LISTEN'], capture_output=True, text=True, timeout=5)
    pids = set(listeners.stdout.split())
    if listeners.returncode != 0 or len(pids) != 1:
        return False
    pid = next(iter(pids))
    if not pid.isdecimal() or int(pid) <= 0:
        return False
    job = launchctl('print', target, check=False)
    if job.returncode or re.findall(r'^\s*pid = (\d+)\s*$', job.stdout, re.MULTILINE) != [pid]:
        return False
    process = subprocess.run(['/bin/ps', '-ww', '-p', pid, '-o', 'uid=', '-o', 'command='],
                             capture_output=True, text=True, timeout=5)
    if process.returncode:
        return False
    try:
        uid, command = process.stdout.strip().split(None, 1)
        args = shlex.split(command)
    except ValueError:
        return False
    if uid != str(os.getuid()) or len(args) < 2:
        return False
    if not re.fullmatch(r'(?:python(?:3(?:\.\d+)?)?|Python)', Path(args[0]).name):
        return False
    if args[1] != str(Path(home) / 'agent-loop/status_server.py'):
        return False
    flags = args[2:]
    if flags not in (['--host', '0.0.0.0', '--port', '8788'],
                     ['--host', '0.0.0.0', '--port', '8788', '--bonjour']):
        return False
    return (pid, uid, tuple(args))


def ensure_bridge_port(home):
    if port_free():
        return
    target = 'gui/' + str(os.getuid()) + '/' + LEGACY_LABEL
    refusal = ('TCP 8788 is occupied; could not positively identify the current user\'s '
               'legacy org.codex-pet.status listener. No conflicting service was stopped. '
               'Inspect with lsof -nP -iTCP:8788 -sTCP:LISTEN and resolve the conflict manually.')
    try:
        # Recheck immediately before mutation to reject changed PID/job metadata.
        evidence = legacy_listener(home, target)
        identified = bool(evidence) and legacy_listener(home, target) == evidence
    except (OSError, subprocess.SubprocessError):
        raise RuntimeError(refusal) from None
    if not identified:
        raise RuntimeError(refusal)
    # Keep the old plist intact, but prevent KeepAlive/login from reclaiming 8788.
    try:
        disabled = launchctl('disable', target, check=False).returncode == 0
    except (OSError, subprocess.SubprocessError):
        disabled = False
    if not disabled:
        raise RuntimeError('Could not disable legacy org.codex-pet.status; installation stopped. '
                           'The job may already be disabled; its files are retained.')
    try:
        stopped = launchctl('bootout', target, check=False).returncode == 0
    except (OSError, subprocess.SubprocessError):
        stopped = False
    if not stopped:
        raise RuntimeError('Could not stop legacy org.codex-pet.status; installation stopped. '
                           'The legacy job remains disabled and may still be running; '
                           'its files are retained.')
    if wait_for_bridge_port():
        print('Migrated legacy org.codex-pet.status: disabled and booted out; TCP 8788 is free.')
        return
    raise RuntimeError('Legacy org.codex-pet.status was disabled and booted out, but TCP 8788 '
                       'remains occupied; installation stopped. No process was killed.')


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


def install(home, source, manage_service=True, provisioner=provision, zdotdir=None):
    paths = layout(home)
    for key in ('support', 'runtime', 'cli', 'plist'):
        safe_path(paths[key])
    safe_path(paths['support'] / MARKER)
    installed = owned(paths)
    check_targets(paths, installed)
    # Refuse unsafe shell targets before any service migration/package mutation.
    zsh_path_update(paths['home'], zdotdir)
    # Resolve only a positively identified legacy conflict before package writes.
    if manage_service and not installed:
        ensure_bridge_port(paths['home'])
    paths['support'].mkdir(parents=True, exist_ok=True)
    # Mark before creating owned targets, so even an interrupted install is retryable.
    atomic_json(paths['support'] / MARKER, {'owner': OWNER})
    # Validate the Mac identity before touching an existing runtime.
    publish_identity(paths['support'])
    if manage_service and installed:
        stop_service()
        # launchd/process shutdown may finish asynchronously after bootout.
        if not wait_for_bridge_port():
            ensure_bridge_port(paths['home'])
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
    ensure_zsh_path(paths['home'], zdotdir)
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
        paths = install(home, Path(__file__).resolve().parents[1], not args.no_launchd,
                        zdotdir=None if args.home else os.environ.get('ZDOTDIR'))
        print('Installed. Run: ' + shlex.quote(str(paths['cli'])) + ' setup')
        print('zsh PATH configured. Open a new Terminal window/tab, then run: codex-pet setup')
        print('This already-open Terminal keeps its current PATH; the absolute command above works now.')
    else:
        uninstall(home, not args.no_launchd)
        print('Codex Pet uninstalled. Local state/logs retained in Application Support/Codex Pet.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('Codex Pet install: ' + str(error), file=sys.stderr)
        sys.exit(1)
