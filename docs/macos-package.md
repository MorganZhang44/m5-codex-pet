# macOS package: v0.6b phase 2

The source installer copies three Python modules plus a private Python environment.
`codex-pet setup` dispatches to the byte-for-byte existing USB helper. `codex-pet
bridge` runs a standard-library HTTP service under launchd. The bridge reads local
Codex usage/lifecycle events and a package-owned local state file; no agent-loop/MCP/ChatGPT
process, checkout, tunnel, API token, Keychain access or cloud request is involved.
Firmware, pairing protocol, UI, NVS behavior and dependencies remain v0.6a.

## Installed paths

| Path under the current user's home | Purpose |
| --- | --- |
| `.local/bin/codex-pet` | Shell launcher with quoted absolute runtime paths |
| `Library/LaunchAgents/com.codex-pet.bridge.plist` | Login LaunchAgent, RunAtLoad/KeepAlive, 10-second restart throttle |
| `Library/Application Support/Codex Pet/runtime/` | Package-owned code and `venv/` (pyserial 3.5 for setup) |
| `Library/Application Support/Codex Pet/bridge-identity.json` | Public `{"endpoint":"local-host-name.local:8788"}` |
| `Library/Application Support/Codex Pet/.codex-pet-install.json` | Installer ownership marker |
| `Library/Application Support/Codex Pet/state.json` | Explicit local task state, expiry and independent last result |
| `Library/Application Support/Codex Pet/bridge.log`, `bridge-error.log` | launchd output/error logs; HTTP requests are not logged |

The service runs in `gui/$(id -u)` with the current user's permissions and HOME,
listening on IPv4 `0.0.0.0:8788`. It runs while that user is logged in, without a
terminal open, and resumes at login. It does not run before login or while the
Mac sleeps. Another user/service cannot share this port. launchd does not provide
log rotation here; normal successful requests generate no log entries.

The public identity is atomically published at install and every bridge start.
macOS's existing mDNSResponder advertises `LocalHostName.local`. No separate
DNS-SD `_service._tcp` record is needed or registered: the device queries this
exact hostname. The name is stable across DHCP changes, not hostname renames.

## Available data and limits

Every `GET /status` returns exactly the existing eight fields. There are no HTTP
write routes, and no MCP dependency. `codex-pet status` reads localhost without
requiring the repository. The LAN status endpoint has no authentication/TLS and
exposes only the eight display fields; deploy on a trusted local network.

**Automatic task state:** the reader uses only timestamped JSONL `type: event_msg`
records in the standard local session directory. It does not infer state from
transcript text, process names, file modification times or network errors.

| Signal (`payload.type`) | Display behavior |
| --- | --- |
| `task_started` | RUNNING; remember the active turn within that session |
| `task_complete`, absent/null `payload.error` | SUCCESS for 30 seconds; record last_result SUCCESS |
| `task_complete`, structured `payload.error` object with string `message` | FAILED for 30 seconds; record last_result FAILED |
| `task_complete` with malformed non-null error | SLEEP, without recording a result |
| `turn_aborted` | SLEEP, without recording a result (cancellation is not failure) |
| No active turn or recent completion | SLEEP |

