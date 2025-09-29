#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "tracer.h"
#include "xxhash.h"

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using i8 = std::int8_t;
using i32 = std::int32_t;
using f64 = double;
using usize = std::size_t;

inline constexpr i32 apow(const f64 base, i32 power) {
  f64 result = 1.0;
  while (power--)
    result *= base;
  return static_cast<i32>(result) + 1;
}

#ifndef HISTORY_COUNT
#define HISTORY_COUNT (16)
#endif

#ifndef HISTORY_GEO_START
#define HISTORY_GEO_START (3)
#endif

#ifndef HISTORY_GEO_FACTOR
#define HISTORY_GEO_FACTOR (1.4)
#endif

#define HISTORY_GEO(i) (HISTORY_GEO_START * apow(HISTORY_GEO_FACTOR, (i)))
#define HISTORY_GEO_END (HISTORY_GEO(HISTORY_COUNT - 1))

#ifndef GSHARE_TABLE_BITS
#define GSHARE_TABLE_BITS (12)
#endif

#define GSHARE_TABLE_LEN (1 << GSHARE_TABLE_BITS)

#ifndef GSHARE_HISTORY_BITS
#define GSHARE_HISTORY_BITS (12)
#endif

#ifndef GSHARE_COUNTER_BITS
#define GSHARE_COUNTER_BITS (3)
#endif

#ifndef TAGE_TABLE_BITS
#define TAGE_TABLE_BITS (11)
#endif

#define TAGE_TABLE_LEN (1 << TAGE_TABLE_BITS)

#ifndef TAGE_TABLE_TAG_BITS
#define TAGE_TABLE_TAG_BITS (10)
#endif

#ifndef UBIT_DECAY_PERIOD
#define UBIT_DECAY_PERIOD (1024)
#endif

#ifndef ALLOC_PROBE_COUNT
#define ALLOC_PROBE_COUNT (4)
#endif

#define SIZE                                                                   \
  (GSHARE_COUNTER_BITS * GSHARE_TABLE_LEN +                                    \
   HISTORY_COUNT * TAGE_TABLE_LEN * (TAGE_TABLE_TAG_BITS + 3 + 2 + 1) +        \
   HISTORY_GEO_END)

// static_assert(SIZE <= 1 << 19, "predictor too large");

class PREDICTOR {

  // -------------------------------------------------------------
  // Single table entry
  // -------------------------------------------------------------
  class Entry {
    u32 m_data; // layout (low -> high):
                // bits [0..1] : usefulness counter (0..3)
                // bits [2..4] : prediction counter (0..7)
                // bit  5      : valid flag
                // bits [6..]  : tag

    u32 useful() const { return m_data & 0b11u; }
    void set_useful(u32 u) { m_data = (m_data & ~0b11u) | (u & 0b11u); }
    void dec_useful() {
      if (useful())
        set_useful(useful() - 1);
    }

    u32 counter() const { return (m_data >> 2) & 0b111u; }
    void set_counter(u32 c) {
      m_data = (m_data & ~(0b111u << 2)) | ((c & 0b111u) << 2);
    }

    bool valid_flag() const { return (m_data >> 5) & 0b1u; }
    void set_valid_flag(bool v) {
      if (v)
        m_data |= 0x20u;
      else
        m_data &= ~0x20u;
    }

  public:
    Entry(u32 tag = 0, bool default_prediction = true) {
      u32 c = default_prediction ? 5u : 2u;
      u32 u = 0u;
      m_data = (tag << 6) | (c << 2) | u;
      set_valid_flag(false); // initially invalid
    }

    void set_tag_and_counter(u32 tag, bool default_prediction) {
      u32 c = default_prediction ? 5u : 2u;
      u32 u = 0u;
      m_data = (tag << 6) | (c << 2) | u;
      set_valid_flag(true); // mark entry valid
    }

    void update(bool prediction, bool actual, bool used, bool alt_disagreed) {
      auto c = counter();
      if (actual && c < 7)
        ++c;
      if (!actual && c > 0)
        --c;
      set_counter(c);

      if (used && prediction == actual && alt_disagreed) {
        if (useful() < 3)
          set_useful(useful() + 1);
      }
    }

    bool is_useful() const { return useful() != 0; }
    void decrement_usefulness() { dec_useful(); }

    bool prediction() const { return counter() >= 4u; }
    bool is_weak_prediction() const {
      const auto c = counter();
      return (c == 3u || c == 4u);
    }
    bool is_strong_prediction() const { return !is_weak_prediction(); }

    u32 tag() const { return m_data >> 6; }
    bool is_valid() const { return valid_flag(); }
  };

  // -------------------------------------------------------------
  // Pattern table
  // -------------------------------------------------------------
  class PatternTable {
    std::vector<Entry> m_entries;
    usize m_bits;
    usize m_tag_bits;

    u32 to_tag(u32 hash) const {
      if (m_tag_bits == 0)
        return 0;
      return (hash >> m_bits) & ((1u << m_tag_bits) - 1u);
    }
    u32 to_idx(u32 hash) const { return hash & ((1u << m_bits) - 1u); }

