#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "tracer.h"
#include "utils.h"
#include "xxhash.h"

// geometric series of predictor tables; each uses a different global length
// each table can store some number of entries (maybe decaying geometric)
// each entry has a counter, a tag and a usefulness counter
//
// index every table
// check the tag; if the tag matches and counter says useful, it is a hit
// take the two hits with longest history. if there is only one, take that.
// if there are none, fall back to another predictor (Gshare)
//
// when updating, only update the tables you actually used, and only on
// mispredictions

using u8 = uint8_t;
using u32 = UINT32;
using usize = std::size_t;

constexpr double apow(const double base, int power) {
  assert(power >= 0);
  assert(base > 1.0);
  int result = 1.0;
  while (power--)
    result *= base;
  return result;
}

#define GSHARE_PATTERN_TABLE_BITS (12)
#define GSHARE_PATTERN_TABLE_LEN (1 << GSHARE_PATTERN_TABLE_BITS)
#define GSHARE_HISTORY_BITS (12)
#define GSHARE_COUNTER_BITS (2)
#define GSHARE_SIZE (GSHARE_COUNTER_BITS * GSHARE_PATTERN_TABLE_LEN)

#define TAGE_MIN_HISTORY_LEN (3)
#define TAGE_HISTORY_GROWTH (2.0)
#define TAGE_HISTORY_COUNT (16)

#define TAGE_PATTERN_TABLE_BITS (12)
#define TAGE_PATTERN_TABLE_LEN (1 << TAGE_PATTERN_TABLE_BITS)
#define TAGE_PATTERN_TABLE_TAG_BITS (12)

class PREDICTOR {

  class Entry {
    // m_data[0..1] are useful bits.
    // m_data[1..3] are counter bits.
    // m_data[3..32] are tag bits.
    u32 m_data;

    u32 useful() const { return m_data & 0b1; }
    void set_useful(u32 u) { m_data = (m_data & ~0b1) | (u & 0b1); }

    u32 counter() const { return (m_data >> 1) & 0b11; }
    void set_counter(u32 c) { m_data = (m_data & ~0b110) | (c & 0b110); }

  public:
    Entry(u32 tag, bool default_prediction) {
      u32 c = default_prediction ? 0b10 : 0b01;
      m_data = tag << 3 | (c << 1) | 0b0;
    }

    void update(bool prediction, bool actual, bool used) {
      auto c = counter();
      if (actual && c < 3)
        ++c;
      if (!actual && c > 0)
        --c;
      set_counter(c);
      if (used)
        set_useful(1);
    }

    bool is_useful() const { return useful(); }

    bool prediction() const { return counter() >= 0b10; };
    bool is_weak_prediction() const {
      const auto c = counter();
      return c == 0b10 || c == 0b00;
    }

    bool is_strong_prediction() const { return !is_weak_prediction(); }

    u32 tag() const { return m_data >> 3; }
  };

  class PatternTable {
    std::vector<Entry> m_entries;
    usize m_bits;
    usize m_tag_bits;

    u32 to_tag(u32 hash) const {
      return (hash >> m_bits) & ((1 << m_tag_bits) - 1);
    }
    u32 to_idx(u32 hash) const { return hash & ((1 << m_bits) - 1); }

  public:
    PatternTable(usize bits, usize tag_bits)
        : m_entries(1 << bits, Entry(0, true)), m_bits(bits),
          m_tag_bits(tag_bits) {}

    Entry *get(u32 hash) {
      const auto idx = to_idx(hash);
      const auto tag = to_tag(hash);
      auto &entry = m_entries[idx];
      return entry.tag() == tag || m_tag_bits == 0 ? &entry : nullptr;
    }

    bool allocate(u32 hash, bool default_prediction) {
      const auto idx = to_idx(hash);
      if (m_entries[idx].is_useful()) {
        return false;
      }
      const auto tag = to_tag(hash);
      m_entries[idx] = Entry(tag, default_prediction);
      return true;
    }
  };

  PatternTable m_gshare_table;
  usize m_gshare_hash_idx;

  std::vector<PatternTable> m_pattern_tables;

