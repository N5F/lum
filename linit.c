/*
** $Id: linit.c $
** Initialization of libraries for lum.c and other clients
** See Copyright Notice in lum.h
*/


#define linit_c
#define LUM_LIB


#include "lprefix.h"


#include <stddef.h>

#include "lum.h"

#include "lumlib.h"
#include "lauxlib.h"
#include "llimits.h"


/*
** Standard Libraries. (Must be listed in the same ORDER of their
** respective constants LUM_<libname>K.)
*/
static const lumL_Reg stdlibs[] = {
  {LUM_GNAME, lumopen_base},
  {LUM_LOADLIBNAME, lumopen_package},
  {LUM_COLIBNAME, lumopen_coroutine},
  {LUM_DBLIBNAME, lumopen_debug},
  {LUM_IOLIBNAME, lumopen_io},
  {LUM_MATHLIBNAME, lumopen_math},
  {LUM_OSLIBNAME, lumopen_os},
  {LUM_STRLIBNAME, lumopen_string},
  {LUM_TABLIBNAME, lumopen_table},
  {LUM_UTF8LIBNAME, lumopen_utf8},
  {NULL, NULL}
};


/*
** require and preload selected standard libraries
*/
LUMLIB_API void lumL_openselectedlibs (lum_State *L, int load, int preload) {
  int mask;
  const lumL_Reg *lib;
  lumL_getsubtable(L, LUM_REGISTRYINDEX, LUM_PRELOAD_TABLE);
  for (lib = stdlibs, mask = 1; lib->name != NULL; lib++, mask <<= 1) {
    if (load & mask) {  /* selected? */
      lumL_requiref(L, lib->name, lib->func, 1);  /* require library */
      lum_pop(L, 1);  /* remove result from the stack */
    }
    else if (preload & mask) {  /* selected? */
      lum_pushcfunction(L, lib->func);
      lum_setfield(L, -2, lib->name);  /* add library to PRELOAD table */
    }
  }
  lum_assert((mask >> 1) == LUM_UTF8LIBK);
  lum_pop(L, 1);  /* remove PRELOAD table */
}

