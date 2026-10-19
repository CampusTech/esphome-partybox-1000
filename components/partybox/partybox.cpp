#include "partybox.h"

#include <cmath>
#include <cstring>

#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_spp_api.h"
#include "esp_system.h"
#include "esphome/core/controller_registry.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/task.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "PartyBox's internal Bluetooth hooks require ESP-IDF 5.5.5; audit before upgrading"
#endif
extern "C" bool partybox_queue_word(uint32_t session, uint8_t handle, uint8_t label, uint32_t word);
extern "C" bool partybox_queue_close(uint32_t session, uint8_t handle);
extern "C" bool partybox_radio_read(uint16_t opcode);
namespace esphome::partybox {
static const char* const TAG = "partybox";
static const char* field_name(Field field) {
  static const char* names[] = {"volume", "mute", "source", "bass", "lighting", "power", "version"};
  const unsigned index = static_cast<unsigned>(field);
  return index < 7 ? names[index] : "unknown";
}
PartyBox* active_partybox = nullptr;
static void radio_readback() {
  for (uint16_t opcode : {0x0C19, 0x0C23, 0x0C38, 0x0C39, 0x0C51}) {
    const bool queued = partybox_radio_read(opcode);
    ESP_LOGI(TAG, "Controller read %04X queued=%u", opcode, queued);
  }
}
static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
    esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, active_partybox->peer_matches(p->cfm_req.bda));
  if (event == ESP_BT_GAP_PIN_REQ_EVT) {
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_bt_gap_pin_reply(p->pin_req.bda, active_partybox->peer_matches(p->pin_req.bda) && !p->pin_req.min_16_digit, 4,
                         pin);
  }
  if (event == ESP_BT_GAP_AUTH_CMPL_EVT) ESP_LOGI(TAG, "Authentication status=%u", p->auth_cmpl.stat);
  if (event == ESP_BT_GAP_CONFIG_EIR_DATA_EVT) ESP_LOGI(TAG, "EIR completion status=%u", p->config_eir_data.stat);
  if (event == ESP_BT_GAP_MODE_CHG_EVT)
    ESP_LOGI(TAG, "Bluetooth mode selected_peer=%u mode=%u interval_slots=%u",
             active_partybox->peer_matches(p->mode_chg.bda), p->mode_chg.mode, p->mode_chg.interval);
  if (event == ESP_BT_GAP_READ_RSSI_DELTA_EVT) {
    const bool selected = active_partybox->peer_matches(p->read_rssi_delta.bda);
    if (p->read_rssi_delta.stat == ESP_BT_STATUS_SUCCESS)
      ESP_LOGI(TAG, "RSSI delta selected_peer=%u status=%u delta=%d", selected, p->read_rssi_delta.stat,
               p->read_rssi_delta.rssi_delta);
    else
      ESP_LOGI(TAG, "RSSI delta selected_peer=%u status=%u", selected, p->read_rssi_delta.stat);
  }
  if (event == ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT)
    ESP_LOGI(TAG, "ACL connection status=%u selected_peer=%u", p->acl_conn_cmpl_stat.stat,
             active_partybox->peer_matches(p->acl_conn_cmpl_stat.bda));
  if (event == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT)
    ESP_LOGI(TAG, "ACL disconnection reason=%u selected_peer=%u", p->acl_disconn_cmpl_stat.reason,
             active_partybox->peer_matches(p->acl_disconn_cmpl_stat.bda));
}
static void ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* p) {
  if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT) ESP_LOGI(TAG, "AVRCP connected=%u", p->conn_stat.connected);
}
static void tg_callback(esp_avrc_tg_cb_event_t, esp_avrc_tg_cb_param_t*) {}
static void audio_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* p) {
  if (!p || !active_partybox) return;
  if (event == ESP_A2D_PROF_STATE_EVT && p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
    esp_a2d_mcc_t caps{};
    caps.type = ESP_A2D_MCT_SBC;
    const uint8_t sbc[] = {0xFF, 0xFF, 2, 53};
    std::memcpy(&caps.cie, sbc, 4);
    ESP_LOGI(TAG, "SBC registration=%d", esp_a2d_sink_register_stream_endpoint(0, &caps));
  }
  if (event == ESP_A2D_SEP_REG_STATE_EVT && p->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS)
    active_partybox->profile_ready(true);
  if (event == ESP_A2D_AUDIO_STATE_EVT) {
    Event e;
    e.type = 14;
    e.session = p->audio_stat.conn_hdl;
    e.value = p->audio_stat.state;
    e.handle = active_partybox->peer_matches(p->audio_stat.remote_bda);
    active_partybox->native_event(e);
  }
  if (event == ESP_A2D_AUDIO_CFG_EVT) {
    Event e;
    e.type = 15;
    e.session = p->audio_cfg.conn_hdl;
    e.value = p->audio_cfg.mcc.type;
    e.length = 4;
    e.handle = active_partybox->peer_matches(p->audio_cfg.remote_bda);
    std::memcpy(e.bytes, &p->audio_cfg.mcc.cie, 4);
    active_partybox->native_event(e);
  }
  if (event == ESP_A2D_SNK_PSC_CFG_EVT) {
    // This SDK event supplies no peer address or connection handle.
    Event e;
    e.type = 16;
    e.value = p->a2d_psc_cfg_stat.psc_mask;
    active_partybox->native_event(e);
  }
  if (event == ESP_A2D_CONNECTION_STATE_EVT) {
    const bool selected = active_partybox->peer_matches(p->conn_stat.remote_bda);
    const int reason = p->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED ? p->conn_stat.disc_rsn : -1;
    ESP_LOGI(TAG, "A2DP state=%u disconnect_reason=%d selected_peer=%u", p->conn_stat.state, reason, selected);
    if (selected) {
      Event e;
      e.type = 9;
      e.value = p->conn_stat.state;
      active_partybox->post(e);
    } else if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
      esp_a2d_sink_disconnect(p->conn_stat.remote_bda);
  }
}
static void audio_data(esp_a2d_conn_hdl_t, esp_a2d_audio_buff_t* buffer) {
  if (buffer) {
    active_partybox->add_audio(buffer->data_len);
    esp_a2d_audio_buff_free(buffer);
  }
}
static void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* p) {
  // This callback alone owns the SPP generation counter.
  static uint32_t generation = 0;
  Event e;
  if (event == ESP_SPP_INIT_EVT && p->init.status == ESP_SPP_SUCCESS)
    active_partybox->profile_ready(false);
  else if (event == ESP_SPP_OPEN_EVT) {
    if (p->open.status == ESP_SPP_SUCCESS && !active_partybox->peer_matches(p->open.rem_bda)) {
      esp_spp_disconnect(p->open.handle);
      return;
    }
    generation = active_partybox->spp_open(p->open.status == ESP_SPP_SUCCESS ? p->open.handle : 0);
  } else if (event == ESP_SPP_CLOSE_EVT) {
    active_partybox->spp_close(p->close.handle);
  } else if (event == ESP_SPP_CL_INIT_EVT && p->cl_init.status != ESP_SPP_SUCCESS) {
    generation = active_partybox->spp_open(0);
  } else if (event == ESP_SPP_CONG_EVT) {
    e.type = 7;
    e.session = generation;
    e.value = p->cong.handle;
    e.label = p->cong.cong;
    active_partybox->post(e);
  } else if (event == ESP_SPP_WRITE_EVT) {
    e.type = 8;
    e.session = generation;
    e.value = p->write.handle;
    e.label = p->write.status == ESP_SPP_SUCCESS;
    e.handle = p->write.cong;
    active_partybox->post(e);
  } else if (event == ESP_SPP_DATA_IND_EVT) {
    for (int offset = 0; offset < p->data_ind.len; offset += 64) {
      e.type = 6;
      e.session = generation;
      e.value = p->data_ind.handle;
      e.length = std::min(64, p->data_ind.len - offset);
      std::memcpy(e.bytes, p->data_ind.data + offset, e.length);
      active_partybox->post(e);
    }
  }
}
bool PartyBox::peer_matches(const uint8_t* p) const {
  return p && !std::memcmp(peer_, p, 6);
}
void PartyBox::post(const Event& event) {
  if (!events_ || xQueueSend(events_, &event, 0) != pdTRUE) {
    ++dropped_;
    overflow_ = true;
  }
}
void PartyBox::native_event(const Event& e) {
  // Optional logs must never consume critical-event capacity or force recovery.
  // Local TX failures still affect command completion and remain critical.
  if ((e.type >= 11 && e.type <= 17) || (e.type == 10 && e.value == 0)) {
    if (diagnostics_) xQueueSend(diagnostics_, &e, 0);
    return;
  }
  // Lifecycle mailboxes cannot be lost when the bounded data-event queue fills.
  portENTER_CRITICAL(&transport_mux_);
  if (e.type == 1) {
    native_rc_session_ = e.session;
    native_rc_handle_ = e.handle;
    native_rc_active_ = true;
  }
  if (e.type == 3 && native_rc_session_ == e.session) native_rc_active_ = false;
  portEXIT_CRITICAL(&transport_mux_);
  post(e);
}
uint32_t PartyBox::spp_open(uint32_t handle) {
  portENTER_CRITICAL(&transport_mux_);
  const bool conflict = native_spp_handle_ != 0;
  if (!conflict) {
    native_spp_handle_ = handle;
    ++native_spp_epoch_;
  }
  const uint32_t epoch = native_spp_epoch_;
  portEXIT_CRITICAL(&transport_mux_);
  if (conflict && handle && handle != native_spp_handle_) esp_spp_disconnect(handle);
  return epoch;
}
void PartyBox::spp_close(uint32_t handle) {
  portENTER_CRITICAL(&transport_mux_);
  if (native_spp_handle_ == handle) {
    native_spp_handle_ = 0;
    ++native_spp_epoch_;
  }
  portEXIT_CRITICAL(&transport_mux_);
}
void PartyBox::sync_spp_() {
  Event e;
  e.type = 4;
  portENTER_CRITICAL(&transport_mux_);
  e.value = native_spp_handle_;
  e.session = native_spp_epoch_;
  portEXIT_CRITICAL(&transport_mux_);
  if (e.session != spp_session_) event_(e);
}
void PartyBox::rssi_worker_(void* context) {
  auto* self = static_cast<PartyBox*>(context);
  for (;;) {
    uint8_t request;
    if (xQueueReceive(self->rssi_requests_, &request, portMAX_DELAY) != pdTRUE) continue;
    if (!self->rssi_active_.load()) continue;
    // The public API can wait indefinitely for BTC queue space. Only this
    // optional worker may wait; the main loop posts requests with zero wait.
    const esp_err_t status = esp_bt_gap_read_rssi_delta(self->peer_);
    ESP_LOGI(TAG, "RSSI delta request status=%d stack_free_bytes=%u", status,
             unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  }
}
void PartyBox::setup() {
  if (active_partybox) {
    ESP_LOGE(TAG, "Only one PartyBox Bluetooth owner is supported");
    mark_failed();
    update_indicator_();
    return;
  }
  active_partybox = this;
  events_ = xQueueCreate(32, sizeof(Event));
  if (!events_) {
    mark_failed();
    update_indicator_();
    return;
  }
  diagnostics_ = xQueueCreate(8, sizeof(Event));  // Best effort; absent/full means logs are lost.
  esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_spp_cfg_t spp{};
  spp.mode = ESP_SPP_MODE_CB;
  spp.enable_l2cap_ertm = false;
  const auto check = [this](esp_err_t result) {
    if (result == ESP_OK) return true;
    ESP_LOGE(TAG, "Bluetooth setup: %s", esp_err_to_name(result));
    mark_failed();
    update_indicator_();
    return false;
  };
  if (!check(esp_bt_controller_init(&controller)) || !check(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) ||
      !check(esp_bluedroid_init()) || !check(esp_bluedroid_enable()) ||
      !check(esp_bt_gap_register_callback(gap_callback)) || !check(esp_bt_gap_set_device_name("PartyBox-ESPHome")) ||
      !check(esp_avrc_ct_init()) || !check(esp_avrc_ct_register_callback(ct_callback)) || !check(esp_avrc_tg_init()) ||
      !check(esp_avrc_tg_register_callback(tg_callback)) || !check(esp_a2d_register_callback(audio_callback)) ||
      !check(esp_a2d_sink_register_audio_data_callback(audio_data)) || !check(esp_a2d_sink_init()) ||
      !check(esp_spp_register_callback(spp_callback)) || !check(esp_spp_enhanced_init(&spp)))
    return;
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  check(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability)));
  last_spp_attempt_ = millis() - 5000;
  // One worker and one queued token bound memory even if BTC stops draining.
  // ESP-IDF expresses task stack size in bytes. Failure disables RSSI polling.
  rssi_requests_ = xQueueCreate(1, sizeof(uint8_t));
  if (rssi_requests_ &&
      xTaskCreate(rssi_worker_, "partybox_rssi", 4096, this, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    vQueueDelete(rssi_requests_);
    rssi_requests_ = nullptr;
  }
  if (!rssi_requests_) ESP_LOGI(TAG, "RSSI delta polling disabled: worker unavailable");
}
void PartyBox::result_(const char* s, bool error) {
  if (error) indicator_.failed(millis());
  last_result_ = s;
  dirty_ = true;
  ESP_LOGI(TAG, "Command: %s", s);
}
bool PartyBox::eligible_(Field f, int v) const {
  if (!valid_command({f, v}) || !tws_) return false;
  if (f == Field::SOURCE || f == Field::VERSION) return spp_enabled_ && spp_handle_ && !spp_poisoned_;
  if (f == Field::LIGHTING && state_.lighting_flag.quality == Quality::UNKNOWN) return false;
  if (f == Field::POWER) return state_.power.quality != Quality::UNKNOWN && state_.power.value != bool(v);
  return state_.power.quality != Quality::UNKNOWN && state_.power.value;
}
bool PartyBox::request(Field f, int value) {
  if (!eligible_(f, value)) {
    result_("refused: unavailable or unknown state", true);
    return false;
  }
  // Power actions cannot sit behind other commands or be duplicated while pending.
  if (f == Field::POWER && (scheduler_.pending() || scheduler_.size())) {
    result_("refused: power requires idle command queue", true);
    return false;
  }
  if (!scheduler_.enqueue({f, value})) {
    result_("refused: queue full or invalid value", true);
    return false;
  }
  result_("queued");
  return true;
}
void PartyBox::action(uint8_t role) {
  if (role == 1) {
    request(Field::POWER, 0);
    return;
  }
  if (role == 2) {
    request(Field::POWER, 1);
    return;
  }
  if (role == 3) {
    request(Field::VERSION, 0);
    return;
  }
  if (identity_) radio_readback();
  if (is_failed() || !a2dp_ready_ || tws_ || connect_pending_) {
    result_("refused: connect not ready or already linked", true);
    return;
  }
  connect_pending_ = true;
  connect_at_ = millis();
  const auto e = esp_a2d_sink_connect(peer_);
  if (e != ESP_OK) connect_pending_ = false;
  result_(e == ESP_OK ? "connect requested" : "connect failed", e != ESP_OK);
}
void PartyBox::lost_() {
  rssi_active_ = false;
  tws_ = false;
  scheduler_.reset();
  state_.disconnect();
  snapshot_ = false;
  framer_.reset();
  versions_ = "unknown";
  uint32_t handle;
  portENTER_CRITICAL(&transport_mux_);
  handle = native_spp_handle_;
  spp_session_ = native_spp_epoch_;
  portEXIT_CRITICAL(&transport_mux_);
  if (handle) esp_spp_disconnect(handle);
  spp_handle_ = 0;
  spp_poisoned_ = true;
  pending_spp_ = false;
  dirty_ = true;
  result_("link lost: state unknown; commands discarded");
}
void PartyBox::recover_() {
  uint8_t handle;
  bool active;
  portENTER_CRITICAL(&transport_mux_);
  fenced_session_ = native_rc_session_;
  handle = native_rc_handle_;
  active = native_rc_active_;
  portEXIT_CRITICAL(&transport_mux_);
  fence_active_ = true;
  lost_();
  if (active && !partybox_queue_close(fenced_session_, handle)) result_("recovery close failed: reboot controller", true);
  esp_a2d_sink_disconnect(peer_);
}
void PartyBox::finish_(bool accepted) {
  const auto c = scheduler_.current;
  if (!scheduler_.ack(pending_label_, accepted)) return;
  ESP_LOGI(TAG, "Transaction resolved field=%s target=%d label=%u accepted=%u", field_name(c.field), c.value,
           pending_label_, accepted);
  if (accepted) state_.assume(c.field, c.value, c.revision, millis());
  pending_spp_ = false;
  result_(accepted ? "accepted: assumed until reported" : "rejected", !accepted);
}
void PartyBox::event_(const Event& e) {
  const uint32_t now = millis();
  if (e.type == 1) {
    if (fence_active_ && e.session == fenced_session_) return;
    fence_active_ = false;
    if (!tws_ || session_ != e.session) {
      scheduler_.reset();
      state_.disconnect();
      snapshot_ = false;
      session_ = e.session;
      rc_handle_ = e.handle;
      tws_ = true;
      result_("TWS connected");
    }
    char hex[9];
    snprintf(hex, sizeof(hex), "%08lX", (unsigned long)e.value);
    last_word_ = hex;
    ++words_;
    ESP_LOGI(TAG, "TWS %s", hex);
    if (state_.decode(e.value, now)) dirty_ = true;
    if ((e.value & 31) == 4 && state_.source.quality == Quality::OBSERVED) {
      snapshot_ = true;
      snapshot_at_ = now;
    }
  } else if (e.type == 2) {
    if (tws_ && e.session == session_ && !pending_spp_ && e.label == pending_label_) finish_(e.value);
  } else if (e.type == 3) {
    ESP_LOGI(TAG, "Native RC closed session=%lu handle=%u pending=%u label=%u field=%s", (unsigned long)e.session,
             e.handle, scheduler_.pending(), pending_label_, field_name(scheduler_.current.field));
    if (tws_ && session_ == e.session) lost_();
  } else if (e.type == 4) {
    ESP_LOGI(TAG, "SPP lifecycle session=%lu handle=%lu previous_handle=%lu pending=%u", (unsigned long)e.session,
             (unsigned long)e.value, (unsigned long)spp_handle_, pending_spp_);
    spp_connecting_ = false;
    spp_connect_warned_ = false;
    versions_ = "unknown";
    if (e.value && (!tws_ || !spp_enabled_)) {
      spp_session_ = e.session;
      esp_spp_disconnect(e.value);
      return;
    }
    if (pending_spp_ && scheduler_.pending()) {
      scheduler_.timeout();
      pending_spp_ = false;
      result_("SPP session changed: command unconfirmed");
    }
    spp_handle_ = e.value;
    spp_session_ = e.session;
    spp_poisoned_ = false;
    spp_congested_ = false;
    framer_.reset();
    dirty_ = true;
    if (spp_handle_) {
      spp_backoff_ = 5000;
      result_("SPP connected");
      request(Field::VERSION, 0);
    } else {
      spp_backoff_ = std::min<uint32_t>(spp_backoff_ * 2, 60000U);
      result_("SPP connection failed", true);
    }
  } else if (e.type == 5 && e.session == spp_session_ && e.value == spp_handle_) {
    spp_handle_ = 0;
    spp_connecting_ = false;
    framer_.reset();
    versions_ = "unknown";
    if (pending_spp_ && scheduler_.pending()) {
      scheduler_.timeout();
      pending_spp_ = false;
      result_("SPP disconnected: command unconfirmed");
    }
    dirty_ = true;
  } else if (e.type >= 6 && e.type <= 8 && e.value == spp_handle_ && e.session == spp_session_) {
    if (e.type == 7) spp_congested_ = e.label;
    if (e.type == 8) {
      if (e.label) indicator_.transmitted(now);
      spp_congested_ = e.handle;
      if (!e.label) {
        spp_poisoned_ = true;
        esp_spp_disconnect(spp_handle_);
        result_("SPP write failed", true);
      }
    }
    if (e.type == 6 && !spp_poisoned_)
      for (unsigned i = 0; i < e.length; i++)
        if (framer_.feed(e.bytes[i])) {
          const auto* p = framer_.data();
          const auto n = framer_.size();
          if (n == 10 && p[2] == 8) {
            char version[48];
            snprintf(version, sizeof(version), "%u/%u/%u/%u/%u", p[3], p[4], p[5], p[6], p[7]);
            versions_ = version;
            dirty_ = true;
            if (pending_spp_ && scheduler_.pending() && scheduler_.current.field == Field::VERSION) finish_(true);
          } else if (n == 5 && p[2] == 10 && pending_spp_ && scheduler_.pending() &&
                     scheduler_.current.field != Field::VERSION)
            finish_(true);
        }
  } else if (e.type == 9 &&
             (e.value == ESP_A2D_CONNECTION_STATE_CONNECTED || e.value == ESP_A2D_CONNECTION_STATE_DISCONNECTED)) {
    if (connect_pending_)
      result_(e.value == ESP_A2D_CONNECTION_STATE_CONNECTED ? "A2DP connected: waiting for TWS"
                                                            : "connect failed: A2DP disconnected before TWS");
    connect_pending_ = false;
  } else if (e.type == 10) {
    if (e.value == 0 && tws_ && e.session == session_ && e.handle == rc_handle_)
      indicator_.transmitted(now);
    const bool matching = tws_ && e.session == session_ && e.handle == rc_handle_ && scheduler_.pending() &&
                          !pending_spp_ && e.label == pending_label_;
    ESP_LOGI(TAG, "Native TX session=%lu handle=%u label=%u status=%lu matching=%u", (unsigned long)e.session, e.handle,
             e.label, (unsigned long)e.value, matching);
    if (matching && e.value != 0) {
      scheduler_.ack(e.label, false);
      pending_spp_ = false;
      result_("native TX failed: no peer ACK expected", true);
    }
  } else if (e.type == 11) {
    ESP_LOGI(TAG, "Native response TX session=%lu handle=%u label=%u ctype=%u status=%u", (unsigned long)e.session,
             e.handle, e.label, unsigned(e.value >> 16), unsigned(e.value & 0xFFFF));
  } else if (e.type == 12) {
    ESP_LOGI(TAG, "Native ACK RX session=%lu handle=%u label=%u ctype=%lu", (unsigned long)e.session, e.handle, e.label,
             (unsigned long)e.value);
  } else if (e.type == 13) {
    ESP_LOGI(TAG, "Native RC close request session=%lu handle=%u status=%lu", (unsigned long)e.session, e.handle,
             (unsigned long)e.value);
  } else if (e.type == 14) {
    const char* state = e.value == ESP_A2D_AUDIO_STATE_STARTED   ? "STARTED"
                        : e.value == ESP_A2D_AUDIO_STATE_SUSPEND ? "SUSPEND"
                                                                 : "UNKNOWN";
    ESP_LOGI(TAG, "A2DP audio selected_peer=%u handle=%lu state=%lu (%s)", e.handle, (unsigned long)e.session,
             (unsigned long)e.value, state);
  } else if (e.type == 15 && e.length == 4) {
    ESP_LOGI(TAG, "A2DP codec selected_peer=%u handle=%lu type=%lu cie4=%02X%02X%02X%02X", e.handle,
             (unsigned long)e.session, (unsigned long)e.value, e.bytes[0], e.bytes[1], e.bytes[2], e.bytes[3]);
  } else if (e.type == 16) {
    ESP_LOGI(TAG, "A2DP sink capabilities mask=%04lX delay_report=%u", (unsigned long)e.value,
             bool(e.value & ESP_A2D_PSC_DELAY_RPT));
  } else if (e.type == 17 && e.length == 5) {
    const unsigned length = (unsigned(e.bytes[1]) << 8) | e.bytes[2];
    ESP_LOGI(TAG,
             "Native vendor forwarded selected_peer=1 session=%lu handle=%u label=%u company=%06lX pdu=%d ctype=%u "
             "length=%u",
             (unsigned long)e.session, e.handle, e.label, (unsigned long)e.value, e.bytes[4] ? int(e.bytes[3]) : -1,
             e.bytes[0], length);
  }
}
void PartyBox::send_next_() {
  if (scheduler_.pending() || uint32_t(millis() - last_send_) < 150) return;
  Command c;
  if (!scheduler_.take(c)) return;
  if (!eligible_(c.field, c.value)) {
    result_("discarded: state changed before send", true);
    return;
  }
  const bool spp = c.field == Field::SOURCE || c.field == Field::VERSION;
  if (spp && spp_congested_) {
    scheduler_.enqueue(c);
    return;
  }
  uint8_t label = next_label_;
  unsigned tries = 0;
  while (tries < 16 && !scheduler_.available(label)) {
    label = (label + 1) & 15;
    ++tries;
  }
  if (tries == 16) {
    result_("refused: transaction labels exhausted; reconnect required", true);
    return;
  }
  next_label_ = (label + 1) & 15;
  if (c.field == Field::SOURCE) {
    state_.begin_source_change();
    dirty_ = true;
  }
  c.revision = state_.revision(c.field);
  if (!scheduler_.begin(c, label, millis())) return;
  pending_label_ = label;
  pending_spp_ = spp;
  last_send_ = millis();
  const uint32_t word = spp ? 0 : command_word(c, state_.lighting_flag.value);
  ESP_LOGI(TAG, "TX queued field=%s target=%d transport=%s word=%08lX session=%lu handle=%lu label=%u",
           field_name(c.field), c.value, spp ? "SPP" : "TWS", (unsigned long)word,
           (unsigned long)(spp ? spp_session_ : session_), (unsigned long)(spp ? spp_handle_ : rc_handle_), label);
  bool queued = false;
  if (spp) {
    uint8_t bytes[] = {0xBA, 7, 0x0F, 1, 0, 255, 255};
    size_t n = 7;
    if (c.field == Field::VERSION) {
      bytes[1] = 6;
      bytes[2] = 8;
      bytes[4] = 255;
      n = 6;
    } else
      bytes[4] = c.value == 13 ? 0 : c.value == 15 ? 1 : c.value == 6 ? 2 : 3;
    queued = esp_spp_write(spp_handle_, n, bytes) == ESP_OK;

  } else
    queued = partybox_queue_word(session_, rc_handle_, label, word);
  if (!queued)
    finish_(false);
  else
    result_("sent: awaiting acknowledgement");
}
void PartyBox::loop() {
  update_indicator_();
  if (is_failed()) return;
  const uint32_t now = millis();
  if (!identity_ && a2dp_ready_ && spp_ready_) {
    esp_bt_cod_t cod{};
    cod.minor = 0x0A;
    cod.major = 4;
    cod.service = 0x1A0;
    static uint8_t manufacturer[] = {0x48, 0x4D, 0x50, 0x42, 0x0D, 0x10};
    esp_bt_eir_data_t eir{};
    eir.include_name = true;
    eir.include_uuid = true;
    eir.manufacturer_len = 6;
    eir.p_manufacturer_data = manufacturer;
    if (esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL) != ESP_OK || esp_bt_gap_config_eir_data(&eir) != ESP_OK ||
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_LIMITED_DISCOVERABLE) != ESP_OK) {
      mark_failed();
      update_indicator_();
      return;
    }
    identity_ = true;
    ESP_LOGI(TAG, "Ready for selected speaker's TWS search");
    set_timeout("controller-readback", 1000, radio_readback);
  }
  if (overflow_.exchange(false)) {
    xQueueReset(events_);
    recover_();
    result_("event overflow: reconnect required", true);
  }
  sync_spp_();
  Event e;
  unsigned processed = 0;
  while (processed++ < 32 && xQueueReceive(events_, &e, 0) == pdTRUE) event_(e);
  processed = 0;
  while (diagnostics_ && processed++ < 8 && xQueueReceive(diagnostics_, &e, 0) == pdTRUE) event_(e);
  rssi_active_ = tws_;
  if (rssi_requests_ && tws_ && uint32_t(now - last_rssi_read_) >= 15000) {
    last_rssi_read_ = now;
    // A local-controller RSSI delta read is not a TWS application keepalive.
    const uint8_t request = 1;
    xQueueSend(rssi_requests_, &request, 0);  // Optional diagnostic: drop if full.
  }
  if (connect_pending_ && uint32_t(now - connect_at_) >= 20000) {
    connect_pending_ = false;
    result_("connect timed out", true);
  }
  if (spp_connecting_ && !spp_connect_warned_ && uint32_t(now - spp_connect_at_) >= 20000) {
    spp_connect_warned_ = true;
    result_("SPP connect slow: waiting for native completion");
  }
  if (spp_enabled_ && tws_ && spp_ready_ && !spp_handle_ && !spp_connecting_ &&
      uint32_t(now - last_spp_attempt_) >= spp_backoff_) {
    last_spp_attempt_ = now;
    spp_connect_at_ = now;
    spp_connecting_ = true;
    spp_connect_warned_ = false;
    if (esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, 1, peer_) != ESP_OK) {
      spp_connecting_ = false;
      spp_backoff_ = std::min<uint32_t>(spp_backoff_ * 2, 60000U);
    }
  }
  if (scheduler_.expired(now)) {
    ESP_LOGI(TAG, "Command timeout field=%s target=%d transport=%s session=%lu handle=%lu label=%u",
             field_name(scheduler_.current.field), scheduler_.current.value, pending_spp_ ? "SPP" : "TWS",
             (unsigned long)(pending_spp_ ? spp_session_ : session_),
             (unsigned long)(pending_spp_ ? spp_handle_ : rc_handle_), pending_label_);
    scheduler_.timeout();
    if (pending_spp_) {
      spp_poisoned_ = true;
      esp_spp_disconnect(spp_handle_);
    }
    pending_spp_ = false;
    result_("timed out: state unconfirmed; no retry", true);
  }
  send_next_();
  if (dirty_ || uint32_t(now - last_publish_) >= 5000) {
    publish_();
    dirty_ = false;
    last_publish_ = now;
  }
}
void PartyBox::update_indicator_() {
#ifdef USE_LIGHT
  if (!status_light_) return;
  bool network_ready = true;
#ifdef USE_WIFI
  network_ready = wifi::global_wifi_component && wifi::global_wifi_component->is_connected();
#endif
#ifdef USE_API
  network_ready = network_ready && api::global_api_server &&
                  api::global_api_server->is_connected_with_state_subscription();
#endif
  const bool speaker_ready = tws_ && (!spp_enabled_ || (spp_handle_ && !spp_poisoned_));
  const auto color = indicator_.color(millis(), network_ready, speaker_ready, is_failed());
  if (indicator_initialized_ && color == last_indicator_) return;
  indicator_initialized_ = true;
  last_indicator_ = color;
  const bool red = color == IndicatorColor::RED || color == IndicatorColor::AMBER;
  const bool green = color == IndicatorColor::GREEN || color == IndicatorColor::AMBER || color == IndicatorColor::CYAN;
  const bool blue = color == IndicatorColor::BLUE || color == IndicatorColor::CYAN;
  auto call = status_light_->make_call();
  call.set_state(color != IndicatorColor::OFF)
      .set_transition_length(0)
      .set_brightness(status_brightness_)
      .set_rgb(red ? 1.0f : 0.0f, green ? (color == IndicatorColor::AMBER ? 0.3f : 1.0f) : 0.0f,
               blue ? 1.0f : 0.0f)
      .set_publish(false)
      .set_save(false)
      .perform();
#endif
}
void PartyBox::publish_() {
  using namespace media_player;
  if (player_) {
    // HA's ESPHome mapper rejects NONE, including during its disconnect callback.
    // IDLE is neutral bridge status, not evidence of power or analog playback.
    player_->state = !tws_ || state_.power.quality == Quality::UNKNOWN ? MEDIA_PLAYER_STATE_IDLE
                     : !state_.power.value                             ? MEDIA_PLAYER_STATE_OFF
                                                                       : MEDIA_PLAYER_STATE_ON;
    if (state_.volume.quality != Quality::UNKNOWN) player_->volume = state_.volume.value / 32.0f;
    player_->publish_state();
  }
  const int fields[] = {state_.source.value, state_.bass.value, state_.lighting.value};
  const Quality qualities[] = {state_.source.quality, state_.bass.quality, state_.lighting.quality};
  static const char* sources[] = {"Bluetooth", "USB", "AUX", "LINE IN"};
  static const char* bass[] = {"Off", "Level 1", "Level 2"};
  static const char* lights[] = {"Off", "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4"};
  const bool spp_usable = spp_enabled_ && tws_ && spp_handle_ && !spp_poisoned_;
  // Source readback remains useful on TWS, but its control requires usable SPP.
  for (unsigned i = 0; i < 3; i++)
    if (choices_[i] && tws_ && (i != 0 || spp_usable) && qualities[i] != Quality::UNKNOWN) {
      int index = fields[i];
      if (i == 0) index = index == 13 ? 0 : index == 15 ? 1 : index == 6 ? 2 : 3;
      choices_[i]->publish_state(i == 0 ? sources[index] : i == 1 ? bass[index] : lights[index]);
    } else if (choices_[i])
      choices_[i]->invalidate();
  const float values[] = {state_.volume.quality == Quality::UNKNOWN ? NAN : float(state_.volume.value),
                          float(esp_get_free_heap_size()),
                          float(esp_get_minimum_free_heap_size()),
                          float(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                          float(words_),
                          float(dropped_.load()),
                          float(audio_bytes_.load()),
                          float(scheduler_.size()),
                          snapshot_ ? float(uint32_t(millis() - snapshot_at_)) / 1000 : NAN};
  for (unsigned i = 0; i < 9; i++)
    if (sensors_[i]) sensors_[i]->publish_state(values[i]);
  if (binaries_[0]) binaries_[0]->publish_state(tws_);
  if (binaries_[1]) binaries_[1]->publish_state(bool(spp_handle_) && !spp_poisoned_);
  const Value<bool>* binary_values[] = {&state_.activity, &state_.smart_dj, &state_.voice_cancel};
  for (unsigned i = 0; i < 3; i++)
    if (binaries_[i + 2]) {
      if (binary_values[i]->quality != Quality::UNKNOWN)
        binaries_[i + 2]->publish_state(binary_values[i]->value);
      else
        binaries_[i + 2]->invalidate_state();
    }
  const std::string text_values[] = {quality_name(state_.volume.quality),
                                     quality_name(state_.mute.quality),
                                     quality_name(state_.source.quality),
                                     quality_name(state_.power.quality),
                                     quality_name(state_.bass.quality),
                                     quality_name(state_.lighting.quality),
                                     last_result_,
                                     versions_,
                                     last_word_,
                                     state_.power.quality == Quality::UNKNOWN ? "unknown"
                                     : state_.power.value                     ? "awake"
                                                                              : "standby requested",
                                     quality_name(state_.activity.quality)};
  for (unsigned i = 0; i < 11; i++)
    if (texts_[i]) {
      if ((i == 7 && (!spp_usable || versions_ == "unknown")) || (i == 8 && !tws_)) {
        if (texts_[i]->has_state()) {
          texts_[i]->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY) && defined(USE_TEXT_SENSOR)
          ControllerRegistry::notify_text_sensor_update(texts_[i]);
#endif
        }
      } else
        texts_[i]->publish_state(text_values[i]);
    }
}
void PartyBox::dump_config() {
  ESP_LOGCONFIG(TAG, "JBL PartyBox 1000 Classic controller; one selected peer, no keepalive audio");
}
media_player::MediaPlayerTraits Player::get_traits() {
  media_player::MediaPlayerTraits t;
  t.clear_feature_flags(0xFFFFFFFF);
  t.add_feature_flags(media_player::VOLUME_SET | media_player::VOLUME_MUTE | media_player::VOLUME_STEP |
                      media_player::TURN_ON | media_player::TURN_OFF);
  return t;
}
bool Player::is_muted() const {
  return parent_->state().mute.value;
}
void Player::control(const media_player::MediaPlayerCall& call) {
  using namespace media_player;
  if (call.get_volume().has_value() && std::isfinite(*call.get_volume()))
    parent_->request(Field::VOLUME, std::lround(std::max(0.0f, std::min(1.0f, *call.get_volume())) * 32));
  if (!call.get_command().has_value()) return;
  const auto c = *call.get_command();
  if (c == MEDIA_PLAYER_COMMAND_MUTE || c == MEDIA_PLAYER_COMMAND_UNMUTE)
    parent_->request(Field::MUTE, c == MEDIA_PLAYER_COMMAND_MUTE);
  else if (c == MEDIA_PLAYER_COMMAND_TURN_ON || c == MEDIA_PLAYER_COMMAND_TURN_OFF)
    parent_->request(Field::POWER, c == MEDIA_PLAYER_COMMAND_TURN_ON);
  else if ((c == MEDIA_PLAYER_COMMAND_VOLUME_UP || c == MEDIA_PLAYER_COMMAND_VOLUME_DOWN) &&
           parent_->state().volume.quality != Quality::UNKNOWN)
    parent_->request(Field::VOLUME, std::max(0, std::min(32, parent_->state().volume.value +
                                                                 (c == MEDIA_PLAYER_COMMAND_VOLUME_UP ? 1 : -1))));
}
void Choice::invalidate() {
  if (!has_state()) return;
  set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY) && defined(USE_SELECT)
  ControllerRegistry::notify_select_update(this);
