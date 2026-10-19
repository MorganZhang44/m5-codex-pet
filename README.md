# M5StickS3 Codex status pet v0.6a — USB configuration

PlatformIO/Arduino firmware using M5Unified. The StickS3 passively reflects the
Mac agent-loop Codex Pet bridge. Pet expressions and real usage bars keep their
v0.4 meaning; Wi-Fi and HTTP failures never become `PetState::FAILED`.

## USB setup (default)

1. Build and flash the firmware. No private header or fixed Mac IP is required.
2. Connect the StickS3 to the computer with a USB data cable. With no valid saved
   Wi-Fi or optional compiled fallback it shows **USB SETUP** automatically.
   Otherwise hold **BtnB** (side button) for about **2 seconds** from PET or DEVICE.
   The same hold works at boot; release early to cancel. BtnA remains the page toggle.
3. Close PlatformIO/other serial monitors, then from the repository root run:

   ```sh
   python3 -m venv /tmp/codex-pet-setup-venv
   /tmp/codex-pet-setup-venv/bin/python -m pip install pyserial
   /tmp/codex-pet-setup-venv/bin/python scripts/codex_pet_setup.py
   ```

   The helper discovers common USB serial devices; if several exist it asks which
   one. Override with `--port /dev/cu.usbmodem...`, `/dev/ttyACM0`, or `COM3`.
   It checks HELP/CANCEL before asking for a password. If opening USB resets the
   board, keep the helper running and hold BtnB for 2 seconds again; it retries
   the handshake for up to 45 seconds with the port held open.
4. Review the **current computer**, its bridge endpoint, and **current Wi-Fi**.
   The helper uses the local bridge package identity when installed, otherwise
   macOS's stable `LocalHostName.local:8788`. It attempts to detect the current
   SSID; if unavailable, enter the computer's current **2.4 GHz Wi-Fi SSID**.
   Confirm this pairing, then enter and repeat the password in hidden prompts.
   A blank password explicitly selects an open network. Passwords are never
   retrieved from Keychain or any stored password source.
5. The helper stages **both** Wi-Fi and bridge, checks each acknowledgement,
   atomically saves the single pairing to NVS, and sends REBOOT. The device waits
   about one second after acknowledging. Check DEVICE and **ONLINE** after it
   reconnects to Wi-Fi and reads valid status.

Every setup replaces the one current computer + Wi-Fi pairing. There is no
pairing history, keep-previous option, bridge chooser, or automatic bridge mode.
Moving to another computer or Wi-Fi means running USB setup there again.

SSID is 1–32 UTF-8 bytes; password is empty or 8–63 UTF-8 bytes. Embedded NULs
are rejected. The helper keeps credentials in memory, never in arguments/files/logs.
Device replies never include stored SSID, passwords, or credential contents.
Without a secure terminal the helper stops before password entry.
NVS is not encrypted by this firmware; USB hex transport is not encryption.
The host helper requires Python 3 and pyserial; firmware dependencies stay unchanged.

Setup starts no AP or HTTP server. It stops polling/recovery, leaves any current
station association alone, and stays on its dedicated screen until reboot.
Entering setup does not modify saved configuration or the cached pet/usage/history.
A serial cable alone does not interrupt normal operation; commands are accepted
only on the USB SETUP screen. The helper asserts DTR for CDC communication and
keeps RTS deasserted without pulsing reset lines. If opening an adapter resets
the device into normal boot, hold BtnB again while the helper waits. If setup
times out, verify the screen, port, data
cable, and that no other program has the port open. Re-running cancels stale staged
changes before starting. A save failure never triggers a reboot; inspect over serial
or retry. A reset discards unsaved staged changes.

### Manual serial protocol

Use a serial terminal at **115200 baud**, local echo **off**, LF or CRLF lines.
The device announces `OK READY codex-pet-usb-v1; use HELP`; send `HELP` after
connecting if the announcement was missed. Commands are case-sensitive:

| Command | Effect / final acknowledgement |
| --- | --- |
| `HELP` | `OK HELP` followed by the command syntax on the same line |
| `STATUS` | Non-secret `INFO` lines: saved-Wi-Fi validity, association, saved bridge; ends `OK STATUS pending=yes` or `pending=no` |
| `WIFI <ssid-hex> <password-hex-or->` | Stage a full replacement pair; `OK WIFI staged` |
| `BRIDGE <host:port>` | Stage the current computer's DNS/IPv4 endpoint; `OK BRIDGE staged` |
| `CLEAR-WIFI` | Stage an empty saved record that suppresses compiled Wi-Fi fallback; `OK CLEAR-WIFI staged` |
| `CANCEL` | Discard all staged changes; `OK CANCEL` |
| `SAVE` | Require both WIFI and BRIDGE staged, or CLEAR-WIFI alone; atomically persist without reboot; `OK SAVE` |
| `REBOOT` | Reject pending changes; otherwise `OK REBOOT`, then restart after one second |

