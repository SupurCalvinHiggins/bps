#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>

#include "tracer.h"
#include "utils.h"

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

// Saturating counter initial value.
#define CNT_INIT ((CNT_MAX >> 1) + 1)

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
  std::unique_ptr<UINT32[]> pht;

  inline UINT32 hash(UINT32 x) const { return x % PHT_SIZE; }

public:
  PREDICTOR(void) {
    ghr = 0;
    pht = std::make_unique<UINT32[]>(PHT_SIZE);
    for (UINT32 i = 0; i < PHT_SIZE; ++i) {
      pht[i] = CNT_INIT;
    }
  };

  bool GetPrediction(UINT32 PC) {
    const auto idx = hash(ghr);
    const bool pred = pht[idx] > (CNT_MAX >> 1);
    return pred;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    const auto idx = hash(ghr);
    if (resolveDir) {
      pht[idx] = SatIncrement(pht[idx], CNT_MAX);
    } else {
      pht[idx] = SatDecrement(pht[idx]);
    }

    ghr <<= 1;
    ghr &= (1 << GHR_SIZE) - 1;
    ghr += resolveDir;
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
