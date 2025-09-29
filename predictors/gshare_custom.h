#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "tracer.h"
#include "utils.h"
#include "xxhash.h"

using u8 = uint8_t;
using u32 = uint32_t;
using i8 = int8_t;
using i32 = int32_t;
using f64 = double;
using usize = size_t;

inline constexpr i32 apow(const f64 base, i32 power) {
  f64 result = 1.0;
  while (power--)
    result *= base;
  return static_cast<i32>(result) + 1;
}

inline i8 add_sat(const i8 a, const i8 b) {
  const int v = static_cast<int>(a) + static_cast<int>(b);
  if (v > std::numeric_limits<int8_t>::max())
    return std::numeric_limits<int8_t>::max();
  if (v < std::numeric_limits<int8_t>::min())
    return std::numeric_limits<int8_t>::min();
  return v;
}

#define HISTORY_COUNT (15)
#define HISTORY_GEO_START (3)
#define HISTORY_GEO_FACTOR (1.4)
#define HISTORY_GEO(i) (HISTORY_GEO_START * apow(HISTORY_GEO_FACTOR, (i)))
#define HISTORY_GEO_END (HISTORY_GEO(HISTORY_COUNT - 2))

#define WEIGHT_TABLE_LEN (4096)

#define THRESHOLD (0)
#define UPDATE_THRESHOLD_INIT (10)
#define UPDATE_THRESHOLD_SPEED (18)

#define GSHARE_LEN (4 * 4096)
#define GSHARE_HISTORY_BITS (16)
#define GSHARE_COUNTER_BITS (3)

#define META_LEN (4 * 4096)
#define META_COUNTER_BITS (2)

#define SIZE (8 * HISTORY_COUNT * WEIGHT_TABLE_LEN + HISTORY_GEO_END)

static_assert(SIZE <= (1 << 19), "predictor too large");

class PREDICTOR {

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

  std::vector<std::vector<i8>> m_weight_tables;

  i8 m_update_threshold;
  i32 m_update_threshold_count;

  i32 m_prediction;

  class Counter {
    usize m_bits;
    u32 m_data;

    usize max() const { return (1 << m_bits) - 1; }
    usize threshold() const { return 1 << (m_bits - 1); }

  public:
    Counter(usize bits, bool weakly_taken)
        : m_bits(bits), m_data(threshold() - !weakly_taken) {}

    void update(bool is_correct) {
      if (is_correct) {
        if (m_data < (1 << m_bits) - 1) {
          m_data += 1;
        }
      } else {
        if (m_data > 0) {
          m_data -= 1;
        }
      }
    }

    bool prediction() const { return m_data >= threshold(); }
    bool is_weak_prediction() const { return !is_strong_prediction(); }
    bool is_strong_prediction() const { return m_data == 0 || m_data == max(); }
  };

  std::vector<Counter> m_gshare;
  u32 m_gshare_hash;

  void set_gshare_hash(u32 pc) {
    XXH32_state_t *state = XXH32_createState();
    XXH32_reset(state, 0);
    XXH32_update(state, &pc, 4);
    assert(GSHARE_HISTORY_BITS <= HISTORY_GEO_END);
    m_history.hash(state, 0, GSHARE_HISTORY_BITS);
    m_gshare_hash = XXH32_digest(state);
    XXH32_freeState(state);
  }

  std::vector<Counter> m_meta;
  u32 m_meta_hash;

  void set_meta_hash(u32 pc) {
    XXH32_state_t *state = XXH32_createState();
    XXH32_reset(state, 0);
    XXH32_update(state, &pc, 4);
    m_meta_hash = XXH32_digest(state);
    XXH32_freeState(state);
  }

public:
  PREDICTOR(void)
      : m_history(HISTORY_GEO_END), m_history_bits(HISTORY_COUNT, 0),
        m_history_hash(HISTORY_COUNT, 0),
        m_weight_tables(HISTORY_COUNT, std::vector<i8>(WEIGHT_TABLE_LEN, 0)),
        m_update_threshold(0), m_update_threshold_count(UPDATE_THRESHOLD_INIT),
        m_prediction(0),
        m_gshare(GSHARE_LEN, Counter(GSHARE_COUNTER_BITS, true)),
        m_gshare_hash(0), m_meta(META_LEN, Counter(META_COUNTER_BITS, true)),
        m_meta_hash(0) {
    m_history_bits[0] = 0;
    for (usize i = 1; i < m_history_bits.size(); ++i)
      m_history_bits[i] = HISTORY_GEO(i - 1);
  }

  bool GetPrediction(u32 PC) {
    set_history_hash(PC);
    set_gshare_hash(PC);
    set_meta_hash(PC);
    m_prediction = 0;
    assert(m_weight_tables.size() == m_history_hash.size());
    for (usize i = 0; i < m_weight_tables.size(); ++i) {
      const auto &weight_table = m_weight_tables[i];
      const auto h = m_history_hash[i] % weight_table.size();
      m_prediction += weight_table[h];
    }

    if (m_meta[m_meta_hash % m_meta.size()].prediction()) {
      return m_prediction >= THRESHOLD;
    } else {
      return m_gshare[m_gshare_hash % m_gshare.size()].prediction();
    }
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {

    auto &gshare_counter = m_gshare[m_gshare_hash % m_gshare.size()];
    auto &meta_counter = m_meta[m_meta_hash % m_meta.size()];

    const auto hp_prediction = m_prediction >= THRESHOLD;
    const auto gshare_prediction = gshare_counter.prediction();

    if (hp_prediction == resolveDir && gshare_prediction != resolveDir) {
      meta_counter.update(true);
    } else if (hp_prediction != resolveDir && gshare_prediction == resolveDir) {
      meta_counter.update(false);
    }

    gshare_counter.update(gshare_counter.prediction() == resolveDir);
    const auto gshare_handles = !meta_counter.prediction() &&
                                gshare_counter.is_strong_prediction() &&
                                gshare_prediction == resolveDir;

    if (!gshare_handles && (resolveDir != predDir ||
                            std::abs(m_prediction) < m_update_threshold)) {
      const i8 offset = resolveDir ? 1 : -1;
      for (usize i = 0; i < m_weight_tables.size(); ++i) {
        auto &weight_table = m_weight_tables[i];
        const auto h = m_history_hash[i] % weight_table.size();
        weight_table[h] = add_sat(weight_table[h], offset);
      }

      if (resolveDir != predDir) {
        m_update_threshold_count += 1;
        if (m_update_threshold_count >= UPDATE_THRESHOLD_SPEED) {
          m_update_threshold_count = 0;
          m_update_threshold = add_sat(m_update_threshold, 1);
        }
      } else {
        m_update_threshold_count -= 1;
        if (m_update_threshold_count <= -UPDATE_THRESHOLD_SPEED) {
          m_update_threshold_count = 0;
          m_update_threshold = add_sat(m_update_threshold, -1);
        }
      }
    }

    m_history.update(resolveDir);
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
