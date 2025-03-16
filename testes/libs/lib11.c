#include "lum.h"

/* function from lib1.c */
int lib1_export (lum_State *L);

LUMMOD_API int lumopen_lib11 (lum_State *L) {
  return lib1_export(L);
}


