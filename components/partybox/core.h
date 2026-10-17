#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::partybox {
enum class Quality : uint8_t { UNKNOWN, OBSERVED, ASSUMED };
inline const char* quality_name(Quality q) {
  return q == Quality::OBSERVED ? "observed" : q == Quality::ASSUMED ? "assumed" : "unknown";
}
enum class Field : uint8_t { VOLUME, MUTE, SOURCE, BASS, LIGHTING, POWER, VERSION };
template <typename T>
struct Value {
  T value{};
  Quality quality{Quality::UNKNOWN};
  uint32_t revision{0}, at{0};
  void set(T v, Quality q, uint32_t now) {
    value = v;
    quality = q;
    at = now;
    ++revision;
  }
  void invalidate() {
    quality = Quality::UNKNOWN;
    ++revision;
  }
};
inline bool valid_source(int v) {
  return v == 13 || v == 15 || v == 6 || v == 9;
}
struct Command {
  Field field{Field::VOLUME};
  int value{0};
  uint32_t revision{0};
};
inline bool valid_command(const Command& c) {
  switch (c.field) {
    case Field::VOLUME:
      return c.value >= 0 && c.value <= 32;
    case Field::MUTE:
    case Field::POWER:
      return c.value == 0 || c.value == 1;
    case Field::SOURCE:
      return valid_source(c.value);
    case Field::BASS:
      return c.value >= 0 && c.value <= 2;
    case Field::LIGHTING:
      return c.value >= 0 && c.value <= 4;
    case Field::VERSION:
      return c.value == 0;
  }
  return false;
}
struct State {
  Value<int> volume, source, bass, lighting;
  Value<bool> mute, power, activity, lighting_flag, smart_dj, voice_cancel;
  uint32_t revision(Field f) const {
    switch (f) {
      case Field::VOLUME:
        return volume.revision;
      case Field::MUTE:
        return mute.revision;
      case Field::SOURCE:
        return source.revision;
      case Field::BASS:
        return bass.revision;
      case Field::LIGHTING:
        return lighting.revision;
      case Field::POWER:
        return power.revision;
      default:
        return 0;
    }
  }
  void begin_source_change() {
    source.invalidate();
    activity.invalidate();
  }
  void disconnect() {
    volume.invalidate();
    source.invalidate();
    bass.invalidate();
    lighting.invalidate();
    mute.invalidate();
    power.invalidate();
    activity.invalidate();
    lighting_flag.invalidate();
    smart_dj.invalidate();
    voice_cancel.invalidate();
  }
  bool decode(uint32_t word, uint32_t now) {
    const auto v = int((word >> 16) & 255), src = int(word >> 24);
    switch (word & 31) {
      case 4:
        if (!valid_source(src) || v > 32 || ((word >> 9) & 3) > 2 || ((word >> 12) & 7) > 4) return false;
        source.set(src, Quality::OBSERVED, now);
        volume.set(v, Quality::OBSERVED, now);
        mute.set(bool(word & 0x100), Quality::OBSERVED, now);
        bass.set((word >> 9) & 3, Quality::OBSERVED, now);
        lighting.set((word >> 12) & 7, Quality::OBSERVED, now);
        lighting_flag.set(bool(word & 0x8000), Quality::OBSERVED, now);
        smart_dj.set(bool(word & 0x800), Quality::OBSERVED, now);
        power.set(true, Quality::OBSERVED, now);
        activity.invalidate();
        return true;
      case 5:
        if (!valid_source(src) || v > 32) return false;
        if (source.quality == Quality::UNKNOWN || source.value != src) activity.invalidate();
        source.set(src, Quality::OBSERVED, now);
        volume.set(v, Quality::OBSERVED, now);
        return true;
      case 10:
        if (v > 32) return false;
        volume.set(v, Quality::OBSERVED, now);
        return true;
      case 14:
        if (v > 1) return false;
        mute.set(v, Quality::OBSERVED, now);
        return true;
      case 15:
        if (v > 2) return false;
        bass.set(v, Quality::OBSERVED, now);
        return true;
      case 16:
        if (v > 1) return false;
        voice_cancel.set(v, Quality::OBSERVED, now);
        return true;
      case 17:
        if (v > 1) return false;
        smart_dj.set(v, Quality::OBSERVED, now);
        return true;
      case 18:
        if (v > 4 || src > 1) return false;
        lighting.set(v, Quality::OBSERVED, now);
        lighting_flag.set(src, Quality::OBSERVED, now);
        return true;
      case 19:
        if (v != 1 && v != 2) return false;
        // A power synchronization request is evidence of a transition, not a hardware getter.
        power.set(v == 2, Quality::ASSUMED, now);
        activity.invalidate();
        return true;
      case 20: {
        if (source.quality != Quality::OBSERVED || source.value == 13) return false;
        const unsigned bit = source.value == 6 ? 1 : source.value == 9 ? 2 : 4;
        activity.set(!(v & bit), Quality::OBSERVED, now);
        return true;
      }
      default:
        return false;
    }
  }
  void assume(Field f, int v, uint32_t baseline, uint32_t now) {
    if (revision(f) != baseline) return;  // a report received after send wins over its ACK
    switch (f) {
      case Field::VOLUME:
        volume.set(v, Quality::ASSUMED, now);
        break;
      case Field::MUTE:
        mute.set(v, Quality::ASSUMED, now);
        break;
      case Field::SOURCE:
        source.set(v, Quality::ASSUMED, now);
        activity.invalidate();
        break;
      case Field::BASS:
        bass.set(v, Quality::ASSUMED, now);
        break;
      case Field::LIGHTING:
        lighting.set(v, Quality::ASSUMED, now);
        break;
      case Field::POWER:
        power.set(v, Quality::ASSUMED, now);
        activity.invalidate();
        break;
      default:
        break;
    }
  }
};
class Scheduler {
 public:
  bool enqueue(Command c) {
    if (!valid_command(c)) return false;
    if (c.field == Field::VOLUME) {
      for (size_t i = 0; i < count_; ++i)
        if (queue_[i].field == Field::VOLUME) {
          queue_[i] = c;
          return true;
        }
    }
    if (count_ == queue_.size()) return false;
    queue_[count_++] = c;
    return true;
  }
  bool take(Command& c) {
    if (pending_ || !count_) return false;
    c = queue_[0];
    for (size_t i = 1; i < count_; ++i) queue_[i - 1] = queue_[i];
    --count_;
    return true;
  }
  bool begin(Command c, uint8_t label, uint32_t now) {
    if (pending_ || label > 15 || (quarantine_ & (1U << label))) return false;
    current = c;
    label_ = label;
    started_ = now;
    pending_ = true;
    return true;
  }
  bool ack(uint8_t label, bool accepted) {
    if (!pending_ || label != label_) return false;
    pending_ = false;
    last_accepted = accepted;
    return true;
  }
  bool expired(uint32_t now) const {
    return pending_ && uint32_t(now - started_) >= 3000;
  }
  void timeout() {
    if (pending_) quarantine_ |= 1U << label_;
    pending_ = false;
  }
  void reset() {
    pending_ = false;
    count_ = 0;
    quarantine_ = 0;
  }
  bool pending() const {
    return pending_;
  }
  bool available(uint8_t l) const {
    return l < 16 && !(quarantine_ & (1U << l));
  }
  size_t size() const {
    return count_;
  }
  Command current{};
  bool last_accepted{false};

