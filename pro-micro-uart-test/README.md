# Pro Micro UART Test

Standalone PlatformIO project for the same `promicro_nrf52840` board used in MeshCore.

Purpose:

- keep Pro Micro test firmware completely separate from MeshCore
- emit simple UART lines compatible with `companion_sensor_ble_uart_read`
- provide USB serial debug output at the same time

## Build

```bash
cd /home/dwblair/gitwork/mesh-westport/v1/pro-micro-uart-test
pio run
```

## Flash

```bash
cd /home/dwblair/gitwork/mesh-westport/v1/pro-micro-uart-test
pio run -t upload
```

## Monitor

```bash
cd /home/dwblair/gitwork/mesh-westport/v1/pro-micro-uart-test
pio device monitor -b 115200
```

## UART Output Format

This project emits lines like:

```text
MC,N=7,T=23.4,B=3.98
```

That matches the parser in `MeshCore/examples/companion_sensor_ble_uart_read`.

## Wiring

- Pro Micro `TX` -> MeshCore sender `GPIO19`
- Pro Micro `GND` -> MeshCore sender `GND`

No RX connection is required for this basic one-way test.

## Customization

You can change these macros in `src/main.cpp`:

- `TEST_NODE_ID`
- `TEST_TEMP_C`
- `TEST_BATTERY_V`
- `SEND_INTERVAL_MS`
- `UART_BAUD`
