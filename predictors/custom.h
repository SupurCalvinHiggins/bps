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
using u64 = uint64_t;
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

#ifndef HISTORY_COUNT
#define HISTORY_COUNT (15)
#endif

#ifndef HISTORY_GEO_START
#define HISTORY_GEO_START (3)
#endif

#ifndef HISTORY_GEO_FACTOR
#define HISTORY_GEO_FACTOR (1.4)
#endif

#define HISTORY_GEO(i) (HISTORY_GEO_START * apow(HISTORY_GEO_FACTOR, (i)))
#define HISTORY_GEO_END (HISTORY_GEO(HISTORY_COUNT - 2))

#ifndef PC_HISTORY_THRESHOLD
#define PC_HISTORY_THRESHOLD 17
#endif

#ifndef WEIGHT_TABLE_LEN
#define WEIGHT_TABLE_LEN (4096)
#endif

#ifndef THRESHOLD
#define THRESHOLD (1)
#endif

#ifndef UPDATE_THRESHOLD_INIT
#define UPDATE_THRESHOLD_INIT (10)
#endif

#ifndef UPDATE_THRESHOLD_SPEED
#define UPDATE_THRESHOLD_SPEED (18)
#endif

#define SIZE (8 * HISTORY_COUNT * WEIGHT_TABLE_LEN + HISTORY_GEO_END + 64)

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
  u64 m_pc_history;
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
      auto h = XXH32_digest(state);
      if (end < PC_HISTORY_THRESHOLD) {
        auto ph = m_pc_history;
        if (end < 16)
          ph &= (1ULL << (4 * end)) - 1;
        h ^= ph;
        h = XXH32(&h, 4, 0);
      }
      m_history_hash[i] = h;
      start += bits;
    }
    XXH32_freeState(state);
  }

  std::vector<std::vector<i8>> m_weight_tables;

  i8 m_update_threshold;
  i32 m_update_threshold_count;

  i32 m_prediction;

public:
  PREDICTOR(void)
      : m_history(HISTORY_GEO_END), m_pc_history(0),
        m_history_bits(HISTORY_COUNT, 0), m_history_hash(HISTORY_COUNT, 0),
        m_weight_tables(HISTORY_COUNT, std::vector<i8>(WEIGHT_TABLE_LEN, 0)),
        m_update_threshold(0), m_update_threshold_count(UPDATE_THRESHOLD_INIT),
        m_prediction(0) {
    m_history_bits[0] = 0;
    for (usize i = 1; i < m_history_bits.size(); ++i)
      m_history_bits[i] = HISTORY_GEO(i - 1);
  }

  bool GetPrediction(u32 PC) {
    set_history_hash(PC);
    m_prediction = 0;
    assert(m_weight_tables.size() == m_history_hash.size());
    for (usize i = 0; i < m_weight_tables.size(); ++i) {
      const auto &weight_table = m_weight_tables[i];
      const auto h = m_history_hash[i] % weight_table.size();
      m_prediction += weight_table[h];
    }
    return m_prediction >= THRESHOLD;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    if (resolveDir != predDir || std::abs(m_prediction) < m_update_threshold) {
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
    m_pc_history = (m_pc_history << 4) | (XXH32(&PC, 4, 0) & 0b1111);
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
