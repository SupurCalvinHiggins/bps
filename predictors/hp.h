#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <cmath>
#include <limits>
#include <vector>

#include "tracer.h"
#include "utils.h"
#include "xxhash.h"

inline constexpr int apow(double base, int power) {
  double res = 1.0;
  while (power) {
    res *= base;
    --power;
  }
  return (int)res + 1;
}

#ifndef THRESHOLD
#define THRESHOLD (1)
#endif

#ifndef PWT_BITS
#define PWT_BITS (10)
#endif

#ifndef GHT_LEN
#define GHT_LEN (16)
#endif

#ifndef GHL_GEO_R
#define GHL_GEO_R (1.33)
#endif

#ifndef GHL_GEO_A
#define GHL_GEO_A (3)
#endif

#define PWT_LEN (1 << PWT_BITS)
#define PWT_SIZE (8 * PWT_LEN)

#define GHT_SIZE (PWT_SIZE * GHT_LEN)

#define GHL_LEN (GHT_LEN)
#define GHL_GEO(i) (apow(GHL_GEO_R, (i)) * GHL_GEO_A)

#define GHR_BITS (GHL_GEO(GHT_LEN - 1))
#define GHR_SIZE (GHR_BITS)

#define SIZE (GHT_SIZE + GHR_SIZE)

static_assert(SIZE <= (1 << 19), "predictor too large");

inline int8_t add_sat(const int8_t a, const int8_t b) {
  const auto c = a + b;
  if (a < 0 && b < 0 && c >= 0) {
    return std::numeric_limits<int8_t>::min();
  }
  if (a > 0 && b > 0 && c <= 0) {
    return std::numeric_limits<int8_t>::max();
  }
  return c;
}

class PREDICTOR {
  std::vector<UINT32> m_ghr;
  std::vector<UINT32> m_ghl;
  std::vector<std::vector<int8_t>> m_w;

  void extend(bool shift_in) {
    const auto bits = 8 * sizeof(decltype(m_ghr)::value_type);
    for (std::size_t i = 0; i < m_ghr.size(); ++i) {
      const bool next_shift_in = m_ghr[i] & (1 << (bits - 1));
      m_ghr[i] <<= 1;
      m_ghr[i] |= shift_in;
      shift_in = next_shift_in;
    }
  }

  int8_t &weight(UINT32 i, UINT32 pc) {
    const auto ghl = m_ghl[i];
    const auto ghl_w = ghl >> 5;
    const auto ghl_r = ghl - (ghl_w << 5);

    XXH32_state_t *state = XXH32_createState();
    XXH32_reset(state, 0);
    XXH32_update(state, &pc, 4);
    if (ghl_w)
      XXH32_update(state, m_ghr.data(),
                   sizeof(decltype(m_ghr)::value_type) * ghl_w);
    if (ghl_r) {
      const auto r = m_ghr[ghl_w] & ((1 << ghl_r) - 1);
      XXH32_update(state, &r, sizeof(r));
    }
    const auto h = XXH32_digest(state);
    XXH32_freeState(state);

    return m_w[i][h % PWT_LEN];
  }

public:
  PREDICTOR(void)
      : m_ghr((GHR_BITS + 31) / 32, 0), m_ghl(GHL_LEN),
        m_w(GHT_LEN, std::vector<int8_t>(PWT_LEN, 0)) {
    m_ghl[0] = 0;
    for (std::size_t i = 1; i < GHL_LEN; ++i)
      m_ghl[i] = GHL_GEO(i - 1);
  }

  bool GetPrediction(UINT32 PC) {
    // there are multiple tables, each indexed with a different number of
    // history bits (geometric)
    // for example: 4 tables, the first indexed with 0 history bits, the next
    // with 3 history, then 4 history, then 6 history
    // all tables have the same number of entries
    //
    // combine the history and the PC into an index
    // index every table
    // these are the weights of the perceptron
    // sum the perceptron weights
    // compare against a threshold

    int x = 0;
    for (std::size_t i = 0; i < m_w.size(); ++i)
      x += weight(i, PC);

    return x >= THRESHOLD;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    // if the prediction was incorrect
    // or if the prediction was weak (correct but distance from threshold was
    // low; the weak threshold is dynamically learned)
    // update the weights
    // if taken, increment by one
    // if not taken, decrement by one
    // increase weak threshold after
    if (resolveDir != predDir) {
      const int8_t offset = resolveDir ? 1 : -1;
      for (std::size_t i = 0; i < m_w.size(); ++i) {
        auto &w = weight(i, PC);
        w = add_sat(w, offset);
      }
    }
    extend(resolveDir);
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
