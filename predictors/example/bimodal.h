#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tracer.h"
#include "utils.h"

// BIMODAL PREDICTOR example for ELE 548

/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 32KB
// Total PHT counters: 2^17
// Total PHT size = 2^17 * 2 bits/counter = 2^18 bits = 32KB
// Total Size = PHT size
/////////////////////////////////////////////////////////////

// change the following numbers for gshare predictor size

#define PHT_CTR_MAX 3
#define PHT_CTR_INIT 2

#define BIMODAL_TABLE_SIZE (1 << 17)

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

class PREDICTOR {

  // The state is defined for bimodal predictor, change for your design

private:
  UINT32 *pht;          // pattern history table
  UINT32 numPhtEntries; // entries in pht

public:
  // The interface to the functions below CAN NOT be changed

  PREDICTOR(void) {

    numPhtEntries = BIMODAL_TABLE_SIZE;

    pht = new UINT32[numPhtEntries];

    for (UINT32 i = 0; i < numPhtEntries; i++) {
      pht[i] = PHT_CTR_INIT;
    }
  };

  bool GetPrediction(UINT32 PC) {

    UINT32 phtIndex = PC % (numPhtEntries);
    UINT32 phtCounter = pht[phtIndex];

    if (phtCounter > (PHT_CTR_MAX / 2)) {
      return TAKEN;
    } else {
      return NOT_TAKEN;
    }
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {

    UINT32 phtIndex = PC % (numPhtEntries);
    UINT32 phtCounter = pht[phtIndex];

    if (resolveDir == TAKEN) {
      pht[phtIndex] = SatIncrement(phtCounter, PHT_CTR_MAX);
    } else {
      pht[phtIndex] = SatDecrement(phtCounter);
    }
  };

  void TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget) {

    // Do not use this for ELE 548 homework

    return;
  };

  // Contestants can define their own functions below
};

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

/***********************************************************/
#endif
