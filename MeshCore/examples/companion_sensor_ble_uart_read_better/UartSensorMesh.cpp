#include "UartSensorMesh.h"

#include <Utils.h>

namespace {
constexpr const char* CARD_PREFIX = "meshcore://";
constexpr size_t CARD_PREFIX_LEN = 11;
constexpr size_t MAX_CARD_PACKET_LEN = 255;
constexpr uint8_t TARGET_KEY[] = {
  'm', 'c', '_', 't', 'g', 't', '_', 'v', '1'
};
constexpr uint8_t NODE_ID_KEY[] = {
  'm', 'c', '_', 'n', 'i', 'd', '_', 'v', '1'
};

static bool isStrictPrintable(int c) {
  return c >= 32 && c <= 126;
}
}

UartSensorMesh::UartSensorMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store)
  : MyMesh(radio, rng, rtc, tables, store), next_send_at(0), response_deadline(0), last_uart_sample_at(0),
    last_raw_uart_byte_at(0), last_request_tag(0), raw_uart_byte_count(0), last_battery_mv(0), last_temperature_x10(0), node_id(SENSOR_NODE_ID),
    last_raw_uart_byte(0), target_valid(false), awaiting_response(false), retry_pending(false),
    sample_valid(false), uart_line_discard(false), uart_line_len(0) {
  memset(target_pub_key, 0, sizeof(target_pub_key));
  memset(uart_line, 0, sizeof(uart_line));
  memset(last_uart_line, 0, sizeof(last_uart_line));
  StrHelper::strncpy(last_status, "boot", sizeof(last_status));
  StrHelper::strncpy(last_uart_detail, "none", sizeof(last_uart_detail));
}

void UartSensorMesh::begin(bool has_display) {
  MyMesh::begin(has_display);
  restoreStoredNodeId();

  if (strlen(SENSOR_TARGET_PUB_KEY) == PUB_KEY_SIZE * 2) {
    mesh::Utils::fromHex(target_pub_key, PUB_KEY_SIZE, SENSOR_TARGET_PUB_KEY);
    target_valid = true;
    ensureTargetContact();
    next_send_at = millis() + 5000;
    StrHelper::strncpy(last_status, "compile target", sizeof(last_status));
  } else {
    restoreStoredTargetSelection();
    if (!target_valid) {
      restoreTargetFromContacts();
    } else {
      StrHelper::strncpy(last_status, "stored target", sizeof(last_status));
    }
  }
}

void UartSensorMesh::loop() {
  MyMesh::loop();

  if (!target_valid) {
    restoreStoredTargetSelection();
    if (!target_valid) {
      restoreTargetFromContacts();
    }
  }

  if (!target_valid) {
    return;
  }

  unsigned long now = millis();
  if (awaiting_response) {
    if ((long)(now - response_deadline) >= 0) {
      awaiting_response = false;
      if (retry_pending) {
        ContactInfo* recipient = lookupContactByPubKey(target_pub_key, PUB_KEY_SIZE);
        if (recipient) {
          resetPathTo(*recipient);
          StrHelper::strncpy(last_status, "retry flood", sizeof(last_status));
          sendSensorUpload(true);
          return;
        }
      }
      retry_pending = false;
      StrHelper::strncpy(last_status, "timeout", sizeof(last_status));
      next_send_at = now + nextSendDelayMillis();
    }
    return;
  }

  if ((long)(now - next_send_at) >= 0) {
    if (!sendSensorUpload(false)) {
      next_send_at = now + nextSendDelayMillis();
    }
  }
}

