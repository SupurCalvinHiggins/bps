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

// PAg
// Index a pattern history table with a per-address history register.

// Whether or not to hash the PC before indexing the local history reigster
// table.
#ifndef USE_HASH
#define USE_HASH 1
#endif

// Local history register bits.
#ifndef LHR_BITS
#define LHR_BITS (2)
static_assert(LHR_BITS <= 32, "invalid lhr size");
#endif

// Bits to index local history table.
#ifndef LHT_BITS
#define LHT_BITS (10)
#endif

// Local history table entries.
#ifndef LHT_SIZE
#define LHT_SIZE (1 << LHT_BITS)
#endif

// Saturating counter bits.
#ifndef CNT_BITS
#define CNT_BITS (2)
static_assert(CNT_BITS <= 32, "invalid cnt size");
#endif

// Saturating counter maximum.
#define CNT_MAX ((1 << CNT_BITS) - 1)

// Saturating counter initial value.
#define CNT_INIT ((CNT_MAX >> 1) + 1)

// Pattern history table entries.
#ifndef PHT_SIZE
#define PHT_SIZE (1 << LHR_BITS)
#endif

// Total bits.
#define SIZE (LHR_BITS * LHT_SIZE + CNT_BITS * PHT_SIZE)
#define MAX_SIZE (1 << 19)
static_assert(SIZE <= MAX_SIZE, "storage budget exceeded");

inline uint32_t MurmurHash3_mix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

class PREDICTOR {

private:
  std::unique_ptr<UINT32[]> lht;
  std::unique_ptr<UINT32[]> pht;

public:
  PREDICTOR(void) {
    lht = std::make_unique<UINT32[]>(LHT_SIZE);
    for (UINT32 i = 0; i < LHT_SIZE; ++i)
      lht[i] = 0;

    pht = std::make_unique<UINT32[]>(PHT_SIZE);
    for (UINT32 i = 0; i < PHT_SIZE; ++i)
      pht[i] = CNT_INIT;
  };

  bool GetPrediction(UINT32 PC) {
    // Read local history.
    if (USE_HASH)
      PC = MurmurHash3_mix32(PC);
    const auto lht_idx = PC % LHT_SIZE;
    const auto lhr = lht[lht_idx];
    // Map pattern to prediction.
    const bool pred = pht[lhr] > (CNT_MAX >> 1);
    return pred;
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {
    if (USE_HASH)
      PC = MurmurHash3_mix32(PC);
    const auto lht_idx = PC % LHT_SIZE;
    auto lhr = lht[lht_idx];

    // Update pattern predictor.
    if (resolveDir) {
      pht[lhr] = SatIncrement(pht[lhr], CNT_MAX);
    } else {
      pht[lhr] = SatDecrement(pht[lhr]);
    }

    // Update local history.
    lhr <<= 1;
    lhr &= (1 << LHR_BITS) - 1;
    lhr += resolveDir;
    lht[lht_idx] = lhr;
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {
    return;
  };
};

#endif
