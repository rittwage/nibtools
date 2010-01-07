#define OPENCBM_42
#include "opencbm.h"
#define delay(x)  Sleep(x)
#define msleep(x) Sleep(x/1000)
#define unlink(x) _unlink(x)

#define ARCH_MAINDECL __cdecl
#define ARCH_SIGNALDECL __cdecl
