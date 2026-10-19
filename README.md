# M5StickS3 Codex status pet v0.5

PlatformIO/Arduino firmware using M5Unified. The StickS3 passively reflects the
Mac agent-loop Codex Pet bridge. Pet expressions and real usage bars keep their
v0.4 meaning; Wi-Fi and HTTP failures never become `PetState::FAILED`.

## Runtime Wi-Fi setup (default)

1. Build and flash the firmware. No private header or Mac IP is required.
2. With no saved Wi-Fi (and no optional legacy credentials), the display shows
   **SETUP WI-FI**, the setup network, and its browser address.
3. Join **CodexPet-Setup-<chip suffix>** from a phone or computer. This local AP
   has no Internet; stay connected if the operating system asks.
4. Open **http://192.168.4.1** manually. Enter the SSID and password for a 2.4 GHz
   Wi-Fi network reachable by the Mac, then select **Save and restart**.
5. The device saves the credential pair atomically in ESP32 Preferences/NVS,
   restarts after about one second, connects, discovers the bridge, and shows
   **ONLINE** after the first valid status response.

SSID length is 1–32 bytes. Passwords may be empty for an open network or 8–63
bytes for a WPA passphrase. The page never fills in or returns saved credentials.
The setup AP is open and the form uses local HTTP; provision it in a trusted
nearby environment. NVS credentials are not encrypted by this firmware. The web
server runs only in setup mode and credentials are never sent to the Mac bridge
or printed to serial. No captive portal/DNS interception or third-party portal
library is used; ESP32 `WiFi`, `WebServer`, and `Preferences` provide setup.

### Reconfigure or recover

Hold **BtnA while powering on/resetting**, continue holding for about 1.5 seconds
after the **Hold BtnA for setup** prompt, and release once setup appears. This
opens setup even with valid saved or compiled credentials. It preserves the
saved pair until you save replacements or choose **Clear saved Wi-Fi** on the
page. Clearing leaves an empty NVS record so legacy credentials cannot silently
reconnect on the next boot. Save a new network on the page to restart, or reset
to remain in setup. If you entered an incorrect password or changed routers,
use this same boot procedure. Brief button holds cancel maintenance; BtnA is
never read for pet transitions during normal operation.

## Mac bridge and Bonjour

Run the existing Mac status-service project (containing `status_server.py` and
its `.venv-mcp` environment), for example:

```sh
.venv-mcp/bin/python status_server.py --host 0.0.0.0
```

The Mac bridge must advertise **`_codex-pet._tcp`** over Bonjour, preferably as
**Codex Pet**, and serve `/status` on its advertised port. The bridge is supplied
by the separate Mac agent-loop project; this repository does not install or
change it. Allow the bridge through the Mac firewall. Both devices must be on
a reachable local network with multicast enabled; guest client isolation or
VLAN boundaries may block discovery/HTTP. On the Mac, inspect advertisements:

```sh
dns-sd -B _codex-pet._tcp local.
```

The firmware browses using `MDNS.begin()`,
`MDNS.queryService("_codex-pet", "_tcp")`, `MDNS.IP(index)`, and
`MDNS.port(index)`. These APIs were verified against the installed
**Arduino ESP32 2.0.17** `libraries/ESPmDNS/src/ESPmDNS.h`, package
`framework-arduinoespressif32@3.20017.241212+sha.dcc1105b`, selected by the pinned
`espressif32@6.12.0` platform. This version has **no instance-name getter**:
`setInstanceName()` names the device itself and cannot inspect remote results.
Selection is deterministic first-match: lowest result index with a nonzero IPv4
address and port. It cannot prefer `Codex Pet` by name. Result ordering can vary
between queries; run one bridge per LAN when selection matters. A newer framework
installed alongside it exposes different APIs but is not the project's framework.

Discovery supplies both address and port; normal configuration needs no Mac IP.
Cached endpoints are discarded after Wi-Fi loss, device IP changes, or any HTTP/
JSON failure. Discovery restarts after reconnect and before retrying a failed
endpoint. Even a healthy endpoint is rediscovered every 60 seconds (at the next
poll) to pick up changed advertisements. The installed browse call is synchronous
and can block for about **3 seconds**; it asks for up to 20 results. Only IPv4
services are used.

### Optional private fallback

