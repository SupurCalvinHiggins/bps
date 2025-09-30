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
#define HISTORY_COUNT (32)
#endif

#ifndef HISTORY_GEO_START
#define HISTORY_GEO_START (3)
#endif

#ifndef HISTORY_GEO_FACTOR
#define HISTORY_GEO_FACTOR (1.3)
#endif

#define HISTORY_GEO(i) (HISTORY_GEO_START * apow(HISTORY_GEO_FACTOR, (i)))
#define HISTORY_GEO_END (HISTORY_GEO(HISTORY_COUNT - 1))

#define GSHARE_COUNTER_BITS (3)

#ifndef GSHARE_TABLE_BITS
#define GSHARE_TABLE_BITS (15)
#endif

#define GSHARE_TABLE_LEN (1 << GSHARE_TABLE_BITS)

#ifndef GSHARE_HISTORY_BITS
#define GSHARE_HISTORY_BITS (16)
#endif

#ifndef GSHARE_COUNTER_BITS
#define GSHARE_COUNTER_BITS (3)
#endif

#ifndef TAGE_TABLE_BITS
#define TAGE_TABLE_BITS (16)
#endif

#define TAGE_TABLE_LEN (1 << TAGE_TABLE_BITS)

#ifndef TAGE_TAG_BITS
#define TAGE_TAG_BITS (16)
#endif

#define TAGE_PREDICTOR_BITS 3
#define TAGE_USEFUL_BITS 2

#define TAGE_CHOICE_BITS (4)
#define TAGE_DECAY_PERIOD (128000)

#define SIZE                                                                   \
  (GSHARE_COUNTER_BITS * GSHARE_TABLE_LEN +                                    \
   HISTORY_COUNT * TAGE_TABLE_LEN *                                            \
       (TAGE_TAG_BITS + TAGE_USEFUL_BITS + TAGE_PREDICTOR_BITS + 1) +          \
   HISTORY_GEO_END + 16 + TAGE_CHOICE_BITS)

// static_assert(SIZE <= 1 << 19, "predictor too large");

template <usize N> class Counter {
  u8 m_data;

  static constexpr const u8 STRONG_YES = (1 << N) - 1;
  static constexpr const u8 STRONG_NO = 0;
  static constexpr const u8 WEAK_YES = 1 << (N - 1);
  static constexpr const u8 WEAK_NO = (1 << (N - 1)) - 1;

public:
  Counter(bool is_yes, bool is_strong) {
    if (is_yes && is_strong) {
      m_data = STRONG_YES;
    } else if (is_yes && !is_strong) {
      m_data = WEAK_YES;
    } else if (!is_yes && is_strong) {
      m_data = STRONG_NO;
    } else {
      m_data = WEAK_NO;
    }
  }

  bool yes() const { return m_data >= WEAK_YES; }
  bool no() const { return !yes(); };

  bool strong() const { return m_data >= STRONG_YES || m_data <= STRONG_NO; }
  bool weak() const { return !strong(); }

  void increment() {
    if (m_data == STRONG_YES)
      return;
    ++m_data;
  }

  void decrement() {
    if (m_data == STRONG_NO)
      return;
    --m_data;
  }
};

using TageCounter = Counter<TAGE_PREDICTOR_BITS>;

struct TageEntry {
  TageCounter predictor;
  Counter<TAGE_USEFUL_BITS> useful;
  bool valid;
  u32 tag;
};

class TageTable {
  std::vector<TageEntry> m_data;

public:
  TageTable()
      : m_data(TAGE_TABLE_LEN, {{true, false}, {false, true}, false, 0}) {}

  bool allocate(u32 hash, bool is_yes, bool is_strong) {
    const auto tag = (hash >> TAGE_TABLE_BITS) & ((1 << TAGE_TAG_BITS) - 1);
    const auto idx = hash & ((1 << TAGE_TABLE_BITS) - 1);
    assert(idx < m_data.size());
    auto &entry = m_data[idx];
    auto &useful = entry.useful;
    if (entry.valid && (useful.yes() || useful.weak())) {
      useful.decrement();
      return false;
    }
    entry = TageEntry({{is_yes, is_strong}, {false, true}, true, tag});
    return true;
  }

  void decay() {
    for (auto &entry : m_data)
      entry.useful.decrement();
  }

  TageEntry *get(u32 hash) {
    const auto tag = (hash >> TAGE_TABLE_BITS) & ((1 << TAGE_TAG_BITS) - 1);
    const auto idx = hash & ((1 << TAGE_TABLE_BITS) - 1);
    assert(idx < m_data.size());
    auto &entry = m_data[idx];
    return entry.valid && entry.tag == tag ? &entry : nullptr;
  }
};

using GShareCounter = Counter<GSHARE_COUNTER_BITS>;

class GShareTable {
  std::vector<GShareCounter> m_data;

public:
  GShareTable() : m_data(GSHARE_TABLE_LEN, {true, false}) {}

