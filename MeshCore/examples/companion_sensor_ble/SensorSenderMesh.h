#pragma once

#define MESHCORE_SUPPRESS_DEFAULT_MYMESH_GLOBAL 1
#include "../companion_radio/MyMesh.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#ifndef SENSOR_TARGET_PUB_KEY
#define SENSOR_TARGET_PUB_KEY ""
#endif

#ifndef SENSOR_TARGET_NAME
#define SENSOR_TARGET_NAME "gateway"
#endif

#ifndef TELEMETRY_INTERVAL_SECS
#define TELEMETRY_INTERVAL_SECS 600
#endif

#ifndef SENSOR_NODE_ID
#define SENSOR_NODE_ID 0
#endif

#ifndef SENSOR_ONEWIRE_DATA_PIN
#define SENSOR_ONEWIRE_DATA_PIN 7
#endif

#ifndef SENSOR_ONEWIRE_POWER_PIN
#define SENSOR_ONEWIRE_POWER_PIN 6
#endif

#ifndef SENSOR_TEMP_PLACEHOLDER_X10
#define SENSOR_TEMP_PLACEHOLDER_X10 (-400)
#endif

#define REQ_TYPE_SENSOR_UPLOAD 0x10

class SensorSenderMesh : public MyMesh {
public:
  SensorSenderMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store);

  void begin(bool has_display);
  void loop();
  bool printSelfCard(Stream& out);
  bool importTargetCard(const char* encoded);
  bool setTargetPubKeyFromHex(const char* pub_key_hex);
  void clearTarget();
  bool triggerManualSend();
  bool hasTarget() const { return target_valid; }
  const uint8_t* getTargetPubKey() const { return target_pub_key; }
  uint16_t getNodeId() const { return node_id; }
  bool setNodeId(uint16_t new_node_id);
  const char* getLastStatus() const { return last_status; }
  uint16_t getLastBatteryMv() const { return last_battery_mv; }
  int16_t getLastTemperatureX10() const { return last_temperature_x10; }
  uint32_t getSecondsUntilNextSend() const;
  bool isWaitingForResponse() const { return awaiting_response; }

protected:
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;

private:
  bool storeTargetSelection();
  bool restoreStoredTargetSelection();
  bool storeNodeId();
  bool restoreStoredNodeId();
  bool restoreTargetFromContacts();
  bool ensureTargetContact();
  int16_t readTemperatureX10();
  bool tryReadTemperatureX10(int16_t& out_temp_x10);
  void resetTemperatureSensorPower();
  bool sendSensorUpload(bool force_flood);
  unsigned long calcResponseTimeout(bool direct, uint8_t path_len) const;
  unsigned long nextSendDelayMillis() const;

  uint8_t target_pub_key[PUB_KEY_SIZE];
  unsigned long next_send_at;
  unsigned long response_deadline;
  uint32_t last_request_tag;
  uint16_t last_battery_mv;
  int16_t last_temperature_x10;
  uint16_t node_id;
  bool target_valid;
  bool awaiting_response;
  bool retry_pending;
  char last_status[32];
  OneWire one_wire;
  DallasTemperature temp_sensors;
};
