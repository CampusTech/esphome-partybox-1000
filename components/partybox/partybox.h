#pragma once
#include <atomic>

#include "core.h"
#include "indicator.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#ifdef USE_LIGHT
#include "esphome/components/light/light_state.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace esphome::partybox {
class PartyBox;
class Player : public media_player::MediaPlayer {
 public:
  explicit Player(PartyBox* parent) : parent_(parent) {
    state = media_player::MEDIA_PLAYER_STATE_IDLE;
    volume = 0;
  }
  media_player::MediaPlayerTraits get_traits() override;
  bool is_muted() const override;

 protected:
  void control(const media_player::MediaPlayerCall& call) override;
  PartyBox* parent_;
};
class Choice : public select::Select {
 public:
  Choice(PartyBox* parent, uint8_t role) : parent_(parent), role_(role) {}
  void invalidate();

 protected:
  void control(const std::string& value) override;
  PartyBox* parent_;
  uint8_t role_;
};
class Action : public button::Button {
 public:
  Action(PartyBox* parent, uint8_t role) : parent_(parent), role_(role) {}

 protected:
  void press_action() override;
  PartyBox* parent_;
  uint8_t role_;
};
struct Event {
  uint8_t type{0}, handle{0}, label{0}, length{0};
  uint32_t session{0}, value{0};
  uint8_t bytes[64]{};
};
class PartyBox : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool request(Field field, int value);
  void action(uint8_t role);
  const State& state() const {
    return state_;
  }
  bool peer_matches(const uint8_t* address) const;
  void post(const Event& event);
  void native_event(const Event& event);
  uint32_t spp_open(uint32_t handle);
  void spp_close(uint32_t handle);
  void add_audio(uint32_t n) {
    audio_bytes_ += n;
  }
  void profile_ready(bool a2dp) {
    (a2dp ? a2dp_ready_ : spp_ready_) = true;
  }
  void set_peer_address(uint64_t address) {
    for (int i = 0; i < 6; i++) peer_[i] = address >> ((5 - i) * 8);
  }
  // Configuration only: call before setup(), never as a runtime toggle.
  void set_spp_enabled(bool enabled) {
    spp_enabled_ = enabled;
  }
#ifdef USE_LIGHT
  void set_status_light(light::LightState* light, float brightness) {
    status_light_ = light;
    status_brightness_ = brightness;
  }
#endif
  void set_player(Player* p) {
    player_ = p;
  }
  void set_choice(uint8_t i, Choice* p) {
    choices_[i] = p;
  }
  void set_sensor(uint8_t i, sensor::Sensor* p) {
    sensors_[i] = p;
  }
  void set_binary(uint8_t i, binary_sensor::BinarySensor* p) {
    binaries_[i] = p;
  }
  void set_text(uint8_t i, text_sensor::TextSensor* p) {
    texts_[i] = p;
  }

 protected:
  void publish_();
  void event_(const Event& event);
  void send_next_();
  void finish_(bool accepted);
  void lost_();
  void recover_();
  void sync_spp_();
  void result_(const char* result, bool error = false);
  void update_indicator_();
  static void rssi_worker_(void* context);
  bool eligible_(Field field, int value) const;
  uint8_t peer_[6]{};
  QueueHandle_t events_{nullptr};
  QueueHandle_t diagnostics_{nullptr};
  QueueHandle_t rssi_requests_{nullptr};
  std::atomic<bool> rssi_active_{false};
  std::atomic<bool> a2dp_ready_{false}, spp_ready_{false};
  std::atomic<uint32_t> dropped_{0}, audio_bytes_{0};
  std::atomic<bool> overflow_{false};
  portMUX_TYPE transport_mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t native_spp_handle_{0}, native_spp_epoch_{0}, native_rc_session_{0};
  uint8_t native_rc_handle_{0};
  bool native_rc_active_{false};
  uint32_t fenced_session_{0};
  bool fence_active_{false}, spp_connect_warned_{false};
  Indicator indicator_{};
#ifdef USE_LIGHT
  light::LightState* status_light_{nullptr};
  float status_brightness_{0.08f};
  IndicatorColor last_indicator_{IndicatorColor::OFF};
  bool indicator_initialized_{false};
#endif
  State state_{};
  Scheduler scheduler_{};
  SppFramer framer_{};
  Player* player_{nullptr};
  Choice* choices_[3]{};
  sensor::Sensor* sensors_[9]{};
  binary_sensor::BinarySensor* binaries_[5]{};
  text_sensor::TextSensor* texts_[11]{};
  bool identity_{false}, tws_{false}, spp_connecting_{false}, spp_congested_{false}, pending_spp_{false};
  bool spp_enabled_{true};
  bool spp_poisoned_{false}, dirty_{true}, snapshot_{false}, connect_pending_{false};
  uint32_t session_{0}, spp_session_{0}, spp_handle_{0}, words_{0}, last_send_{0};
  uint32_t last_spp_attempt_{0}, snapshot_at_{0}, last_publish_{0}, connect_at_{0}, spp_connect_at_{0};
  uint32_t last_rssi_read_{0};
  uint8_t rc_handle_{0}, next_label_{0}, pending_label_{0};
  uint32_t spp_backoff_{5000};
  std::string last_word_, last_result_{"idle"}, versions_{"unknown"};
};
extern PartyBox* active_partybox;
}  // namespace esphome::partybox
