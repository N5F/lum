/*
** $Id: ldblib.c $
** Interface from Lum to its debug API
** See Copyright Notice in lum.h
*/

#define ldblib_c
#define LUM_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


/*
** The hook table at registry[HOOKKEY] maps threads to their current
** hook function.
*/
static const char *const HOOKKEY = "_HOOKKEY";


/*
** If L1 != L, L1 can be in any state, and therefore there are no
** guarantees about its stack space; any push in L1 must be
** checked.
*/
static void checkstack (lum_State *L, lum_State *L1, int n) {
  if (l_unlikely(L != L1 && !lum_checkstack(L1, n)))
    lumL_error(L, "stack overflow");
}


static int db_getregistry (lum_State *L) {
  lum_pushvalue(L, LUM_REGISTRYINDEX);
  return 1;
}


static int db_getmetatable (lum_State *L) {
  lumL_checkany(L, 1);
  if (!lum_getmetatable(L, 1)) {
    lum_pushnil(L);  /* no metatable */
  }
  return 1;
}


static int db_setmetatable (lum_State *L) {
  int t = lum_type(L, 2);
  lumL_argexpected(L, t == LUM_TNIL || t == LUM_TTABLE, 2, "nil or table");
  lum_settop(L, 2);
  lum_setmetatable(L, 1);
  return 1;  /* return 1st argument */
}


static int db_getuservalue (lum_State *L) {
  int n = (int)lumL_optinteger(L, 2, 1);
  if (lum_type(L, 1) != LUM_TUSERDATA)
    lumL_pushfail(L);
  else if (lum_getiuservalue(L, 1, n) != LUM_TNONE) {
    lum_pushboolean(L, 1);
    return 2;
  }
  return 1;
}


static int db_setuservalue (lum_State *L) {
  int n = (int)lumL_optinteger(L, 3, 1);
  lumL_checktype(L, 1, LUM_TUSERDATA);
  lumL_checkany(L, 2);
  lum_settop(L, 2);
  if (!lum_setiuservalue(L, 1, n))
    lumL_pushfail(L);
  return 1;
}


/*
** Auxiliary function used by several library functions: check for
** an optional thread as function's first argument and set 'arg' with
** 1 if this argument is present (so that functions can skip it to
** access their other arguments)
*/
static lum_State *getthread (lum_State *L, int *arg) {
  if (lum_isthread(L, 1)) {
    *arg = 1;
    return lum_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;  /* function will operate over current thread */
  }
}


/*
** Variations of 'lum_settable', used by 'db_getinfo' to put results
** from 'lum_getinfo' into result table. Key is always a string;
** value can be a string, an int, or a boolean.
*/
static void settabss (lum_State *L, const char *k, const char *v) {
  lum_pushstring(L, v);
  lum_setfield(L, -2, k);
}

static void settabsi (lum_State *L, const char *k, int v) {
  lum_pushinteger(L, v);
  lum_setfield(L, -2, k);
}

static void settabsb (lum_State *L, const char *k, int v) {
  lum_pushboolean(L, v);
  lum_setfield(L, -2, k);
}


/*
** In function 'db_getinfo', the call to 'lum_getinfo' may push
** results on the stack; later it creates the result table to put
** these objects. Function 'treatstackoption' puts the result from
** 'lum_getinfo' on top of the result table so that it can call
** 'lum_setfield'.
*/
static void treatstackoption (lum_State *L, lum_State *L1, const char *fname) {
  if (L == L1)
    lum_rotate(L, -2, 1);  /* exchange object and table */
  else
    lum_xmove(L1, L, 1);  /* move object to the "main" stack */
  lum_setfield(L, -2, fname);  /* put object into table */
}