  std::vector<bool> m_history;
  std::vector<usize> m_history_bits;
  std::vector<u32> m_history_hashes;

  Entry *m_primary = nullptr;
  usize m_primary_idx = 0;
  Entry *m_backup = nullptr;
  Entry *m_gshare = nullptr;

#define USED_PRIMARY 0
#define USED_BACKUP 1
#define USED_GSHARE 2
  u32 m_used = USED_GSHARE;

public:
  PREDICTOR(void)
      : m_gshare_table(GSHARE_PATTERN_TABLE_BITS, 0),
        m_pattern_tables(
            TAGE_HISTORY_COUNT,
            PatternTable(TAGE_PATTERN_TABLE_BITS, TAGE_PATTERN_TABLE_TAG_BITS)),
        m_history_hashes(TAGE_HISTORY_COUNT, 0) {
    //    std::cerr << "BUILD" << std::endl;
    m_history_bits.reserve(TAGE_HISTORY_COUNT);
    for (usize i = 0; i < TAGE_HISTORY_COUNT; ++i) {
      m_history_bits.push_back(TAGE_MIN_HISTORY_LEN *
                               apow(TAGE_HISTORY_GROWTH, i));
    }

    m_history.reserve(m_history_bits.back());
    for (usize i = 0; i < m_history_bits.back(); ++i)
      m_history.push_back(false);

    m_gshare_hash_idx = 0;
    while (m_gshare_hash_idx + 1 < m_history_bits.size() &&
           m_history_bits[m_gshare_hash_idx] < GSHARE_HISTORY_BITS) {
      ++m_gshare_hash_idx;
    }
  }

  bool GetPrediction(u32 PC) {
    // std::cerr << "HIT" << std::endl;
    usize prev_bits = 0;
    XXH32_state_t *state = XXH32_createState();
    XXH32_reset(state, 0);
    XXH32_update(state, &PC, 4);
    for (usize i = 0; i < m_history_bits.size(); ++i) {
      auto bits = m_history_bits[i];
      while (prev_bits < bits) {
        u8 bit = m_history[prev_bits];
        XXH32_update(state, &bit, 1);
        ++prev_bits;
      }
      m_history_hashes[i] = XXH32_digest(state);
    }
    XXH32_freeState(state);

    m_primary = nullptr;
    m_backup = nullptr;

    for (usize i = m_pattern_tables.size(); i-- > 0;) {
      const auto h = m_history_hashes[i];
      auto entry = m_pattern_tables[i].get(h);
      if (!entry)
        continue;
      if (!entry->is_useful())
        continue;

      if (!m_primary) {
        m_primary = entry;
        m_primary_idx = i;
      } else {
        m_backup = entry;
        break;
      }
    }

    const auto h = m_history_hashes[m_gshare_hash_idx];
    m_gshare = m_gshare_table.get(h);

    if (m_primary && (m_primary->is_strong_prediction() || !m_backup)) {
      m_used = USED_PRIMARY;
      return m_primary->prediction();
    }

    if (m_backup && m_backup->is_strong_prediction()) {
      m_used = USED_BACKUP;
      return m_backup->prediction();
    }

    m_used = USED_GSHARE;
    return m_gshare->prediction();
  };

  void UpdatePredictor(u32 PC, bool resolveDir, bool predDir,
                       u32 branchTarget) {
    m_history.insert(m_history.begin(), resolveDir);
    m_history.pop_back();

    if (m_primary && m_primary->prediction() != predDir &&
        m_primary_idx < m_pattern_tables.size() - 1) {
      auto idx = m_primary_idx + 1;
      m_pattern_tables[idx].allocate(m_history_hashes[idx], resolveDir);
    }

    if (m_primary) {
      m_primary->update(predDir, resolveDir, m_used == USED_PRIMARY);
    }

    if (m_backup) {
      m_backup->update(predDir, resolveDir, m_used == USED_BACKUP);
    }

    m_gshare->update(predDir, resolveDir, 0);
  };

  void TrackOtherInst(u32 PC, OpType opType, u32 branchTarget) { return; };
};

#endif
