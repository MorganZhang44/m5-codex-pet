# M5StickS3 Codex status pet v0.3

Minimal PlatformIO/Arduino firmware using M5Unified. The M5StickS3 passively
reflects the Mac agent-loop Codex Pet HTTP status service over Wi-Fi:

```text
GET http://<mac-lan-ip>:8788/status
{"state":"SLEEP"}
```

The service must return HTTP 200 with a JSON object containing exactly one
`state` field. The four case-sensitive values map directly to `PetState`:

- `SLEEP`: gray closed eyes and a small sleeping mouth.
- `RUNNING`: cyan open eyes, open mouth, and activity rays.
- `SUCCESS`: green happy eyes and a smile.
- `FAILED`: red crossed eyes and a frown (only an explicit service state).

There is no timed demo cycle or button-driven state change. Usage remains mock
data: **5h=42%, 7d=68%**. The expressions are unchanged.

## 1. Start the Mac service

In the Mac status-service project directory (the directory containing
`status_server.py` and its existing `.venv-mcp` environment), run:

```sh
.venv-mcp/bin/python status_server.py --host 0.0.0.0
```

This service is supplied by the Mac agent-loop project, not this firmware repo.
Keep it running and ensure the real agent loop updates its status. Connect the
Mac and M5StickS3 to the same reachable LAN; the StickS3 needs 2.4 GHz Wi-Fi.
Find the Mac's LAN IPv4 address in System Settings → Wi-Fi → Details → TCP/IP.
Use that address below, not `localhost` or `0.0.0.0`. Allow the Python service
through the Mac firewall if prompted; guest-network client isolation can prevent
connection to the Mac.

Check the endpoint, substituting the Mac address:

```sh
curl --fail http://192.168.1.100:8788/status
```

Expected shape: `{"state":"SLEEP"}` (or `RUNNING`, `SUCCESS`, `FAILED`).

## 2. Create private firmware configuration

From this firmware directory:

```sh
cp include/pet_config.example.h include/pet_config.local.h
```

Edit **`include/pet_config.local.h`** with your Wi-Fi SSID/password and the Mac's
LAN IP. `PET_STATUS_HOST` is a bare IP or hostname, without a scheme, port or
path. `PET_STATUS_PORT` defaults to `8788`; change it in the same private file
if the service uses another port. Empty passwords are allowed for open Wi-Fi.

Only the example is intended for version control. The private file is ignored
by `.gitignore`; never put real credentials in the example or other tracked
files. Credentials are compiled into the firmware, so keep configured build
artifacts private too. If the private header is absent, safe empty defaults
still compile: the device stays at **SLEEP / STATUS OFFLINE** without attempting
a connection. Rebuild and upload after changing configuration.

## 3. Build and upload

With PlatformIO Core installed, run from this directory:

```sh
pio run
pio run --target upload
```

If `pio` is not on your PATH, the usual local installation is
`~/.platformio/penv/bin/pio` (use it in place of `pio` in these commands).
Connect an M5StickS3 by USB. If automatic port detection fails, run:

```sh
pio device list
pio run --target upload --upload-port /dev/cu.YOUR_DEVICE
```

To enter download mode, hold the side reset button until the internal green LED
flashes, then retry upload.

## Runtime and module boundaries

`src/http_state_source.cpp` connects asynchronously to Wi-Fi, retries a lost
connection every 10 seconds, and polls `/status` about once per second when
connected. HTTP connect/read timeouts are 750 ms each; slow requests can stretch
the interval and briefly block the main loop. Responses are bounded to 128 bytes,
including chunked bodies. `src/status_parser.h` validates the single-field JSON
contract with optional JSON whitespace and no additional JSON library.

Only valid replies reach `PetUi::setState()`. Wi-Fi loss, HTTP errors, timeouts,
malformed JSON, unknown states, or oversized responses keep the last valid pet
state and show **STATUS OFFLINE**. Until the first valid reply, the pet sleeps.
**STATUS ONLINE** means the most recent poll returned a valid state, including
an explicit `FAILED`. Recovery resumes automatically. The footer is transport
health, independent of the agent state; it is refreshed separately from the pet.

`src/pet_state.h` defines the enum; `src/pet_ui.cpp` owns rendering. The source
module can be replaced without changing expressions or adding transitions to
the UI. The firmware only reads the service; it never writes agent state.

The existing PlatformIO configuration uses the generic ESP32-S3 board with 8 MB
flash and octal PSRAM. M5Unified and M5GFX versions remain pinned; M5Unified
includes the StickS3 power-management driver. No separate M5PM1 library is needed.

The parser's host-side checks cover all four states, whitespace, and rejection
of malformed/unknown/duplicate states while preserving the previous state:

```sh
c++ -std=c++11 -Isrc tests/status_parser_test.cpp -o /tmp/pet-parser-test
/tmp/pet-parser-test
```
