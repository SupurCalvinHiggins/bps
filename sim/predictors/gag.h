#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tracer.h"
#include "utils.h"

/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 32KB + 17 bits
// Total PHT counters: 2^17
// Total PHT size = 2^17 * 2 bits/counter = 2^18 bits = 32KB
// GHR size: 17 bits
// Total Size = PHT size + GHR size
/////////////////////////////////////////////////////////////

// GAg
// Index a pattern history table with a global history register.

// Global history register bits.
#ifndef GHR_SIZE
#define GHR_SIZE (17)
static_assert(GHR_SIZE <= 32, "invalid ghr size");
#endif

// Saturating counter bits.
#ifndef CNT_SIZE
#define CNT_SIZE 2
static_assert(CNT_SIZE <= 32, "invalid cnt size");
#endif

// Saturating counter maximum.
#define CNT_MAX ((1 << CNT_SIZE) - 1)

// Pattern history table entries.
#ifndef PHT_SIZE
#define PHT_SIZE (1 << GHR_SIZE)
#endif

// Total bits.
#define SIZE (GHR_SIZE + PHT_SIZE * CNT_SIZE)
#define MAX_SIZE (1 << 19)
static_assert(SIZE <= MAX_SIZE, "storage budget exceeded");

class PREDICTOR {

private:
  UINT32 ghr;
  UINT32 pht[PHT_SIZE];

  inline UINT32 hash(UINT32 pc) const { return pc & (PHT_SIZE - 1); }

public:
  PREDICTOR(void) {
    ghr = 0;
    for (UINT32 i = 0; i < PHT_SIZE; ++i) {
      pht[i] = 0;
    }
  };

  bool GetPrediction(UINT32 PC) {
    const auto idx = hash(PC);
    const auto pred = pht[idx] > (CNT_MAX >> 1);
    return pred;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    const auto idx = hash(PC);
    if (predDir) {
      pht[idx] = SatIncrement(pht[idx], CNT_MAX);
    } else {
      pht[idx] = SatDecrement(pht[idx]);
    }

    ghr <<= 1;
    ghr += predDir;
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