/*
** Calls 'lum_getinfo' and collects all results in a new table.
** L1 needs stack space for an optional input (function) plus
** two optional outputs (function and line table) from function
** 'lum_getinfo'.
*/
static int db_getinfo (lum_State *L) {
  lum_Debug ar;
  int arg;
  lum_State *L1 = getthread(L, &arg);
  const char *options = lumL_optstring(L, arg+2, "flnSrtu");
  checkstack(L, L1, 3);
  lumL_argcheck(L, options[0] != '>', arg + 2, "invalid option '>'");
  if (lum_isfunction(L, arg + 1)) {  /* info about a function? */
    options = lum_pushfstring(L, ">%s", options);  /* add '>' to 'options' */
    lum_pushvalue(L, arg + 1);  /* move function to 'L1' stack */
    lum_xmove(L, L1, 1);
  }
  else {  /* stack level */
    if (!lum_getstack(L1, (int)lumL_checkinteger(L, arg + 1), &ar)) {
      lumL_pushfail(L);  /* level out of range */
      return 1;
    }
  }
  if (!lum_getinfo(L1, options, &ar))
    return lumL_argerror(L, arg+2, "invalid option");
  lum_newtable(L);  /* table to collect results */
  if (strchr(options, 'S')) {
    lum_pushlstring(L, ar.source, ar.srclen);
    lum_setfield(L, -2, "source");
    settabss(L, "short_src", ar.short_src);
    settabsi(L, "linedefined", ar.linedefined);
    settabsi(L, "lastlinedefined", ar.lastlinedefined);
    settabss(L, "what", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "currentline", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "nups", ar.nups);
    settabsi(L, "nparams", ar.nparams);
    settabsb(L, "isvararg", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "name", ar.name);
    settabss(L, "namewhat", ar.namewhat);
  }
  if (strchr(options, 'r')) {
    settabsi(L, "ftransfer", ar.ftransfer);
    settabsi(L, "ntransfer", ar.ntransfer);
  }
  if (strchr(options, 't')) {
    settabsb(L, "istailcall", ar.istailcall);
    settabsi(L, "extraargs", ar.extraargs);
  }
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");
  return 1;  /* return table */
}


static int db_getlocal (lum_State *L) {
  int arg;
  lum_State *L1 = getthread(L, &arg);
  int nvar = (int)lumL_checkinteger(L, arg + 2);  /* local-variable index */
  if (lum_isfunction(L, arg + 1)) {  /* function argument? */
    lum_pushvalue(L, arg + 1);  /* push function */
    lum_pushstring(L, lum_getlocal(L, NULL, nvar));  /* push local name */
    return 1;  /* return only name (there is no value) */
  }
  else {  /* stack-level argument */
    lum_Debug ar;
    const char *name;
    int level = (int)lumL_checkinteger(L, arg + 1);
    if (l_unlikely(!lum_getstack(L1, level, &ar)))  /* out of range? */
      return lumL_argerror(L, arg+1, "level out of range");
    checkstack(L, L1, 1);
    name = lum_getlocal(L1, &ar, nvar);
    if (name) {
      lum_xmove(L1, L, 1);  /* move local value */
      lum_pushstring(L, name);  /* push name */
      lum_rotate(L, -2, 1);  /* re-order */
      return 2;
    }
    else {
      lumL_pushfail(L);  /* no name (nor value) */
      return 1;
    }
  }
}


static int db_setlocal (lum_State *L) {
  int arg;
  const char *name;
  lum_State *L1 = getthread(L, &arg);
  lum_Debug ar;
  int level = (int)lumL_checkinteger(L, arg + 1);
  int nvar = (int)lumL_checkinteger(L, arg + 2);
  if (l_unlikely(!lum_getstack(L1, level, &ar)))  /* out of range? */
    return lumL_argerror(L, arg+1, "level out of range");
  lumL_checkany(L, arg+3);
  lum_settop(L, arg+3);
  checkstack(L, L1, 1);
  lum_xmove(L, L1, 1);
  name = lum_setlocal(L1, &ar, nvar);
  if (name == NULL)
    lum_pop(L1, 1);  /* pop value (if not popped by 'lum_setlocal') */
  lum_pushstring(L, name);
  return 1;
}


/*
** get (if 'get' is true) or set an upvalue from a closure
*/
static int auxupvalue (lum_State *L, int get) {
  const char *name;
  int n = (int)lumL_checkinteger(L, 2);  /* upvalue index */
  lumL_checktype(L, 1, LUM_TFUNCTION);  /* closure */
  name = get ? lum_getupvalue(L, 1, n) : lum_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  lum_pushstring(L, name);
  lum_insert(L, -(get+1));  /* no-op if get is false */
  return get + 1;
}


static int db_getupvalue (lum_State *L) {
  return auxupvalue(L, 1);
}


static int db_setupvalue (lum_State *L) {
  lumL_checkany(L, 3);
  return auxupvalue(L, 0);
}


/*
** Check whether a given upvalue from a given closure exists and
** returns its index
*/
static void *checkupval (lum_State *L, int argf, int argnup, int *pnup) {
  void *id;
  int nup = (int)lumL_checkinteger(L, argnup);  /* upvalue index */
  lumL_checktype(L, argf, LUM_TFUNCTION);  /* closure */
  id = lum_upvalueid(L, argf, nup);
  if (pnup) {
    lumL_argcheck(L, id != NULL, argnup, "invalid upvalue index");
    *pnup = nup;
  }
  return id;
}


static int db_upvalueid (lum_State *L) {
  void *id = checkupval(L, 1, 2, NULL);
  if (id != NULL)
    lum_pushlightuserdata(L, id);
  else
    lumL_pushfail(L);
  return 1;
}


