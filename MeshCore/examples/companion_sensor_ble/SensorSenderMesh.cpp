#include "SensorSenderMesh.h"

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
}

SensorSenderMesh::SensorSenderMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store)
  : MyMesh(radio, rng, rtc, tables, store), next_send_at(0), response_deadline(0),
    last_request_tag(0), last_battery_mv(0), node_id(SENSOR_NODE_ID), target_valid(false), awaiting_response(false), retry_pending(false) {
  memset(target_pub_key, 0, sizeof(target_pub_key));
  StrHelper::strncpy(last_status, "boot", sizeof(last_status));
}

void SensorSenderMesh::begin(bool has_display) {
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

void SensorSenderMesh::loop() {
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

void SensorSenderMesh::onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
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

bool SensorSenderMesh::printSelfCard(Stream& out) {
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

bool SensorSenderMesh::importTargetCard(const char* encoded) {
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

bool SensorSenderMesh::setTargetPubKeyFromHex(const char* pub_key_hex) {
  if (!pub_key_hex || !mesh::Utils::fromHex(target_pub_key, PUB_KEY_SIZE, pub_key_hex)) {
    return false;
  }

  target_valid = true;
  next_send_at = millis() + 5000;
  StrHelper::strncpy(last_status, "target set", sizeof(last_status));
  return storeTargetSelection();
}

void SensorSenderMesh::clearTarget() {
  memset(target_pub_key, 0, sizeof(target_pub_key));
  target_valid = false;
  awaiting_response = false;
  retry_pending = false;
  response_deadline = 0;
  next_send_at = 0;
  deleteStoredBlob(TARGET_KEY, sizeof(TARGET_KEY));
  StrHelper::strncpy(last_status, "target cleared", sizeof(last_status));
}

bool SensorSenderMesh::triggerManualSend() {
  if (awaiting_response || !target_valid) {
    return false;
  }

  StrHelper::strncpy(last_status, "button send", sizeof(last_status));
  return sendSensorUpload(false);
}

bool SensorSenderMesh::setNodeId(uint16_t new_node_id) {
  node_id = new_node_id;
  return storeNodeId();
}

bool SensorSenderMesh::storeTargetSelection() {
  if (!target_valid) {
    return false;
  }

  return storeBlob(TARGET_KEY, sizeof(TARGET_KEY), target_pub_key, sizeof(target_pub_key));
}

bool SensorSenderMesh::storeNodeId() {
  return storeBlob(NODE_ID_KEY, sizeof(NODE_ID_KEY), reinterpret_cast<const uint8_t*>(&node_id), sizeof(node_id));
}

bool SensorSenderMesh::restoreStoredTargetSelection() {
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

bool SensorSenderMesh::restoreStoredNodeId() {
  uint16_t stored_node_id = 0;
  int len = loadStoredBlob(NODE_ID_KEY, sizeof(NODE_ID_KEY), reinterpret_cast<uint8_t*>(&stored_node_id));
  if (len == sizeof(stored_node_id)) {
    node_id = stored_node_id;
    return true;
  }

  node_id = SENSOR_NODE_ID;
  return false;
}

bool SensorSenderMesh::restoreTargetFromContacts() {
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

bool SensorSenderMesh::ensureTargetContact() {
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

bool SensorSenderMesh::sendSensorUpload(bool force_flood) {
  if (!ensureTargetContact()) {
    return false;
  }

  ContactInfo* recipient = lookupContactByPubKey(target_pub_key, PUB_KEY_SIZE);
  if (!recipient) {
    return false;
  }

  uint8_t req_data[8];
  req_data[0] = REQ_TYPE_SENSOR_UPLOAD;
  req_data[1] = 1;

  last_battery_mv = board.getBattMilliVolts();
  memcpy(&req_data[2], &last_battery_mv, sizeof(last_battery_mv));

  uint16_t interval_secs = TELEMETRY_INTERVAL_SECS;
  memcpy(&req_data[4], &interval_secs, sizeof(interval_secs));
  memcpy(&req_data[6], &node_id, sizeof(node_id));

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

unsigned long SensorSenderMesh::calcResponseTimeout(bool direct, uint8_t path_len) const {
  if (!direct || path_len == OUT_PATH_UNKNOWN) {
    return 30000;
  }

  uint8_t hops = path_len & 63;
  return 8000 + ((unsigned long)hops * 4000);
}

unsigned long SensorSenderMesh::nextSendDelayMillis() const {
  return ((unsigned long)TELEMETRY_INTERVAL_SECS * 1000UL) + getRNG()->nextInt(0, 2000);
}

uint32_t SensorSenderMesh::getSecondsUntilNextSend() const {
  if (!target_valid) {
    return 0;
  }

  unsigned long now = millis();
  if ((long)(next_send_at - now) <= 0) {
    return 0;
  }

  return (uint32_t)((next_send_at - now + 999) / 1000);
}
