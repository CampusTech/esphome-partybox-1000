#pragma once
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::partybox {
struct PeerAddress {
  uint8_t bytes[6]{};
  bool configured() const {
    for (auto byte : bytes)
      if (byte) return true;
    return false;
  }
};
inline bool parse_address(const std::string& value, PeerAddress& output) {
  PeerAddress parsed{};
  if (value.empty()) {
    output = parsed;
    return true;
  }
  if (value.size() != 17) return false;
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  bool all_ff = true;
  for (int i = 0; i < 6; i++) {
    int hi = digit(value[i * 3]), lo = digit(value[i * 3 + 1]);
    if (hi < 0 || lo < 0 || (i < 5 && value[i * 3 + 2] != ':')) return false;
    parsed.bytes[i] = (hi << 4) | lo;
    all_ff &= parsed.bytes[i] == 255;
  }
  if (!parsed.configured() || all_ff) return false;
  output = parsed;
  return true;
}
inline std::string format_address(const PeerAddress& address) {
  if (!address.configured()) return {};
  char text[18];
  std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", address.bytes[0], address.bytes[1],
                address.bytes[2], address.bytes[3], address.bytes[4], address.bytes[5]);
  return text;
}
// Classic EIR, not BLE advertising. Require a complete model name as well as
// the legacy TWS signature/class: HMPB alone is shared by other models/bridges.
inline bool is_pairing_candidate(uint32_t cod, const uint8_t* eir, size_t size) {
  if (!eir || (cod & 0x040FFF) != 0x040428) return false;
  bool name = false, manufacturer = false;
  for (size_t offset = 0; offset < size;) {
    const size_t length = eir[offset];
    if (!length) break;
    if (length + 1 > size - offset) return false;
    const uint8_t type = eir[offset + 1];
    const uint8_t* data = eir + offset + 2;
    const size_t n = length - 1;
    if (type == 9 && n == 17 && !std::memcmp(data, "JBL PartyBox 1000", 17)) name = true;
    if (type == 255 && n >= 6 && !std::memcmp(data, "HMPB", 4)) manufacturer = true;
    offset += length + 1;
  }
  return name && manufacturer;
}
class PairingCandidates {
 public:
  void clear() {
    count_ = 0;
    overflow_ = false;
  }
  void add(const PeerAddress& address) {
    if (!address.configured()) return;
    for (size_t i = 0; i < count_; i++)
      if (!std::memcmp(address.bytes, values_[i].bytes, 6)) return;
    if (count_ == values_.size()) {
      overflow_ = true;
      return;
    }
    values_[count_++] = address;
  }
  size_t size() const {
    return count_;
  }
  bool overflowed() const {
    return overflow_;
  }
  const PeerAddress* single() const {
    return count_ == 1 && !overflow_ ? &values_[0] : nullptr;
  }
  const PeerAddress& operator[](size_t index) const {
    return values_[index];
  }

 private:
  std::array<PeerAddress, 8> values_{};
  size_t count_{0};
  bool overflow_{false};
};
struct SavedPeer {
  uint32_t version{1};
  PeerAddress address{};
};
}  // namespace esphome::partybox
