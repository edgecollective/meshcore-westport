#include <Arduino.h>
#include <Mesh.h>

#include "SensorSenderMesh.h"

#if !defined(BLE_PIN_CODE)
#error "companion_sensor_ble_uart_v2 requires BLE_PIN_CODE to be defined by the board environment"
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
#elif defined(NRF52_PLATFORM)
  #include <helpers/nrf52/SerialBLEInterface.h>
  SerialBLEInterface serial_interface;
#else
  #error "companion_sensor_ble_uart_v2 currently supports ESP32 and NRF52 BLE targets"
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
SensorSenderMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
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
  char line[48];

  // Line 1: self_id --> target_id
  char self_hex[9], tgt_hex[9];
  mesh::Utils::toHex(self_hex, the_mesh.self_id.pub_key, 2);
  self_hex[4] = 0;
  if (the_mesh.hasTarget()) {
    mesh::Utils::toHex(tgt_hex, the_mesh.getTargetPubKey(), 2);
    tgt_hex[4] = 0;
  } else {
    strcpy(tgt_hex, "none");
  }
  display.setCursor(0, 0);
  snprintf(line, sizeof(line), "%s-->%s", self_hex, tgt_hex);
  display.print(line);

  // Line 2: Last: elapsed since last successful UART parse
  display.setCursor(0, 12);
  if (the_mesh.hasFreshSample()) {
    char elapsed[12];
    formatElapsed(the_mesh.getSecondsSinceLastSample(), elapsed, sizeof(elapsed));
    snprintf(line, sizeof(line), "Last: %s", elapsed);
  } else {
    snprintf(line, sizeof(line), "Last: none");
  }
  display.print(line);

  // Line 3: N, T, B from UART
  display.setCursor(0, 24);
  snprintf(line, sizeof(line), "N:%u T:%.1f B:%.1f",
           (unsigned)the_mesh.getNodeId(),
           ((double)the_mesh.getLastTemperatureX10()) / 10.0,
           ((double)the_mesh.getLastBatteryMv()) / 1000.0);
  display.print(line);

  // Line 4: send status
  display.setCursor(0, 36);
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
    Serial.println("!nodeid show");
    Serial.println("!nodeid set <number>");
    Serial.println("!self pubkey");
    Serial.println("!self card");
    Serial.println("!target import <meshcore://card>");
    Serial.println("!target set <pubkey_hex>");
    Serial.println("!target clear");
    Serial.println("!target show");
    Serial.println("!uart show");
    return;
  }

  if (strcmp(local_command, "!self pubkey") == 0) {
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    return;
  }

  if (strcmp(local_command, "!nodeid show") == 0) {
    Serial.println((unsigned)the_mesh.getNodeId());
    return;
  }

  if (strncmp(local_command, "!nodeid set ", 12) == 0) {
    char* value = &local_command[12];
    while (*value == ' ') {
      value++;
    }

    if (*value == 0) {
      Serial.println("ERR missing node_id");
      return;
    }

    if (the_mesh.setNodeId((uint16_t)strtoul(value, NULL, 10))) {
      Serial.println("OK");
    } else {
      Serial.println("ERR unable to store node_id");
    }
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
    Serial.printf("status=%s sample=%s n=%u t=%.1f b=%.3f\n",
                  the_mesh.getLastStatus(),
                  the_mesh.hasFreshSample() ? "fresh" : "none",
                  (unsigned)the_mesh.getNodeId(),
                  ((double)the_mesh.getLastTemperatureX10()) / 10.0,
                  ((double)the_mesh.getLastBatteryMv()) / 1000.0);
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
#endif

  the_mesh.begin(false);
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  the_mesh.startInterface(serial_interface);
  sensors.begin();

  // Reclaim Serial1 from GPS init and reinitialize on GPIO 47 at 9600 baud
  Serial1.end();
  Serial1.begin(9600, SERIAL_8N1, 47, -1);
  Serial.println("UART v2 ready. Listening on Serial1 (GPIO 47, 9600 baud). Local commands: !help");

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    user_btn.begin();
    renderStatusScreen();
  }
#endif
}

void loop() {
  the_mesh.pollUart(Serial1);
  handleLocalCommand();
  handleButtonSend();
  the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();
  renderStatusScreen();
}
