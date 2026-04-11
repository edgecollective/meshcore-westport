# companion_sensor_ble_uart_v2

## Overview

A MeshCore example that reads sensor data from an external board via UART and immediately transmits it over the LoRa mesh network. Based on `companion_sensor_ble`, with the OneWire temperature sensor replaced by UART input.

## How it works

1. An external board sends formatted lines at 9600 baud to **GPIO 47** (the Heltec V3's designated GPS RX pin).
2. The firmware parses each line and immediately sends the data over the mesh to a configured target (gateway).
3. The mesh upload uses the same `REQ_TYPE_SENSOR_UPLOAD` (0x10) packet format as the original `companion_sensor_ble`.

## UART protocol

Expected input format (terminated with `\n`):

```
MC,N=<node_id>,T=<temperature_c>,B=<battery_v>
```

Example: `MC,N=7,T=24.3,B=3.98`

- **N** = node ID (uint16, 0-65535)
- **T** = temperature in Celsius (float, -100 to 150)
- **B** = battery voltage (float, 0.0 to 6.0)

## Key design decision: reusing Serial1 from GPS init

The Heltec V3 board config defines `PIN_GPS_RX=47` and `ENV_INCLUDE_GPS=1` (inherited from `sensor_base`). During `sensors.begin()`, the GPS init code opens `Serial1` on GPIO 47 at 9600 baud, waits 1 second looking for GPS data, and when none is found, sets `gps_detected = false` — but **never calls `Serial1.end()`**.

We explicitly tear down and reinitialize Serial1 after `sensors.begin()`:

```cpp
Serial1.end();
Serial1.begin(9600, SERIAL_8N1, 47, -1);
```

This avoids needing `-UENV_INCLUDE_GPS` in the build flags, keeping the standard sensor infrastructure intact.

## Sending behavior

- **Send-on-receive**: each valid UART line triggers an immediate mesh send (no timer-based interval).
- If already awaiting a response from the target, incoming UART data is still parsed and stored but not sent until the previous send completes or times out.
- The retry/flood logic from companion_sensor_ble is preserved: if a direct send times out, it retries with flood routing.

## Display layout (SSD1306, 128x64)

```
ab12-->1a2b           (self pub key 4 chars --> target pub key 4 chars)
Last: 3s              (time since last successful UART parse)
N:7 T:24.3 B:3.98    (parsed UART values)
sent direct           (mesh send status)
```

## Serial commands

All commands are prefixed with `!` and sent over USB serial at 115200 baud:

- `!help` — list commands
- `!nodeid show` / `!nodeid set <n>` — get/set node ID
- `!self pubkey` — print this node's public key
- `!self card` — export identity as meshcore:// card
- `!target import <card>` — import target from meshcore:// card
- `!target set <hex>` — set target by public key hex
- `!target clear` — remove target
- `!target show` — show target public key
- `!uart show` — show current UART/send status

## Build

```
pio run -e Heltec_v3_companion_sensor_ble_uart_v2
```

## Files

```
examples/companion_sensor_ble_uart_v2/
  main.cpp              — setup, loop, display, serial commands, Serial1 init
  SensorSenderMesh.h    — class with UART parsing + mesh send (replaces OneWire)
  SensorSenderMesh.cpp  — implementation
  companion_base.cpp    — LoRa params (910.525 MHz, BW 62.5, SF 7, CR 5)
```

Build environment defined in `variants/heltec_v3/platformio.ini` under `[env:Heltec_v3_companion_sensor_ble_uart_v2]`.

## Wiring

- External board TX --> Heltec V3 GPIO 47
- External board GND --> Heltec V3 GND
- Logic levels must be 3.3V (ESP32-S3 GPIOs are NOT 5V tolerant)
