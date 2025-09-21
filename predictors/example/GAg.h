#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tracer.h"
#include "utils.h"

// GAg PREDICTOR example for ELE 548

/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 64KB + 18 bits
// Total PHT counters: 2^18
// Total PHT size = 2^18 * 2 bits/counter = 2^19 bits = 64KB
// GHR size: 18 bits
// Total Size = PHT size + GHR size
/////////////////////////////////////////////////////////////

// change the following numbers for gshare predictor size

#define PHT_CTR_MAX 3
#define PHT_CTR_INIT 2

#define HIST_LEN 18

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

class PREDICTOR {

  // The state is defined for Gshare, change for your design

private:
  UINT32 ghr;           // global history register
  UINT32 *pht;          // pattern history table
  UINT32 historyLength; // history length
  UINT32 numPhtEntries; // entries in pht

public:
  // The interface to the functions below CAN NOT be changed

  PREDICTOR(void) {

    historyLength = HIST_LEN;
    ghr = 0;
    numPhtEntries = (1 << HIST_LEN);

    pht = new UINT32[numPhtEntries];

    for (UINT32 i = 0; i < numPhtEntries; i++) {
      pht[i] = PHT_CTR_INIT;
    }
  };

  bool GetPrediction(UINT32 PC) {

    UINT32 phtIndex = ghr % (numPhtEntries);
    UINT32 phtCounter = pht[phtIndex];

    //  printf(" ghr: %x index: %x counter: %d prediction: %d\n", ghr, phtIndex,
    //  phtCounter, phtCounter > PHT_CTR_MAX/2);

    if (phtCounter > (PHT_CTR_MAX / 2)) {
      return TAKEN;
    } else {
      return NOT_TAKEN;
    }
  };

  void UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir,
                       UINT32 branchTarget) {

    UINT32 phtIndex = ghr % (numPhtEntries);
    UINT32 phtCounter = pht[phtIndex];

    if (resolveDir == TAKEN) {
      pht[phtIndex] = SatIncrement(phtCounter, PHT_CTR_MAX);
    } else {
      pht[phtIndex] = SatDecrement(phtCounter);
    }

    // update the GHR
    ghr = (ghr << 1);

    if (resolveDir == TAKEN) {
      ghr++;
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
