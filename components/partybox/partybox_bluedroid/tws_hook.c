// Pinned ESP-IDF5.5.5 C shim: internal SDK headers use C flexible-array structs.
// SDK event ownership/cleanup remains with the original outer dispatcher.
#ifdef ESP_PLATFORM
#include "esp_idf_version.h"
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "PartyBox native hooks require ESP-IDF 5.5.5; verify SDK layouts before upgrading"
#endif
#endif

#include <stdbool.h>
#include <stddef.h>

#include "bta_av_int.h"
#include "stack/avrc_api.h"

_Static_assert(sizeof(tAVRC_MSG_VENDOR) == 16 && sizeof(tAVRC_MSG) == 16, "AVRCP SDK types changed");
_Static_assert(offsetof(tBTA_AV_RC_MSG, msg) == 8 && offsetof(tBTA_AV_RC_MSG, handle) == 24, "BTA SDK offsets changed");
_Static_assert(sizeof(tBTA_AV_RC_MSG) == 28, "BTA SDK event changed");
extern void __real_bta_av_rc_msg(tBTA_AV_CB* control, tBTA_AV_DATA* data);
extern bool partybox_peer_matches(const uint8_t* address);
extern void partybox_native_event(uint8_t type, uint32_t session, uint8_t handle, uint8_t label, uint32_t value);
extern void partybox_native_vendor_event(uint32_t session, uint8_t handle, uint8_t label, uint32_t company,
                                         uint8_t ctype, uint16_t length, int16_t pdu);
static uint32_t session;
static uint8_t active_handle = 0xFF;

static bool selected_link(const tBTA_AV_CB* control, const tBTA_AV_RC_MSG* event) {
  if (control == NULL || event == NULL || event->handle >= BTA_AV_NUM_RCB) return false;
  const tBTA_AV_RCB* channel = &control->rcb[event->handle];
  if (channel->handle != event->handle || !(channel->status & BTA_AV_RC_CONN_MASK)) return false;
  if (channel->shdl > 0 && channel->shdl <= BTA_AV_NUM_STRS && control->p_scb[channel->shdl - 1] != NULL)
    return partybox_peer_matches(control->p_scb[channel->shdl - 1]->peer_addr);
  if (channel->lidx > 0 && channel->lidx <= BTA_AV_NUM_LINKS + 1)
    return partybox_peer_matches(control->lcb[channel->lidx - 1].addr);
  return false;
}

void __wrap_bta_av_rc_msg(tBTA_AV_CB* control, tBTA_AV_DATA* data) {
  tBTA_AV_RC_MSG* event = data == NULL ? NULL : &data->rc_msg;
  if (selected_link(control, event) && event->opcode == 0) {
    const tAVRC_MSG_VENDOR* vendor = &event->msg.vendor;
    const uint8_t* bytes = vendor->p_vendor_data;
    if (vendor->hdr.opcode == 0 && vendor->hdr.subunit_type == 9 && vendor->hdr.subunit_id == 0 && bytes != NULL) {
      const uint8_t* reply = NULL;
      uint16_t size = 0;
      uint8_t response_code = 0;
      static const uint8_t ack[] = {0x19, 0, 0, 0};
      static const uint8_t f5[] = {0xF5, 0, 0, 1, 0x0F};
      if (vendor->company_id == 0x1958 && vendor->hdr.ctype == 0 && vendor->vendor_len == 9 && bytes[0] == 0x19 &&
          bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 5) {
        const uint32_t word = (uint32_t)bytes[5] << 24 | (uint32_t)bytes[6] << 16 | (uint32_t)bytes[7] << 8 | bytes[8];
        if (active_handle != event->handle) {
          active_handle = event->handle;
          ++session;
        }
        partybox_native_event(1, session, event->handle, event->label, word);
        reply = ack;
        size = sizeof(ack);
        response_code = 9;
      }
      if (vendor->company_id == 0xC4 && vendor->hdr.ctype == 1 && vendor->vendor_len == 5 && bytes[0] == 0xF5 &&
          bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1 && bytes[4] == 0) {
        reply = f5;
        size = sizeof(f5);
        response_code = 12;
      }
      if (active_handle == event->handle && vendor->company_id == 0x1958 &&
          (vendor->hdr.ctype == 8 || vendor->hdr.ctype == 9 || vendor->hdr.ctype == 10) && vendor->vendor_len == 4 &&
          bytes[0] == 0x19 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) {
        partybox_native_event(2, session, event->handle, event->label, vendor->hdr.ctype == 9);
        partybox_native_event(12, session, event->handle, event->label, vendor->hdr.ctype);
        return;
      }
      if (reply != NULL) {
        tAVRC_MSG_VENDOR response = {0};
        response.hdr = (tAVRC_HDR){response_code, 9, 0, 0};
        response.company_id = vendor->company_id;
        response.p_vendor_data = (uint8_t*)reply;
        response.vendor_len = size;
        const uint16_t status = AVRC_VendorRsp(event->handle, event->label, &response);
        partybox_native_event(11, session, event->handle, event->label, ((uint32_t)response_code << 16) | status);
        return;
      }
    }
    // Observe only messages continuing to the original handler. Read at
    // most the PDU identifier; a missing/empty payload has no identifier.
    partybox_native_vendor_event(session, event->handle, event->label, vendor->company_id, vendor->hdr.ctype,
                                 vendor->vendor_len, bytes != NULL && vendor->vendor_len > 0 ? bytes[0] : -1);
  }
  __real_bta_av_rc_msg(control, data);
}

