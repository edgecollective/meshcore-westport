# MeshCore Telemetry Progress

Date: 2026-04-10

## Goal

Build a companion-based telemetry path in MeshCore where:

- a sender node is based on companion BLE firmware
- the sender measures simple sensor data, starting with battery level
- the sender pushes telemetry toward a specific companion receiver
- the receiver is based on serial companion firmware
- telemetry must work through repeaters, not only at zero hops
- Wi-Fi upload is deferred until the RF path is proven

## Investigation Findings

The initial codebase review focused on how MeshCore handles:

- companion contacts and shared secrets
- direct versus flood routing
- telemetry request/response handling
- path learning through repeaters

Main findings:

1. Repeaters do not need the end nodes' public keys or ECDH shared secrets to forward companion-to-companion traffic.
   They only evaluate the path hash and retransmit if they are the next hop.

2. End-to-end encrypted companion traffic depends on both nodes already having each other in their contact list.
   The receiver decrypts by looking up the sender via its contact table and deriving or using the cached shared secret.

3. The likely multi-hop failure mode is stale or missing `out_path`, not repeater-side key knowledge.
   The stock companion sends direct if a path is known and flood if not.

4. The stock companion firmware does not implement useful resend logic in `onSendTimeout()`.
   That means a bad remembered path can fail without automatic recovery.

5. MeshCore already contains a path-discovery mechanism that forces a flood telemetry request to refresh the route.

6. Path hash size compatibility matters.
   Older repeaters only forward 1-byte path-hash packets.

## Architecture Decision

We decided not to base the solution on `simple_sensor`.

Instead, the design is:

- sender: companion-based BLE firmware that pushes telemetry periodically
- receiver/gateway: companion-based serial firmware that accepts pushed telemetry and logs it locally

This is a better fit than the stock `simple_sensor` model, which is oriented around server-style login and pull telemetry.

## New Firmware Work Completed

### Shared companion helper changes

Added helper support in the shared companion implementation for manual contact management:

- `upsertManualContact(...)`
- `persistContactsNow()`

Files changed:

- `MeshCore/examples/companion_radio/MyMesh.h`
- `MeshCore/examples/companion_radio/MyMesh.cpp`

Also made the default `extern MyMesh the_mesh` declaration suppressible so derived companion examples can use their own mesh globals cleanly.

### New sender example

Added new example directory:

- `MeshCore/examples/companion_sensor_ble`

Purpose:

- BLE companion sender
- can use a configured compile-time target, but now also supports explicit runtime target provisioning over USB serial
- sends battery telemetry every `TELEMETRY_INTERVAL_SECS`
- retries once as flood if a direct attempt times out

Current payload:

- custom request type `REQ_TYPE_SENSOR_UPLOAD`
- version byte
- battery millivolts
- configured send interval

Current sender behavior:

- default telemetry interval is now `20` seconds
- a user-button click triggers an immediate telemetry send when no response is pending
- the Heltec OLED shows sender state, including target status, next send time, battery mv, and the last send/result message

### New gateway example

Added new example directory:

- `MeshCore/examples/companion_gateway_serial`

Purpose:

- serial companion receiver/gateway
- accepts the custom `REQ_TYPE_SENSOR_UPLOAD`
- logs received uploads to local serial

Added local serial console commands:

- `!help`
- `!self pubkey`
- `!contact add <pubkey_hex> <name>`
- `!self card`
- `!contact import <meshcore://card>`

This allows provisioning a sender contact on the gateway without requiring the two radios to be in RF range of each other.

The `meshcore://` card currently contains the full advert packet encoded as hex. The important design point is that the full advert is being imported, not just a raw pubkey.

### Sender target-selection updates

The sender firmware was extended so the gateway target does not have to be compiled in.

Added local USB serial commands on the sender:

- `!help`
- `!self pubkey`
- `!self card`
- `!target import <meshcore://card>`
- `!target set <pubkey_hex>`
- `!target show`
- `!target clear`

Behavior:

- `!target import` imports the gateway advert and sets that contact as the telemetry destination
- `!target set` explicitly sets the telemetry destination by pubkey
- `!target show` prints the currently selected telemetry target
- `!target clear` removes the explicit target selection

Important implementation detail:

- the sender now persists the selected target pubkey in the blob store, so explicit target choice survives reboot
- if no explicit target is stored, the sender falls back to heuristics:
  - prefer a contact named `gateway`
  - otherwise use the only chat contact if exactly one exists

