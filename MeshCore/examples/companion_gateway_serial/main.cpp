#include <Arduino.h>
#include <Mesh.h>

#include "GatewayMesh.h"

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

#include <helpers/ArduinoSerialInterface.h>
ArduinoSerialInterface serial_interface;

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #if defined(SERIAL_RX)
    HardwareSerial companion_serial(1);
  #endif
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
GatewayMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

static char local_command[768];
static unsigned long next_display_refresh = 0;

static void halt() {
  while (1) ;
}

#ifdef DISPLAY_CLASS
static void renderStatusScreen() {
  if (millis() < next_display_refresh) {
    return;
  }
  next_display_refresh = millis() + 500;

  display.startFrame();
  if (!the_mesh.hasLastUpload()) {
    display.setCursor(0, 0);
    display.print("Gateway");
    display.setCursor(0, 16);
    display.print("No sensor data yet");
  } else {
    char line[48];
    display.setCursor(0, 0);
    display.print("From:");
    display.setCursor(40, 0);
    display.print(the_mesh.getLastUploadName());

    snprintf(line, sizeof(line), "Batt: %umV", (unsigned)the_mesh.getLastUploadBatteryMv());
    display.setCursor(0, 16);
    display.print(line);

    snprintf(line, sizeof(line), "Relay: %s", the_mesh.getLastUploadRelay());
    display.setCursor(0, 32);
    display.print(line);

    snprintf(line, sizeof(line), "Hops: %u", (unsigned)the_mesh.getLastUploadHops());
    display.setCursor(0, 48);
    display.print(line);
  }

  display.endFrame();
}
#else
static void renderStatusScreen() {}
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
    Serial.println("!contact add <pubkey_hex> <name>");
    Serial.println("!contact import <meshcore://card>");
    Serial.println("!self card");
    Serial.println("!self pubkey");
    return;
  }

  if (strcmp(local_command, "!self card") == 0) {
    if (!the_mesh.printSelfCard(Serial)) {
      Serial.println("ERR unable to export self card");
    }
    return;
  }

  if (strcmp(local_command, "!self pubkey") == 0) {
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    return;
  }

  if (strncmp(local_command, "!contact add ", 13) == 0) {
    char* pub_hex = &local_command[13];
    char* name = strchr(pub_hex, ' ');
    if (!name) {
      Serial.println("ERR missing contact name");
      return;
    }

    *name++ = 0;
    while (*name == ' ') {
      name++;
    }

    if (*name == 0) {
      Serial.println("ERR missing contact name");
      return;
    }

    if (the_mesh.addContactFromHex(pub_hex, name)) {
      Serial.println("OK");
    } else {
      Serial.println("ERR unable to add contact");
    }
    return;
  }

  if (strncmp(local_command, "!contact import ", 16) == 0) {
    char* encoded = &local_command[16];
    while (*encoded == ' ') {
      encoded++;
    }

    if (*encoded == 0) {
      Serial.println("ERR missing contact card");
      return;
    }

    if (the_mesh.importContactCard(encoded)) {
      Serial.println("OK");
    } else {
      Serial.println("ERR unable to import contact");
    }
    return;
  }

  Serial.println("ERR unknown command");
}

void setup() {
  Serial.begin(115200);
  board.begin();

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

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    renderStatusScreen();
  }
#endif

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
#else
  serial_interface.begin(Serial);
#endif

  the_mesh.startInterface(serial_interface);
  sensors.begin();

  Serial.println("Gateway ready. Local commands: !help");
}

void loop() {
  handleLocalCommand();
  the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();
  renderStatusScreen();
}