static int db_upvaluejoin (lum_State *L) {
  int n1, n2;
  checkupval(L, 1, 2, &n1);
  checkupval(L, 3, 4, &n2);
  lumL_argcheck(L, !lum_iscfunction(L, 1), 1, "Lum function expected");
  lumL_argcheck(L, !lum_iscfunction(L, 3), 3, "Lum function expected");
  lum_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


/*
** Call hook function registered at hook table for the current
** thread (if there is one)
*/
static void hookf (lum_State *L, lum_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  lum_getfield(L, LUM_REGISTRYINDEX, HOOKKEY);
  lum_pushthread(L);
  if (lum_rawget(L, -2) == LUM_TFUNCTION) {  /* is there a hook function? */
    lum_pushstring(L, hooknames[(int)ar->event]);  /* push event name */
    if (ar->currentline >= 0)
      lum_pushinteger(L, ar->currentline);  /* push current line */
    else lum_pushnil(L);
    lum_assert(lum_getinfo(L, "lS", ar));
    lum_call(L, 2, 0);  /* call hook function */
  }
}


/*
** Convert a string mask (for 'sethook') into a bit mask
*/
static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= LUM_MASKCALL;
  if (strchr(smask, 'r')) mask |= LUM_MASKRET;
  if (strchr(smask, 'l')) mask |= LUM_MASKLINE;
  if (count > 0) mask |= LUM_MASKCOUNT;
  return mask;
}


/*
** Convert a bit mask (for 'gethook') into a string mask
*/
static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & LUM_MASKCALL) smask[i++] = 'c';
  if (mask & LUM_MASKRET) smask[i++] = 'r';
  if (mask & LUM_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (lum_State *L) {
  int arg, mask, count;
  lum_Hook func;
  lum_State *L1 = getthread(L, &arg);
  if (lum_isnoneornil(L, arg+1)) {  /* no hook? */
    lum_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = lumL_checkstring(L, arg+2);
    lumL_checktype(L, arg+1, LUM_TFUNCTION);
    count = (int)lumL_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (!lumL_getsubtable(L, LUM_REGISTRYINDEX, HOOKKEY)) {
    /* table just created; initialize it */
    lum_pushliteral(L, "k");
    lum_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
    lum_pushvalue(L, -1);
    lum_setmetatable(L, -2);  /* metatable(hooktable) = hooktable */
  }
  checkstack(L, L1, 1);
  lum_pushthread(L1); lum_xmove(L1, L, 1);  /* key (thread) */
  lum_pushvalue(L, arg + 1);  /* value (hook function) */
  lum_rawset(L, -3);  /* hooktable[L1] = new Lum hook */
  lum_sethook(L1, func, mask, count);
  return 0;
}


static int db_gethook (lum_State *L) {
  int arg;
  lum_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = lum_gethookmask(L1);
  lum_Hook hook = lum_gethook(L1);
  if (hook == NULL) {  /* no hook? */
    lumL_pushfail(L);
    return 1;
  }
  else if (hook != hookf)  /* external hook? */
    lum_pushliteral(L, "external hook");
  else {  /* hook table must exist */
    lum_getfield(L, LUM_REGISTRYINDEX, HOOKKEY);
    checkstack(L, L1, 1);
    lum_pushthread(L1); lum_xmove(L1, L, 1);
    lum_rawget(L, -2);   /* 1st result = hooktable[L1] */
    lum_remove(L, -2);  /* remove hook table */
  }
  lum_pushstring(L, unmakemask(mask, buff));  /* 2nd result = mask */
  lum_pushinteger(L, lum_gethookcount(L1));  /* 3rd result = count */
  return 3;
}


static int db_debug (lum_State *L) {
  for (;;) {
    char buffer[250];
    lum_writestringerror("%s", "lum_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (lumL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        lum_pcall(L, 0, 0, 0))
      lum_writestringerror("%s\n", lumL_tolstring(L, -1, NULL));
    lum_settop(L, 0);  /* remove eventual returns */
  }
}


static int db_traceback (lum_State *L) {
  int arg;
  lum_State *L1 = getthread(L, &arg);
  const char *msg = lum_tostring(L, arg + 1);
  if (msg == NULL && !lum_isnoneornil(L, arg + 1))  /* non-string 'msg'? */
    lum_pushvalue(L, arg + 1);  /* return it untouched */
  else {
    int level = (int)lumL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    lumL_traceback(L, L1, msg, level);
  }
  return 1;
}


static const lumL_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {NULL, NULL}
};


LUMMOD_API int lumopen_debug (lum_State *L) {
  lumL_newlib(L, dblib);
  return 1;
}