This is the preferred model when the sender may have multiple contacts. The contact list remains general-purpose, and telemetry uses one separately selected destination.

## Radio Default Changes

The repo-wide defaults were updated to the user-specified USA values:

- frequency: `910.525`
- bandwidth: `62.5`
- spreading factor: `7`
- coding rate: `5`

Updated in:

- `MeshCore/platformio.ini`
- `MeshCore/examples/companion_radio/MyMesh.h`
- `MeshCore/examples/simple_repeater/MyMesh.cpp`
- `MeshCore/examples/simple_sensor/SensorMesh.cpp`
- `MeshCore/examples/simple_secure_chat/main.cpp`
- `MeshCore/examples/simple_room_server/MyMesh.h`

The new sender/gateway example wrappers were aligned to the same values.

## Build Integration Completed

Added concrete Heltec v3 PlatformIO environments:

- `Heltec_v3_companion_sensor_ble`
- `Heltec_v3_companion_gateway_serial`

Updated file:

- `MeshCore/variants/heltec_v3/platformio.ini`

Important follow-up fix:

- both the sender and gateway example source files had OLED display code
- however, their original Heltec v3 build targets did not include `DISPLAY_CLASS=SSD1306Display` or the SSD1306 source files
- this meant the display logic was compiled out even though the code existed
- both Heltec v3 targets were later corrected to include:
  - `-D DISPLAY_CLASS=SSD1306Display`
  - `helpers/ui/SSD1306Display.cpp`
  - `helpers/ui/MomentaryButton.cpp`

## Latest Build Results

Both new Heltec v3 targets were compiled successfully.

Results:

- `pio run -e Heltec_v3_companion_gateway_serial`
  - SUCCESS
  - Duration: about 3m 19s

- `pio run -e Heltec_v3_companion_sensor_ble`
  - SUCCESS
  - Duration: about 3m 39s

Approximate resulting image sizes:

- gateway serial:
  - RAM: 30.9%
  - Flash: 17.8%

- sender BLE:
  - RAM: 41.3%
  - Flash: 37.0%

Later OLED-enabled rebuilds also succeeded:

- `pio run -e Heltec_v3_companion_gateway_serial`
  - SUCCESS
  - RAM: 31.0%
  - Flash: 18.2%

- `pio run -e Heltec_v3_companion_sensor_ble`
  - SUCCESS
  - RAM: 41.5%
  - Flash: 37.5%

## Hardware Bring-Up Results

The gateway firmware was flashed successfully to a connected Heltec v3 on:

- `/dev/ttyUSB0`

Upload command used:

- `pio run -e Heltec_v3_companion_gateway_serial -t upload --upload-port /dev/ttyUSB0`

Result:

- upload succeeded
- device reset successfully after flashing
- later reflashed successfully with OLED-enabled gateway target
- gateway now has compiled-in display support for showing last telemetry received

The sender firmware was also flashed successfully to a connected Heltec after the USB serial device re-enumerated from `/dev/ttyUSB0` to `/dev/ttyUSB1`.

Upload commands used:

- initial attempt:
  `pio run -e Heltec_v3_companion_sensor_ble -t upload --upload-port /dev/ttyUSB0`
- successful retry after re-enumeration:
  `pio run -e Heltec_v3_companion_sensor_ble -t upload --upload-port /dev/ttyUSB1`

Result:

- initial sender upload failed only because `/dev/ttyUSB0` disappeared during re-enumeration
- retry on `/dev/ttyUSB1` succeeded
- device reset successfully after flashing
- later reflashed successfully with OLED-enabled sender target
- sender now has compiled-in display support for send/status feedback

Final flash state:

- gateway OLED-enabled firmware flashed successfully to `/dev/ttyUSB0`
- sender OLED-enabled firmware flashed successfully to `/dev/ttyUSB1`

## Serial Console Verification

The gateway serial console was verified on:

- port: `/dev/ttyUSB0`
- baud: `115200`
- format: `8-N-1`

Known-good monitor command:

- `pio device monitor -p /dev/ttyUSB0 -b 115200 --echo`

Observed behavior:

- `!help` returned the expected command list
- the gateway console is working
- some terminal programs may not locally echo typed characters by default
- lack of local echo does not mean the command failed

Practical note:

- if the terminal does not echo typed input, type the command anyway and press Enter
- `--echo` with `pio device monitor` is the easiest way to avoid confusion