Wi-Fi tokens encode UTF-8 bytes as hexadecimal to preserve spaces and punctuation;
`-` means an empty password. Use the helper to avoid exposing typed credentials in
terminal history. No command reads saved credential contents. Bridge names are
lowercased; only bare DNS hostname/IPv4 and port 1–65535 are supported (no URL,
path, IPv6, or Bonjour display-instance name). Lines are bounded to 511 bytes;
overlong or control-containing lines return `ERR invalid-line` and resynchronize
at the next newline. Malformed commands return `ERR ...` without echoing input or
changing staged values. Commands may be fragmented across serial reads.

Wi-Fi and bridge share one atomic NVS blob (`pairing` in `codex-pet`). SAVE
requires a complete replacement, preventing mixed old/new pairs. Failed writes
leave the old pair authoritative and keep staged inputs for retry. The helper
never reboots after a failure. The transitional v0.6a `wifi` and `bridge` keys are
read only when no pairing record exists; a successful save deletes those obsolete
keys. Cleanup failure reports an error and requires retry, even if the new pair
has already been committed. This keeps only the current logical configuration;
NVS deletion is not a forensic secure erase of flash pages.
Save/clear never changes the active Wi-Fi session; reboot applies saved Wi-Fi.
The manual recovery command CLEAR-WIFI preserves the current bridge and enters
USB setup after reboot. It cannot be combined with a bridge replacement.

During normal runtime, saved credentials are tried first. If association fails
within 20 seconds, valid optional compiled credentials are tried for that boot only.
Without them, saved credentials retry with bounded backoff. HTTP/bridge failures
never switch Wi-Fi credentials. Corrupt or cleared saved Wi-Fi enters USB setup.

The pinned M5Unified **0.2.26** StickS3 implementation maps **BtnA to GPIO11**
and **BtnB to GPIO12**, active low, through `M5.update()` / `isPressed()`. The
side button here means **BtnB**, not the power/reset control (`BtnPWR`). The API
mapping is confirmed in the installed library; physical button identity, boot
power-button interactions, and real hold timing still need on-device validation.

## Normal pages

The device starts on the existing main pet/usage page. During normal operation,
short press and release **BtnA** (under 600 ms) to toggle between it and the
read-only **DEVICE** page. Longer BtnA holds do nothing. Holding BtnB for about
two seconds enters setup from either page. Setup retains its dedicated screen
and disables page switching.

DEVICE shows the current station Wi-Fi SSID, local IPv4 address, connected RSSI
in dBm, resolved **BRIDGE IP**, firmware **v0.6a**, and transport
health. Network code supplies a portable `DeviceInfo` snapshot; rendering does
not call Wi-Fi or discovery. Values refresh once per second and when transport
changes, subject to the existing synchronous network calls below. Unchanged
snapshots do not redraw; changes redraw only affected device rows, and hidden
pages cache updates without drawing. Returning
to the main page renders the latest passive snapshot, including reset labels.

OFFLINE/CONNECTING do not erase station SSID/IP that the Wi-Fi API still
reports. Unavailable values show **--**; RSSI is available only while associated.
Unresolved/invalidated bridges show **--**, independent of station connectivity.
The portrait layout uses separate label/value lines; long SSIDs end
in **...** within the screen width. Unsupported non-ASCII/control bytes display
as **?** in the built-in font, without wrapping or interpreting control text.
The existing DEVICE layout stays unchanged; full bridge identifiers are shown in
serial STATUS to avoid squeezing another row onto the portrait display. There is
no Wi-Fi SSID scanning, OTA, installer, or new animation.

## Current computer bridge identity

Run the existing Mac status-service project (containing `status_server.py` and
its `.venv-mcp` environment), for example:

```sh
.venv-mcp/bin/python status_server.py --host 0.0.0.0
```

The bridge must serve `/status` on a LAN-accessible listener, normally port 8788.
It is supplied by the separate Mac agent-loop project; this repository does not
install or change it. Allow it through the Mac firewall. Both devices must be on
a reachable local network; guest client isolation, VLAN boundaries, or blocked
multicast may prevent `.local` resolution/HTTP. IPv6-only endpoints are unsupported.

### Host helper detection and future installer contract

