#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>
inline std::vector<std::string> host_logs;
inline void host_log(const char* format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  host_logs.emplace_back(text);
}
#define ESP_LOGI(tag, ...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) \
  do {                \
  } while (0)
#define ESP_LOGCONFIG(...) \
  do {                     \
  } while (0)
#define ESP_IDF_VERSION_VAL(a, b, c) ((a) * 10000 + (b) * 100 + (c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 5, 5)
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() \
  esp_bt_controller_config_t {}
#define ESP_BT_SET_COD_ALL 0
#define ESP_BT_CONNECTABLE 0
#define ESP_BT_LIMITED_DISCOVERABLE 0
#define ESP_BT_MODE_CLASSIC_BT 0
#define ESP_BT_SP_IOCAP_MODE 0
#define ESP_BT_IO_CAP_NONE 0
#define ESP_A2D_MCT_SBC 0
#define ESP_A2D_INIT_SUCCESS 0
#define ESP_A2D_SEP_REG_SUCCESS 0
#define ESP_SPP_MODE_CB 0
#define ESP_SPP_SUCCESS 0
#define ESP_SPP_SEC_AUTHENTICATE 0
#define ESP_SPP_ROLE_MASTER 0
#define MALLOC_CAP_8BIT 0
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) \
  do {                        \
  } while (0)
#define portEXIT_CRITICAL(x) \
  do {                       \
  } while (0)