Commands confirmed working:

- `!help`
- `!self pubkey`
- `!self card`

Sender provisioning and verification status:

- sender firmware was flashed successfully
- sender target import and verification were reported as working
- the next practical step is RF path verification rather than more provisioning work
- mixed binary companion frames and readable serial logs on the gateway console are expected because both share the same USB serial port

## Current State

What is done:

- codebase investigation completed
- companion-to-companion telemetry approach selected
- manual contact add support implemented
- BLE sender example added
- serial gateway example added
- gateway business-card export/import added
- Heltec v3 build targets added
- both new firmware targets compile successfully
- gateway firmware flashed to hardware successfully
- gateway serial console verified successfully
- sender target selection can now be provisioned over USB serial
- sender target selection is now persisted across reboot
- sender firmware flashed to hardware successfully
- sender target import/verification completed
- sender now transmits on a 20-second interval by default
- sender now supports manual send on button click
- sender OLED now displays send/status information
- gateway OLED now displays the last received telemetry
- both flashed Heltec builds now actually include OLED support
- repo defaults switched to requested US radio values

What is not done yet:

- no RF telemetry testing yet
- no Wi-Fi forwarding yet
- no extra board families added beyond Heltec v3
- no higher-level documentation for using the new targets yet

## Recommended Next Steps

1. On the sender, confirm the target is still selected using:
   `!target show`
2. On the gateway serial console, make sure the sender is still present as a contact using:
   `!contact add <sender_pubkey_hex> <name>`
   or
   `!contact import <meshcore://...>`
3. Verify direct zero-hop uploads first.
4. Watch for:
   - gateway serial log lines for `sensor_upload`
   - sender OLED status changing through `sent ...` and `ack ok`
   - gateway OLED updating with sender name, battery mv, and interval
5. Press the sender button and confirm that it causes an immediate upload outside the normal 20-second cadence.
6. Then verify uploads through one repeater.
7. If multi-hop succeeds, add Wi-Fi forwarding on top of the gateway path.

## Bayou Gateway Work

Added a new Wi-Fi posting gateway variant:

- `MeshCore/examples/companion_gateway_bayou_serial`

Purpose:

- receive MeshCore telemetry over RF like the serial gateway
- connect to Wi-Fi
- POST decoded telemetry to Bayou
- show compact Wi-Fi, Bayou, and telemetry status on the Heltec OLED

Added Heltec v3 build target:

- `Heltec_v3_companion_gateway_bayou_serial`

### Bayou gateway configuration changes

The Bayou gateway originally carried Wi-Fi and Bayou credentials in tracked PlatformIO build flags.
This was changed so live credentials live only in a local header:

- live local file:
  `MeshCore/examples/companion_gateway_bayou_serial/secrets.h`
- checked-in template:
  `MeshCore/examples/companion_gateway_bayou_serial/secrets.example.h`
- ignored by git in:
  `.gitignore`

Configured local values:

- `WIFI_SSID`: `nebo-nuthouse`
- `WIFI_PWD`: `spacecat`
- `BAYOU_PUBLIC_KEY`: `digpudphgqj9`
- `BAYOU_PRIVATE_KEY`: `3e3qk844e6gc`

### Bayou posting behavior

The gateway now posts JSON shaped for Bayou with:

- `private_key`
- `node_id`
- `temperature_c`
- `battery_volts`

Important corrections made:

1. `battery_volts` is posted under exactly that field name.
2. `distance_meters` was removed and replaced with `temperature_c`.
3. the Bayou URL was switched from HTTP to HTTPS to avoid a `301` redirect failure.
4. redirect following was enabled as an extra safeguard.

### Bayou gateway OLED behavior

The Bayou gateway display was compressed to fit the small Heltec screen.

Current display intent:

- title line: `Bayou GW`
- Wi-Fi state line: `WiFi: ...`
- Bayou post line: `Post: ...`
- sender line: `From: ...` or `No sensor data`
- condensed data line: `N:<node_id> T:<temperature_c> B:<battery_volts>`
- route line: `R:<relay> H:<hops>`

Later improvement:

- after a successful post, the display now shows:
  `Post: ok <Ns ago>`
- that age updates live as time passes since the last successful Bayou POST

### Bayou gateway route display

The receiver/gateway display was updated to show route details from the received RF packet:

- sender name
- relay
- hops

Interpretation:

- `relay` is the last repeater hash carried in the received path
- if no repeater participated, the relay shows `direct`
- `hops` comes from the received path hash count

### Bayou gateway build and flash status

Latest verified build:

- `pio run -e Heltec_v3_companion_gateway_bayou_serial`
  - SUCCESS
  - RAM: 38.9%
  - Flash: 35.9%

Latest verified flash:

- `pio run -e Heltec_v3_companion_gateway_bayou_serial -t upload --upload-port /dev/ttyUSB1`
  - SUCCESS
  - duration about 2m 5s

Current hardware state:

- the Bayou gateway is flashed on `/dev/ttyUSB1`
- it should now show Wi-Fi status, Bayou post status, sender identity, condensed telemetry, relay, and hops

## Sender Telemetry Payload Work

The sender telemetry payload was extended beyond battery-only reporting.

### Node ID support

The sender now persists and transmits a `node_id`.

Local sender serial commands:

- `!nodeid show`
- `!nodeid set <number>`

Behavior:

- `node_id` is stored persistently on the sender
- the sender includes `node_id` in every telemetry upload
- gateways that understand the newer payload forward the real `node_id`
- older sender firmware will still be accepted by the gateway, with fallback `node_id = 0`

### 1-wire temperature support

The sender now includes support for a 1-wire temperature sensor.

Electrical assumptions:

- sensor data line on `GPIO7`
- sensor power on `GPIO6`

Behavior:

1. the sender powers the sensor from `GPIO6`
2. it reads temperature over 1-wire on `GPIO7`
3. if the sensor does not respond, it power-cycles `GPIO6`
4. it retries the read after the reset
5. if the sensor still does not respond, it sends placeholder temperature `-40.0 C`

Implementation notes:

- `OneWire` library added
- `DallasTemperature` library added
- sender payload version increased to carry:
  - battery millivolts
  - interval seconds
  - `node_id`
  - `temperature_x10`

The gateway parser and Bayou gateway were updated to understand both:

- old payloads without temperature
- new payloads with `node_id` and `temperature_x10`

### Temperature to Bayou mapping

The end-to-end intent is now implemented as:

- sender reads sensor temperature
- sender transmits `temperature_x10`
- Bayou gateway converts this to `temperature_c`
- Bayou POST sends the real `temperature_c` field

If the sender is running older firmware or the sensor cannot be read:

- `temperature_c` will currently resolve to `-40.0`

### Sender display update

The sender OLED was also updated so its compact status line now includes temperature and battery instead of battery alone.

Current sender condensed data style:

- `T:<temp> B:<volts>`

### Sender build status

Latest verified build:

- `pio run -e Heltec_v3_companion_sensor_ble`
  - SUCCESS
  - RAM: 41.5%
  - Flash: 37.6%

At the close of this round:

- the updated sender firmware builds successfully with `OneWire` and `DallasTemperature`
- the Bayou gateway has already been reflashed with the matching parser
- the next hardware step is to flash the sender and then set its desired node id with:
  `!nodeid set <number>`

## Current State After Bayou And Temperature Work

What is done now in addition to the earlier bring-up work:

- Bayou Wi-Fi gateway variant added
- Bayou credentials moved out of tracked config into local `secrets.h`
- Bayou POST uses `private_key`, `node_id`, `temperature_c`, and `battery_volts`
- Bayou gateway display now shows Wi-Fi state and POST success age
- sender now persists and transmits `node_id`
- sender now supports a 1-wire temperature sensor on `GPIO7`
- sender can power-cycle the sensor using `GPIO6`
- sender now sends placeholder `-40.0 C` if the temperature sensor still fails after reset
- gateway parser and Bayou gateway now accept the new temperature-bearing payload
- Bayou gateway has been rebuilt and reflashed successfully
- sender temperature-capable firmware now builds successfully

## Recommended Next Steps After This Round

1. Flash the updated sender firmware to the sender board.
2. On the sender serial console, set the intended node id:
   `!nodeid set <number>`
3. Connect the 1-wire sensor:
   data to `GPIO7`, power to `GPIO6`
4. Confirm the sender OLED shows temperature and battery.
5. Confirm the gateway OLED shows:
   - sender
   - `N/T/B`
   - relay
   - hops
6. Confirm Bayou posts now contain:
   - real `battery_volts`
   - real `node_id`
   - real `temperature_c`
7. Then repeat the test through a repeater and compare the displayed hop information.
