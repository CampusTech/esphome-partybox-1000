#include "core.h"

#include <cassert>
#include <iostream>
using namespace esphome::partybox;
int main() {
  State input_flags;
  // A TWS silence mask is useful as per-input reported flags even when the
  // selected input has not been independently confirmed.
  assert(input_flags.decode(0x000D0014, 1));
  assert(input_flags.activity.quality == Quality::UNKNOWN);
  for (unsigned mask = 0; mask < 16; ++mask) {
    State flags;
    assert(flags.decode((mask << 16) | 0x14, 10 + mask));
    assert(flags.activity.quality == Quality::UNKNOWN);
    for (unsigned bit = 0; bit < 4; ++bit) {
      assert(flags.input_signals[bit].quality == Quality::OBSERVED);
      assert(flags.input_signals[bit].value == !(mask & (1U << bit)));
      assert(flags.input_signals[bit].at == 10 + mask);
    }
    flags.assume(Field::SOURCE, 9, flags.source.revision, 30);
    assert(flags.decode((mask << 16) | 0x14, 31));
    assert(flags.activity.quality == Quality::UNKNOWN);  // assumed source is not selected-source evidence
    flags.begin_source_change();
    for (const auto& signal : flags.input_signals) assert(signal.quality == Quality::UNKNOWN);
    assert(flags.decode((mask << 16) | 0x14, 32));
    flags.assume(Field::SOURCE, 6, flags.source.revision, 33);
    for (const auto& signal : flags.input_signals) assert(signal.quality == Quality::OBSERVED);
    flags.disconnect();
    for (const auto& signal : flags.input_signals) assert(signal.quality == Quality::UNKNOWN);
    assert(flags.decode((mask << 16) | 0x14, 34));
    flags.assume(Field::POWER, 0, flags.power.revision, 35);
    for (const auto& signal : flags.input_signals) assert(signal.quality == Quality::UNKNOWN);
    assert(flags.decode((mask << 16) | 0x14, 36));
    assert(flags.decode(0x00020013, 37));
    for (const auto& signal : flags.input_signals) assert(signal.quality == Quality::UNKNOWN);
    for (int src : {6, 9, 15, 13}) {
      State selected;
      selected.source.set(src, Quality::OBSERVED, 40);
      assert(selected.decode((mask << 16) | 0x14, 41));
      if (src == 13) {
        assert(selected.activity.quality == Quality::UNKNOWN);  // Bluetooth bit is not playback status
      } else {
        const unsigned bit = src == 6 ? 0 : src == 9 ? 1 : 2;
        assert(selected.activity.quality == Quality::OBSERVED);
        assert(selected.activity.value == !(mask & (1U << bit)));
      }
    }
  }
  State transitions;
  assert(transitions.decode(0x090B1244, 1));
  assert(transitions.decode(0x000D0014, 2));
  assert(transitions.decode(0x090B0005, 3));
  assert(transitions.input_signals[1].quality == Quality::OBSERVED);  // same source preserves flags
  assert(transitions.decode(0x060B0005, 4));
  for (const auto& signal : transitions.input_signals) assert(signal.quality == Quality::UNKNOWN);
  assert(transitions.decode(0x000D0014, 5));
  assert(transitions.decode(0x060B1244, 6));
  for (const auto& signal : transitions.input_signals) assert(signal.quality == Quality::UNKNOWN);
  State s;
  assert(s.volume.quality == Quality::UNKNOWN);
  assert(!s.decode(0x09211244, 10));  // invalid volume, no partial snapshot
  assert(s.source.quality == Quality::UNKNOWN);
  assert(s.decode(0x090B1244, 20));
  assert(s.source.value == 9 && s.volume.value == 11 && s.bass.value == 1);
  assert(!s.mute.value && s.lighting.value == 1);
  assert(s.decode(0x00010014, 30) && s.activity.quality == Quality::OBSERVED && s.activity.value);
  s.begin_source_change();
  assert(s.source.quality == Quality::UNKNOWN && s.activity.quality == Quality::UNKNOWN);
  assert(s.decode(0x00010014, 35));  // raw report is useful while source attribution is pending
  assert(s.activity.quality == Quality::UNKNOWN);
  auto rev = s.source.revision;
  s.assume(Field::SOURCE, 6, rev, 40);
  assert(s.source.quality == Quality::ASSUMED && s.activity.quality == Quality::UNKNOWN);
  assert(s.decode(0x000C000A, 45));
  s.assume(Field::VOLUME, 2, 1, 50);  // late ACK must not overwrite physical report
  assert(s.volume.value == 12 && s.volume.quality == Quality::OBSERVED);
  s.disconnect();
  assert(s.volume.quality == Quality::UNKNOWN && s.power.quality == Quality::UNKNOWN);
  Scheduler q;
  Command c{Field::VOLUME, 1};
  for (int i = 0; i < 40; i++) {
    c.value = i % 33;
    assert(q.enqueue(c));
  }
  assert(q.size() == 1);
  Command out;
  assert(q.take(out) && out.value == 6);
  assert(q.begin(out, 7, 0xFFFFFF00U));
  assert(!q.ack(8, true));
  assert(q.expired(0x00000AB9U));
  q.timeout();
  assert(!q.begin(out, 7, 0));  // timed-out label quarantined
  q.reset();
  assert(q.begin(out, 7, 0));
  assert(q.ack(7, true));
  assert(!q.ack(7, true));
  for (int i = 0; i < 8; i++) assert(q.enqueue({Field::MUTE, i % 2}));
  assert(!q.enqueue({Field::BASS, 1}));
  SppFramer f;
  uint8_t version[] = {0xBB, 10, 8, 75, 32, 17, 15, 7, 255, 255};
  for (int i = 0; i < 9; i++) assert(!f.feed(version[i]));
  assert(f.feed(version[9]));
  assert(f.size() == 10 && f.data()[3] == 75);
  assert(!f.feed(0));
  assert(!f.feed(0xBB));
  assert(!f.feed(255));
  uint8_t ack[] = {0xBB, 5, 10, 0x9C, 0x2D};  // exact real SPP generic ACK, not FFFF
  for (int i = 0; i < 4; i++) assert(!f.feed(ack[i]));
  assert(f.feed(ack[4]));
  uint8_t replies[] = {0xBB, 5, 10, 0x9C, 0x2D, 0xBB, 5, 1, 0x2D, 0x46};
  unsigned frames = 0;
  for (auto b : replies)
    if (f.feed(b)) {
      assert(f.size() == 5);
      ++frames;
    }
  assert(frames == 2);  // coalesced generic ACK and source reply both survive framing
  const uint8_t malformed[][5] = {{0xBB, 5, 10, 255, 255},  {0xBB, 5, 10, 0x9C, 0x2E}, {0xBB, 5, 1, 255, 255},
                                  {0xBB, 5, 1, 0x2D, 0x47}, {0xBB, 5, 10, 0x2D, 0x46}, {0xBB, 5, 1, 0x9C, 0x2D},
                                  {0xBB, 5, 8, 255, 255},   {0xBB, 5, 2, 255, 255}};
  for (const auto& packet : malformed) {
    f.reset();
    for (auto b : packet) assert(!f.feed(b));
  }
  uint8_t wrong_version[] = {0xBB, 10, 8, 75, 32, 17, 15, 7, 0x9C, 0x2D};
  for (auto b : wrong_version) assert(!f.feed(b));
  for (int i = 0; i < 9; i++) assert(!f.feed(version[i]));
  assert(f.feed(version[9]));  // malformed replies cannot poison the next valid frame
  assert(valid_command({Field::VOLUME, 32}) && !valid_command({Field::VOLUME, 33}));
  assert(!valid_command({Field::SOURCE, 1}) && valid_command({Field::SOURCE, 9}));
  std::cout << "PASS: protocol, state confidence, ACK race, queue, timeouts and SPP framing\n";
}
