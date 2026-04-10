#pragma once

#include "../companion_gateway_serial/GatewayMesh.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifdef ESP32
  #include <WiFi.h>
#endif

#ifndef BAYOU_BASE_URL
#define BAYOU_BASE_URL "https://bayou.pvos.org/data/"
#endif

class BayouGatewayMesh : public GatewayMesh {
public:
  BayouGatewayMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store);

  void beginWifi();
  void serviceBayou();
  bool isWifiConnected() const;
  const char* getWifiStatus() const { return wifi_status; }
  const char* getLastPostStatus() const { return last_post_status; }
  bool hasSuccessfulPost() const { return last_post_ok_at != 0; }
  unsigned long getSecondsSinceSuccessfulPost() const;
  float getLastUploadTemperatureC() const { return last_upload_temperature_c; }

protected:
  void onSensorUploadReceived(const ContactInfo& contact, uint16_t battery_mv, uint16_t interval_secs, uint16_t node_id) override;

private:
  bool ensureWifiConnected();
  bool postPendingUpload();

  uint16_t pending_node_id;
  uint16_t pending_battery_mv;
  bool post_pending;
  unsigned long next_post_retry_at;
  unsigned long wifi_retry_at;
  unsigned long last_post_ok_at;
  float last_upload_temperature_c;
  char wifi_status[20];
  char last_post_status[32];
};