The helper reads only a dedicated **public identity file**, by default
`~/Library/Application Support/Codex Pet/bridge-identity.json`, or an explicit
`--identity-file PATH`. The future bridge installer should maintain this file:

```json
{"endpoint":"my-mac.local:8788"}
```

Publish the actual local computer's stable, LAN-resolvable hostname and actual
listener port; prefer `.local` over DHCP IPv4. On macOS, an address-only package
endpoint is upgraded to the current `LocalHostName.local` with the same port when
that hostname is available. Update the file when hostname/port
changes, and never put passwords, tokens, or other credentials in it. The helper
validates the endpoint and bounds file size; it does not inspect general bridge
configuration or private credential files. The current separate bridge project
has not been changed to publish this contract. A package could instead provide a
public local identity API in future; the helper currently implements the file
contract only.

Without the file, macOS `scutil --get LocalHostName` supplies `hostname.local`,
assuming the standard port 8788. `scutil --get ComputerName` supplies the displayed
computer name. This fallback identifies the current Mac but **does not verify**
that the bridge is installed, listening, or reachable from the Pet. Nonstandard
ports should come from the package file. `--bridge hostname:port` is an explicit
diagnostic override, with no interactive hostname entry in normal setup. On other
OSes an identity file or diagnostic override is required; `.local` advertisement
must be supplied by that computer's mDNS service.

On macOS, the helper finds Wi-Fi interfaces with `networksetup
-listallhardwareports` and asks `-getairportnetwork` for the associated SSID. It
never scans saved networks or reads Keychain/stored passwords. macOS versions,
Location Services/privacy restrictions, command output changes, or no active
Wi-Fi may prevent detection; it then asks for the current SSID. Verify the shown
SSID before confirming, including that the network supports the Pet's 2.4 GHz
radio. Detection does not establish band compatibility or end-to-end reachability.
SSID and password whitespace/UTF-8 bytes are preserved. There is no browser/AP
provisioning or permission-changing workaround.

### Runtime endpoint policy

Runtime connects only to the one configured hostname/IP and port. There is no
service browsing, compiled bridge fallback, previous bridge, or automatic mode.
Old empty/automatic records, missing records, and corrupt bridge records fail
closed: the Pet stays OFFLINE until paired over USB. A valid specific transitional
v0.6a bridge record remains usable until the next complete pairing replaces it.

For `.local`, the firmware starts its own mDNS client and calls
`MDNS.queryHost(hostname, 750)` for that exact hostname before each status request.
The installed Arduino ESP32 2.0.17 API supports this call. Other DNS names are
resolved by the HTTP connection. A changed DHCP address therefore requires no
new setup; changing the computer's hostname or bridge port does. An IP-only
diagnostic pairing requires setup again after its address changes. No other
advertised bridge is eligible. This is endpoint selection, not authenticated
computer identity.

Cached display addresses are cleared after Wi-Fi loss, device IP changes, or
HTTP/JSON failure. DEVICE shows the actual bridge IPv4 after a valid response,
and **--** after invalidation. DNS/mDNS resolution is retried with HTTP backoff.

### Optional private Wi-Fi recovery

USB setup is the default. For troubleshooting or migration, optionally copy
`include/pet_config.example.h` to **`include/pet_config.local.h`** and uncomment
only the Wi-Fi settings needed. Legacy `PET_STATUS_HOST` / `PET_STATUS_PORT`
definitions are deprecated and ignored; a USB bridge pairing is required.

Optional `PET_WIFI_SSID` / `PET_WIFI_PASSWORD` are used when no saved NVS
record exists, or after a saved association attempt times out (20 seconds).
Saved runtime credentials are attempted first; maintenance mode always wins.
Fallback never overwrites or clears saved credentials, and HTTP/bridge failure
alone never switches Wi-Fi credentials. After a connection drops, the current
credentials get a fresh 20-second attempt before fallback; subsequent failed
attempts are spaced up to 30 seconds apart. Normal connections explicitly use
station-only mode. USB REBOOT acknowledgements get a full one-second grace
before restart, including across `millis()` rollover.
A corrupt or explicitly cleared saved record enters setup instead of reverting
to compiled secrets. Changes to the optional header require rebuilding/flashing.
The private header and `.pio/` build artifacts remain gitignored. Keep configured
firmware binaries private as they may contain compiled credentials.

## Status contract and display

The bridge returns HTTP 200 with exactly these eight fields, once each, in any order:

```json
{"state":"SLEEP","last_result":"SUCCESS","usage_5h_percent":42.5,"usage_7d_percent":68,"usage_observed_at":"2026-10-19T14:00:00Z","usage_stale":false,"usage_5h_resets_at":"2026-10-19T18:22:00Z","usage_7d_resets_at":"2026-10-26T09:30:00Z"}
```

