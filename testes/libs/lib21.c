#include "lum.h"


int lumopen_lib2 (lum_State *L);

LUMMOD_API int lumopen_lib21 (lum_State *L) {
  return lumopen_lib2(L);
}


