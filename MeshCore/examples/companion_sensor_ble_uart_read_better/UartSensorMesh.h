#pragma once

#define MESHCORE_SUPPRESS_DEFAULT_MYMESH_GLOBAL 1
#include "../companion_radio/MyMesh.h"

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

#ifndef UART_SAMPLE_MAX_AGE_SECS
#define UART_SAMPLE_MAX_AGE_SECS 1200
#endif

#ifndef UART_LINE_MAX
#define UART_LINE_MAX 128
#endif

#define REQ_TYPE_SENSOR_UPLOAD 0x10

class UartSensorMesh : public MyMesh {
public:
  UartSensorMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store);

  void begin(bool has_display);
  void loop();
  void pollUart(Stream& in);
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
  bool hasFreshSample() const;
  const char* getLastUartLine() const { return last_uart_line; }
  const char* getLastUartDetail() const { return last_uart_detail; }
  unsigned long getRawUartByteCount() const { return raw_uart_byte_count; }
  uint8_t getLastRawUartByte() const { return last_raw_uart_byte; }
  uint32_t getSecondsSinceLastRawUartByte() const;

protected:
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;

private:
  bool storeTargetSelection();
  bool restoreStoredTargetSelection();
  bool storeNodeId();
  bool restoreStoredNodeId();
  bool restoreTargetFromContacts();
  bool ensureTargetContact();
  bool parseUartLine(const char* line, uint16_t& parsed_node_id, int16_t& parsed_temp_x10, uint16_t& parsed_battery_mv);
  bool sendSensorUpload(bool force_flood);
  unsigned long calcResponseTimeout(bool direct, uint8_t path_len) const;
  unsigned long nextSendDelayMillis() const;

  uint8_t target_pub_key[PUB_KEY_SIZE];
  unsigned long next_send_at;
  unsigned long response_deadline;
  unsigned long last_uart_sample_at;
  unsigned long last_raw_uart_byte_at;
  uint32_t last_request_tag;
  unsigned long raw_uart_byte_count;
  uint16_t last_battery_mv;
  int16_t last_temperature_x10;
  uint16_t node_id;
  uint8_t last_raw_uart_byte;
  bool target_valid;
  bool awaiting_response;
  bool retry_pending;
  bool sample_valid;
  bool uart_line_discard;
  size_t uart_line_len;
  char last_status[32];
  char last_uart_detail[64];
  char uart_line[UART_LINE_MAX];
  char last_uart_line[UART_LINE_MAX];
};