`state` maps directly to the passive pet expression:

- `SLEEP`: gray closed eyes and sleeping mouth.
- `RUNNING`: cyan open eyes, open mouth, and activity rays.
- `SUCCESS`: green happy eyes and smile.
- `FAILED`: red crossed eyes and frown, only on this explicit current state.

`last_result` independently accepts `SUCCESS`, `FAILED`, or JSON `null`, showing
**LAST OK**, **LAST FAIL**, or **LAST --** in a separate muted line. For example,
`state: SLEEP` with `last_result: FAILED` keeps the sleeping expression and shows
**LAST FAIL**. History reflects each valid server snapshot; the device does not
infer it from current state or persist it over reboot.

Usage bars show remotely supplied **USED percent**, 0–100 including fractions,
for the 5h and 7d windows. Labels round to whole percentages. Each percentage may
be `null`, displayed as **--%** with an empty bar. Before the first valid response,
both are unavailable and the pet sleeps.

`usage_5h_resets_at` and `usage_7d_resets_at` each require a UTC ISO-8601 string
or JSON `null`. The compact rows show **5h R18:22** and **7d RMon**, with the
used percentage aligned at the right and the existing usage bar below. Reset
labels use the timestamp's UTC time/day, not local time. Weekdays are English
**Mon/Tue/Wed/Thu/Fri/Sat/Sun**. Supported timestamps have the form
`YYYY-MM-DDTHH:MM:SS[.fraction]Z` (also accepting `+00:00`); dates and times are
validated, including Gregorian leap years. Null, unparseable, or unavailable
reset values display **R--**. Invalid timestamp text does not discard otherwise
valid usage. Wrong JSON types, malformed strings, missing fields, duplicates,
and extra fields reject the entire snapshot atomically. The eight-field bridge
contract is required; legacy six-field replies are rejected.

Reset labels need no network time, NTP, timezone setup, or device clock. They
never determine freshness or trigger a local countdown.

`usage_stale` must be a boolean. True shows **USAGE STALE** in yellow while
retaining the supplied values. `usage_observed_at` accepts a finite JSON number,
JSON string, or `null`. It is validated without assuming units/format, then
ignored; the device has no need to calculate freshness using its own clock.
The server-side usage snapshot is cached by the Mac bridge. Usage arrives in
the same `/status` response: there is no second endpoint/request. Device usage
redraws are change-driven, only when percentage values, parsed reset display
components (UTC hour/minute/weekday), or `usage_stale` change. Changes only to
`usage_observed_at`, pet state, history, or transport do not redraw usage.

The footer reports **CONNECTING**, **ONLINE**, or **OFFLINE** independently;
setup uses its dedicated **USB SETUP** screen. ONLINE means the most recent request
returned an entirely valid snapshot, even when its current pet state is FAILED.
Wi-Fi loss, timeouts, HTTP failures, oversized bodies, and invalid JSON retain
the last valid pet, history, usage values, reset labels, and supplied freshness.
Transport loss does not synthesize stale usage or a failed pet. Recovery resumes
automatically.

## Polling and reconnect

Intervals are measured from request completion, avoiding immediate catch-up
requests after a slow response:

| Last valid current state | Next status request |
| --- | --- |
| RUNNING | 900 ms |
| SLEEP | 2500 ms |
| SUCCESS / FAILED | 1500 ms |

HTTP/resolution failures back off **2, 4, 8, 16, then 30 seconds**, capped at 30;
a valid snapshot resets that backoff. Wi-Fi association/reconnect attempts wait
**20, then 30 seconds**, capped at 30; a successful connection resets this
sequence. Link loss starts a fresh attempt immediately. There are no HTTP
requests while Wi-Fi is disconnected or in setup.
HTTP connect/read timeouts are 750 ms each, not an overall wall-clock deadline;
slow/chunked peers can extend blocking time. Hostname resolution may also briefly pause the display loop. No periodic demo or autonomous pet transitions
are introduced.

## Build and verification

With PlatformIO Core installed:

```sh
pio run
```

If needed use `~/.platformio/penv/bin/pio`. To flash deliberately, connect an
M5StickS3 by USB and run `pio run --target upload`; specify
`--upload-port /dev/cu.YOUR_DEVICE` if automatic detection fails. To enter download
mode, hold the side reset button until the internal green LED flashes, then retry.
The existing board configuration retains 8 MB flash and octal PSRAM; M5Unified
and M5GFX versions remain pinned. No new third-party dependencies are required.

Host checks (no hardware or private configuration needed):

