#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <assert.h>
#include <vector>

#include "tracer.h"
#include "utils.h"

// Tournament branch predictor
//
// Local predictor: index a pattern history table with a per-address history
// register.
//
// Global predictor: index a pattern history table with a global history
// register.
//
// Choice predictor: bimodal predictor

#ifndef LHT_BITS
#define LHT_BITS 1
#endif

#ifndef LPT_BITS
#define LPT_BITS 1
#endif

#ifndef GPT_BITS
#define GPT_BITS 1
#endif

#ifndef GHR_BITS
#define GHR_BITS 1
#endif

#ifndef BPT_BITS
#define BPT_BITS 1
#endif

#ifndef LHT_R_BITS
#define LHT_R_BITS 1
#endif

#ifndef LPT_R_BITS
#define LPT_R_BITS 1
#endif

#ifndef GPT_R_BITS
#define GPT_R_BITS 1
#endif

#ifndef BPT_R_BITS
#define BPT_R_BITS 1
#endif

// LHT_R is an index into LPT.
static_assert(LHT_R_BITS >= LPT_BITS, "LPT size is overallocated");

// GHR is an index into GPT.
static_assert(GHR_BITS >= GPT_BITS, "GPT size is overallocated");

#define LHT_R_MAX ((1 << LHT_R_BITS) - 1)
#define LPT_R_MAX ((1 << LPT_R_BITS) - 1)
#define GPT_R_MAX ((1 << GPT_R_BITS) - 1)
#define GHR_MAX ((1 << GHR_BITS) - 1)
#define BPT_R_MAX ((1 << BPT_R_BITS) - 1)

#define LPT_R_THRES ((LPT_R_MAX >> 1) + 1)
#define GPT_R_THRES ((GPT_R_MAX >> 1) + 1)
#define BPT_R_THRES ((BPT_R_MAX >> 1) + 1)

#define LHT_R_INIT (0)
#define LPT_R_INIT (LPT_R_THRES)
#define GPT_R_INIT (GPT_R_THRES)
#define GHR_INIT (0)
#define BPT_R_INIT (BPT_R_THRES)

#define LHT_LEN (1 << LHT_BITS)
#define LPT_LEN (1 << LPT_BITS)
#define GPT_LEN (1 << GPT_BITS)
#define BPT_LEN (1 << BPT_BITS)

#define LHT_SIZE (LHT_LEN * LHT_R_BITS)
#define LPT_SIZE (LPT_LEN * LPT_R_BITS)
#define GPT_SIZE (GPT_LEN * GPT_R_BITS)
#define GHR_SIZE (GHR_BITS)
#define BPT_SIZE (BPT_LEN * BPT_R_BITS)

#define SIZE (LHT_SIZE + LPT_SIZE + GPT_SIZE + GHR_SIZE + BPT_SIZE)

static_assert(SIZE <= (1 << 19), "predictor too large");

inline UINT32 MurmurHash3_mix32(UINT32 h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

class PREDICTOR {
  std::vector<UINT32> m_lht;
  UINT32 m_ghr;
  std::vector<UINT32> m_lpt;
  std::vector<UINT32> m_gpt;
  std::vector<UINT32> m_bpt;

  UINT32 &get(std::vector<UINT32> &v, UINT32 idx) {
    return v[MurmurHash3_mix32(idx) % v.size()];
  }

  UINT32 &lht_get(UINT32 idx) { return get(m_lht, idx); }

  UINT32 &lpt_get(UINT32 idx) { return get(m_lpt, idx); }

  UINT32 &gpt_get(UINT32 idx) { return get(m_gpt, idx); }

  UINT32 &bpt_get(UINT32 idx) { return get(m_bpt, idx); }

  UINT32 extend(UINT32 hr, UINT32 max, UINT32 d) {
    hr <<= 1;
    hr &= max;
    hr += d;
    return hr;
  }

  UINT32 update(UINT32 r, UINT32 max, UINT32 d) {
    return d ? SatIncrement(r, max) : SatDecrement(r);
  }

public:
  PREDICTOR(void)
      : m_lht(LHT_LEN, LHT_R_INIT), m_ghr(GHR_INIT), m_lpt(LPT_LEN, LPT_R_INIT),
        m_gpt(GPT_LEN, GPT_R_INIT), m_bpt(BPT_LEN, BPT_R_INIT) {};

  bool GetPrediction(UINT32 PC) {
    const auto lp = lpt_get(lht_get(PC)) >= LPT_R_THRES;
    const auto gp = gpt_get(m_ghr) >= GPT_R_THRES;
    const auto bp = bpt_get(PC) >= BPT_R_THRES;
    return bp ? gp : lp;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    // update global
    auto &gpt_r = gpt_get(m_ghr);
    const auto gp = gpt_r >= GPT_R_THRES;
    m_ghr = extend(m_ghr, GHR_MAX, resolveDir);
    gpt_r = update(gpt_r, GPT_R_MAX, resolveDir);

    // update local
    auto &lht_r = lht_get(PC);
    auto &lpt_r = lpt_get(lht_r);
    const auto lp = lpt_r >= LPT_R_THRES;
    lht_r = extend(lht_r, LHT_R_MAX, resolveDir);
    lpt_r = update(lpt_r, LPT_R_MAX, resolveDir);

    // update choice
    auto &bpt_r = bpt_get(PC);
    if (gp != lp) {
      // bias towards the right one
      bpt_r = update(bpt_r, BPT_R_MAX, gp == resolveDir);
    }
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