#endif
}
void Choice::control(const std::string& value) {
  const auto& options = traits.get_options();
  for (unsigned i = 0; i < options.size(); i++)
    if (options[i] == value) {
      static const int sources[] = {13, 15, 6, 9};
      parent_->request(role_ == 0   ? Field::SOURCE
                       : role_ == 1 ? Field::BASS
                                    : Field::LIGHTING,
                       role_ == 0 ? sources[i] : i);
      return;
    }
}
void Action::press_action() {
  parent_->action(role_);
}
}  // namespace esphome::partybox
extern "C" void partybox_radio_event(uint16_t opcode, uint8_t status, const uint8_t* parameters, uint16_t length) {
  char text[33] = {};
  const uint16_t count = length < 16 ? length : 16;
  for (uint16_t index = 0; parameters != nullptr && index < count; ++index)
    snprintf(text + index * 2, sizeof(text) - index * 2, "%02X", parameters[index]);
  ESP_LOGI("partybox", "Controller opcode=%04X status=%u parameters=%s", opcode, status, text);
  if (opcode == 0x0C51) {
    bool name_present = false, hmpb = false;
    uint16_t manufacturer_len = 0;
    const int fec = parameters != nullptr && length ? parameters[0] : -1;
    // Return parameters contain FEC followed by at most 240 EIR bytes.
    const uint16_t end = std::min<uint16_t>(length, 241);
    for (uint16_t index = 1; parameters != nullptr && index < end && parameters[index];) {
      const uint8_t size = parameters[index];
      if (index + size >= end) break;
      const uint8_t type = parameters[index + 1];
      if (type == 0x08 || type == 0x09) name_present = true;
      if (type == 0xFF) {
        manufacturer_len = size - 1;
        static const uint8_t marker[] = {0x48, 0x4D, 0x50, 0x42, 0x0D, 0x10};
        hmpb = manufacturer_len >= sizeof(marker) && !std::memcmp(parameters + index + 2, marker, sizeof(marker));
      }
      index += size + 1;
    }
    ESP_LOGI("partybox", "EIR length=%u fec=%d name_present=%u manufacturer_len=%u hmpb=%u", length, fec, name_present,
             manufacturer_len, hmpb);
  }
}
extern "C" bool partybox_peer_matches(const uint8_t* address) {
  return esphome::partybox::active_partybox && esphome::partybox::active_partybox->peer_matches(address);
}
extern "C" void partybox_native_event(uint8_t type, uint32_t session, uint8_t handle, uint8_t label, uint32_t value) {
  auto* p = esphome::partybox::active_partybox;
  if (p) {
    esphome::partybox::Event e;
    e.type = type;
    e.session = session;
    e.handle = handle;
    e.label = label;
    e.value = value;
    p->native_event(e);
  }
}
extern "C" void partybox_native_vendor_event(uint32_t session, uint8_t handle, uint8_t label, uint32_t company,
                                             uint8_t ctype, uint16_t length, int16_t pdu) {
  auto* p = esphome::partybox::active_partybox;
  if (p) {
    esphome::partybox::Event e;
    e.type = 17;
    e.session = session;
    e.handle = handle;
    e.label = label;
    e.value = company;
    e.length = 5;
    e.bytes[0] = ctype;
    e.bytes[1] = length >> 8;
    e.bytes[2] = length;
    e.bytes[3] = uint8_t(pdu);
    e.bytes[4] = pdu >= 0;
    p->native_event(e);
  }
}