Runtime setup is the default. For troubleshooting or migration, optionally copy
`include/pet_config.example.h` to **`include/pet_config.local.h`** and uncomment
only the settings needed. `PET_STATUS_HOST` is a bare IP/hostname (no scheme,
port, or path), used when discovery yields no usable service. Its
`PET_STATUS_PORT` defaults to 8788; a discovered service always uses its own port.
Discovery is retried while using a fallback, too.

Optional `PET_WIFI_SSID` / `PET_WIFI_PASSWORD` are used only when no saved NVS
record exists. Saved runtime credentials win; maintenance mode always wins.
A corrupt or explicitly cleared saved record enters setup instead of reverting
to compiled secrets. Changes to the optional header require rebuilding/flashing.
The private header and `.pio/` build artifacts remain gitignored. Keep configured
firmware binaries private as they may contain compiled credentials.

## Status contract and display

The bridge returns HTTP 200 with exactly these six fields, once each, in any order:

```json
{"state":"SLEEP","last_result":"SUCCESS","usage_5h_percent":42.5,"usage_7d_percent":68,"usage_observed_at":"2026-10-19T14:00:00Z","usage_stale":false}
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

`usage_stale` must be a boolean. True shows **USAGE STALE** in yellow while
retaining the supplied values. `usage_observed_at` accepts a finite JSON number,
JSON string, or `null`. It is validated without assuming units/format, then
ignored; the device has no need to calculate freshness using its own clock.
The server-side usage snapshot is cached by the Mac bridge. Usage arrives in
the same `/status` response: there is no second endpoint/request. Device usage
redraws are change-driven, only when percentage values or `usage_stale` change.
Timestamp-only, pet-state, history, and transport changes do not redraw usage.

The footer reports **CONNECTING**, **ONLINE**, or **OFFLINE** independently;
setup uses its own **SETUP WI-FI** screen. ONLINE means the most recent request
returned an entirely valid snapshot, even when its current pet state is FAILED.
Wi-Fi loss, timeouts, HTTP failures, oversized bodies, and invalid JSON retain
the last valid pet, history, usage values, and supplied freshness. Transport loss
does not synthesize stale usage or a failed pet. Recovery resumes automatically.

## Polling and reconnect

Intervals are measured from request completion, avoiding immediate catch-up
requests after a slow response:

| Last valid current state | Next status request |
| --- | --- |
| RUNNING | 900 ms |
| SLEEP | 2500 ms |
| SUCCESS / FAILED | 1500 ms |

HTTP/discovery failures back off **2, 4, 8, 16, then 30 seconds**, capped at 30;
a valid snapshot resets that backoff. Wi-Fi association/reconnect attempts wait
**10, 20, then 30 seconds**, capped at 30; a successful connection resets this
sequence. There are no HTTP requests while Wi-Fi is disconnected or in setup.
HTTP connect/read timeouts are 750 ms each, not an overall wall-clock deadline;
slow/chunked peers can extend blocking time. Bonjour's synchronous browse also
briefly pauses the display loop. No periodic demo or autonomous pet transitions
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
git diff --check
```

The parser suite covers all current/history combinations, numeric/null usage,
freshness, timestamp forms, field ordering, truncation, and malformed/missing/
extra/duplicate fields. The entire previous snapshot survives rejection.
Policy/config tests cover polling intervals, retry caps/reset, millis rollover,
credential bounds and embedded NULs, NVS/legacy precedence, and maintenance/clear
recovery decisions. AP/NVS hardware persistence, button timing, actual Bonjour,
DHCP changes, reconnect, and physical display layout need on-device validation.

## Module boundaries

- `status_data.h`, `pet_state.h`: passive snapshot types and independent history.
- `status_parser.h`: portable strict six-field parsing, atomic snapshot update.
- `network_policy.h`: portable transport enum, intervals, timers, and backoff.
- `wifi_credentials.h`: portable validation, stable storage layout, source choice.
- `wifi_config.cpp`: runtime AP/page, Preferences storage, optional legacy config.
- `http_state_source.cpp`: Wi-Fi retry timing, discovery, bounded 512-byte response
  handling (including chunked bodies), adaptive HTTP polling, transport health.
- `pet_ui.cpp`: change-driven rendering; `main.cpp`: boot maintenance and wiring.

The firmware only reads the bridge; it never writes agent state.