using portMUX_TYPE = int;
using esp_err_t = int;
using esp_bt_io_cap_t = int;
using esp_bt_pin_code_t = uint8_t[16];
constexpr int ESP_OK = 0, pdTRUE = 1;
constexpr int pdPASS = 1, tskIDLE_PRIORITY = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline bool host_in_task = false, host_fail_single_queue = false, host_fail_task = false;
struct HostTaskWait {};
using TaskFunction_t = void (*)(void*);
using TaskHandle_t = void*;
inline TaskFunction_t host_task_entry = nullptr;
inline void* host_task_context = nullptr;
inline unsigned host_task_creations = 0, host_task_stack = 0, host_queue_deletes = 0;
inline int xTaskCreate(TaskFunction_t entry, const char*, uint32_t stack, void* context, unsigned, TaskHandle_t*) {
  ++host_task_creations;
  if (host_fail_task) return 0;
  host_task_entry = entry;
  host_task_context = context;
  host_task_stack = stack;
  return pdPASS;
}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) {
  return 2048;
}
inline void host_run_task() {
  assert(host_task_entry);
  host_in_task = true;
  try {
    host_task_entry(host_task_context);
  } catch (const HostTaskWait&) {
  }
  host_in_task = false;
}
inline uint32_t host_now = 1000, host_spp_connects = 0, host_spp_disconnects = 0;
inline std::function<void()> host_disconnect;
struct HostQueue {
  size_t size, count;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = HostQueue*;
inline QueueHandle_t xQueueCreate(size_t count, size_t size) {
  if (count == 1 && host_fail_single_queue) return nullptr;
  return new HostQueue{size, count, {}};
}
inline int xQueueSend(QueueHandle_t q, const void* p, uint32_t wait) {
  assert(host_in_task || wait == 0);
  if (q->items.size() == q->count) return 0;
  q->items.emplace_back((const uint8_t*)p, (const uint8_t*)p + q->size);
  return 1;
}
inline int xQueueReceive(QueueHandle_t q, void* p, uint32_t wait) {
  if (q->items.empty()) {
    if (wait == portMAX_DELAY) throw HostTaskWait{};
    return 0;
  }
  memcpy(p, q->items.front().data(), q->size);
  q->items.pop_front();
  return 1;
}
inline void xQueueReset(QueueHandle_t q) {
  q->items.clear();
}
inline void vQueueDelete(QueueHandle_t q) {
  ++host_queue_deletes;
  delete q;
}
struct esp_bt_controller_config_t {};
struct esp_spp_cfg_t {
  int mode;
  bool enable_l2cap_ertm;
};
struct esp_a2d_mcc_t {
  int type;
  uint8_t cie[8];
};
struct esp_bt_cod_t {
  unsigned minor, major, service;
};
struct esp_bt_eir_data_t {
  bool include_name, include_uuid;
  unsigned manufacturer_len;
  uint8_t* p_manufacturer_data;
};
struct esp_a2d_audio_buff_t {
  uint32_t data_len;
};
using esp_a2d_conn_hdl_t = int;
enum esp_bt_gap_cb_event_t {
  ESP_BT_GAP_DISC_RES_EVT,
  ESP_BT_GAP_DISC_STATE_CHANGED_EVT,
  ESP_BT_GAP_CFM_REQ_EVT,
  ESP_BT_GAP_PIN_REQ_EVT,
  ESP_BT_GAP_AUTH_CMPL_EVT,
  ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT,
  ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT,
  ESP_BT_GAP_CONFIG_EIR_DATA_EVT,
  ESP_BT_GAP_MODE_CHG_EVT,
  ESP_BT_GAP_READ_RSSI_DELTA_EVT
};
constexpr int ESP_BT_GAP_DEV_PROP_COD = 1, ESP_BT_GAP_DEV_PROP_EIR = 2;
constexpr int ESP_BT_GAP_DISCOVERY_STARTED = 1, ESP_BT_GAP_DISCOVERY_STOPPED = 0, ESP_BT_INQ_MODE_LIMITED_INQUIRY = 1;
struct esp_bt_gap_dev_prop_t {
  int type;
  int len;
  void* val;
};
inline unsigned host_discoveries = 0;
inline esp_err_t esp_bt_gap_start_discovery(int mode, int length, int count) {
  assert(mode == ESP_BT_INQ_MODE_LIMITED_INQUIRY && length == 8 && count == 0);
  ++host_discoveries;
  return ESP_OK;
}
inline esp_err_t esp_bt_gap_cancel_discovery() {
  return ESP_OK;
}
struct esp_bt_gap_cb_param_t {
  struct {
    uint8_t bda[6];
    int num_prop;
    esp_bt_gap_dev_prop_t* prop;
  } disc_res;
  struct {
    int state;
  } disc_st_chg;
  struct {
    uint8_t bda[6];
  } cfm_req;
  struct {
    uint8_t bda[6];
    bool min_16_digit;
  } pin_req;
  struct {
    int stat;
  } auth_cmpl;
  struct {
    int stat;
    uint8_t bda[6];
  } acl_conn_cmpl_stat;
  struct {
    int reason;
    uint8_t bda[6];
  } acl_disconn_cmpl_stat;
  struct {
    int stat;
  } config_eir_data;
  struct {
    uint8_t bda[6];
    int mode;
    uint16_t interval;
  } mode_chg;
  struct {
    uint8_t bda[6];
    int stat;
    int8_t rssi_delta;
  } read_rssi_delta;
};
constexpr int ESP_BT_STATUS_SUCCESS = 0;
using esp_bt_gap_cb_t = void (*)(esp_bt_gap_cb_event_t, esp_bt_gap_cb_param_t*);
inline esp_bt_gap_cb_t host_gap_callback = nullptr;
inline esp_err_t esp_bt_gap_register_callback(esp_bt_gap_cb_t callback) {
  host_gap_callback = callback;
  return ESP_OK;
}
inline unsigned host_rssi_requests = 0;
inline unsigned host_rssi_main_calls = 0;
inline esp_err_t host_rssi_request_status = ESP_OK;
inline uint8_t host_rssi_peer[6]{};
inline esp_err_t esp_bt_gap_read_rssi_delta(uint8_t remote_addr[6]) {
  ++host_rssi_requests;
  if (!host_in_task) ++host_rssi_main_calls;
  memcpy(host_rssi_peer, remote_addr, 6);
  return host_rssi_request_status;
}
enum esp_avrc_ct_cb_event_t { ESP_AVRC_CT_CONNECTION_STATE_EVT };
struct esp_avrc_ct_cb_param_t {
  struct {
    bool connected;
  } conn_stat;
};
using esp_avrc_tg_cb_event_t = int;
struct esp_avrc_tg_cb_param_t {};
enum esp_a2d_cb_event_t {
  ESP_A2D_PROF_STATE_EVT,
  ESP_A2D_SEP_REG_STATE_EVT,
  ESP_A2D_CONNECTION_STATE_EVT,
  ESP_A2D_AUDIO_STATE_EVT,
  ESP_A2D_AUDIO_CFG_EVT,
  ESP_A2D_SNK_PSC_CFG_EVT
};
constexpr int ESP_A2D_CONNECTION_STATE_CONNECTED = 2, ESP_A2D_CONNECTION_STATE_DISCONNECTED = 0;
constexpr int ESP_A2D_AUDIO_STATE_SUSPEND = 0, ESP_A2D_AUDIO_STATE_STARTED = 1, ESP_A2D_PSC_DELAY_RPT = 1;
struct esp_a2d_cb_param_t {
  struct {
    int init_state;
  } a2d_prof_stat;
  struct {
    int reg_state;
  } a2d_sep_reg_stat;
  struct {
    uint8_t remote_bda[6];
    int state, disc_rsn;
  } conn_stat;
  struct {
    int state;
    uint8_t remote_bda[6];
    uint16_t conn_hdl;
  } audio_stat;
  struct {
    uint8_t remote_bda[6];
    uint16_t conn_hdl;
    esp_a2d_mcc_t mcc;
  } audio_cfg;
  struct {
    uint16_t psc_mask;
  } a2d_psc_cfg_stat;
};
using esp_a2d_cb_t = void (*)(esp_a2d_cb_event_t, esp_a2d_cb_param_t*);
inline esp_a2d_cb_t host_a2d_callback = nullptr;
inline esp_err_t esp_a2d_register_callback(esp_a2d_cb_t callback) {
  host_a2d_callback = callback;
  return ESP_OK;
}
enum esp_spp_cb_event_t {
  ESP_SPP_INIT_EVT,
  ESP_SPP_OPEN_EVT,
  ESP_SPP_CLOSE_EVT,
  ESP_SPP_CL_INIT_EVT,
  ESP_SPP_CONG_EVT,
  ESP_SPP_WRITE_EVT,
  ESP_SPP_DATA_IND_EVT
};
struct esp_spp_cb_param_t {
  struct {
    int status;
  } init, cl_init;
  struct {
    int status;
    uint8_t rem_bda[6];
    uint32_t handle;
  } open;
  struct {
    uint32_t handle;
  } close;
  struct {
    uint32_t handle;
    bool cong;
  } cong;
  struct {
    uint32_t handle;
    bool cong;
    int status;
  } write;
  struct {
    uint32_t handle;
    int len;
    uint8_t* data;
  } data_ind;
};
#define HOST_OK(name)      \
  template <typename... T> \
  inline int name(T...) {  \
    return 0;              \
  }
HOST_OK(esp_bt_gap_ssp_confirm_reply)
HOST_OK(esp_bt_gap_pin_reply)
inline esp_err_t host_controller_init_result = ESP_OK;
inline esp_err_t esp_bt_controller_init(void*) {
  return host_controller_init_result;
}
HOST_OK(esp_a2d_sink_register_stream_endpoint)
HOST_OK(esp_bt_controller_enable) HOST_OK(esp_bluedroid_init) HOST_OK(esp_bluedroid_enable)
    HOST_OK(esp_bt_gap_set_device_name) HOST_OK(esp_avrc_ct_init) HOST_OK(esp_avrc_ct_register_callback)
        HOST_OK(esp_avrc_tg_init) HOST_OK(esp_avrc_tg_register_callback)
            HOST_OK(esp_a2d_sink_register_audio_data_callback) HOST_OK(esp_a2d_sink_init)
                HOST_OK(esp_spp_register_callback) HOST_OK(esp_spp_enhanced_init) HOST_OK(esp_bt_gap_set_security_param)
                    HOST_OK(esp_a2d_sink_connect) HOST_OK(esp_spp_write) HOST_OK(esp_bt_gap_set_cod)
                        HOST_OK(esp_bt_gap_config_eir_data)
                            HOST_OK(esp_bt_gap_set_scan_mode) inline int esp_spp_connect(int, int, int, uint8_t*) {
  ++host_spp_connects;
  return 0;
}
inline int esp_spp_disconnect(uint32_t) {
  ++host_spp_disconnects;
  return 0;
}
inline int esp_a2d_sink_disconnect(uint8_t*) {
  if (host_disconnect) host_disconnect();
  return 0;
}
inline void esp_a2d_audio_buff_free(esp_a2d_audio_buff_t*) {}
inline uint32_t esp_get_free_heap_size() {
  return 90000;
}
inline uint32_t esp_get_minimum_free_heap_size() {
  return 80000;
}
inline uint32_t heap_caps_get_largest_free_block(int) {
  return 70000;
}
namespace esphome {
class HostPreferences;
class ESPPreferenceObject {
 public:
  HostPreferences* owner;
  template <class T>
  bool load(T* value);
  template <class T>
  bool save(const T* value);
};
class HostPreferences {
 public:
  std::vector<uint8_t> disk, pending;
  unsigned saves{0};
  bool fail_sync_once{false};
  template <class T>
  ESPPreferenceObject make_preference(uint32_t key) {
    assert(key == 0x50425801);
    return {this};
  }
  bool sync() {
    if (fail_sync_once) {
      fail_sync_once = false;
      return false;
    }
    disk = pending;
    return true;
  }
};
template <class T>
bool ESPPreferenceObject::load(T* value) {
  if (owner->disk.size() != sizeof(T)) return false;
  std::memcpy(value, owner->disk.data(), sizeof(T));
  return true;
}
template <class T>
bool ESPPreferenceObject::save(const T* value) {
  ++owner->saves;
  const auto* bytes = reinterpret_cast<const uint8_t*>(value);
  owner->pending.assign(bytes, bytes + sizeof(T));
  return true;
}
inline HostPreferences host_preferences;
inline HostPreferences* global_preferences = &host_preferences;
inline struct HostApp {
  unsigned reboots{0};
  void safe_reboot() {
    ++reboots;
  }
} App;
namespace text {
class Text {
 public:
  std::string state;
  void publish_state(const std::string& value) {
    state = value;
  }