 protected:
  std::array<Command, 8> queue_{};
  size_t count_{0};
  bool pending_{false};
  uint8_t label_{0};
  uint16_t quarantine_{0};
  uint32_t started_{0};
};
class SppFramer {
 public:
  bool feed(uint8_t b) {
    if (complete_) {
      length_ = 0;
      complete_ = false;
    }
    if (!length_ && b != 0xBB) return false;
    bytes_[length_++] = b;
    if (length_ == 2 && (b < 5 || b > bytes_.size())) {
      length_ = 0;
      return false;
    }
    if (length_ >= 2 && length_ == bytes_[1]) {
      // SPP replies use command-specific trailers; only version replies end in FFFF.
      const bool valid = (length_ == 5 && ((bytes_[2] == 0x0A && bytes_[3] == 0x9C && bytes_[4] == 0x2D) ||
                                           (bytes_[2] == 0x01 && bytes_[3] == 0x2D && bytes_[4] == 0x46))) ||
                         (length_ == 10 && bytes_[2] == 0x08 && bytes_[8] == 0xFF && bytes_[9] == 0xFF);
      if (!valid) {
        length_ = 0;
        return false;
      }
      complete_ = true;
      return true;
    }
    return false;
  }
  void reset() {
    length_ = 0;
    complete_ = false;
  }
  const uint8_t* data() const {
    return bytes_.data();
  }
  size_t size() const {
    return length_;
  }

 protected:
  std::array<uint8_t, 64> bytes_{};
  size_t length_{0};
  bool complete_{false};
};
inline uint32_t command_word(const Command& c, bool light_flag) {
  const uint8_t kind = c.field == Field::VOLUME     ? 10
                       : c.field == Field::MUTE     ? 14
                       : c.field == Field::BASS     ? 15
                       : c.field == Field::LIGHTING ? 18
                                                    : 19;
  const uint32_t value = c.field == Field::POWER ? (c.value ? 2 : 1) : c.value;
  return value << 16 | kind | (c.field == Field::LIGHTING && light_flag ? 0x01000000 : 0);
}
}  // namespace esphome::partybox
