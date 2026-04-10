#include <Arduino.h>
#include <Mesh.h>

#include "BayouGatewayMesh.h"

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

StdRNG fast_rng;
SimpleMeshTables tables;
BayouGatewayMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

static char local_command[768];
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
  display.setCursor(0, 0);
  display.print("Bayou GW");

  snprintf(line, sizeof(line), "WiFi: %s", the_mesh.getWifiStatus());
  display.setCursor(0, 10);
  display.print(line);

  if (strcmp(the_mesh.getLastPostStatus(), "post ok") == 0 && the_mesh.hasSuccessfulPost()) {
    char elapsed[12];
    formatElapsed(the_mesh.getSecondsSinceSuccessfulPost(), elapsed, sizeof(elapsed));
    snprintf(line, sizeof(line), "Post: ok %s ago", elapsed);
  } else {
    snprintf(line, sizeof(line), "Post: %s", the_mesh.getLastPostStatus());
  }
  display.setCursor(0, 20);
  display.print(line);

  if (!the_mesh.hasLastUpload()) {
    display.setCursor(0, 32);
    display.print("No sensor data");
  } else {
    snprintf(line, sizeof(line), "From: %s", the_mesh.getLastUploadName());
    display.setCursor(0, 32);
    display.print(line);

    snprintf(line, sizeof(line), "N:%u T:%.1f B:%.1f",
             (unsigned)the_mesh.getLastUploadNodeId(),
             (double)the_mesh.getLastUploadTemperatureC(),
             ((double)the_mesh.getLastUploadBatteryMv()) / 1000.0);
    display.setCursor(0, 42);
    display.print(line);

    snprintf(line, sizeof(line), "R:%s H:%u", the_mesh.getLastUploadRelay(), (unsigned)the_mesh.getLastUploadHops());
    display.setCursor(0, 52);
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
    Serial.println("!bayou status");
    Serial.println("!contact add <pubkey_hex> <name>");
    Serial.println("!contact find <prefix>");
    Serial.println("!contact import <meshcore://card>");
    Serial.println("!contact list");
    Serial.println("!self card");
    Serial.println("!self pubkey");
    return;
  }

  if (strcmp(local_command, "!bayou status") == 0) {
    Serial.println(the_mesh.getLastPostStatus());
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

  if (strcmp(local_command, "!contact list") == 0) {
    the_mesh.printContactList(Serial);
    return;
  }

  if (strncmp(local_command, "!contact find ", 14) == 0) {
    char* prefix = &local_command[14];
    while (*prefix == ' ') {
      prefix++;
    }

    if (*prefix == 0) {
      Serial.println("ERR missing prefix");
      return;
    }

    the_mesh.printContactMatch(Serial, prefix);
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
  the_mesh.beginWifi();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    renderStatusScreen();
  }
#endif

  serial_interface.begin(Serial);
  the_mesh.startInterface(serial_interface);
  sensors.begin();

  Serial.println("Bayou gateway ready. Local commands: !help");
}

void loop() {
  handleLocalCommand();
  the_mesh.loop();
  the_mesh.serviceBayou();
  sensors.loop();
  rtc_clock.tick();
  renderStatusScreen();
}