 protected:
  virtual void control(const std::string&) = 0;
};
}  // namespace text
inline uint32_t millis() {
  return host_now;
}
class Component {
  bool failed_ = false;

 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void mark_failed() {
    failed_ = true;
  }
  bool is_failed() const {
    return failed_;
  }
  std::vector<std::function<void()>> timeouts;
  void set_timeout(const std::string&, uint32_t, std::function<void()> callback) {
    timeouts.push_back(callback);
  }
};
namespace media_player {
enum MediaPlayerState {
  MEDIA_PLAYER_STATE_NONE,
  MEDIA_PLAYER_STATE_IDLE,
  MEDIA_PLAYER_STATE_PLAYING,
  MEDIA_PLAYER_STATE_PAUSED,
  MEDIA_PLAYER_STATE_ANNOUNCING,
  MEDIA_PLAYER_STATE_OFF,
  MEDIA_PLAYER_STATE_ON
};
enum MediaPlayerCommand {
  MEDIA_PLAYER_COMMAND_MUTE,
  MEDIA_PLAYER_COMMAND_UNMUTE,
  MEDIA_PLAYER_COMMAND_TURN_ON,
  MEDIA_PLAYER_COMMAND_TURN_OFF,
  MEDIA_PLAYER_COMMAND_VOLUME_UP,
  MEDIA_PLAYER_COMMAND_VOLUME_DOWN
};
constexpr int VOLUME_SET = 4, VOLUME_MUTE = 8, VOLUME_STEP = 1024, TURN_ON = 128, TURN_OFF = 256;
class MediaPlayerTraits {
 public:
  void clear_feature_flags(uint32_t) {}
  void add_feature_flags(uint32_t) {}
};
class MediaPlayerCall {
 public:
  std::optional<float> get_volume() const {
    return {};
  }
  std::optional<MediaPlayerCommand> get_command() const {
    return {};
  }
};
class MediaPlayer {
 public:
  float volume = 1;
  MediaPlayerState state = MEDIA_PLAYER_STATE_NONE;
  virtual MediaPlayerTraits get_traits() = 0;
  virtual bool is_muted() const {
    return false;
  }
  void publish_state() {}

