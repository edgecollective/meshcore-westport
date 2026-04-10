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

#ifndef UART_READ_LINE_MAX
#define UART_READ_LINE_MAX 64
#endif

#ifndef UART_SAMPLE_MAX_AGE_SECS
#define UART_SAMPLE_MAX_AGE_SECS (TELEMETRY_INTERVAL_SECS * 2)
#endif

#define REQ_TYPE_SENSOR_UPLOAD 0x10

class UartReadSenderMesh : public MyMesh {
public:
  UartReadSenderMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store);

  void begin(bool has_display);
  void loop();
  void pollUart(Stream& in);
  bool printSelfCard(Stream& out);
  bool importTargetCard(const char* encoded);
  bool setTargetPubKeyFromHex(const char* pub_key_hex);
  void clearTarget();
  bool triggerManualSend();
  bool hasTarget() const { return target_valid; }
  bool hasFreshSample() const;
  const uint8_t* getTargetPubKey() const { return target_pub_key; }
  const char* getLastStatus() const { return last_status; }
  const char* getLastUartLine() const { return last_uart_line; }
  const char* getLastUartDetail() const { return last_uart_detail; }
  uint16_t getLastBatteryMv() const { return last_battery_mv; }
  int16_t getLastTemperatureX10() const { return last_temperature_x10; }
  uint16_t getLastNodeId() const { return node_id; }
  uint32_t getSecondsUntilNextSend() const;
  uint32_t getSecondsSinceLastSample() const;
  bool isWaitingForResponse() const { return awaiting_response; }

protected:
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;

private:
  bool storeTargetSelection();
  bool restoreStoredTargetSelection();
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
  uint32_t last_request_tag;
  uint16_t last_battery_mv;
  int16_t last_temperature_x10;
  uint16_t node_id;
  bool target_valid;
  bool awaiting_response;
  bool retry_pending;
  bool sample_valid;
  bool uart_line_discard;
  uint8_t uart_line_len;
  char last_status[32];
  char last_uart_detail[48];
  char last_uart_line[UART_READ_LINE_MAX];
  char uart_line[UART_READ_LINE_MAX];
};
