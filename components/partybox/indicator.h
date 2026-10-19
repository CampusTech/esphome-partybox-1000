#pragma once
#include <cstdint>

namespace esphome::partybox {
enum class IndicatorColor : uint8_t { OFF, GREEN, BLUE, AMBER, CYAN, RED };
class Indicator {
 public:
  void transmitted(uint32_t now) {
    activity_at_ = now;
    activity_ = true;
  }
  void failed(uint32_t now) {
    error_at_ = now;
    error_ = true;
    activity_ = false;
  }
  IndicatorColor color(uint32_t now, bool network_ready, bool speaker_ready, bool fatal) {
    if (fatal) return IndicatorColor::RED;
    if (error_ && uint32_t(now - error_at_) >= 600) error_ = false;
    if (activity_ && uint32_t(now - activity_at_) >= 120) activity_ = false;
    if (error_) return (uint32_t(now - error_at_) / 100) % 2 ? IndicatorColor::OFF : IndicatorColor::RED;
    if (activity_) return IndicatorColor::CYAN;
    if (!network_ready) return now % 1200 < 400 ? IndicatorColor::AMBER : IndicatorColor::OFF;
    if (!speaker_ready) return now % 1200 < 400 ? IndicatorColor::BLUE : IndicatorColor::OFF;
    return IndicatorColor::GREEN;
  }

 protected:
  uint32_t activity_at_{0}, error_at_{0};
  bool activity_{false}, error_{false};
};
}  // namespace esphome::partybox