 protected:
  virtual void control(const MediaPlayerCall&) = 0;
};
}  // namespace media_player
namespace select {
class Select {
  bool has_ = false;

 public:
  struct Traits {
    std::vector<std::string> options;
    void set_options(const std::vector<std::string>& values) {
      options = values;
    }
    std::vector<std::string> get_options() const {
      return options;
    }
  } traits;
  void publish_state(const std::string&) {
    has_ = true;
  }
  bool has_state() const {
    return has_;
  }
  void set_has_state(bool b) {
    has_ = b;
  }

 protected:
  virtual void control(const std::string&) = 0;
};
}  // namespace select
namespace button {
class Button {
 protected:
  virtual void press_action() = 0;
};
}  // namespace button
namespace sensor {
class Sensor {
 public:
  void publish_state(float) {}
};
}  // namespace sensor
namespace binary_sensor {
class BinarySensor {
 public:
  bool state{false};
  bool known{false};
  void publish_state(bool value) {
    state = value;
    known = true;
  }
  void invalidate_state() {
    known = false;
  }
  bool has_state() const {
    return known;
  }
};
}  // namespace binary_sensor
namespace text_sensor {
class TextSensor {
 public:
  std::string state;
  void publish_state(const std::string& v) {
    state = v;
    set_has_state(true);
  }
  void set_has_state(bool v) {
    has_state_ = v;
  }
  bool has_state() const {
    return has_state_;
  }

 private:
  bool has_state_{false};
};
}  // namespace text_sensor
// ESPHome exposes registry methods only for configured entity domains, even
// though AUTO_LOAD makes the component classes available to the compiler.
inline unsigned host_select_notifications = 0, host_text_notifications = 0;
#ifdef USE_CONTROLLER_REGISTRY
class ControllerRegistry {
 public:
#ifdef USE_SELECT
  static void notify_select_update(select::Select*) {
    ++host_select_notifications;
  }
#endif
#ifdef USE_TEXT_SENSOR
  static void notify_text_sensor_update(text_sensor::TextSensor*) {
    ++host_text_notifications;
  }
#endif
};
#endif
}  // namespace esphome
