#if defined(BPS_USE_GAG)
#include "gag.h"
#elif defined(BPS_USE_PAG)
#include "pag.h"
#elif defined(BPS_USE_TBP)
#include "tbp.h"
#elif defined(BPS_USE_HP)
#include "hp.h"
#elif defined(BPS_USE_CUSTOM)
#include "custom.h"
#else
#error "no predictor defined"
#endif
