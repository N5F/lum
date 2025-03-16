/*
** $Id: lbaselib.c $
** Basic library
** See Copyright Notice in lum.h
*/

#define lbaselib_c
#define LUM_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


static int lumB_print (lum_State *L) {
  int n = lum_gettop(L);  /* number of arguments */
  int i;
  for (i = 1; i <= n; i++) {  /* for each argument */
    size_t l;
    const char *s = lumL_tolstring(L, i, &l);  /* convert it to string */
    if (i > 1)  /* not the first element? */
      lum_writestring("\t", 1);  /* add a tab before it */
    lum_writestring(s, l);  /* print it */
    lum_pop(L, 1);  /* pop result */
  }
  lum_writeline();
  return 0;
}


/*
** Creates a warning with all given arguments.
** Check first for errors; otherwise an error may interrupt
** the composition of a warning, leaving it unfinished.
*/
static int lumB_warn (lum_State *L) {
  int n = lum_gettop(L);  /* number of arguments */
  int i;
  lumL_checkstring(L, 1);  /* at least one argument */
  for (i = 2; i <= n; i++)
    lumL_checkstring(L, i);  /* make sure all arguments are strings */
  for (i = 1; i < n; i++)  /* compose warning */
    lum_warning(L, lum_tostring(L, i), 1);
  lum_warning(L, lum_tostring(L, n), 0);  /* close warning */
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, unsigned base, lum_Integer *pn) {
  lum_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle sign */
  else if (*s == '+') s++;
  if (!isalnum(cast_uchar(*s)))  /* no digit? */
    return NULL;
  do {
    unsigned digit = cast_uint(isdigit(cast_uchar(*s))
                               ? *s - '0'
                               : (toupper(cast_uchar(*s)) - 'A') + 10);
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum(cast_uchar(*s)));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (lum_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int lumB_tonumber (lum_State *L) {
  if (lum_isnoneornil(L, 2)) {  /* standard conversion? */
    if (lum_type(L, 1) == LUM_TNUMBER) {  /* already a number? */
      lum_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = lum_tolstring(L, 1, &l);
      if (s != NULL && lum_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
      lumL_checkany(L, 1);  /* (but there must be some parameter) */
    }
  }
  else {
    size_t l;
    const char *s;
    lum_Integer n = 0;  /* to avoid warnings */
    lum_Integer base = lumL_checkinteger(L, 2);
    lumL_checktype(L, 1, LUM_TSTRING);  /* no numbers as strings */
    s = lum_tolstring(L, 1, &l);
    lumL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, cast_uint(base), &n) == s + l) {
      lum_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  lumL_pushfail(L);  /* not a number */
  return 1;
}


static int lumB_error (lum_State *L) {
  int level = (int)lumL_optinteger(L, 2, 1);
  lum_settop(L, 1);
  if (lum_type(L, 1) == LUM_TSTRING && level > 0) {
    lumL_where(L, level);   /* add extra information */
    lum_pushvalue(L, 1);
    lum_concat(L, 2);
  }
  return lum_error(L);
}


static int lumB_getmetatable (lum_State *L) {
  lumL_checkany(L, 1);
  if (!lum_getmetatable(L, 1)) {
    lum_pushnil(L);
    return 1;  /* no metatable */
  }
  lumL_getmetafield(L, 1, "__metatable");
  return 1;  /* returns either __metatable field (if present) or metatable */
}


static int lumB_setmetatable (lum_State *L) {
  int t = lum_type(L, 2);
  lumL_checktype(L, 1, LUM_TTABLE);
  lumL_argexpected(L, t == LUM_TNIL || t == LUM_TTABLE, 2, "nil or table");
  if (l_unlikely(lumL_getmetafield(L, 1, "__metatable") != LUM_TNIL))
    return lumL_error(L, "cannot change a protected metatable");
  lum_settop(L, 2);
  lum_setmetatable(L, 1);
  return 1;
}


static int lumB_rawequal (lum_State *L) {
  lumL_checkany(L, 1);
  lumL_checkany(L, 2);
  lum_pushboolean(L, lum_rawequal(L, 1, 2));
  return 1;
}


static int lumB_rawlen (lum_State *L) {
  int t = lum_type(L, 1);
  lumL_argexpected(L, t == LUM_TTABLE || t == LUM_TSTRING, 1,
                      "table or string");
  lum_pushinteger(L, l_castU2S(lum_rawlen(L, 1)));
  return 1;
}


static int lumB_rawget (lum_State *L) {
  lumL_checktype(L, 1, LUM_TTABLE);
  lumL_checkany(L, 2);
  lum_settop(L, 2);
  lum_rawget(L, 1);
  return 1;
}

static int lumB_rawset (lum_State *L) {
  lumL_checktype(L, 1, LUM_TTABLE);
  lumL_checkany(L, 2);
  lumL_checkany(L, 3);
  lum_settop(L, 3);
  lum_rawset(L, 1);
  return 1;
}


static int pushmode (lum_State *L, int oldmode) {
  if (oldmode == -1)
    lumL_pushfail(L);  /* invalid call to 'lum_gc' */
  else
    lum_pushstring(L, (oldmode == LUM_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}


/*
** check whether call to 'lum_gc' was valid (not inside a finalizer)
*/
#define checkvalres(res) { if (res == -1) break; }

static int lumB_collectgarbage (lum_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "isrunning", "generational", "incremental",
    "param", NULL};
  static const char optsnum[] = {LUM_GCSTOP, LUM_GCRESTART, LUM_GCCOLLECT,
    LUM_GCCOUNT, LUM_GCSTEP, LUM_GCISRUNNING, LUM_GCGEN, LUM_GCINC,
    LUM_GCPARAM};
  int o = optsnum[lumL_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case LUM_GCCOUNT: {
      int k = lum_gc(L, o);
      int b = lum_gc(L, LUM_GCCOUNTB);
      checkvalres(k);
      lum_pushnumber(L, (lum_Number)k + ((lum_Number)b/1024));
      return 1;
    }
    case LUM_GCSTEP: {
      lum_Integer n = lumL_optinteger(L, 2, 0);
      int res = lum_gc(L, o, cast_sizet(n));
      checkvalres(res);
      lum_pushboolean(L, res);
      return 1;
    }
    case LUM_GCISRUNNING: {
      int res = lum_gc(L, o);
      checkvalres(res);
      lum_pushboolean(L, res);
      return 1;
    }
    case LUM_GCGEN: {
      return pushmode(L, lum_gc(L, o));
    }
    case LUM_GCINC: {
      return pushmode(L, lum_gc(L, o));
    }
    case LUM_GCPARAM: {
      static const char *const params[] = {
        "minormul", "majorminor", "minormajor",
        "pause", "stepmul", "stepsize", NULL};
      static const char pnum[] = {
        LUM_GCPMINORMUL, LUM_GCPMAJORMINOR, LUM_GCPMINORMAJOR,
        LUM_GCPPAUSE, LUM_GCPSTEPMUL, LUM_GCPSTEPSIZE};
      int p = pnum[lumL_checkoption(L, 2, NULL, params)];
      lum_Integer value = lumL_optinteger(L, 3, -1);
      lum_pushinteger(L, lum_gc(L, o, p, (int)value));
      return 1;
    }
    default: {
      int res = lum_gc(L, o);
      checkvalres(res);
      lum_pushinteger(L, res);
      return 1;
    }
  }
  lumL_pushfail(L);  /* invalid call (inside a finalizer) */
  return 1;
}


static int lumB_type (lum_State *L) {
  int t = lum_type(L, 1);
  lumL_argcheck(L, t != LUM_TNONE, 1, "value expected");
  lum_pushstring(L, lum_typename(L, t));
  return 1;
}


static int lumB_next (lum_State *L) {
  lumL_checktype(L, 1, LUM_TTABLE);
  lum_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (lum_next(L, 1))
    return 2;
  else {
    lum_pushnil(L);
    return 1;
  }
}


static int pairscont (lum_State *L, int status, lum_KContext k) {
  (void)L; (void)status; (void)k;  /* unused */
  return 3;
}

static int lumB_pairs (lum_State *L) {
  lumL_checkany(L, 1);
  if (lumL_getmetafield(L, 1, "__pairs") == LUM_TNIL) {  /* no metamethod? */
    lum_pushcfunction(L, lumB_next);  /* will return generator, */
    lum_pushvalue(L, 1);  /* state, */
    lum_pushnil(L);  /* and initial value */
  }
  else {
    lum_pushvalue(L, 1);  /* argument 'self' to metamethod */
    lum_callk(L, 1, 3, 0, pairscont);  /* get 3 values from metamethod */
  }
  return 3;
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (lum_State *L) {
  lum_Integer i = lumL_checkinteger(L, 2);
  i = lumL_intop(+, i, 1);
  lum_pushinteger(L, i);
  return (lum_geti(L, 1, i) == LUM_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int lumB_ipairs (lum_State *L) {
  lumL_checkany(L, 1);
  lum_pushcfunction(L, ipairsaux);  /* iteration function */
  lum_pushvalue(L, 1);  /* state */
  lum_pushinteger(L, 0);  /* initial value */
  return 3;
}


static int load_aux (lum_State *L, int status, int envidx) {
  if (l_likely(status == LUM_OK)) {
    if (envidx != 0) {  /* 'env' parameter? */
      lum_pushvalue(L, envidx);  /* environment for loaded function */
      if (!lum_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        lum_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    lumL_pushfail(L);
    lum_insert(L, -2);  /* put before error message */
    return 2;  /* return fail plus error message */
  }
}


static const char *getMode (lum_State *L, int idx) {
  const char *mode = lumL_optstring(L, idx, "bt");
  if (strchr(mode, 'B') != NULL)  /* Lum code cannot use fixed buffers */
    lumL_argerror(L, idx, "invalid mode");
  return mode;
}


static int lumB_loadfile (lum_State *L) {
  const char *fname = lumL_optstring(L, 1, NULL);
  const char *mode = getMode(L, 2);
  int env = (!lum_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = lumL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' function: 'lum_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (lum_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  lumL_checkstack(L, 2, "too many nested functions");
  lum_pushvalue(L, 1);  /* get function */
  lum_call(L, 0, 1);  /* call it */
  if (lum_isnil(L, -1)) {
    lum_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (l_unlikely(!lum_isstring(L, -1)))
    lumL_error(L, "reader function must return a string");
  lum_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return lum_tolstring(L, RESERVEDSLOT, size);
}


static int lumB_load (lum_State *L) {
  int status;
  size_t l;
  const char *s = lum_tolstring(L, 1, &l);
  const char *mode = getMode(L, 3);
  int env = (!lum_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = lumL_optstring(L, 2, s);
    status = lumL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = lumL_optstring(L, 2, "=(load)");
    lumL_checktype(L, 1, LUM_TFUNCTION);
    lum_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = lum_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (lum_State *L, int d1, lum_KContext d2) {
  (void)d1;  (void)d2;  /* only to match 'lum_Kfunction' prototype */
  return lum_gettop(L) - 1;
}


static int lumB_dofile (lum_State *L) {
  const char *fname = lumL_optstring(L, 1, NULL);
  lum_settop(L, 1);
  if (l_unlikely(lumL_loadfile(L, fname) != LUM_OK))
    return lum_error(L);
  lum_callk(L, 0, LUM_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}


static int lumB_assert (lum_State *L) {
  if (l_likely(lum_toboolean(L, 1)))  /* condition is true? */
    return lum_gettop(L);  /* return all arguments */
  else {  /* error */
    lumL_checkany(L, 1);  /* there must be a condition */
    lum_remove(L, 1);  /* remove it */
    lum_pushliteral(L, "assertion failed!");  /* default message */
    lum_settop(L, 1);  /* leave only message (default if no other one) */
    return lumB_error(L);  /* call 'error' */
  }
}


static int lumB_select (lum_State *L) {
  int n = lum_gettop(L);
  if (lum_type(L, 1) == LUM_TSTRING && *lum_tostring(L, 1) == '#') {
    lum_pushinteger(L, n-1);
    return 1;
  }
  else {
    lum_Integer i = lumL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    lumL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (lum_State *L, int status, lum_KContext extra) {
  if (l_unlikely(status != LUM_OK && status != LUM_YIELD)) {  /* error? */
    lum_pushboolean(L, 0);  /* first result (false) */
    lum_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return lum_gettop(L) - (int)extra;  /* return all results */
}


static int lumB_pcall (lum_State *L) {
  int status;
  lumL_checkany(L, 1);
  lum_pushboolean(L, 1);  /* first result if no errors */
  lum_insert(L, 1);  /* put it in place */
  status = lum_pcallk(L, lum_gettop(L) - 2, LUM_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'lum_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int lumB_xpcall (lum_State *L) {
  int status;
  int n = lum_gettop(L);
  lumL_checktype(L, 2, LUM_TFUNCTION);  /* check error function */
  lum_pushboolean(L, 1);  /* first result */
  lum_pushvalue(L, 1);  /* function */
  lum_rotate(L, 3, 2);  /* move them below function's arguments */
  status = lum_pcallk(L, n - 2, LUM_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}


static int lumB_tostring (lum_State *L) {
  lumL_checkany(L, 1);
  lumL_tolstring(L, 1, NULL);
  return 1;
}


static const lumL_Reg base_funcs[] = {
  {"assert", lumB_assert},
  {"collectgarbage", lumB_collectgarbage},
  {"dofile", lumB_dofile},
  {"error", lumB_error},
  {"getmetatable", lumB_getmetatable},
  {"ipairs", lumB_ipairs},
  {"loadfile", lumB_loadfile},
  {"load", lumB_load},
  {"next", lumB_next},
  {"pairs", lumB_pairs},
  {"pcall", lumB_pcall},
  {"print", lumB_print},
  {"warn", lumB_warn},
  {"rawequal", lumB_rawequal},
  {"rawlen", lumB_rawlen},
  {"rawget", lumB_rawget},
  {"rawset", lumB_rawset},
  {"select", lumB_select},
  {"setmetatable", lumB_setmetatable},
  {"tonumber", lumB_tonumber},
  {"tostring", lumB_tostring},
  {"type", lumB_type},
  {"xpcall", lumB_xpcall},
  /* placeholders */
  {LUM_GNAME, NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};


LUMMOD_API int lumopen_base (lum_State *L) {
  /* open lib into global table */
  lum_pushglobaltable(L);
  lumL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  lum_pushvalue(L, -1);
  lum_setfield(L, -2, LUM_GNAME);
  /* set global _VERSION */
  lum_pushliteral(L, LUM_VERSION);
  lum_setfield(L, -2, "_VERSION");
  return 1;
}