void UartSensorMesh::pollUart(Stream& in) {
  while (in.available()) {
    int c = in.read();
    if (c < 0) {
      return;
    }

    raw_uart_byte_count++;
    last_raw_uart_byte = (uint8_t)c;
    last_raw_uart_byte_at = millis();

    char raw_desc[12];
    if (c == '\n') {
      StrHelper::strncpy(raw_desc, "\\n", sizeof(raw_desc));
    } else if (c == '\r') {
      StrHelper::strncpy(raw_desc, "\\r", sizeof(raw_desc));
    } else if (isStrictPrintable(c)) {
      snprintf(raw_desc, sizeof(raw_desc), "%c", c);
    } else {
      snprintf(raw_desc, sizeof(raw_desc), "0x%02X", (unsigned)(uint8_t)c);
    }
    Serial.printf("UART BYTE: #%lu %s\n", (unsigned long)raw_uart_byte_count, raw_desc);

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      if (!uart_line_discard && uart_line_len > 0) {
        uart_line[uart_line_len] = 0;
        StrHelper::strncpy(last_uart_line, uart_line, sizeof(last_uart_line));
        Serial.print("UART RX: ");
        Serial.println(last_uart_line);

        uint16_t parsed_node_id = 0;
        int16_t parsed_temp_x10 = 0;
        uint16_t parsed_battery_mv = 0;
        if (parseUartLine(uart_line, parsed_node_id, parsed_temp_x10, parsed_battery_mv)) {
          node_id = parsed_node_id;
          last_temperature_x10 = parsed_temp_x10;
          last_battery_mv = parsed_battery_mv;
          last_uart_sample_at = millis();
          sample_valid = true;
          StrHelper::strncpy(last_status, "uart ok", sizeof(last_status));
          snprintf(last_uart_detail, sizeof(last_uart_detail),
                   "ok n=%u t=%.1f b=%.3f",
                   (unsigned)node_id,
                   ((double)last_temperature_x10) / 10.0,
                   ((double)last_battery_mv) / 1000.0);
          Serial.print("UART OK: ");
          Serial.println(last_uart_detail);
        } else {
          StrHelper::strncpy(last_status, "uart bad", sizeof(last_status));
          Serial.print("UART BAD: ");
          Serial.println(last_uart_detail);
        }
      }

      uart_line_len = 0;
      uart_line_discard = false;
      continue;
    }

    if (uart_line_discard) {
      continue;
    }

    if (!isStrictPrintable(c) || uart_line_len >= (UART_LINE_MAX - 1)) {
      uart_line_discard = true;
      StrHelper::strncpy(last_status, "uart drop", sizeof(last_status));
      if (!isStrictPrintable(c)) {
        snprintf(last_uart_detail, sizeof(last_uart_detail), "drop nonprint 0x%02X", (unsigned)(uint8_t)c);
      } else {
        snprintf(last_uart_detail, sizeof(last_uart_detail), "drop too long >=%u", (unsigned)(UART_LINE_MAX - 1));
      }
      Serial.print("UART DROP: ");
      Serial.println(last_uart_detail);
      continue;
    }

    uart_line[uart_line_len++] = (char)c;
  }
}

void UartSensorMesh::onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
  if (awaiting_response && len >= 5 && memcmp(contact.id.pub_key, target_pub_key, PUB_KEY_SIZE) == 0) {
    uint32_t tag;
    memcpy(&tag, data, 4);
    if (tag == last_request_tag) {
      awaiting_response = false;
      retry_pending = false;
      StrHelper::strncpy(last_status, "ack ok", sizeof(last_status));
      next_send_at = millis() + nextSendDelayMillis();
    }
  }

  MyMesh::onContactResponse(contact, data, len);
}

bool UartSensorMesh::printSelfCard(Stream& out) {
  NodePrefs* prefs = getNodePrefs();
  mesh::Packet* pkt;
  if (prefs->advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(prefs->node_name);
  } else {
    pkt = createSelfAdvert(prefs->node_name, sensors.node_lat, sensors.node_lon);
  }

  if (!pkt) {
    return false;
  }

  pkt->header |= ROUTE_TYPE_FLOOD;

  uint8_t advert_buf[MAX_CARD_PACKET_LEN];
  uint8_t advert_len = pkt->writeTo(advert_buf);
  releasePacket(pkt);

  out.print(CARD_PREFIX);
  mesh::Utils::printHex(out, advert_buf, advert_len);
  out.println();
  return true;
}

bool UartSensorMesh::importTargetCard(const char* encoded) {
  if (!encoded) {
    return false;
  }

  if (strncmp(encoded, CARD_PREFIX, CARD_PREFIX_LEN) == 0) {
    encoded += CARD_PREFIX_LEN;
  }

  size_t hex_len = strlen(encoded);
  if (hex_len == 0 || (hex_len & 1) != 0 || hex_len > MAX_CARD_PACKET_LEN * 2) {
    return false;
  }

  uint8_t advert_buf[MAX_CARD_PACKET_LEN];
  uint8_t advert_len = hex_len / 2;
  if (!mesh::Utils::fromHex(advert_buf, advert_len, encoded)) {
    return false;
  }

  mesh::Packet pkt;
  if (!pkt.readFrom(advert_buf, advert_len) || pkt.getPayloadType() != PAYLOAD_TYPE_ADVERT || pkt.payload_len < PUB_KEY_SIZE) {
    return false;
  }

  memcpy(target_pub_key, pkt.payload, PUB_KEY_SIZE);
  target_valid = true;
  next_send_at = millis() + 5000;

  if (!importContact(advert_buf, advert_len)) {
    return false;
  }

  StrHelper::strncpy(last_status, "target imported", sizeof(last_status));
  return storeTargetSelection();
}

bool UartSensorMesh::setTargetPubKeyFromHex(const char* pub_key_hex) {
  if (!pub_key_hex || !mesh::Utils::fromHex(target_pub_key, PUB_KEY_SIZE, pub_key_hex)) {
    return false;
  }

  target_valid = true;
  next_send_at = millis() + 5000;
  StrHelper::strncpy(last_status, "target set", sizeof(last_status));
  return storeTargetSelection();
}

