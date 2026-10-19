# macOS package: v0.6b phase 1

The source installer copies three Python modules plus a private Python environment.
`codex-pet setup` dispatches to the byte-for-byte existing USB helper. `codex-pet
bridge` runs a standard-library HTTP service under launchd. The bridge reads local
Codex usage events and a package-owned local state file; no agent-loop/MCP/ChatGPT
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

**Task state:** no independent, authoritative Codex task lifecycle integration is
available in this phase. The bridge does not infer success/failure from transcript
text, process names, file activity or network errors. With no valid local state it
returns `SLEEP` and `last_result: null`. An optional explicit local integration is:

```sh
codex-pet state RUNNING                 # default expiry: 900 seconds
codex-pet state SUCCESS --ttl 30        # also records last_result SUCCESS
codex-pet state FAILED --ttl 30         # also records last_result FAILED
codex-pet state SLEEP                  # preserves independent last_result
```

Each write atomically replaces `state.json`. TTL must be 1–86400 seconds. On expiry,
the current expression becomes SLEEP; the last explicit result persists across
restarts until overwritten. RUNNING/SLEEP never manufacture a result. Malformed or
missing state falls back to SLEEP/null. Use one state writer; simultaneous writers
have last-write-wins behavior. For long tasks, refresh RUNNING before its TTL.
There is no automatic Codex hook installation or automatic task history.

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
It does not compute quota percentages from token counts. It scans at most every
30 seconds, enumerates session paths and reads only the last 256 KiB of the 20
most recently modified session files. It selects the newest supported observation
across them. Unsupported versions/windows, older logs outside that bound, remote
sessions, archived sessions, missing rate-limit records, and Codex app storage
outside that directory are unavailable. No auth/config/credential files or session
text are exposed in the response, and no Codex API is polled.

With no observation: both percentages/resets/observation are null and
`usage_stale` is true. Observations older than 10 minutes are stale; values remain
visible. Cached usage survives transient read failures within the running process,
but is reconstructed from logs after restart. It is an observation, not a live
account quota or per-task consumption meter. Usage events never change task state.

## Install, remove and recovery

From `/Users/morganmacmini/m5-codex-pet`, run `./install_mac.sh` without sudo.
Python 3.9+ with venv/pip and installation-time access to pyserial 3.5 are required.
The resulting venv still depends on that base Python installation. This is a source
package, not a signed installer with an embedded Python distribution.

The installer rejects unowned target collisions and symlinked target paths. It
leaves other LaunchAgents alone. It stops only its owned launchd label on update,
and marks ownership before dependency provisioning so an interrupted install is
retryable. If provisioning/bootstrap fails, correct the reported problem and
rerun the installer; there is no transactional rollback to an earlier runtime.
An occupied port is rejected before a first install. After install, inspect
`bridge-error.log` if launchd cannot bind (for example, a competing server starts).

`./uninstall_mac.sh` stops only `com.codex-pet.bridge`, removes only the owned CLI,
LaunchAgent, runtime subtree and public identity, and retains state/logs/ownership
marker and unrelated files. An absent install is a no-op; a replaced CLI/plist is
left intact with an error. No shell rc, firewall settings, Python installation,
Codex logs/credentials, other services or device NVS are changed. Reinstall is
supported. Remove the retained state/logs manually if desired. The uninstall script
uses host Python and does not require a functioning package venv.

For development, `--no-launchd --home /absolute/staging/home` stages files under
that home without launchctl. It still validates the Mac hostname and provisions
the real venv; it does not install a service for another user. Tests inject fake
provisioning and launchctl and use temporary homes, not this flag on your home.

## Exact manual smoke test on this Mac

These steps deliberately install/start the package and, at pairing, replace the
device's current pairing. Automated tests do neither.

1. Log in to the Mac desktop. Stop the old agent-loop status server if it owns
   port 8788. Inspect before installing:

   ```sh
   cd /Users/morganmacmini/m5-codex-pet
   lsof -nP -iTCP:8788 -sTCP:LISTEN
   ./install_mac.sh
   export PATH="$HOME/.local/bin:$PATH"
   launchctl print "gui/$(id -u)/com.codex-pet.bridge"
   lsof -nP -iTCP:8788 -sTCP:LISTEN
   cat "$HOME/Library/Application Support/Codex Pet/bridge-identity.json"
   curl --fail --silent http://127.0.0.1:8788/status
   codex-pet status
   ```

   Expect the package Python process on `*:8788`, the current
   `scutil --get LocalHostName` plus `.local:8788` in the public file, and eight
   fields with SLEEP/null history unless previously set. Usage may be null/stale.
   If bootstrap succeeds before the bridge is ready, retry curl after a second.
   If needed inspect `tail -50 "$HOME/Library/Application Support/Codex Pet/bridge-error.log"`.

2. Verify launchd restart and state expiry:

   ```sh
   codex-pet state RUNNING --ttl 10
   codex-pet status
   # Wait at least 10 seconds, then expect SLEEP.
   codex-pet status
   codex-pet state SUCCESS --ttl 30
   codex-pet state SLEEP
   launchctl kickstart -k "gui/$(id -u)/com.codex-pet.bridge"
   # Retry after a second if restarting.
   codex-pet status
   ```

   Expect SLEEP with last_result SUCCESS after restart. Run a normal local Codex
   session; if it emits the supported rate-limit event, usage should appear within
   30 seconds. Otherwise null/stale is the expected fallback. No private-file
   inspection or credentials are needed for this test.

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
ownership/collision/symlink guards, stop failure, launchd generation/dispatch,
unchanged helper/CLI dispatch, state expiry/history, local usage/schema/fallbacks,
and absence of HTTP mutation routes. They do not exercise real launchd, firewall,
pip networking, mDNS, USB or a LAN listener.