  public:
    PatternTable(usize bits, usize tag_bits)
        : m_entries(1 << bits, Entry(0, true)), m_bits(bits),
          m_tag_bits(tag_bits) {}

    // Returns pointer only if entry is valid and tag matches
    Entry *get(u32 hash) {
      auto &entry = m_entries[to_idx(hash)];
      if (!entry.is_valid())
        return nullptr;
      return (m_tag_bits == 0 || entry.tag() == to_tag(hash)) ? &entry
                                                              : nullptr;
    }

    // TAGE-style allocation with usefulness check
    bool allocate(u32 hash, bool default_prediction) {
      const auto idx = to_idx(hash);
      const auto tag = to_tag(hash);

      // Direct replacement if not useful or invalid
      if (!m_entries[idx].is_valid() || !m_entries[idx].is_useful()) {
        m_entries[idx].set_tag_and_counter(tag, default_prediction);
        return true;
      }

      // Probe nearby entries for unused slots
      int chosen = -1;
      for (int p = 1; p <= ALLOC_PROBE_COUNT; ++p) {
        usize i = (idx + p) & ((1u << m_bits) - 1u);
        if (!m_entries[i].is_useful()) {
          chosen = i;
          break;
        }
      }

      if (chosen >= 0) {
        m_entries[chosen].set_tag_and_counter(tag, default_prediction);
        return true;
      }

      // No free candidate: decay a few and allocate at primary
      for (int p = 0; p <= ALLOC_PROBE_COUNT; ++p) {
        usize i = (idx + p) & ((1u << m_bits) - 1u);
        m_entries[i].decrement_usefulness();
      }

      m_entries[idx].set_tag_and_counter(tag, default_prediction);
      return true;
    }

    std::vector<Entry> &entries() { return m_entries; }
  };

  PatternTable m_gshare_table;
  usize m_gshare_hash_idx;

  // TAGE pattern tables
  std::vector<PatternTable> m_pattern_tables;

  class History {
    std::vector<u8> m_data;
    usize m_head;

  public:
    History(usize n) : m_data(n, 0), m_head(0) {}

    void hash(XXH32_state_t *state, usize start, usize bits) const {
      if (bits == 0)
        return;

      const auto n = m_data.size();
      assert(start < n);
      assert(start + bits <= n);

      start = (m_head + start) % n;
      if (start + bits < n) {
        XXH32_update(state, m_data.data() + start, bits);
        return;
      }

      XXH32_update(state, m_data.data() + start, n - start);
      XXH32_update(state, m_data.data(), bits - (n - start));
    }

    void update(u8 bit) {
      const auto n = m_data.size();
      const auto tail = m_head != 0 ? m_head - 1 : n - 1;
      m_data[tail] = bit;
      m_head = tail;
    }
  };

  History m_history;
  std::vector<u32> m_history_bits;
  std::vector<u32> m_history_hash;

  void set_history_hash(u32 pc) {
    XXH32_state_t *state = XXH32_createState();
    XXH32_reset(state, 0);
    XXH32_update(state, &pc, 4);
    usize start = 0;
    assert(m_history_bits.size() == m_history_hash.size());
    for (usize i = 0; i < m_history_bits.size(); ++i) {
      const auto end = m_history_bits[i];
      const auto bits = end - start;
      m_history.hash(state, start, bits);
      m_history_hash[i] = XXH32_digest(state);
      start += bits;
    }
    XXH32_freeState(state);
  }

  // primary / backup
  Entry *m_primary = nullptr;
  usize m_primary_idx = 0;
  Entry *m_backup = nullptr;
  Entry *m_gshare = nullptr;

#define USED_PRIMARY 0
#define USED_BACKUP 1
#define USED_GSHARE 2
  u32 m_used = USED_GSHARE;

  // altpred chosen (pointer to entry used as "alternate" when primary weak)
  Entry *m_alt = nullptr;

  // for usefulness decay
  usize m_branch_since_decay = 0;

public:
  PREDICTOR(void)
      : m_gshare_table(GSHARE_TABLE_BITS, 0),
        m_pattern_tables(HISTORY_COUNT,
                         PatternTable(TAGE_TABLE_BITS, TAGE_TABLE_TAG_BITS)),
        m_history(HISTORY_GEO_END), m_history_bits(HISTORY_COUNT, 0),
        m_history_hash(HISTORY_COUNT, 0) {

    for (usize i = 0; i < HISTORY_COUNT; ++i)
      m_history_bits[i] = static_cast<usize>(HISTORY_GEO(i));

    // find gshare index (first table with >= GSHARE_HISTORY_BITS)
    m_gshare_hash_idx = 0;
    while (m_gshare_hash_idx + 1 < m_history_bits.size() &&
           m_history_bits[m_gshare_hash_idx] < GSHARE_HISTORY_BITS) {
      ++m_gshare_hash_idx;
    }
  }

