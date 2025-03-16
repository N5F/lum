/*
** $Id: lcorolib.c $
** Coroutine Library
** See Copyright Notice in lum.h
*/

#define lcorolib_c
#define LUM_LIB

#include "lprefix.h"


#include <stdlib.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


static lum_State *getco (lum_State *L) {
  lum_State *co = lum_tothread(L, 1);
  lumL_argexpected(L, co, 1, "thread");
  return co;
}


/*
** Resumes a coroutine. Returns the number of results for non-error
** cases or -1 for errors.
*/
static int auxresume (lum_State *L, lum_State *co, int narg) {
  int status, nres;
  if (l_unlikely(!lum_checkstack(co, narg))) {
    lum_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  lum_xmove(L, co, narg);
  status = lum_resume(co, L, narg, &nres);
  if (l_likely(status == LUM_OK || status == LUM_YIELD)) {
    if (l_unlikely(!lum_checkstack(L, nres + 1))) {
      lum_pop(co, nres);  /* remove results anyway */
      lum_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    lum_xmove(co, L, nres);  /* move yielded values */
    return nres;
  }
  else {
    lum_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}


static int lumB_coresume (lum_State *L) {
  lum_State *co = getco(L);
  int r;
  r = auxresume(L, co, lum_gettop(L) - 1);
  if (l_unlikely(r < 0)) {
    lum_pushboolean(L, 0);
    lum_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lum_pushboolean(L, 1);
    lum_insert(L, -(r + 1));
    return r + 1;  /* return true + 'resume' returns */
  }
}


static int lumB_auxwrap (lum_State *L) {
  lum_State *co = lum_tothread(L, lum_upvalueindex(1));
  int r = auxresume(L, co, lum_gettop(L));
  if (l_unlikely(r < 0)) {  /* error? */
    int stat = lum_status(co);
    if (stat != LUM_OK && stat != LUM_YIELD) {  /* error in the coroutine? */
      stat = lum_closethread(co, L);  /* close its tbc variables */
      lum_assert(stat != LUM_OK);
      lum_xmove(co, L, 1);  /* move error message to the caller */
    }
    if (stat != LUM_ERRMEM &&  /* not a memory error and ... */
        lum_type(L, -1) == LUM_TSTRING) {  /* ... error object is a string? */
      lumL_where(L, 1);  /* add extra info, if available */
      lum_insert(L, -2);
      lum_concat(L, 2);
    }
    return lum_error(L);  /* propagate error */
  }
  return r;
}


static int lumB_cocreate (lum_State *L) {
  lum_State *NL;
  lumL_checktype(L, 1, LUM_TFUNCTION);
  NL = lum_newthread(L);
  lum_pushvalue(L, 1);  /* move function to top */
  lum_xmove(L, NL, 1);  /* move function from L to NL */
  return 1;
}


static int lumB_cowrap (lum_State *L) {
  lumB_cocreate(L);
  lum_pushcclosure(L, lumB_auxwrap, 1);
  return 1;
}


static int lumB_yield (lum_State *L) {
  return lum_yield(L, lum_gettop(L));
}


#define COS_RUN		0
#define COS_DEAD	1
#define COS_YIELD	2
#define COS_NORM	3


static const char *const statname[] =
  {"running", "dead", "suspended", "normal"};


static int auxstatus (lum_State *L, lum_State *co) {
  if (L == co) return COS_RUN;
  else {
    switch (lum_status(co)) {
      case LUM_YIELD:
        return COS_YIELD;
      case LUM_OK: {
        lum_Debug ar;
        if (lum_getstack(co, 0, &ar))  /* does it have frames? */
          return COS_NORM;  /* it is running */
        else if (lum_gettop(co) == 0)
            return COS_DEAD;
        else
          return COS_YIELD;  /* initial state */
      }
      default:  /* some error occurred */
        return COS_DEAD;
    }
  }
}


static int lumB_costatus (lum_State *L) {
  lum_State *co = getco(L);
  lum_pushstring(L, statname[auxstatus(L, co)]);
  return 1;
}


static int lumB_yieldable (lum_State *L) {
  lum_State *co = lum_isnone(L, 1) ? L : getco(L);
  lum_pushboolean(L, lum_isyieldable(co));
  return 1;
}


static int lumB_corunning (lum_State *L) {
  int ismain = lum_pushthread(L);
  lum_pushboolean(L, ismain);
  return 2;
}


static int lumB_close (lum_State *L) {
  lum_State *co = getco(L);
  int status = auxstatus(L, co);
  switch (status) {
    case COS_DEAD: case COS_YIELD: {
      status = lum_closethread(co, L);
      if (status == LUM_OK) {
        lum_pushboolean(L, 1);
        return 1;
      }
      else {
        lum_pushboolean(L, 0);
        lum_xmove(co, L, 1);  /* move error message */
        return 2;
      }
    }
    default:  /* normal or running coroutine */
      return lumL_error(L, "cannot close a %s coroutine", statname[status]);
  }
}


static const lumL_Reg co_funcs[] = {
  {"create", lumB_cocreate},
  {"resume", lumB_coresume},
  {"running", lumB_corunning},
  {"status", lumB_costatus},
  {"wrap", lumB_cowrap},
  {"yield", lumB_yield},
  {"isyieldable", lumB_yieldable},
  {"close", lumB_close},
  {NULL, NULL}
};



LUMMOD_API int lumopen_coroutine (lum_State *L) {
  lumL_newlib(L, co_funcs);
  return 1;
}

