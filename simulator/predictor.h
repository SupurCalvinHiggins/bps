#if defined(BPS_USE_GAG)
#include "gag.h"
#elif defined(BPS_USE_PAG)
#include "pag.h"
#else
#error "no predictor defined"
#endif