  bool GetPrediction(u32 PC) {
    // compute hashes using current history
    set_history_hash(PC);

    m_primary_idx = 0;
    m_primary = nullptr;
    m_backup = nullptr;
    m_alt = nullptr;

    // find primary and backup (search from longest to shortest)
    for (usize i = m_pattern_tables.size(); i-- > 0;) {
      const auto h = m_history_hash[i];
      auto entry = m_pattern_tables[i].get(h);
      if (!entry)
        continue;
      if (!m_primary) {
        m_primary = entry;
        m_primary_idx = i;
      } else {
        m_backup = entry;
        break;
      }
    }

    // gshare entry
    const auto h = m_history_hash[m_gshare_hash_idx];
    m_gshare = m_gshare_table.get(h);

    // compute alternate predictor: if backup exists -> backup else gshare
    m_alt = m_backup ? m_backup : m_gshare;

    // If we have a primary and it's strong -> use primary
    if (m_primary && m_primary->is_strong_prediction()) {
      m_used = USED_PRIMARY;
      return m_primary->prediction();
    }

    // If primary weak, consult meta-predictor
    if (m_primary && m_alt) {
      bool choose_primary = !m_alt->is_strong_prediction();
      if (choose_primary) {
        m_used = USED_PRIMARY;
        return m_primary->prediction();
      } else {
        // choose alternate (backup/gshare)
        if (m_alt == m_backup) {
          m_used = USED_BACKUP;
          return m_backup->prediction();
        } else {
          m_used = USED_GSHARE;
          return m_gshare->prediction();
        }
      }
    }

    // No primary: prefer backup if strong
    if (m_backup && m_backup->is_strong_prediction()) {
      m_used = USED_BACKUP;
      return m_backup->prediction();
    }

    // fallback to gshare
    m_used = USED_GSHARE;
    return m_gshare ? m_gshare->prediction() : false;
  };

  void UpdatePredictor(u32 PC, bool resolveDir, bool predDir,
                       u32 branchTarget) {
    // --- IMPORTANT: use pre-update history for allocation & updates ---
    // Recompute hashes for the current (pre-update) history
    set_history_hash(PC);

    // Re-find primary / backup / gshare using the same logic as GetPrediction
    m_primary = nullptr;
    m_backup = nullptr;
    for (usize i = m_pattern_tables.size(); i-- > 0;) {
      const auto h = m_history_hash[i];
      auto entry = m_pattern_tables[i].get(h);
      if (!entry)
        continue;
      if (!m_primary) {
        m_primary = entry;
        m_primary_idx = i;
      } else {
        m_backup = entry;
        break;
      }
    }
    const auto h = m_history_hash[m_gshare_hash_idx];
    m_gshare = m_gshare_table.get(h);
    m_alt = m_backup ? m_backup : m_gshare;

    // Save the predictions that were made at prediction time (pre-update)
    bool prim_pred = m_primary ? m_primary->prediction() : false;
    bool backup_pred = m_backup ? m_backup->prediction() : false;
    bool gshare_pred = m_gshare ? m_gshare->prediction() : false;
    bool alt_pred = m_alt ? m_alt->prediction() : false;

    // Allocate only on misprediction of the primary (using pre-update hashes)
    if (m_primary == nullptr || (m_primary && (prim_pred != resolveDir) &&
                                 m_primary_idx + 1 < m_pattern_tables.size())) {
      auto idx = m_primary_idx + 1;
      m_pattern_tables[idx].allocate(m_history_hash[idx], resolveDir);
    }

    // Whether primary and alt disagreed at prediction time
    bool alt_disagreed = (m_primary && m_alt) ? (prim_pred != alt_pred) : false;

    // Update the entries using the saved pre-update predictions
    if (m_primary) {
      bool used = (m_used == USED_PRIMARY);
      m_primary->update(prim_pred, resolveDir, used, alt_disagreed);
    }

    if (m_backup) {
      bool used = (m_used == USED_BACKUP);
      bool alt_disagreed_backup =
          (m_backup && m_primary) ? (backup_pred != prim_pred) : false;
      m_backup->update(backup_pred, resolveDir, used, alt_disagreed_backup);
    }

    if (m_gshare) {
      bool used = (m_used == USED_GSHARE);
      bool alt_disagreed_gshare =
          (m_gshare && m_primary) ? (gshare_pred != prim_pred) : false;
      m_gshare->update(gshare_pred, resolveDir, used, alt_disagreed_gshare);
    }

    // Periodic usefulness decay (unchanged)
    ++m_branch_since_decay;
    if (m_branch_since_decay >= UBIT_DECAY_PERIOD) {
      m_branch_since_decay = 0;
      for (auto &e : m_gshare_table.entries())
        e.decrement_usefulness();
      for (auto &pt : m_pattern_tables)
        for (auto &e : pt.entries())
          e.decrement_usefulness();
    }

    // Now update the global history (post-update)
    m_history.update(resolveDir);
  };

  void TrackOtherInst(u32 PC, OpType opType, u32 branchTarget) { return; };
};

#endif