A terminal event closes only its matching `turn_id` when both records supply IDs;
older records without IDs are matched within their session. Any active session
wins over another session's completion. Structured `event_msg` progress renews an
already observed RUNNING turn; it cannot start one. After 15 minutes without such
an event, RUNNING expires to SLEEP, never FAILED. This is an inactivity safety
lease, not proof that a process stopped: long silent tools, approval waits, crashes,
missing terminal records, or delayed log flushes cannot be distinguished exactly.
State becomes visible on the next `/status` poll after Codex flushes its log, with
a scan interval of two seconds (plus the device's polling delay).

SUCCESS means a normal turn completion was recorded, not that every tool/test or
the user's objective succeeded. Only the structured terminal error on
`task_complete` supplies automatic FAILED. It is defined in the current upstream
[Codex protocol source](https://github.com/openai/codex/blob/main/codex-rs/protocol/src/protocol.rs)
(`TurnCompleteEvent.error`); not all installed versions write it. Standalone
`error`, `stream_error`, failed commands/patches, and message wording do not set
FAILED, because they can be recoverable or unrelated to terminal task status.
Older writers may end without a terminal record (lease then SLEEP), or write a
completion without failure details (displayed as completion/SUCCESS). Exact FAILED
detection is therefore unavailable for those writers; use an explicit integration
when an authoritative task outcome is required. The boundary names are documented
in the upstream [Codex protocol](https://github.com/openai/codex/blob/main/codex-rs/docs/protocol_v1.md).

**Local investigation on this Mac (2026-10-19):** metadata-only inspection found
seven `codex_vscode` sessions (`source: vscode`, versions `0.131.0-alpha.9` and
`0.140.0-alpha.2`) in `~/.codex/sessions`, in addition to `codex_exec` and Codex
Desktop sessions. One representative log per VS Code version contained paired
`task_started`/`task_complete` records (23/23 and 12/12 respectively). The 40 most
recent exec logs had 40 starts and 37 completions at inspection time. This confirms
local extension support here; it does not certify every extension release or
failure path. No structured terminal failure was validated locally; that path is covered by synthetic tests.

CLI, exec and VS Code use the same reader when they persist these standard local
records; the reader does not filter on originator or require a running CLI.
Remote/SSH/WSL/container/cloud sessions are visible only if their standard logs
are available in the configured local session directory. Ephemeral sessions,
versions that do not persist boundary events, and alternate app storage are not
covered. No extension settings, private databases, credentials or unrelated user
files are read, and no hooks are installed.

**Explicit override/fallback:** case-insensitive input is normalized before
validation; storage and the eight-field response remain uppercase.

```sh
codex-pet state running                 # default expiry: 900 seconds
codex-pet state success --ttl 30        # also records last_result SUCCESS
codex-pet state failed --ttl 30         # also records last_result FAILED
codex-pet state sleep                  # preserves independent last_result
```

Each write atomically replaces `state.json`. TTL must be 1–86400 seconds. A valid
explicit state takes priority until expiry, including SLEEP; automatic observation
continues underneath. On expiry, automatic state resumes (SLEEP if unavailable).
RUNNING/SLEEP never manufacture a result. Explicit history persists across bridge
restarts; automatic history is cached in memory and reconstructed from the bounded
logs on restart. Outside an active override, the newer automatic result wins over
an explicit record's `updated_at`; old phase-1 files without that timestamp yield
to an available automatic result. Missing/malformed state files allow automatic
state; with neither source, the response is SLEEP/null. Use one explicit state
writer; simultaneous explicit writes have last-write-wins behavior.

**Usage:** best-effort, read-only rate-limit observations in
`~/.codex/sessions/**/*.jsonl`. A foreground `codex-pet bridge` honors `CODEX_HOME`;
the installed LaunchAgent uses `~/.codex` (it does not inherit terminal variables).
For a custom Codex home, update the LaunchAgent's `EnvironmentVariables` deliberately
and reload it; restore the generated plist before using the installer/uninstaller,
which refuse to replace a modified service configuration.

The reader recognizes JSONL `type: event_msg`, `payload.type: token_count`,
`payload.rate_limits.primary/secondary` objects with `window_minutes` of 300/10080,
`used_percent` of 0–100, and Unix-seconds `resets_at`. The event's ISO timestamp
is the observation time. Resets are converted to UTC, missing resets become null.
It does not compute quota percentages from token counts. The shared usage/lifecycle
reader scans on demand at most every two seconds,
enumerates session paths, and watches the 20 most recently modified JSONL files.
It initially reads only the last 256 KiB per file, then reads appended complete
lines using in-memory offsets (at most 256 KiB per file per scan). Partial final
lines are retried. Symlinked session files/directories are skipped. File replacement,
truncation or an append gap larger than 256 KiB resets that file to its bounded
tail and clears its active turn. Evicted files also lose their active turn.
A start outside the initial/reset tail cannot establish RUNNING, even if progress
is present; this conservative limit also applies after a bridge restart. Records
more than 60 seconds in the future are ignored. For usage it selects the newest
supported observation across these files. Unsupported versions/windows, older logs outside that bound, remote
sessions, archived sessions, missing rate-limit records, and Codex app storage
outside that directory are unavailable. No auth/config/credential files or session
text are exposed in the response, and no Codex API is polled.

With no observation: both percentages/resets/observation are null and
`usage_stale` is true. Observations older than 10 minutes are stale; values remain
visible. Cached usage survives transient read failures within the running process,
but is reconstructed from logs after restart. It is an observation, not a live
account quota or per-task consumption meter. Usage events cannot start or finish
a turn, but can renew the inactivity lease of an already observed active turn.

## Install, remove and recovery

From `/Users/morganmacmini/m5-codex-pet`, run `./install_mac.sh` without sudo.
Python 3.9+ with venv/pip and installation-time access to pyserial 3.5 are required.
The resulting venv still depends on that base Python installation. This is a source
package, not a signed installer with an embedded Python distribution.

Installation appends one marked, guarded PATH block to `~/.zshrc`, or to
`$ZDOTDIR/.zshrc` if an absolute `ZDOTDIR` is exported to the installer. The block
adds `$HOME/.local/bin` only when it is absent from PATH. Repeated installs leave
the same block in place; existing shell configuration bytes and permissions are
preserved. Symlinked/non-regular rc targets and partially edited package blocks
are refused before service migration. No other shell startup files are edited.
Relative `ZDOTDIR` is refused; a `ZDOTDIR` set only inside shell startup files and
not exported cannot be detected by the installer. Other shells, disabled rc
loading, and startup code that later replaces PATH require user configuration.

Open a new Terminal window/tab after installation to use `codex-pet setup`
directly in interactive zsh (login or non-login). The installer cannot change its
parent Terminal's environment. For immediate use in the already-open Terminal,
run `"$HOME/.local/bin/codex-pet" setup`; no manual export is needed in new sessions.

The installer rejects unowned target collisions and symlinked target paths. It
stops its owned launchd label on update,
and marks ownership before dependency provisioning so an interrupted install is
retryable. If provisioning/bootstrap fails, correct the reported problem and
rerun the installer; there is no transactional rollback to an earlier runtime.
Before a fresh install writes package files, it tests TCP 8788. If occupied, the
only automatic migration is the current user's `gui/<uid>/org.codex-pet.status`
LaunchAgent. Its launchd PID must match the sole listening PID reported by lsof,
and ps must report the current UID and a Python command running exactly
`~/agent-loop/status_server.py --host 0.0.0.0 --port 8788` (optionally followed by
`--bonjour`). The listener PID, job PID, UID and full command must remain identical
across two checks immediately before mutation; a restarting or changing job is refused.
Missing metadata, a different label/path/command, multiple listening PIDs, another
user or an ambiguous command fails closed with a diagnostic. Resolve those
conflicts manually; the installer never kills a PID or unloads arbitrary jobs.

For a confirmed legacy listener, the installer first runs `launchctl disable` for
that exact service target, then `launchctl bootout`. This stops KeepAlive and
prevents the retained legacy plist from loading again at login. It waits up to
five seconds for a successful IPv4 bind on 8788 before continuing. Disable/bootout
failure or a port that remains occupied aborts installation without provisioning
the new runtime. Upgrades also check the port after stopping the owned bridge;
retrying an interrupted migration or reinstalling with a free port is safe.
Inspection reads only launchd/listener/process metadata, never legacy source,
credentials or configuration files; the installed runtime remains independent of
agent-loop and MCP. The legacy plist and checkout are neither removed nor edited.

After a partial migration, the old job may already be disabled. Correct the
reported conflict and rerun the installer. There is no automatic legacy rollback.
If deliberately restoring the old service, uninstall the new package and verify
8788 is free first, then use `launchctl enable "gui/$(id -u)/org.codex-pet.status"`
and bootstrap its retained, verified plist in that GUI domain. Do not run both
bridges on 8788.

After install, inspect
`bridge-error.log` if launchd cannot bind (for example, a competing server starts).

`./uninstall_mac.sh` stops only `com.codex-pet.bridge`, removes only the owned CLI,
LaunchAgent, runtime subtree and public identity, and retains state/logs/ownership
marker and unrelated files. An absent install is a no-op; a replaced CLI/plist is
left intact with an error. Uninstall retains the PATH block and leaves shell rc,
firewall settings, Python installation, Codex logs/credentials, unrelated services
and device NVS unchanged. Reinstall is
supported. Remove the retained state/logs manually if desired. The uninstall script
uses host Python and does not require a functioning package venv.
Uninstall never re-enables or deletes the migrated legacy service; it stays
disabled, including across login, until explicitly restored.

For development, `--no-launchd --home /absolute/staging/home` stages files under
that home without launchctl (including that home's `.zshrc`, ignoring the real
user's `ZDOTDIR`). It still validates the Mac hostname and provisions
the real venv; it does not install a service for another user. Tests inject fake
provisioning and launchctl and use temporary homes, not this flag on your home.

## Exact manual smoke test on this Mac

These steps deliberately install/start the package and, at pairing, replace the
device's current pairing. Automated tests do neither.

1. Log in to the Mac desktop. Inspect before installing; a matching legacy
   `org.codex-pet.status` listener is migrated automatically:

   ```sh
   cd /Users/morganmacmini/m5-codex-pet
   lsof -nP -iTCP:8788 -sTCP:LISTEN
   ./install_mac.sh
   # Open a new Terminal window/tab before running the remaining commands.
   launchctl print "gui/$(id -u)/com.codex-pet.bridge"
   lsof -nP -iTCP:8788 -sTCP:LISTEN
   cat "$HOME/Library/Application Support/Codex Pet/bridge-identity.json"
   curl --fail --silent http://127.0.0.1:8788/status
   codex-pet status
   ```

   Expect the package Python process on `*:8788`, the current
   `scutil --get LocalHostName` plus `.local:8788` in the public file, and eight
   fields with automatic state/history, or SLEEP/null when unavailable. Usage may be null/stale.
   If bootstrap succeeds before the bridge is ready, retry curl after a second.
   If needed inspect `tail -50 "$HOME/Library/Application Support/Codex Pet/bridge-error.log"`.
   After migration, `launchctl print "gui/$(id -u)/org.codex-pet.status"` should
   report no loaded job, and `launchctl print-disabled "gui/$(id -u)"` should list
   `org.codex-pet.status` as disabled. Its original files should still exist.

2. Verify launchd restart and state expiry:

   ```sh
   codex-pet state RUNNING --ttl 10
   codex-pet status
   # Wait at least 10 seconds, then expect automatic state (SLEEP if idle).
   codex-pet status
   codex-pet state SUCCESS --ttl 30
   codex-pet state SLEEP
   launchctl kickstart -k "gui/$(id -u)/com.codex-pet.bridge"
   # Retry after a second if restarting.
   codex-pet status
   ```

   Expect SLEEP with last_result SUCCESS after restart. Run a normal local Codex
   turn in CLI and then VS Code; observe RUNNING while it works, SUCCESS for
   30 seconds after completion, then SLEEP. Cancel another turn and expect SLEEP
   without a new result. Before these automatic checks, run
   `codex-pet state sleep --ttl 1` and wait at least one second to expire the override.
   If the writer
   emits supported rate-limit events, usage should appear within two seconds of
   a flushed record and status poll. Otherwise null/stale is the expected fallback.
   No private-file inspection or credentials are needed for this test.

3. Verify LAN/mDNS before pairing. From another device on the same reachable LAN,
   request `http://<endpoint-from-identity-file>/status`. Allow the package Python
   through the macOS firewall if prompted. Keep the LAN trusted. Stop here to fix
   reachability if this fails; local curl alone does not prove Pet connectivity.

4. Connect the flashed v0.6a StickS3 via USB data cable, close serial monitors, hold
   BtnB for two seconds until USB SETUP, then from any directory run:

   ```sh
   cd "$HOME"
   codex-pet setup
   ```

   Confirm this Mac/bridge and 2.4 GHz SSID, enter the Wi-Fi password twice in the
   hidden prompts. If USB open resets the board, hold BtnB again while the helper
   waits. Expect save/reboot then DEVICE ONLINE with the resolved bridge IPv4.
   Use `codex-pet state RUNNING`, `SUCCESS`, `FAILED`, then `SLEEP` to verify the
   four expressions and independent LAST result. Do not treat missing usage as
   a transport failure: valid null usage is ONLINE.

5. Close terminals, confirm the Pet still updates; log out/in and verify the
   LaunchAgent resumes. For DHCP testing, change the Mac's address without changing
   LocalHostName, then verify Pet reconnects without pairing again. These checks
   require real hardware/network and are not covered by host tests.

6. Test removal and repeatability from the checkout:

   ```sh
   cd /Users/morganmacmini/m5-codex-pet
   ./uninstall_mac.sh
   ./uninstall_mac.sh
   launchctl print "gui/$(id -u)/com.codex-pet.bridge"
   lsof -nP -iTCP:8788 -sTCP:LISTEN
   ./install_mac.sh
   codex-pet status
   ```

   After removal, launchctl should report no such service; this package should
   have no listener/CLI/runtime/identity. Pet becomes OFFLINE while retaining its
   last display snapshot. Other apps/files and retained state/logs remain. After
   reinstall, service and endpoint return; pairing again is unnecessary if the
   hostname/port and Wi-Fi did not change.

## Automated verification

Run the host checks listed in the root README, `python3 tests/package_test.py`,
`git diff --check`, and `~/.platformio/penv/bin/pio run`. Package tests use temporary
homes, fake dependency provisioning/scutil/launchctl, and real HTTP parsing over
in-memory connections. They cover layout, quoting, repeated install/removal,
append-only zsh configuration, real isolated login/non-login zsh startup and CLI
discovery, PATH deduplication, exported ZDOTDIR and unsafe/edited rc refusal,
ownership/collision/symlink guards, stop failure, launchd generation/dispatch,
free-port installation, legacy PID/UID/command identification, successful migration
and repeat install, disable/bootout failure, port-release timeout, unrelated or
ambiguous listener refusal, owned upgrade with a competing listener,
unchanged helper/CLI dispatch, case normalization, override expiry/history, turn boundaries/concurrency,
partial appends, bounded reading, restart recovery, cancellation/terminal errors,
inactivity expiry, local usage/schema/fallbacks,
and absence of HTTP mutation routes. They do not exercise real launchd, firewall,
pip networking, mDNS, USB or a LAN listener.
