#include "GatewayMesh.h"

#include <Utils.h>

namespace {
constexpr const char* CARD_PREFIX = "meshcore://";
constexpr size_t CARD_PREFIX_LEN = 11;
constexpr size_t MAX_CARD_PACKET_LEN = 255;
}

GatewayMesh::GatewayMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store)
  : MyMesh(radio, rng, rtc, tables, store) {
  memset(last_upload_name, 0, sizeof(last_upload_name));
  StrHelper::strncpy(last_upload_relay, "-", sizeof(last_upload_relay));
  last_upload_battery_mv = 0;
  last_upload_node_id = 0;
  last_upload_temperature_x10 = -400;
  last_upload_hops = 0;
  has_last_upload = false;
}

bool GatewayMesh::addContactFromHex(const char* pub_key_hex, const char* name) {
  if (strlen(pub_key_hex) != PUB_KEY_SIZE * 2) {
    return false;
  }

  uint8_t pub_key[PUB_KEY_SIZE];
  mesh::Utils::fromHex(pub_key, PUB_KEY_SIZE, pub_key_hex);

  if (!upsertManualContact(pub_key, name, ADV_TYPE_CHAT)) {
    return false;
  }

  persistContactsNow();
  return true;
}

bool GatewayMesh::printSelfCard(Stream& out) {
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

bool GatewayMesh::importContactCard(const char* encoded) {
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

  return importContact(advert_buf, advert_len);
}

void GatewayMesh::onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) {
  if (type == PAYLOAD_TYPE_REQ && len >= 10 && data[4] == REQ_TYPE_SENSOR_UPLOAD) {
    last_upload_hops = packet->isRouteFlood() ? packet->getPathHashCount() : 0;

    if (packet->isRouteFlood() && last_upload_hops > 0) {
      char relay_hex[8];
      uint8_t hash_size = packet->getPathHashSize();
      const uint8_t* relay = &packet->path[(last_upload_hops - 1) * hash_size];
      mesh::Utils::toHex(relay_hex, relay, hash_size);
      StrHelper::strncpy(last_upload_relay, relay_hex, sizeof(last_upload_relay));
    } else {
      StrHelper::strncpy(last_upload_relay, "direct", sizeof(last_upload_relay));
    }
  }

  MyMesh::onPeerDataRecv(packet, type, sender_idx, secret, data, len);
}

uint8_t GatewayMesh::onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) {
  if (len >= 6 && data[0] == REQ_TYPE_SENSOR_UPLOAD) {
    uint16_t battery_mv = 0;
    uint16_t interval_secs = 0;
    uint16_t node_id = 0;
    int16_t temperature_x10 = -400;
    uint8_t version = data[1];
    memcpy(&battery_mv, &data[2], sizeof(battery_mv));
    memcpy(&interval_secs, &data[4], sizeof(interval_secs));
    if (len >= 8) {
      memcpy(&node_id, &data[6], sizeof(node_id));
    }
    if (version >= 2 && len >= 10) {
      memcpy(&temperature_x10, &data[8], sizeof(temperature_x10));
    }

    StrHelper::strncpy(last_upload_name, contact.name, sizeof(last_upload_name));
    last_upload_battery_mv = battery_mv;
    last_upload_node_id = node_id;
    last_upload_temperature_x10 = temperature_x10;
    has_last_upload = true;

    onSensorUploadReceived(contact, battery_mv, interval_secs, node_id, temperature_x10);

    Serial.printf("sensor_upload from=%s node_id=%u temp_x10=%d battery_mv=%u relay=%s hops=%u\n",
                  contact.name, (unsigned)node_id, (int)temperature_x10, battery_mv, last_upload_relay, (unsigned)last_upload_hops);

    memcpy(reply, &sender_timestamp, 4);
    reply[4] = 0;
    return 5;
  }

  return MyMesh::onContactRequest(contact, sender_timestamp, data, len, reply);
}

void GatewayMesh::onSensorUploadReceived(const ContactInfo& contact, uint16_t battery_mv, uint16_t interval_secs, uint16_t node_id, int16_t temperature_x10) {
  (void)contact;
  (void)battery_mv;
  (void)interval_secs;
  (void)node_id;
  (void)temperature_x10;
}