void UartSensorMesh::clearTarget() {
  memset(target_pub_key, 0, sizeof(target_pub_key));
  target_valid = false;
  awaiting_response = false;
  retry_pending = false;
  response_deadline = 0;
  next_send_at = 0;
  deleteStoredBlob(TARGET_KEY, sizeof(TARGET_KEY));
  StrHelper::strncpy(last_status, "target cleared", sizeof(last_status));
}

bool UartSensorMesh::triggerManualSend() {
  if (awaiting_response || !target_valid) {
    return false;
  }

  if (!hasFreshSample()) {
    StrHelper::strncpy(last_status, "uart stale", sizeof(last_status));
    return false;
  }

  StrHelper::strncpy(last_status, "button send", sizeof(last_status));
  return sendSensorUpload(false);
}

bool UartSensorMesh::setNodeId(uint16_t new_node_id) {
  node_id = new_node_id;
  return storeNodeId();
}

bool UartSensorMesh::storeTargetSelection() {
  if (!target_valid) {
    return false;
  }

  return storeBlob(TARGET_KEY, sizeof(TARGET_KEY), target_pub_key, sizeof(target_pub_key));
}

bool UartSensorMesh::storeNodeId() {
  return storeBlob(NODE_ID_KEY, sizeof(NODE_ID_KEY), reinterpret_cast<const uint8_t*>(&node_id), sizeof(node_id));
}

bool UartSensorMesh::restoreStoredTargetSelection() {
  uint8_t stored_key[PUB_KEY_SIZE];
  int len = loadStoredBlob(TARGET_KEY, sizeof(TARGET_KEY), stored_key);
  if (len != PUB_KEY_SIZE) {
    return false;
  }

  memcpy(target_pub_key, stored_key, sizeof(target_pub_key));
  target_valid = true;
  next_send_at = millis() + 5000;
  return true;
}

bool UartSensorMesh::restoreStoredNodeId() {
  uint16_t stored_node_id = 0;
  int len = loadStoredBlob(NODE_ID_KEY, sizeof(NODE_ID_KEY), reinterpret_cast<uint8_t*>(&stored_node_id));
  if (len == sizeof(stored_node_id)) {
    node_id = stored_node_id;
    return true;
  }

  node_id = SENSOR_NODE_ID;
  return false;
}

bool UartSensorMesh::restoreTargetFromContacts() {
  ContactInfo contact;
  bool found_named = false;
  bool found_single = false;
  ContactInfo single_contact;
  int chat_contacts = 0;

  for (int i = 0; getContactByIdx(i, contact); i++) {
    if (contact.type != ADV_TYPE_CHAT) {
      continue;
    }

    if (strcmp(contact.name, SENSOR_TARGET_NAME) == 0) {
      memcpy(target_pub_key, contact.id.pub_key, PUB_KEY_SIZE);
      target_valid = true;
      next_send_at = millis() + 5000;
      found_named = true;
      break;
    }

    if (chat_contacts == 0) {
      single_contact = contact;
      found_single = true;
    } else {
      found_single = false;
    }
    chat_contacts++;
  }

  if (found_named) {
    storeTargetSelection();
    StrHelper::strncpy(last_status, "named target", sizeof(last_status));
    return true;
  }

  if (found_single && chat_contacts == 1) {
    memcpy(target_pub_key, single_contact.id.pub_key, PUB_KEY_SIZE);
    target_valid = true;
    next_send_at = millis() + 5000;
    storeTargetSelection();
    StrHelper::strncpy(last_status, "single target", sizeof(last_status));
    return true;
  }

  return false;
}

bool UartSensorMesh::ensureTargetContact() {
  if (!target_valid) {
    return false;
  }

  if (!lookupContactByPubKey(target_pub_key, PUB_KEY_SIZE)) {
    if (!upsertManualContact(target_pub_key, SENSOR_TARGET_NAME, ADV_TYPE_CHAT)) {
      return false;
    }
    persistContactsNow();
  }
  return true;
}

