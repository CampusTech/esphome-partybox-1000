#include <iostream>

#include "partybox.h"
using namespace esphome::partybox;
extern "C" bool partybox_queue_close(uint32_t, uint8_t) {
  return true;
}
extern "C" bool partybox_queue_word(uint32_t, uint8_t, uint8_t, uint32_t) {
  return true;
}
extern "C" bool partybox_radio_read(uint16_t) {
  return true;
}
extern "C" void partybox_radio_event(uint16_t, uint8_t, const uint8_t*, uint16_t);
extern "C" void partybox_native_event(uint8_t, uint32_t, uint8_t, uint8_t, uint32_t);
extern "C" void partybox_native_vendor_event(uint32_t, uint8_t, uint8_t, uint32_t, uint8_t, uint16_t, int16_t);
struct Test : PartyBox {
  Test() {
    set_peer_address(0x685210540098ULL);
    events_ = xQueueCreate(32, sizeof(Event));
    diagnostics_ = xQueueCreate(8, sizeof(Event));
    tws_ = true;
    session_ = 7;
    spp_handle_ = 42;
    spp_session_ = 1;
    native_spp_handle_ = 42;
    native_spp_epoch_ = 1;
    native_rc_session_ = 7;
    state_.decode(0x090B1244, host_now);
  }
  using PartyBox::a2dp_ready_;
  using PartyBox::connect_pending_;
  using PartyBox::dropped_;
  using PartyBox::event_;
  using PartyBox::identity_;
  using PartyBox::indicator_;
  using PartyBox::last_result_;
  using PartyBox::last_spp_attempt_;
  using PartyBox::load_target_;
  using PartyBox::lost_;
  using PartyBox::native_rc_active_;
  using PartyBox::native_rc_session_;
  using PartyBox::native_spp_epoch_;
  using PartyBox::native_spp_handle_;
  using PartyBox::overflow_;
  using PartyBox::pairing_;
  using PartyBox::scheduler_;
  using PartyBox::spp_backoff_;
  using PartyBox::spp_connect_at_;
  using PartyBox::spp_connecting_;
  using PartyBox::spp_handle_;
  using PartyBox::spp_ready_;
  using PartyBox::target_restart_pending_;
  using PartyBox::tws_;
  using PartyBox::words_;
  Event word(uint32_t v) {
    Event e;
    e.type = 1;
    e.session = 7;
    e.value = v;
    return e;
  }
};
int main(int argc, char** argv) {
  assert(argc == 2);
  std::string test = argv[1];
  Test t;
#ifdef USE_LIGHT
  if (test == "indicator_setup_failure") {
    esphome::light::LightState light;
    t.set_status_light(&light, 0.08f);
    host_controller_init_result = -1;
    t.setup();
    assert(t.is_failed());
    assert(light.calls == 1);  // mark_failed removes the component from the real loop.
    std::cout << "PASS: RGB setup failure is rendered before loop removal\n";
    return 0;
  }
#endif
  if (test == "target_restore") {
    t.set_peer_address(0x685210540098ULL);
    t.load_target_();
    uint8_t old[] = {0x68, 0x52, 0x10, 0x54, 0, 0x98};
    assert(t.peer_matches(old));
    assert(t.configure_target("68:52:10:54:01:1e"));
    assert(t.target_restart_pending_);
    assert(!t.request(Field::VOLUME, 12));
    assert(!t.peer_matches(old));
    t.action(0);
    assert(!t.connect_pending_);
    Test restored;
    restored.load_target_();
    uint8_t fresh[] = {0x68, 0x52, 0x10, 0x54, 1, 0x1e};
    assert(restored.peer_matches(fresh));
    assert(!restored.configure_target("bad"));
    assert(!restored.target_restart_pending_);
    unsigned writes = esphome::global_preferences->saves;
    assert(!restored.configure_target("68:52:10:54:01:1E"));
    assert(esphome::global_preferences->saves == writes);
    esphome::global_preferences->fail_sync_once = true;
    assert(!restored.configure_target("68:52:10:54:00:98"));
    assert(restored.peer_matches(fresh));
    Test after_failure;
    after_failure.load_target_();
    assert(after_failure.peer_matches(fresh));
    assert(restored.configure_target(""));
    Test disabled;
    disabled.set_peer_address(0x685210540098ULL);
    disabled.load_target_();
    assert(!disabled.peer_matches(old));
    std::cout << "PASS: durable target override, no-op, save failure rollback, clearing and fences\n";
    return 0;
  }
  if (test == "pairing_window") {
    t.identity_ = true;
    t.action(4);
    assert(!t.pairing_);  // Do not scan an active TWS connection.
    t.tws_ = false;
    t.native_rc_active_ = false;
    t.native_spp_handle_ = 0;
    t.spp_handle_ = 0;
    t.action(4);
    assert(t.pairing_ && host_discoveries == 1);
    t.discovery_state(true);
    Event found;
    found.type = 20;
    uint8_t address[] = {0x68, 0x52, 0x10, 0x54, 1, 0x1e};
    std::memcpy(found.bytes, address, 6);
    t.event_(found);
    assert(!t.target_restart_pending_);  // Collect the full inquiry, not first result.
    t.discovery_state(false);
    t.loop();
    assert(!t.pairing_ && t.target_restart_pending_);
    Test restored;
    restored.load_target_();
    assert(restored.peer_matches(address));
    std::cout << "PASS: active-link refusal, bounded discovery and single-candidate save\n";
    return 0;
  }
  if (test == "pairing_ambiguity") {
    t.tws_ = false;
    t.native_rc_active_ = false;
    t.native_spp_handle_ = 0;
    t.spp_handle_ = 0;
    t.identity_ = true;
    t.action(4);
    t.discovery_state(true);
    Event found;
    found.type = 20;
    uint8_t address[] = {0x68, 0x52, 0x10, 0x54, 1, 0x1e};
    std::memcpy(found.bytes, address, 6);
    t.event_(found);
    found.bytes[5] = 0x99;
    t.event_(found);
    t.discovery_state(false);
    t.loop();
    assert(!t.target_restart_pending_);
    t.choose_candidate("Candidate 8");
    assert(!t.target_restart_pending_);
    t.choose_candidate("Candidate 1");
    assert(t.target_restart_pending_);
    Test restored;
    restored.load_target_();
    assert(restored.peer_matches(address));
    std::cout << "PASS: multiple matches require explicit valid candidate selection\n";
    return 0;
  }
  if (test == "target_restart_owner") {
    struct Target : TargetAddress {
      using TargetAddress::control;
      using TargetAddress::TargetAddress;
    } target(&t);
    t.set_target_text(&target);
    t.load_target_();
    assert(target.state == "68:52:10:54:00:98");
    t.mark_failed();
    target.control("68:52:10:54:01:1e");
    assert(target.state == "68:52:10:54:01:1E");
    assert(target.timeouts.size() == 1);
    target.control("68:52:10:54:00:98");
    assert(target.timeouts.size() == 1);
    target.timeouts.front()();
    assert(esphome::App.reboots == 1);
    std::cout << "PASS: target change safely restarts even when radio component failed\n";
    return 0;
  }
  if (test == "pairing_lifecycle") {
    t.tws_ = false;
    t.native_rc_active_ = false;
    t.native_spp_handle_ = 0;
    t.spp_handle_ = 0;
    t.identity_ = true;
    t.action(4);
    t.discovery_state(false);
    t.loop();
    assert(t.pairing_);  // Ignore old STOP before this scan's STARTED.
    t.discovery_state(true);
    Event found;
    found.type = 20;
    uint8_t address[] = {0x68, 0x52, 0x10, 0x54, 1, 0x1e};
    std::memcpy(found.bytes, address, 6);
    t.event_(found);
    t.overflow_ = true;
    t.discovery_state(false);
    t.discovery_state(false);
    t.loop();
    assert(!t.pairing_ && !t.target_restart_pending_);
    t.choose_candidate("Candidate 1");
    assert(!t.target_restart_pending_);  // Never accept a partial failed scan.
    t.action(4);
    assert(!t.pairing_);  // Let duplicate callbacks drain before a new scan.
    host_now += 2000;
    t.action(4);
    assert(t.pairing_);
    assert(!t.audio_link(true));  // Connection crossing pairing admission fence is rejected.
    unsigned closes = host_spp_disconnects;
    t.spp_open(99);
    assert(!t.native_spp_handle_ && host_spp_disconnects == closes + 1);
    std::cout << "PASS: lossless scan completion, START barrier, failed-result refusal and admission fence\n";
    return 0;
  }
  if (test == "indicator_activity") {
    assert(t.request(Field::VOLUME, 12));
    t.loop();
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::GREEN);
    Event tx;
    tx.type = 10;
    tx.session = 7;
    tx.handle = 0;
    tx.value = 0;
    t.event_(tx);
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::CYAN);
    host_now += 120;
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::GREEN);
    tx.session = 6;
    t.event_(tx);
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::GREEN);
    assert(!t.request(Field::VOLUME, 33));
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::RED);
  } else if (test == "indicator_spp_activity") {
    assert(t.request(Field::SOURCE, 6));
    t.loop();
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::GREEN);
    Event tx;
    tx.type = 8;
    tx.session = 1;
    tx.value = 42;
    tx.label = 1;
    t.event_(tx);
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::CYAN);
    tx.label = 0;
    t.event_(tx);
    assert(t.indicator_.color(host_now, true, true, false) == IndicatorColor::RED);
  } else if (test == "source") {
    assert(t.request(Field::SOURCE, 6));
    t.loop();
    t.event_(t.word(0x00010014));
    assert(t.state().source.quality == Quality::UNKNOWN);
    assert(t.state().activity.quality == Quality::UNKNOWN);
  } else if (test == "overflow_handle") {
    t.overflow_ = true;
    t.loop();
    assert(t.spp_handle_ == 0);
  } else if (test == "overflow_fence") {
    host_disconnect = [&] { t.post(t.word(0x090B1244)); };
    t.overflow_ = true;
    t.loop();
    assert(!t.tws_ && t.state().volume.quality == Quality::UNKNOWN);
  } else if (test == "connect_timeout") {
    t.spp_handle_ = 0;
    t.spp_connecting_ = true;
    t.spp_connect_at_ = 1;
    t.spp_ready_ = true;
    host_now = 200002;
    t.loop();
    assert(t.spp_connecting_);
    assert(host_spp_connects == 0);
  } else if (test == "real_spp_source_ack") {
    assert(t.request(Field::SOURCE, 13));
    t.loop();
    Event rx;
    rx.type = 6;
    rx.session = 1;
    rx.value = 42;
    rx.length = 10;
    const uint8_t packet[] = {0xBB, 5, 10, 0x9C, 0x2D, 0xBB, 5, 1, 0x2D, 0x46};
    memcpy(rx.bytes, packet, sizeof(packet));
    t.event_(rx);
    assert(t.state().source.quality == Quality::ASSUMED && t.state().source.value == 13);
  } else if (test == "a2dp_telemetry") {
    t.set_peer_address(0x010203040506ULL);
    t.setup();
    assert(host_a2d_callback);
    const uint8_t peer[] = {1, 2, 3, 4, 5, 6};
    esp_a2d_cb_param_t params{};
    memcpy(params.audio_stat.remote_bda, peer, 6);
    params.audio_stat.conn_hdl = 258;
    params.audio_stat.state = ESP_A2D_AUDIO_STATE_STARTED;
    const auto logs = host_logs.size();
    host_a2d_callback(ESP_A2D_AUDIO_STATE_EVT, &params);
    assert(host_logs.size() == logs);
    t.loop();  // telemetry is deferred through the lossy queue
    assert(!host_logs.empty() && host_logs.back() == "A2DP audio selected_peer=1 handle=258 state=1 (STARTED)");
    params.audio_stat.state = ESP_A2D_AUDIO_STATE_SUSPEND;
    host_a2d_callback(ESP_A2D_AUDIO_STATE_EVT, &params);
    t.loop();
    assert(host_logs.back() == "A2DP audio selected_peer=1 handle=258 state=0 (SUSPEND)");
    memcpy(params.audio_cfg.remote_bda, peer, 6);
    params.audio_cfg.conn_hdl = 258;
    params.audio_cfg.mcc.type = 0;
    const uint8_t codec[] = {0x21, 0x15, 2, 0x30};
    memcpy(&params.audio_cfg.mcc.cie, codec, 4);
    host_a2d_callback(ESP_A2D_AUDIO_CFG_EVT, &params);
    t.loop();
    assert(host_logs.back() == "A2DP codec selected_peer=1 handle=258 type=0 cie4=21150230");
    params.a2d_psc_cfg_stat.psc_mask = ESP_A2D_PSC_DELAY_RPT;
    host_a2d_callback(ESP_A2D_SNK_PSC_CFG_EVT, &params);
    t.loop();
    assert(host_logs.back() == "A2DP sink capabilities mask=0001 delay_report=1");
    params.a2d_psc_cfg_stat.psc_mask = 0;
    host_a2d_callback(ESP_A2D_SNK_PSC_CFG_EVT, &params);
    t.loop();
    assert(host_logs.back() == "A2DP sink capabilities mask=0000 delay_report=0");
    params.audio_stat.remote_bda[0] = 9;
    host_a2d_callback(ESP_A2D_AUDIO_STATE_EVT, &params);
    t.loop();
    assert(host_logs.back() == "A2DP audio selected_peer=0 handle=258 state=0 (SUSPEND)");
    partybox_native_vendor_event(7, 0, 3, 0x1958, 1, 5, 0x10);
    t.loop();
    assert(host_logs.back() ==
           "Native vendor forwarded selected_peer=1 session=7 handle=0 label=3 company=001958 pdu=16 ctype=1 length=5");
    partybox_native_vendor_event(7, 0, 3, 0xC4, 9, 65535, -1);
    t.loop();
    assert(host_logs.back() ==
           "Native vendor forwarded selected_peer=1 session=7 handle=0 label=3 company=0000C4 pdu=-1 ctype=9 "
           "length=65535");
    const auto before_null = host_logs.size();
    host_a2d_callback(ESP_A2D_AUDIO_CFG_EVT, nullptr);
    t.loop();
    assert(host_logs.size() == before_null);
    // All new callback/bridge telemetry can be dropped without consuming any
    // of the original 32 critical event slots or forcing link recovery.
    for (unsigned i = 0; i < 32; ++i) {
      partybox_native_event(1, 7, 0, 0, 0x090B1244);
      host_a2d_callback(ESP_A2D_AUDIO_STATE_EVT, &params);
      host_a2d_callback(ESP_A2D_AUDIO_CFG_EVT, &params);
      host_a2d_callback(ESP_A2D_SNK_PSC_CFG_EVT, &params);
      partybox_native_vendor_event(7, 0, 3, 0x1958, 1, 5, 0x10);
    }
    assert(!t.overflow_);
    t.loop();
    assert(t.tws_ && t.words_ == 32 && t.dropped_ == 0);
  } else if (test == "spp_disabled") {
    t.set_spp_enabled(false);
    t.spp_ready_ = true;
    assert(!t.request(Field::SOURCE, 6) && !t.request(Field::VERSION, 0));  // even with a native handle
    t.spp_close(42);
    host_now = 12000;
    t.loop();
    assert(t.tws_ && host_spp_connects == 0);
    host_now = 70000;
    t.loop();
    assert(host_spp_connects == 0);
    const auto disconnects = host_spp_disconnects;
    t.spp_open(81);
    t.loop();
    assert(t.spp_handle_ == 0 && host_spp_disconnects == disconnects + 1);
    assert(!t.request(Field::SOURCE, 6) && !t.request(Field::VERSION, 0));
    assert(t.request(Field::VOLUME, 10));
    t.loop();
    assert(t.scheduler_.pending());
  } else if (test == "spp_default_enabled") {
    t.spp_ready_ = true;
    t.spp_close(42);
    host_now = 12000;
    t.loop();
    assert(t.tws_ && host_spp_connects == 1 && t.spp_connecting_);
  } else if (test == "rssi_worker") {
    t.set_peer_address(0x010203040506ULL);
    t.setup();
    host_now = 15000;
    t.loop();
    assert(host_rssi_main_calls == 0 && host_rssi_requests == 0);
    // The worker may remain unscheduled or blocked; later periods cannot grow
    // memory or block the main loop behind its single queued diagnostic request.
    for (unsigned i = 2; i <= 32; ++i) {
      host_now = i * 15000;
      t.loop();
    }
    assert(host_rssi_main_calls == 0 && host_rssi_requests == 0 && host_task_creations == 1);
    host_run_task();
    assert(host_rssi_requests == 1 && host_rssi_main_calls == 0);
    const uint8_t peer[] = {1, 2, 3, 4, 5, 6};
    assert(!memcmp(host_rssi_peer, peer, 6));
    host_now += 15000;
    t.loop();
    t.lost_();
    host_run_task();
    assert(host_rssi_requests == 1);  // discard a queued read after the link drops
  } else if (test == "rssi_worker_no_queue" || test == "rssi_worker_no_task") {
    const bool no_queue = test == "rssi_worker_no_queue";
    host_fail_single_queue = no_queue;
    host_fail_task = !no_queue;
    t.setup();
    assert(!t.is_failed() && host_task_entry == nullptr);
    assert(host_task_creations == (no_queue ? 0U : 1U));
    assert(host_queue_deletes == (no_queue ? 0U : 1U));
    assert(host_logs.back() == "RSSI delta polling disabled: worker unavailable");
    host_now = 15000;
    t.loop();
    assert(host_rssi_requests == 0);
    assert(t.request(Field::POWER, 0));
    t.loop();
    assert(t.scheduler_.pending());
  } else if (test == "gap_telemetry") {
    t.set_peer_address(0x010203040506ULL);
    t.setup();
    assert(host_gap_callback);
    esp_bt_gap_cb_param_t params{};
    const uint8_t peer[] = {1, 2, 3, 4, 5, 6};
    memcpy(params.mode_chg.bda, peer, 6);
    params.mode_chg.mode = 2;
    params.mode_chg.interval = 54;
    host_gap_callback(ESP_BT_GAP_MODE_CHG_EVT, &params);
    assert(!host_logs.empty());
    assert(host_logs.back() == "Bluetooth mode selected_peer=1 mode=2 interval_slots=54");
    params.mode_chg.bda[0] = 9;
    host_gap_callback(ESP_BT_GAP_MODE_CHG_EVT, &params);
    assert(host_logs.back() == "Bluetooth mode selected_peer=0 mode=2 interval_slots=54");
    memcpy(params.read_rssi_delta.bda, peer, 6);
    params.read_rssi_delta.stat = ESP_BT_STATUS_SUCCESS;
    params.read_rssi_delta.rssi_delta = -7;
    host_gap_callback(ESP_BT_GAP_READ_RSSI_DELTA_EVT, &params);
    assert(host_logs.back() == "RSSI delta selected_peer=1 status=0 delta=-7");
    params.read_rssi_delta.stat = 1;
    host_gap_callback(ESP_BT_GAP_READ_RSSI_DELTA_EVT, &params);
    assert(host_logs.back() == "RSSI delta selected_peer=1 status=1");
    host_now = 14999;
    t.loop();
    assert(host_rssi_requests == 0);
    host_now = 15000;
    t.loop();
    assert(host_rssi_requests == 0);
    host_run_task();
    assert(host_rssi_requests == 1);
    assert(!memcmp(host_rssi_peer, peer, 6));
    t.loop();
    assert(host_rssi_requests == 1);  // no busy polling within the interval
    t.tws_ = false;
    host_now = 30000;
    t.loop();
    assert(host_rssi_requests == 1);
    t.tws_ = true;
    host_now = 45000;
    host_rssi_request_status = -1;
    t.loop();
    host_run_task();
    assert(host_rssi_requests == 2 && host_logs.back() == "RSSI delta request status=-1 stack_free_bytes=2048");
    t.loop();
    assert(host_rssi_requests == 2);  // synchronous API failure is also throttled
  } else if (test == "diagnostic_burst") {
    active_partybox = &t;
    // Each received word also generates a response-TX diagnostic in the C hook.
    // Seventeen protocol events fitted before diagnostics were introduced.
    for (unsigned i = 0; i < 17; ++i) {
      partybox_native_event(1, 7, 0, 0, 0x090B1244);
      partybox_native_event(11, 7, 0, 0, 9U << 16);
    }
    assert(!t.overflow_);
    t.loop();
    assert(t.tws_ && t.words_ == 17 && t.dropped_ == 0);
    // All optional diagnostic kinds may saturate without reducing the original
    // capacity of 32 critical events or incrementing the protocol-loss counter.
    for (unsigned i = 0; i < 32; ++i) {
      partybox_native_event(1, 7, 0, 0, 0x090B1244);
      partybox_native_event(10, 7, 0, 0, 0);
      partybox_native_event(11, 7, 0, 0, 9U << 16);
      partybox_native_event(12, 7, 0, 0, 9);
      partybox_native_event(13, 7, 0, 0, 0);
    }
    assert(!t.overflow_);
    t.loop();
    assert(t.tws_ && t.words_ == 49 && t.dropped_ == 0);
    // A real critical overflow must retain its existing fail-closed recovery.
    for (unsigned i = 0; i < 33; ++i) partybox_native_event(1, 7, 0, 0, 0x090B1244);
    assert(t.overflow_);
    t.loop();
    assert(!t.tws_ && t.dropped_ == 1);
  } else if (test == "native_tx_failure") {
    assert(t.request(Field::POWER, 0));
    t.loop();
    assert(t.scheduler_.pending());
    Event failed;
    failed.type = 10;
    failed.session = 7;
    failed.handle = 0;
    failed.label = 0;
    failed.value = 4;
    Event queued = failed;
    queued.value = 0;
    t.event_(queued);
    assert(t.scheduler_.pending());
    Event stale = failed;
    stale.session = 6;
    t.event_(stale);
    assert(t.scheduler_.pending());
    stale = failed;
    stale.handle = 1;
    t.event_(stale);
    assert(t.scheduler_.pending());
    stale = failed;
    stale.label = 1;
    t.event_(stale);
    assert(t.scheduler_.pending());
    // A functional send failure must survive a saturated optional-log queue.
    active_partybox = &t;
    for (unsigned i = 0; i < 32; ++i) partybox_native_event(11, 7, 0, 0, 9U << 16);
    partybox_native_event(failed.type, failed.session, failed.handle, failed.label, failed.value);
    t.loop();
    assert(!t.scheduler_.pending());
    assert(t.state().power.value && t.state().power.quality == Quality::OBSERVED);
    assert(t.last_result_ == "native TX failed: no peer ACK expected");
    Event late;
    late.type = 2;
    late.session = 7;
    late.label = 0;
    late.value = 1;
    t.event_(late);
    assert(t.state().power.value && t.state().power.quality == Quality::OBSERVED);
  } else if (test == "explicit_connect_failure") {
    t.tws_ = false;
    t.a2dp_ready_ = true;
    t.action(0);
    assert(t.connect_pending_);
    Event disconnected;
    disconnected.type = 9;
    disconnected.value = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    t.event_(disconnected);
    assert(!t.connect_pending_);
    assert(t.last_result_ == "connect failed: A2DP disconnected before TWS");
  } else if (test == "power_guard") {
    assert(t.request(Field::POWER, 0));
    assert(!t.request(Field::POWER, 0));
    t.loop();
    Event ack;
    ack.type = 2;
    ack.session = 7;
    ack.label = 0;
    ack.value = 1;
    t.event_(ack);
    assert(!t.state().power.value && t.state().power.quality == Quality::ASSUMED);
    assert(!t.request(Field::POWER, 0));
    assert(t.request(Field::POWER, 1));
  } else if (test == "spp_mailbox_close") {
    t.spp_close(42);  // survives loss of every queued lifecycle event
    t.overflow_ = true;
    t.loop();
    assert(t.spp_handle_ == 0);
    auto fresh = t.word(0x090B1244);
    fresh.session = 9;
    t.event_(fresh);
    host_now = 7000;
    t.spp_ready_ = true;
    t.loop();
    assert(host_spp_connects == 1);
  } else if (test == "spp_failure_active") {
    t.spp_open(0);
    t.loop();
    assert(t.spp_handle_ == 42);  // stale failure cannot clear established channel
  } else if (test == "radio_eir_summary") {
    uint8_t parameters[241] = {};
    const uint8_t fields[] = {4, 9, 'A', 'B', 'C', 7, 255, 0x48, 0x4D, 0x50, 0x42, 0x0D, 0x10};
    memcpy(parameters + 1, fields, sizeof(fields));
    partybox_radio_event(0x0C51, 0, parameters, sizeof(parameters));
    assert(host_logs.back() == "EIR length=241 fec=0 name_present=1 manufacturer_len=6 hmpb=1");
    parameters[1] = 240;
    parameters[2] = 255;  // malformed: field exceeds remaining EIR bytes
    partybox_radio_event(0x0C51, 0, parameters, sizeof(parameters));
    assert(host_logs.back() == "EIR length=241 fec=0 name_present=0 manufacturer_len=0 hmpb=0");
    partybox_radio_event(0x0C51, 1, nullptr, 0);
    assert(host_logs.back() == "EIR length=0 fec=-1 name_present=0 manufacturer_len=0 hmpb=0");
  } else if (test == "input_signal_flags") {
    esphome::binary_sensor::BinarySensor selected, dj, voice;
    esphome::binary_sensor::BinarySensor signals[4];
    t.set_binary(2, &selected);
    t.set_binary(3, &dj);
    t.set_binary(4, &voice);
    for (unsigned bit = 0; bit < 4; ++bit) t.set_binary(5 + bit, &signals[bit]);
    t.event_(t.word(0x000D0014));
    t.loop();
    assert(selected.has_state() && selected.state);  // LINE IN selected, bit 1 clear
    assert(dj.has_state());  // existing binary indices did not move
    for (unsigned bit = 0; bit < 4; ++bit)
      assert(signals[bit].has_state() && signals[bit].state == !(0xD & (1U << bit)));
    host_now += 200;
    assert(t.request(Field::SOURCE, 6));
    t.loop();
    assert(!selected.has_state());
    for (const auto& signal : signals) assert(!signal.has_state());
    t.event_(t.word(0x000E0014));  // fresh raw mask before source ACK
    t.loop();
    assert(!selected.has_state());
    for (unsigned bit = 0; bit < 4; ++bit)
      assert(signals[bit].has_state() && signals[bit].state == !(0xE & (1U << bit)));
    Event rx;
    rx.type = 6;
    rx.session = 1;
    rx.value = 42;
    rx.length = 10;
    const uint8_t packet[] = {0xBB, 5, 10, 0x9C, 0x2D, 0xBB, 5, 1, 0x2D, 0x46};
    memcpy(rx.bytes, packet, sizeof(packet));
    t.event_(rx);
    t.loop();
    assert(t.state().source.quality == Quality::ASSUMED && !selected.has_state());
    for (const auto& signal : signals) assert(signal.has_state());  // ACK preserves fresh mask
    t.spp_close(42);
    t.loop();
    for (const auto& signal : signals) assert(signal.has_state());  // TWS reports do not depend on SPP
    t.event_(t.word(0x00010013));
    t.loop();
    for (const auto& signal : signals) assert(!signal.has_state());
    t.event_(t.word(0x00000014));
    t.loop();
    for (const auto& signal : signals) assert(signal.has_state() && signal.state);
    t.lost_();
    t.loop();
    for (const auto& signal : signals) assert(!signal.has_state());
    auto snapshot = t.word(0x090B1244);
    snapshot.session = 9;
    t.event_(snapshot);
    t.loop();
    for (const auto& signal : signals) assert(!signal.has_state());  // no cached previous-session mask
  } else if (test == "protocol_entity_invalidation") {
    Choice source(&t, 0), bass(&t, 1), lighting(&t, 2);
    esphome::text_sensor::TextSensor version, last_word;
    t.set_choice(0, &source);
    t.set_choice(1, &bass);
    t.set_choice(2, &lighting);
    t.set_text(7, &version);
    t.set_text(8, &last_word);
    Event rx;
    rx.type = 6;
    rx.session = 1;
    rx.value = 42;
    rx.length = 10;
    const uint8_t packet[] = {0xBB, 10, 8, 75, 32, 17, 15, 7, 255, 255};
    memcpy(rx.bytes, packet, sizeof(packet));
    t.event_(rx);
    t.event_(t.word(0x090B1244));
    t.loop();
    assert(source.has_state() && bass.has_state() && lighting.has_state());
    assert(version.has_state() && last_word.has_state());
    t.spp_close(42);
    t.loop();
    assert(!source.has_state());  // source setter requires SPP, readback remains on TWS
    assert(!version.has_state());
    assert(esphome::host_select_notifications == 1 && esphome::host_text_notifications == 1);
    assert(bass.has_state() && lighting.has_state() && last_word.has_state());
    assert(t.state().source.quality == Quality::OBSERVED);
    t.spp_open(42);
    t.loop();
    assert(!version.has_state());  // require fresh version reply
    t.lost_();
    t.loop();
    assert(!source.has_state() && !bass.has_state() && !lighting.has_state() && !last_word.has_state());
    assert(esphome::host_select_notifications == 4 && esphome::host_text_notifications == 2);
  } else if (test == "ha_unknown_state_compatibility") {
    Player player(&t);
    assert(player.state == esphome::media_player::MEDIA_PLAYER_STATE_IDLE);  // before first parent loop
    t.set_player(&player);
    t.lost_();
    t.loop();
    // HA2026.10.2 rejects native NONE(0), even in its disconnect callback.
    // IDLE is a neutral bridge state; power confidence must remain unknown.
    assert(player.state == esphome::media_player::MEDIA_PLAYER_STATE_IDLE);
    assert(t.state().power.quality == Quality::UNKNOWN);
    assert(!t.request(Field::POWER, 1));
    auto fresh = t.word(0x090B1244);
    fresh.session = 9;
    t.event_(fresh);
    t.loop();
    assert(player.state == esphome::media_player::MEDIA_PLAYER_STATE_ON);
    t.lost_();
    t.loop();
    assert(player.state == esphome::media_player::MEDIA_PLAYER_STATE_IDLE);
    assert(t.state().power.quality == Quality::UNKNOWN);
  } else if (test == "physical_report_wins") {
    assert(t.request(Field::VOLUME, 10));
    t.loop();
    t.event_(t.word(0x000C000A));
    Event ack;
    ack.type = 2;
    ack.session = 7;
    ack.label = 0;
    ack.value = 1;
    t.event_(ack);
    assert(t.state().volume.value == 12 && t.state().volume.quality == Quality::OBSERVED);
  } else
    return 1;
  std::cout << "PASS: runtime " << test << "\n";
}