```sh
c++ -std=c++11 -Wall -Wextra -pedantic -Isrc tests/status_parser_test.cpp -o /tmp/pet-parser-test
/tmp/pet-parser-test
c++ -std=c++11 -Wall -Wextra -pedantic -Isrc tests/network_policy_test.cpp -o /tmp/pet-network-test
/tmp/pet-network-test
c++ -std=c++11 -Wall -Wextra -pedantic -Isrc tests/device_page_test.cpp -o /tmp/pet-device-test
/tmp/pet-device-test
c++ -std=c++11 -Wall -Wextra -pedantic -Isrc tests/setup_entry_test.cpp -o /tmp/pet-setup-test
/tmp/pet-setup-test
c++ -std=c++11 -Wall -Wextra -pedantic -Itests/stubs -Isrc tests/pet_ui_test.cpp src/pet_ui.cpp -o /tmp/pet-ui-test
/tmp/pet-ui-test
c++ -std=c++11 -Wall -Wextra -pedantic -Isrc tests/bridge_selection_test.cpp -o /tmp/pet-bridge-test
/tmp/pet-bridge-test
python3 tests/run_wifi_config_test.py
python3 tests/setup_helper_test.py
git diff --check
```

The parser suite covers all current/history combinations, numeric/null usage,
freshness, UTC reset labels and English weekdays, leap years, null/invalid reset
fallbacks, JSON escapes, timestamp forms, field ordering, truncation, and
malformed/missing/extra/duplicate fields. The entire previous snapshot survives rejection.
Policy/config tests cover polling intervals, retry caps/reset, millis rollover,
credential bounds and embedded NULs, NVS/legacy precedence, and maintenance/clear
recovery decisions. Hardware NVS persistence, button timing, real USB enumeration,
mDNS, DHCP changes, reconnect, and physical display layout need on-device validation.
Provisioning tests run the real serial parser, Wi-Fi and bridge modules against
fake USB/Wi-Fi/NVS with synthetic credentials and a staged public config header.
They cover saved/legacy recovery, absent/corrupt/cleared records, online/offline
USB entry without AP or reassociation, rejected/overlong/NUL input, staging/cancel,
rejection of partial pairings, atomic replacement/failure, legacy-key cleanup,
exact-host DHCP re-resolution, rejection of legacy automatic records, clear
isolation, secret-free replies, and
reboot grace across clock rollover. Host-helper tests cover encoding/validation,
acknowledgement failures/timeouts, public package identity/macOS hostname fallback,
SSID detection/fallback, confirmation before secure password entry, cancellation,
and successful complete pairing/reboot.
Device/page tests cover boot-release suppression, repeated short presses, long
holds, millis rollover, setup exclusion, snapshot comparisons, safe truncation,
and placeholder formatting. Setup-entry tests cover the same 2-second threshold
at boot and during use, early release/cancellation, one-shot requests, millis
rollover, delayed sampling, both pages, and simultaneous BtnA/setup priority.
The actual UI runs against a host display stub to check all pet/history
combinations survive page switching, unchanged snapshots skip redraws, hidden
pet/usage updates appear on return, and setup stays dedicated from either page.

## Module boundaries

- `status_data.h`, `pet_state.h`: passive snapshot types and independent history.
- `status_parser.h`: portable strict eight-field parsing, atomic snapshot update.
- `usage_reset.h`: portable UTC reset validation and compact label formatting.
- `network_policy.h`: portable transport enum, intervals, timers, and backoff.
- `device_info.h`: portable read-only device snapshot and bounded display text.
- `page_navigation.h`: portable BtnA page controller with boot-release guard.
- `setup_entry.h`: portable one-shot BtnB hold detector shared by boot and runtime.
- `wifi_credentials.h`: portable validation, stable storage layout, source choice.
- `wifi_config.cpp`: USB serial adapter, Wi-Fi Preferences storage, legacy config.
- `serial_setup.h`: bounded portable serial parser, staging and save/reboot protocol.
- `scripts/codex_pet_setup.py`: interactive USB host setup helper.
- `bridge_selection.h`: portable endpoint validation and exact matching policy.
- `bridge_config.cpp`: current endpoint loading and exact-host mDNS resolution.
- `pairing_config.h`: single atomic Wi-Fi + bridge record layout.
- `http_state_source.cpp`: Wi-Fi retry timing, resolution, bounded 512-byte response
  handling (including chunked bodies), adaptive HTTP polling, transport health,
  and station/current bridge information.
- `pet_ui.cpp`: change-driven rendering; `main.cpp`: boot/runtime button handling and wiring.

The firmware only reads the bridge; it never writes agent state.