  GShareCounter *get(u32 hash) {
    const auto idx = hash & ((1 << GSHARE_TABLE_BITS) - 1);
    return &m_data[idx];
  }
};

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

class PREDICTOR {

  GShareTable m_gshare_table;
  u32 m_gshare_hash;

  std::vector<TageTable> m_tage_tables;

  History m_history;
  std::vector<u32> m_history_bits;
  std::vector<u32> m_history_hash;

  using ChoiceCounter = Counter<TAGE_CHOICE_BITS>;
  ChoiceCounter m_use_primary;

  u32 m_branch_since_decay;

  void set_hash(u32 pc) {
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

    XXH32_reset(state, 0);
    XXH32_update(state, &pc, 4);
    m_history.hash(state, 0, GSHARE_HISTORY_BITS);
    m_gshare_hash = XXH32_digest(state);
    XXH32_freeState(state);
  }

  std::tuple<TageEntry *, TageEntry *, usize, usize> find_primary_alt() {
    TageEntry *primary = nullptr;
    TageEntry *alt = nullptr;
    usize primary_idx = 0;
    usize alt_idx = 0;

    for (usize i = m_tage_tables.size(); i-- > 0;) {
      const auto h = m_history_hash[i];
      auto entry = m_tage_tables[i].get(h);
      if (!entry)
        continue;
      if (!primary) {
        primary = entry;
        primary_idx = i;
      } else {
        alt = entry;
        alt_idx = i;
        break;
      }
    }

    assert(!primary || primary->valid);
    assert(!alt || alt->valid);
    return std::make_tuple(primary, alt, primary_idx, alt_idx);
  }

public:
  PREDICTOR(void)
      : m_tage_tables(HISTORY_COUNT), m_history(HISTORY_GEO_END),
        m_history_bits(HISTORY_COUNT, 0), m_history_hash(HISTORY_COUNT, 0),
        m_use_primary(false, false), m_branch_since_decay(0) {
    for (usize i = 0; i < HISTORY_COUNT; ++i)
      m_history_bits[i] = static_cast<usize>(HISTORY_GEO(i));
  }

  bool GetPrediction(u32 PC) {
    set_hash(PC);

    auto [primary, alt, primary_idx, alt_idx] = find_primary_alt();
    GShareCounter *gshare = m_gshare_table.get(m_gshare_hash);
    assert(gshare != nullptr);

    // If the primary exists, and the primary is not newly allocated or we
    // should use it, use it.
    if (primary && (primary->useful.yes() || primary->useful.weak() ||
                    m_use_primary.yes())) {
      return primary->predictor.yes();
    }

    // Otherwise, we should use the alternate. If the alternate exists, use it.
    if (alt) {
      return alt->predictor.yes();
    }

    // If the alternate does not exist, use gshare.
    return gshare->yes();
  };

  void UpdatePredictor(u32 PC, bool resolveDir, bool predDir,
                       u32 branchTarget) {
    auto [primary, alt, primary_idx, alt_idx] = find_primary_alt();
    GShareCounter *gshare = m_gshare_table.get(m_gshare_hash);
    assert(gshare != nullptr);

    TageEntry *used = nullptr;
    i32 used_idx = -1;
    if (primary && (primary->useful.yes() || primary->useful.weak() ||
                    m_use_primary.yes())) {
      used = primary;
      used_idx = primary_idx;
    } else if (alt) {
      used = alt;
      used_idx = alt_idx;
    }

    if (primary) {
      auto primary_pred = primary->predictor.yes();
      auto alt_pred = alt ? alt->predictor.yes() : gshare->yes();
      if (primary_pred != alt_pred && primary->useful.no() &&
          primary->useful.strong()) {
        if (primary_pred == resolveDir)
          m_use_primary.increment();
        else
          m_use_primary.decrement();
      }

      if (primary_pred != alt_pred) {
        if (primary_pred == resolveDir) {
          primary->useful.increment();
          if (alt)
            alt->useful.decrement();
        } else {
          primary->useful.decrement();
          if (alt)
            alt->useful.increment();
        }
      }
    }

    if (used) {
      if (resolveDir)
        used->predictor.increment();
      else
        used->predictor.decrement();
    } else {
      // we used gshare
      if (resolveDir)
        gshare->increment();
      else
        gshare->decrement();
    }

    // If we did not use the last table, try to allocate.
    if (predDir != resolveDir && used_idx != HISTORY_COUNT - 1) {
      for (usize i = used_idx + 1; i < HISTORY_COUNT; ++i) {
        if (m_tage_tables[i].allocate(m_history_hash[i], resolveDir, false))
          break;
      }
    }

    ++m_branch_since_decay;
    if (m_branch_since_decay >= TAGE_DECAY_PERIOD) {
      for (auto &table : m_tage_tables)
        table.decay();
      m_branch_since_decay = 0;
    }

    m_history.update(resolveDir);
  };

  void TrackOtherInst(u32 PC, OpType opType, u32 branchTarget) { return; };
};

#endif
