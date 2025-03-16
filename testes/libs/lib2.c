#include "lum.h"
#include "lauxlib.h"

static int id (lum_State *L) {
  return lum_gettop(L);
}


static const struct lumL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


LUMMOD_API int lumopen_lib2 (lum_State *L) {
  lum_settop(L, 2);
  lum_setglobal(L, "y");  /* y gets 2nd parameter */
  lum_setglobal(L, "x");  /* x gets 1st parameter */
  lumL_newlib(L, funcs);
  return 1;
}


