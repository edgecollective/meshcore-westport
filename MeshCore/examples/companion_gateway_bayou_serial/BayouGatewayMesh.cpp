#include "BayouGatewayMesh.h"

#ifdef ESP32
  #include <HTTPClient.h>
#endif

BayouGatewayMesh::BayouGatewayMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store)
  : GatewayMesh(radio, rng, rtc, tables, store), pending_node_id(0), pending_battery_mv(0),
    post_pending(false), next_post_retry_at(0), wifi_retry_at(0), last_post_ok_at(0), last_upload_temperature_c(0.0f) {
  StrHelper::strncpy(wifi_status, "init", sizeof(wifi_status));
  StrHelper::strncpy(last_post_status, "idle", sizeof(last_post_status));
}

void BayouGatewayMesh::beginWifi() {
#ifdef ESP32
  board.setInhibitSleep(true);
  WiFi.mode(WIFI_STA);

  #if defined(WIFI_SSID) && defined(WIFI_PWD)
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    StrHelper::strncpy(wifi_status, "connecting", sizeof(wifi_status));
    StrHelper::strncpy(last_post_status, "wifi connect", sizeof(last_post_status));
  #else
    StrHelper::strncpy(wifi_status, "cfg missing", sizeof(wifi_status));
    StrHelper::strncpy(last_post_status, "wifi cfg missing", sizeof(last_post_status));
  #endif
#else
  StrHelper::strncpy(wifi_status, "unsupported", sizeof(wifi_status));
  StrHelper::strncpy(last_post_status, "wifi unsupported", sizeof(last_post_status));
#endif
}

void BayouGatewayMesh::serviceBayou() {
  if (!post_pending) {
    return;
  }

  unsigned long now = millis();
  if ((long)(now - next_post_retry_at) < 0) {
    return;
  }

  if (!postPendingUpload()) {
    next_post_retry_at = now + 15000;
  }
}

void BayouGatewayMesh::onSensorUploadReceived(const ContactInfo& contact, uint16_t battery_mv, uint16_t interval_secs, uint16_t node_id, int16_t temperature_x10) {
  (void)contact;
  (void)interval_secs;

  pending_node_id = node_id;
  pending_battery_mv = battery_mv;
  last_upload_temperature_c = ((float)temperature_x10) / 10.0f;
  post_pending = true;
  next_post_retry_at = millis();
  StrHelper::strncpy(last_post_status, "post queued", sizeof(last_post_status));
}

bool BayouGatewayMesh::isWifiConnected() const {
#if defined(ESP32)
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

unsigned long BayouGatewayMesh::getSecondsSinceSuccessfulPost() const {
  if (last_post_ok_at == 0) {
    return 0;
  }

  unsigned long now = millis();
  if ((long)(now - last_post_ok_at) < 0) {
    return 0;
  }
  return (now - last_post_ok_at) / 1000UL;
}

bool BayouGatewayMesh::ensureWifiConnected() {
#if !defined(ESP32)
  StrHelper::strncpy(wifi_status, "unsupported", sizeof(wifi_status));
  StrHelper::strncpy(last_post_status, "wifi unsupported", sizeof(last_post_status));
  return false;
#elif !defined(WIFI_SSID) || !defined(WIFI_PWD)
  StrHelper::strncpy(wifi_status, "cfg missing", sizeof(wifi_status));
  StrHelper::strncpy(last_post_status, "wifi cfg missing", sizeof(last_post_status));
  return false;
#else
  if (WiFi.status() == WL_CONNECTED) {
    StrHelper::strncpy(wifi_status, "connected", sizeof(wifi_status));
    return true;
  }

  unsigned long now = millis();
  if ((long)(now - wifi_retry_at) >= 0) {
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    wifi_retry_at = now + 10000;
    StrHelper::strncpy(wifi_status, "reconnect", sizeof(wifi_status));
    StrHelper::strncpy(last_post_status, "wifi reconnect", sizeof(last_post_status));
  } else {
    StrHelper::strncpy(wifi_status, "connecting", sizeof(wifi_status));
  }
  return WiFi.status() == WL_CONNECTED;
#endif
}

bool BayouGatewayMesh::postPendingUpload() {
#if !defined(ESP32)
  return false;
#elif !defined(BAYOU_PUBLIC_KEY) || !defined(BAYOU_PRIVATE_KEY)
  StrHelper::strncpy(last_post_status, "bayou cfg missing", sizeof(last_post_status));
  return false;
#else
  if (!ensureWifiConnected()) {
    return false;
  }

  HTTPClient http;
  String url = String(BAYOU_BASE_URL) + String(BAYOU_PUBLIC_KEY);
  float battery_volts = ((float)pending_battery_mv) / 1000.0f;

  char body[192];
  snprintf(body, sizeof(body),
           "{\"private_key\":\"%s\",\"node_id\":%u,\"temperature_c\":%.1f,\"battery_volts\":%.3f}",
           BAYOU_PRIVATE_KEY, (unsigned)pending_node_id, (double)last_upload_temperature_c, battery_volts);

  if (!http.begin(url)) {
    StrHelper::strncpy(last_post_status, "http begin fail", sizeof(last_post_status));
    return false;
  }

  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");
  int http_code = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
  String response = (http_code > 0) ? http.getString() : String();
  http.end();

  if (http_code >= 200 && http_code < 300) {
    post_pending = false;
    last_post_ok_at = millis();
    StrHelper::strncpy(last_post_status, "post ok", sizeof(last_post_status));
    Serial.printf("bayou_post ok node_id=%u battery_mv=%u resp=%s\n",
                  (unsigned)pending_node_id, (unsigned)pending_battery_mv, response.c_str());
    return true;
  }

  char status[32];
  snprintf(status, sizeof(status), "post fail %d", http_code);
  StrHelper::strncpy(last_post_status, status, sizeof(last_post_status));
  Serial.printf("bayou_post fail node_id=%u battery_mv=%u code=%d resp=%s\n",
                (unsigned)pending_node_id, (unsigned)pending_battery_mv, http_code, response.c_str());
  return false;
#endif
}
