#pragma once

#define MESHCORE_SUPPRESS_DEFAULT_MYMESH_GLOBAL 1
#include "../companion_radio/MyMesh.h"

#define REQ_TYPE_SENSOR_UPLOAD 0x10

class GatewayMesh : public MyMesh {
public:
  GatewayMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store);

  bool addContactFromHex(const char* pub_key_hex, const char* name);
  bool printSelfCard(Stream& out);
  bool importContactCard(const char* encoded);
  bool hasLastUpload() const { return has_last_upload; }
  const char* getLastUploadName() const { return last_upload_name; }
  uint16_t getLastUploadBatteryMv() const { return last_upload_battery_mv; }
  uint16_t getLastUploadNodeId() const { return last_upload_node_id; }
  const char* getLastUploadRelay() const { return last_upload_relay; }
  uint8_t getLastUploadHops() const { return last_upload_hops; }

protected:
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override;
  virtual void onSensorUploadReceived(const ContactInfo& contact, uint16_t battery_mv, uint16_t interval_secs, uint16_t node_id);

private:
  char last_upload_name[sizeof(ContactInfo::name)];
  char last_upload_relay[16];
  uint16_t last_upload_battery_mv;
  uint16_t last_upload_node_id;
  uint8_t last_upload_hops;
  bool has_last_upload;
};