// Queue a private message onto the SDK's BTA task. Stock BTA_AvVendorCmd hardcodes
// company C4, so use its allocation/dispatch pattern with our own company and
// session trailer. Only this component generates company 1958 API messages.
#include "bta/bta_sys.h"
#include "osi/allocator.h"
typedef struct {
  tBTA_AV_API_VENDOR api;
  uint8_t payload[9];
  uint32_t session;
} partybox_tx;
extern void __real_bta_av_rc_vendor_cmd(tBTA_AV_CB*, tBTA_AV_DATA*);
extern void __real_bta_av_rc_closed(tBTA_AV_DATA*);
bool partybox_queue_word(uint32_t expected_session, uint8_t handle, uint8_t label, uint32_t word) {
  if (label > 15 || handle >= BTA_AV_NUM_RCB) return false;
  partybox_tx* tx = osi_calloc(sizeof(*tx));
  if (!tx) return false;
  tx->api.hdr.event = BTA_AV_API_VENDOR_CMD_EVT;
  tx->api.hdr.layer_specific = handle;
  tx->api.label = label;
  tx->api.msg.hdr = (tAVRC_HDR){0, 9, 0, 0};
  tx->api.msg.company_id = 0x1958;
  tx->api.msg.vendor_len = 9;
  tx->api.msg.p_vendor_data = tx->payload;
  tx->payload[0] = 0x19;
  tx->payload[3] = 5;
  tx->payload[5] = word >> 24;
  tx->payload[6] = word >> 16;
  tx->payload[7] = word >> 8;
  tx->payload[8] = word;
  tx->session = expected_session;
  bta_sys_sendmsg(tx);
  return true;
}
bool partybox_queue_close(uint32_t expected_session, uint8_t handle) {
  if (handle >= BTA_AV_NUM_RCB) return false;
  partybox_tx* tx = osi_calloc(sizeof(*tx));
  if (!tx) return false;
  tx->api.hdr.event = BTA_AV_API_VENDOR_CMD_EVT;
  tx->api.hdr.layer_specific = handle;
  tx->api.msg.company_id = 0x1958;
  tx->session = expected_session;
  bta_sys_sendmsg(tx);
  return true;
}
void __wrap_bta_av_rc_vendor_cmd(tBTA_AV_CB* control, tBTA_AV_DATA* data) {
  if (data && data->api_vendor.msg.company_id == 0x1958) {
    const partybox_tx* tx = (const partybox_tx*)data;
    tBTA_AV_RC_MSG event = {0};
    event.handle = data->hdr.layer_specific;
    if (tx->session == session && active_handle == event.handle && selected_link(control, &event)) {
      if (data->api_vendor.msg.vendor_len == 9) {
        const uint16_t status = AVRC_VendorCmd(event.handle, data->api_vendor.label, &data->api_vendor.msg);
        partybox_native_event(10, tx->session, event.handle, data->api_vendor.label, status);
      } else if (data->api_vendor.msg.vendor_len == 0) {
        const uint16_t status = AVRC_Close(event.handle);
        partybox_native_event(13, tx->session, event.handle, 0, status);
      }
    } else if (data->api_vendor.msg.vendor_len == 9) {
      // These local guard failures never reached the native send API.
      partybox_native_event(10, tx->session, event.handle, data->api_vendor.label,
                            tx->session != session || active_handle != event.handle ? 0x10000 : 0x10001);
    }
    return;
  }
  __real_bta_av_rc_vendor_cmd(control, data);
}
void __wrap_bta_av_rc_closed(tBTA_AV_DATA* data) {
  const tBTA_AV_RC_CONN_CHG* event = (const tBTA_AV_RC_CONN_CHG*)data;
  if (event && active_handle != 0xFF && event->handle == active_handle) {
    partybox_native_event(3, session, active_handle, 0, 0);
    active_handle = 0xFF;
    ++session;
  }
  __real_bta_av_rc_closed(data);
}
