#include <Arduino.h>
#include <Mesh.h>

#include "UartReadSenderMesh.h"

#if !defined(BLE_PIN_CODE)
#error "companion_sensor_ble_uart_read requires BLE_PIN_CODE to be defined by the board environment"
#endif

#ifndef UART_READ_RX_PIN
#define UART_READ_RX_PIN 47
#endif

#ifndef UART_READ_BAUD
#define UART_READ_BAUD 9600
#endif

#ifndef UART_READ_TX_PIN
#define UART_READ_TX_PIN 45
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #include <helpers/esp32/SerialBLEInterface.h>
  SerialBLEInterface serial_interface;
  HardwareSerial uart_input(1);
#elif defined(NRF52_PLATFORM)
  #include <helpers/nrf52/SerialBLEInterface.h>
  SerialBLEInterface serial_interface;
#else
  #error "companion_sensor_ble_uart_read currently supports ESP32 and NRF52 BLE targets"
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
UartReadSenderMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
static char local_command[320];
static unsigned long next_display_refresh = 0;

static void halt() {
  while (1) ;
}

static void formatElapsed(unsigned long seconds, char* out, size_t out_len) {
  if (seconds < 60) {
    snprintf(out, out_len, "%lus", seconds);
  } else if (seconds < 3600) {
    snprintf(out, out_len, "%lum", seconds / 60);
  } else if (seconds < 86400) {
    snprintf(out, out_len, "%luh", seconds / 3600);
  } else {
    snprintf(out, out_len, "%lud", seconds / 86400);
  }
}

#ifdef DISPLAY_CLASS
static void renderStatusScreen() {
  if (millis() < next_display_refresh) {
    return;
  }
  next_display_refresh = millis() + 500;

  display.startFrame();
  char id_hex[9];
  mesh::Utils::toHex(id_hex, the_mesh.self_id.pub_key, 4);
  display.setCursor(0, 0);
  display.print("UART ");
  display.print(id_hex);
  display.setCursor(0, 12);
  display.print(the_mesh.hasTarget() ? "Target: set" : "Target: none");

  char line[48];
  char elapsed[12];
  display.setCursor(0, 24);
  if (the_mesh.hasFreshSample()) {
    formatElapsed(the_mesh.getSecondsSinceLastSample(), elapsed, sizeof(elapsed));
    snprintf(line, sizeof(line), "Last: %s", elapsed);
  } else {
    snprintf(line, sizeof(line), "Last: none");
  }
  display.print(line);

  display.setCursor(0, 36);
  snprintf(line, sizeof(line), "N:%u T:%.1f B:%.1f",
           (unsigned)the_mesh.getLastNodeId(),
           ((double)the_mesh.getLastTemperatureX10()) / 10.0,
           ((double)the_mesh.getLastBatteryMv()) / 1000.0);
  display.print(line);

  display.setCursor(0, 48);
  display.print(the_mesh.getLastStatus());
  display.endFrame();
}

static void handleButtonSend() {
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (the_mesh.triggerManualSend()) {
      Serial.println("Manual send triggered");
    } else {
      Serial.println("Manual send unavailable");
    }
  }
}
#else
static void renderStatusScreen() {}
static void handleButtonSend() {
#ifdef PIN_USER_BTN
  static int prev_state = HIGH;
  int state = digitalRead(PIN_USER_BTN);
  if (state != prev_state) {
    if (state == LOW) {
      if (the_mesh.triggerManualSend()) {
        Serial.println("Manual send triggered");
      } else {
        Serial.println("Manual send unavailable");
      }
    }
    prev_state = state;
  }
#endif
}
#endif

static bool readLocalCommand() {
  if (!Serial.available() || Serial.peek() != '!') {
    return false;
  }

  size_t len = Serial.readBytesUntil('\n', local_command, sizeof(local_command) - 1);
  local_command[len] = 0;

  while (len > 0 && (local_command[len - 1] == '\r' || local_command[len - 1] == '\n')) {
    local_command[--len] = 0;
  }
  return len > 0;
}

