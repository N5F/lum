#include "lum.h"
#include "lauxlib.h"

static int id (lum_State *L) {
  return lum_gettop(L);
}


static const struct lumL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


/* function used by lib11.c */
LUMMOD_API int lib1_export (lum_State *L) {
  lum_pushstring(L, "exported");
  return 1;
}


LUMMOD_API int onefunction (lum_State *L) {
  lumL_checkversion(L);
  lum_settop(L, 2);
  lum_pushvalue(L, 1);
  return 2;
}


LUMMOD_API int anotherfunc (lum_State *L) {
  lumL_checkversion(L);
  lum_pushfstring(L, "%d%%%d\n", (int)lum_tointeger(L, 1),
                                 (int)lum_tointeger(L, 2));
  return 1;
} 


LUMMOD_API int lumopen_lib1_sub (lum_State *L) {
  lum_setglobal(L, "y");  /* 2nd arg: extra value (file name) */
  lum_setglobal(L, "x");  /* 1st arg: module name */
  lumL_newlib(L, funcs);
  return 1;
}