bool UartSensorMesh::parseUartLine(const char* line, uint16_t& parsed_node_id, int16_t& parsed_temp_x10, uint16_t& parsed_battery_mv) {
  if (!line || strncmp(line, "MC,N=", 5) != 0) {
    StrHelper::strncpy(last_uart_detail, "expected MC,N=", sizeof(last_uart_detail));
    return false;
  }

  char* end = NULL;
  unsigned long node = strtoul(line + 5, &end, 10);
  if (!end || end == line + 5 || node > 65535 || strncmp(end, ",T=", 3) != 0) {
    StrHelper::strncpy(last_uart_detail, "bad node or missing ,T=", sizeof(last_uart_detail));
    return false;
  }

  char* temp_end = NULL;
  float temp_c = strtof(end + 3, &temp_end);
  if (!temp_end || temp_end == end + 3 || strncmp(temp_end, ",B=", 3) != 0) {
    StrHelper::strncpy(last_uart_detail, "bad temp or missing ,B=", sizeof(last_uart_detail));
    return false;
  }

  char* batt_end = NULL;
  float batt_v = strtof(temp_end + 3, &batt_end);
  if (!batt_end || batt_end == temp_end + 3 || *batt_end != 0) {
    StrHelper::strncpy(last_uart_detail, "bad battery field", sizeof(last_uart_detail));
    return false;
  }

  if (temp_c < -100.0f || temp_c > 150.0f) {
    StrHelper::strncpy(last_uart_detail, "temp out of range", sizeof(last_uart_detail));
    return false;
  }

  if (batt_v < 0.0f || batt_v > 6.0f) {
    StrHelper::strncpy(last_uart_detail, "battery out of range", sizeof(last_uart_detail));
    return false;
  }

  parsed_node_id = (uint16_t)node;
  parsed_temp_x10 = (int16_t)lroundf(temp_c * 10.0f);
  parsed_battery_mv = (uint16_t)lroundf(batt_v * 1000.0f);
  return true;
}

bool UartSensorMesh::sendSensorUpload(bool force_flood) {
  if (!ensureTargetContact()) {
    return false;
  }

  if (!hasFreshSample()) {
    StrHelper::strncpy(last_status, sample_valid ? "uart stale" : "no uart", sizeof(last_status));
    return false;
  }

  ContactInfo* recipient = lookupContactByPubKey(target_pub_key, PUB_KEY_SIZE);
  if (!recipient) {
    return false;
  }

  uint8_t req_data[10];
  req_data[0] = REQ_TYPE_SENSOR_UPLOAD;
  req_data[1] = 2;
  memcpy(&req_data[2], &last_battery_mv, sizeof(last_battery_mv));

  uint16_t interval_secs = TELEMETRY_INTERVAL_SECS;
  memcpy(&req_data[4], &interval_secs, sizeof(interval_secs));
  memcpy(&req_data[6], &node_id, sizeof(node_id));
  memcpy(&req_data[8], &last_temperature_x10, sizeof(last_temperature_x10));

  last_request_tag = getRTCClock()->getCurrentTimeUnique();

  uint8_t plaintext[4 + sizeof(req_data)];
  memcpy(plaintext, &last_request_tag, 4);
  memcpy(&plaintext[4], req_data, sizeof(req_data));

  mesh::Packet* pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient->id, recipient->getSharedSecret(self_id), plaintext, sizeof(plaintext));
  if (!pkt) {
    return false;
  }

  bool direct = !force_flood && recipient->out_path_len != OUT_PATH_UNKNOWN;
  if (direct) {
    sendDirect(pkt, recipient->out_path, recipient->out_path_len);
    StrHelper::strncpy(last_status, "sent direct", sizeof(last_status));
  } else {
    sendFloodScoped(*recipient, pkt);
    StrHelper::strncpy(last_status, "sent flood", sizeof(last_status));
  }

  awaiting_response = true;
  retry_pending = direct;
  response_deadline = millis() + calcResponseTimeout(direct, recipient->out_path_len);
  return true;
}

unsigned long UartSensorMesh::calcResponseTimeout(bool direct, uint8_t path_len) const {
  if (!direct || path_len == OUT_PATH_UNKNOWN) {
    return 30000;
  }

  uint8_t hops = path_len & 63;
  return 8000 + ((unsigned long)hops * 4000);
}

unsigned long UartSensorMesh::nextSendDelayMillis() const {
  return ((unsigned long)TELEMETRY_INTERVAL_SECS * 1000UL) + getRNG()->nextInt(0, 2000);
}

bool UartSensorMesh::hasFreshSample() const {
  if (!sample_valid) {
    return false;
  }

  unsigned long max_age = (unsigned long)UART_SAMPLE_MAX_AGE_SECS * 1000UL;
  return (long)(millis() - last_uart_sample_at) < (long)max_age;
}

uint32_t UartSensorMesh::getSecondsUntilNextSend() const {
  if (!target_valid) {
    return 0;
  }

  unsigned long now = millis();
  if ((long)(next_send_at - now) <= 0) {
    return 0;
  }

  return (uint32_t)((next_send_at - now + 999) / 1000);
}

uint32_t UartSensorMesh::getSecondsSinceLastRawUartByte() const {
  if (raw_uart_byte_count == 0) {
    return 0;
  }

  return (uint32_t)((millis() - last_raw_uart_byte_at) / 1000UL);
}