static void handleLocalCommand() {
  if (!readLocalCommand()) {
    return;
  }

  if (strcmp(local_command, "!help") == 0) {
    Serial.println("!self pubkey");
    Serial.println("!self card");
    Serial.println("!target import <meshcore://card>");
    Serial.println("!target set <pubkey_hex>");
    Serial.println("!target clear");
    Serial.println("!target show");
    Serial.println("!uart show");
    Serial.println("!uart loop");
    return;
  }

  if (strcmp(local_command, "!self pubkey") == 0) {
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    return;
  }

  if (strcmp(local_command, "!self card") == 0) {
    if (!the_mesh.printSelfCard(Serial)) {
      Serial.println("ERR unable to export self card");
    }
    return;
  }

  if (strcmp(local_command, "!target show") == 0) {
    if (!the_mesh.hasTarget()) {
      Serial.println("ERR no target configured");
      return;
    }

    mesh::Utils::printHex(Serial, the_mesh.getTargetPubKey(), PUB_KEY_SIZE);
    Serial.println();
    return;
  }

  if (strncmp(local_command, "!target import ", 15) == 0) {
    char* encoded = &local_command[15];
    while (*encoded == ' ') {
      encoded++;
    }

    if (*encoded == 0) {
      Serial.println("ERR missing target card");
      return;
    }

    if (the_mesh.importTargetCard(encoded)) {
      Serial.println("OK");
    } else {
      Serial.println("ERR unable to import target");
    }
    return;
  }

  if (strncmp(local_command, "!target set ", 12) == 0) {
    char* pub_hex = &local_command[12];
    while (*pub_hex == ' ') {
      pub_hex++;
    }

    if (*pub_hex == 0) {
      Serial.println("ERR missing target pubkey");
      return;
    }

    if (the_mesh.setTargetPubKeyFromHex(pub_hex)) {
      Serial.println("OK");
    } else {
      Serial.println("ERR unable to set target");
    }
    return;
  }

  if (strcmp(local_command, "!target clear") == 0) {
    the_mesh.clearTarget();
    Serial.println("OK");
    return;
  }

  if (strcmp(local_command, "!uart show") == 0) {
    Serial.printf("status=%s detail=%s raw=%s bytes=%lu last_byte=0x%02X byte_age_s=%lu\n",
                  the_mesh.getLastStatus(),
                  the_mesh.getLastUartDetail(),
                  the_mesh.getLastUartLine()[0] ? the_mesh.getLastUartLine() : "<none>",
                  (unsigned long)the_mesh.getRawUartByteCount(),
                  (unsigned)the_mesh.getLastRawUartByte(),
                  (unsigned long)the_mesh.getSecondsSinceLastRawUartByte());
    if (!the_mesh.hasFreshSample()) {
      Serial.println("ERR no fresh uart sample");
      return;
    }

    Serial.printf("node_id=%u temp_c=%.1f battery_v=%.3f age_s=%lu\n",
                  (unsigned)the_mesh.getLastNodeId(),
                  ((double)the_mesh.getLastTemperatureX10()) / 10.0,
                  ((double)the_mesh.getLastBatteryMv()) / 1000.0,
                  (unsigned long)the_mesh.getSecondsSinceLastSample());
    return;
  }

  if (strcmp(local_command, "!uart loop") == 0) {
#ifdef ESP32
    uart_input.print("MC,N=321,T=12.3,B=3.70\n");
    uart_input.flush();
    Serial.printf("UART loop sent on GPIO%d at %d baud\n", UART_READ_TX_PIN, UART_READ_BAUD);
#else
    Serial.println("ERR uart loop unsupported on this target");
#endif
    return;
  }

  Serial.println("ERR unknown command");
}

void setup() {
  Serial.begin(115200);
  board.begin();

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT);
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    QSPIFlash.begin();
  #else
    #if defined(EXTRAFS)
      ExtraFS.begin();
    #endif
  #endif
  store.begin();
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  uart_input.begin(UART_READ_BAUD, SERIAL_8N1, UART_READ_RX_PIN, UART_READ_TX_PIN);
#endif

  the_mesh.begin(false);
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  the_mesh.startInterface(serial_interface);
  sensors.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    user_btn.begin();
    renderStatusScreen();
  }
#endif

  Serial.printf("UART sender ready. RX=GPIO%d TX=GPIO%d Expected line: MC,N=<id>,T=<temp_c>,B=<battery_v>\n",
                UART_READ_RX_PIN, UART_READ_TX_PIN);
}

void loop() {
#ifdef ESP32
  the_mesh.pollUart(uart_input);
#endif
  handleLocalCommand();
  handleButtonSend();
  the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();
  renderStatusScreen();
}
