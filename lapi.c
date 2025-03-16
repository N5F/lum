/*
** $Id: lapi.c $
** Lum API
** See Copyright Notice in lum.h
*/

#define lapi_c
#define LUM_CORE

#include "lprefix.h"


#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "lum.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char lum_ident[] =
  "$LumVersion: " LUM_COPYRIGHT " $"
  "$LumAuthors: " LUM_AUTHORS " $";



/*
** Test for a valid index (one that is not the 'nilvalue').
*/
#define isvalid(L, o)	((o) != &G(L)->nilvalue)


/* test for pseudo index */
#define ispseudo(i)		((i) <= LUM_REGISTRYINDEX)

/* test for upvalue */
#define isupvalue(i)		((i) < LUM_REGISTRYINDEX)


/*
** Convert an acceptable index to a pointer to its respective value.
** Non-valid indices return the special nil value 'G(L)->nilvalue'.
*/
static TValue *index2value (lum_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, idx <= ci->top.p - (ci->func.p + 1), "unacceptable index");
    if (o >= L->top.p) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    return s2v(L->top.p + idx);
  }
  else if (idx == LUM_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = LUM_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(ci->func.p))) {  /* C closure? */
      CClosure *func = clCvalue(s2v(ci->func.p));
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1]
                                      : &G(L)->nilvalue;
    }
    else {  /* light C function or Lum function (through a hook)?) */
      api_check(L, ttislcf(s2v(ci->func.p)), "caller not a C function");
      return &G(L)->nilvalue;  /* no upvalues */
    }
  }
}



/*
** Convert a valid actual index (not a pseudo-index) to its address.
*/
static StkId index2stack (lum_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, o < L->top.p, "invalid index");
    return o;
  }
  else {    /* non-positive index */
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return L->top.p + idx;
  }
}


LUM_API int lum_checkstack (lum_State *L, int n) {
  int res;
  CallInfo *ci;
  lum_lock(L);
  ci = L->ci;
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last.p - L->top.p > n)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else  /* need to grow stack */
    res = lumD_growstack(L, n, 0);
  if (res && ci->top.p < L->top.p + n)
    ci->top.p = L->top.p + n;  /* adjust frame top */
  lum_unlock(L);
  return res;
}


LUM_API void lum_xmove (lum_State *from, lum_State *to, int n) {
  int i;
  if (from == to) return;
  lum_lock(to);
  api_checkpop(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top.p - to->top.p >= n, "stack overflow");
  from->top.p -= n;
  for (i = 0; i < n; i++) {
    setobjs2s(to, to->top.p, from->top.p + i);
    to->top.p++;  /* stack already checked by previous 'api_check' */
  }
  lum_unlock(to);
}


LUM_API lum_CFunction lum_atpanic (lum_State *L, lum_CFunction panicf) {
  lum_CFunction old;
  lum_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lum_unlock(L);
  return old;
}


LUM_API lum_Number lum_version (lum_State *L) {
  UNUSED(L);
  return LUM_VERSION_NUM;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
LUM_API int lum_absindex (lum_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top.p - L->ci->func.p) + idx;
}


LUM_API int lum_gettop (lum_State *L) {
  return cast_int(L->top.p - (L->ci->func.p + 1));
}


LUM_API void lum_settop (lum_State *L, int idx) {
  CallInfo *ci;
  StkId func, newtop;
  ptrdiff_t diff;  /* difference for new top */
  lum_lock(L);
  ci = L->ci;
  func = ci->func.p;
  if (idx >= 0) {
    api_check(L, idx <= ci->top.p - (func + 1), "new top too large");
    diff = ((func + 1) + idx) - L->top.p;
    for (; diff > 0; diff--)
      setnilvalue(s2v(L->top.p++));  /* clear new slots */
  }
  else {
    api_check(L, -(idx+1) <= (L->top.p - (func + 1)), "invalid new top");
    diff = idx + 1;  /* will "subtract" index (as it is negative) */
  }
  newtop = L->top.p + diff;
  if (diff < 0 && L->tbclist.p >= newtop) {
    lum_assert(ci->callstatus & CIST_TBC);
    newtop = lumF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->top.p = newtop;  /* correct top only after closing any upvalue */
  lum_unlock(L);
}


LUM_API void lum_closeslot (lum_State *L, int idx) {
  StkId level;
  lum_lock(L);
  level = index2stack(L, idx);
  api_check(L, (L->ci->callstatus & CIST_TBC) && (L->tbclist.p == level),
     "no variable to close at given level");
  level = lumF_close(L, level, CLOSEKTOP, 0);
  setnilvalue(s2v(level));
  lum_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lum_rotate')
** Note that we move(copy) only the value inside the stack.
** (We do not move additional fields that may exist.)
*/
static void reverse (lum_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, s2v(from));
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
LUM_API void lum_rotate (lum_State *L, int idx, int n) {
  StkId p, t, m;
  lum_lock(L);
  t = L->top.p - 1;  /* end of stack segment being rotated */
  p = index2stack(L, idx);  /* start of segment */
  api_check(L, L->tbclist.p < p, "moving a to-be-closed slot");
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  lum_unlock(L);
}


LUM_API void lum_copy (lum_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  lum_lock(L);
  fr = index2value(L, fromidx);
  to = index2value(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* function upvalue? */
    lumC_barrier(L, clCvalue(s2v(L->ci->func.p)), fr);
  /* LUM_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lum_unlock(L);
}


LUM_API void lum_pushvalue (lum_State *L, int idx) {
  lum_lock(L);
  setobj2s(L, L->top.p, index2value(L, idx));
  api_incr_top(L);
  lum_unlock(L);
}



/*
** access functions (stack -> C)
*/


LUM_API int lum_type (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (isvalid(L, o) ? ttype(o) : LUM_TNONE);
}


LUM_API const char *lum_typename (lum_State *L, int t) {
  UNUSED(L);
  api_check(L, LUM_TNONE <= t && t < LUM_NUMTYPES, "invalid type");
  return ttypename(t);
}


LUM_API int lum_iscfunction (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


LUM_API int lum_isinteger (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return ttisinteger(o);
}


LUM_API int lum_isnumber (lum_State *L, int idx) {
  lum_Number n;
  const TValue *o = index2value(L, idx);
  return tonumber(o, &n);
}


LUM_API int lum_isstring (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


LUM_API int lum_isuserdata (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


LUM_API int lum_rawequal (lum_State *L, int index1, int index2) {
  const TValue *o1 = index2value(L, index1);
  const TValue *o2 = index2value(L, index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? lumV_rawequalobj(o1, o2) : 0;
}


LUM_API void lum_arith (lum_State *L, int op) {
  lum_lock(L);
  if (op != LUM_OPUNM && op != LUM_OPBNOT)
    api_checkpop(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checkpop(L, 1);
    setobjs2s(L, L->top.p, L->top.p - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  lumO_arith(L, op, s2v(L->top.p - 2), s2v(L->top.p - 1), L->top.p - 2);
  L->top.p--;  /* pop second operand */
  lum_unlock(L);
}


LUM_API int lum_compare (lum_State *L, int index1, int index2, int op) {
  const TValue *o1;
  const TValue *o2;
  int i = 0;
  lum_lock(L);  /* may call tag method */
  o1 = index2value(L, index1);
  o2 = index2value(L, index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case LUM_OPEQ: i = lumV_equalobj(L, o1, o2); break;
      case LUM_OPLT: i = lumV_lessthan(L, o1, o2); break;
      case LUM_OPLE: i = lumV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lum_unlock(L);
  return i;
}


LUM_API unsigned (lum_numbertocstring) (lum_State *L, int idx, char *buff) {
  const TValue *o = index2value(L, idx);
  if (ttisnumber(o)) {
    unsigned len = lumO_tostringbuff(o, buff);
    buff[len++] = '\0';  /* add final zero */
    return len;
  }
  else
    return 0;
}


LUM_API size_t lum_stringtonumber (lum_State *L, const char *s) {
  size_t sz = lumO_str2num(s, s2v(L->top.p));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


LUM_API lum_Number lum_tonumberx (lum_State *L, int idx, int *pisnum) {
  lum_Number n = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}


LUM_API lum_Integer lum_tointegerx (lum_State *L, int idx, int *pisnum) {
  lum_Integer res = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}


LUM_API int lum_toboolean (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return !l_isfalse(o);
}


LUM_API const char *lum_tolstring (lum_State *L, int idx, size_t *len) {
  TValue *o;
  lum_lock(L);
  o = index2value(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      lum_unlock(L);
      return NULL;
    }
    lumO_tostring(L, o);
    lumC_checkGC(L);
    o = index2value(L, idx);  /* previous call may reallocate the stack */
  }
  lum_unlock(L);
  if (len != NULL)
    return getlstr(tsvalue(o), *len);
  else
    return getstr(tsvalue(o));
}


LUM_API lum_Unsigned lum_rawlen (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case LUM_VSHRSTR: return cast(lum_Unsigned, tsvalue(o)->shrlen);
    case LUM_VLNGSTR: return cast(lum_Unsigned, tsvalue(o)->u.lnglen);
    case LUM_VUSERDATA: return cast(lum_Unsigned, uvalue(o)->len);
    case LUM_VTABLE: return lumH_getn(hvalue(o));
    default: return 0;
  }
}


LUM_API lum_CFunction lum_tocfunction (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


l_sinline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case LUM_TUSERDATA: return getudatamem(uvalue(o));
    case LUM_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


LUM_API void *lum_touserdata (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return touserdata(o);
}


LUM_API lum_State *lum_tothread (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


/*
** Returns a pointer to the internal representation of an object.
** Note that ANSI C does not allow the conversion of a pointer to
** function to a 'void*', so the conversion here goes through
** a 'size_t'. (As the returned pointer is only informative, this
** conversion should not be a problem.)
*/
LUM_API const void *lum_topointer (lum_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case LUM_VLCF: return cast_voidp(cast_sizet(fvalue(o)));
    case LUM_VUSERDATA: case LUM_VLIGHTUSERDATA:
      return touserdata(o);
    default: {
      if (iscollectable(o))
        return gcvalue(o);
      else
        return NULL;
    }
  }
}



/*
** push functions (C -> stack)
*/


LUM_API void lum_pushnil (lum_State *L) {
  lum_lock(L);
  setnilvalue(s2v(L->top.p));
  api_incr_top(L);
  lum_unlock(L);
}


LUM_API void lum_pushnumber (lum_State *L, lum_Number n) {
  lum_lock(L);
  setfltvalue(s2v(L->top.p), n);
  api_incr_top(L);
  lum_unlock(L);
}


LUM_API void lum_pushinteger (lum_State *L, lum_Integer n) {
  lum_lock(L);
  setivalue(s2v(L->top.p), n);
  api_incr_top(L);
  lum_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
LUM_API const char *lum_pushlstring (lum_State *L, const char *s, size_t len) {
  TString *ts;
  lum_lock(L);
  ts = (len == 0) ? lumS_new(L, "") : lumS_newlstr(L, s, len);
  setsvalue2s(L, L->top.p, ts);
  api_incr_top(L);
  lumC_checkGC(L);
  lum_unlock(L);
  return getstr(ts);
}


LUM_API const char *lum_pushexternalstring (lum_State *L,
	        const char *s, size_t len, lum_Alloc falloc, void *ud) {
  TString *ts;
  lum_lock(L);
  api_check(L, len <= MAX_SIZE, "string too large");
  api_check(L, s[len] == '\0', "string not ending with zero");
  ts = lumS_newextlstr (L, s, len, falloc, ud);
  setsvalue2s(L, L->top.p, ts);
  api_incr_top(L);
  lumC_checkGC(L);
  lum_unlock(L);
  return getstr(ts);
}


LUM_API const char *lum_pushstring (lum_State *L, const char *s) {
  lum_lock(L);
  if (s == NULL)
    setnilvalue(s2v(L->top.p));
  else {
    TString *ts;
    ts = lumS_new(L, s);
    setsvalue2s(L, L->top.p, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  lumC_checkGC(L);
  lum_unlock(L);
  return s;
}


LUM_API const char *lum_pushvfstring (lum_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lum_lock(L);
  ret = lumO_pushvfstring(L, fmt, argp);
  lumC_checkGC(L);
  lum_unlock(L);
  return ret;
}


LUM_API const char *lum_pushfstring (lum_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lum_lock(L);
  va_start(argp, fmt);
  ret = lumO_pushvfstring(L, fmt, argp);
  va_end(argp);
  lumC_checkGC(L);
  if (ret == NULL)  /* error? */
    lumD_throw(L, LUM_ERRMEM);
  lum_unlock(L);
  return ret;
}


LUM_API void lum_pushcclosure (lum_State *L, lum_CFunction fn, int n) {
  lum_lock(L);
  if (n == 0) {
    setfvalue(s2v(L->top.p), fn);
    api_incr_top(L);
  }
  else {
    int i;
    CClosure *cl;
    api_checkpop(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    cl = lumF_newCclosure(L, n);
    cl->f = fn;
    for (i = 0; i < n; i++) {
      setobj2n(L, &cl->upvalue[i], s2v(L->top.p - n + i));
      /* does not need barrier because closure is white */
      lum_assert(iswhite(cl));
    }
    L->top.p -= n;
    setclCvalue(L, s2v(L->top.p), cl);
    api_incr_top(L);
    lumC_checkGC(L);
  }
  lum_unlock(L);
}


LUM_API void lum_pushboolean (lum_State *L, int b) {
  lum_lock(L);
  if (b)
    setbtvalue(s2v(L->top.p));
  else
    setbfvalue(s2v(L->top.p));
  api_incr_top(L);
  lum_unlock(L);
}


LUM_API void lum_pushlightuserdata (lum_State *L, void *p) {
  lum_lock(L);
  setpvalue(s2v(L->top.p), p);
  api_incr_top(L);
  lum_unlock(L);
}


LUM_API int lum_pushthread (lum_State *L) {
  lum_lock(L);
  setthvalue(L, s2v(L->top.p), L);
  api_incr_top(L);
  lum_unlock(L);
  return (mainthread(G(L)) == L);
}



/*
** get functions (Lum -> stack)
*/


static int auxgetstr (lum_State *L, const TValue *t, const char *k) {
  lu_byte tag;
  TString *str = lumS_new(L, k);
  lumV_fastget(t, str, s2v(L->top.p), lumH_getstr, tag);
  if (!tagisempty(tag))
    api_incr_top(L);
  else {
    setsvalue2s(L, L->top.p, str);
    api_incr_top(L);
    tag = lumV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, tag);
  }
  lum_unlock(L);
  return novariant(tag);
}


static void getGlobalTable (lum_State *L, TValue *gt) {
  Table *registry = hvalue(&G(L)->l_registry);
  lu_byte tag = lumH_getint(registry, LUM_RIDX_GLOBALS, gt);
  (void)tag;  /* avoid not-used warnings when checks are off */
  api_check(L, novariant(tag) == LUM_TTABLE, "global table must exist");
}


LUM_API int lum_getglobal (lum_State *L, const char *name) {
  TValue gt;
  lum_lock(L);
  getGlobalTable(L, &gt);
  return auxgetstr(L, &gt, name);
}


LUM_API int lum_gettable (lum_State *L, int idx) {
  lu_byte tag;
  TValue *t;
  lum_lock(L);
  api_checkpop(L, 1);
  t = index2value(L, idx);
  lumV_fastget(t, s2v(L->top.p - 1), s2v(L->top.p - 1), lumH_get, tag);
  if (tagisempty(tag))
    tag = lumV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, tag);
  lum_unlock(L);
  return novariant(tag);
}


LUM_API int lum_getfield (lum_State *L, int idx, const char *k) {
  lum_lock(L);
  return auxgetstr(L, index2value(L, idx), k);
}


LUM_API int lum_geti (lum_State *L, int idx, lum_Integer n) {
  TValue *t;
  lu_byte tag;
  lum_lock(L);
  t = index2value(L, idx);
  lumV_fastgeti(t, n, s2v(L->top.p), tag);
  if (tagisempty(tag)) {
    TValue key;
    setivalue(&key, n);
    tag = lumV_finishget(L, t, &key, L->top.p, tag);
  }
  api_incr_top(L);
  lum_unlock(L);
  return novariant(tag);
}


static int finishrawget (lum_State *L, lu_byte tag) {
  if (tagisempty(tag))  /* avoid copying empty items to the stack */
    setnilvalue(s2v(L->top.p));
  api_incr_top(L);
  lum_unlock(L);
  return novariant(tag);
}


l_sinline Table *gettable (lum_State *L, int idx) {
  TValue *t = index2value(L, idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}


LUM_API int lum_rawget (lum_State *L, int idx) {
  Table *t;
  lu_byte tag;
  lum_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  tag = lumH_get(t, s2v(L->top.p - 1), s2v(L->top.p - 1));
  L->top.p--;  /* pop key */
  return finishrawget(L, tag);
}


LUM_API int lum_rawgeti (lum_State *L, int idx, lum_Integer n) {
  Table *t;
  lu_byte tag;
  lum_lock(L);
  t = gettable(L, idx);
  lumH_fastgeti(t, n, s2v(L->top.p), tag);
  return finishrawget(L, tag);
}


LUM_API int lum_rawgetp (lum_State *L, int idx, const void *p) {
  Table *t;
  TValue k;
  lum_lock(L);
  t = gettable(L, idx);
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, lumH_get(t, &k, s2v(L->top.p)));
}


LUM_API void lum_createtable (lum_State *L, int narray, int nrec) {
  Table *t;
  lum_lock(L);
  t = lumH_new(L);
  sethvalue2s(L, L->top.p, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    lumH_resize(L, t, cast_uint(narray), cast_uint(nrec));
  lumC_checkGC(L);
  lum_unlock(L);
}


LUM_API int lum_getmetatable (lum_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lum_lock(L);
  obj = index2value(L, objindex);
  switch (ttype(obj)) {
    case LUM_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUM_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue2s(L, L->top.p, mt);
    api_incr_top(L);
    res = 1;
  }
  lum_unlock(L);
  return res;
}


LUM_API int lum_getiuservalue (lum_State *L, int idx, int n) {
  TValue *o;
  int t;
  lum_lock(L);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->nuvalue) {
    setnilvalue(s2v(L->top.p));
    t = LUM_TNONE;
  }
  else {
    setobj2s(L, L->top.p, &uvalue(o)->uv[n - 1].uv);
    t = ttype(s2v(L->top.p));
  }
  api_incr_top(L);
  lum_unlock(L);
  return t;
}


/*
** set functions (stack -> Lum)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
static void auxsetstr (lum_State *L, const TValue *t, const char *k) {
  int hres;
  TString *str = lumS_new(L, k);
  api_checkpop(L, 1);
  lumV_fastset(t, str, s2v(L->top.p - 1), hres, lumH_psetstr);
  if (hres == HOK) {
    lumV_finishfastset(L, t, s2v(L->top.p - 1));
    L->top.p--;  /* pop value */
  }
  else {
    setsvalue2s(L, L->top.p, str);  /* push 'str' (to make it a TValue) */
    api_incr_top(L);
    lumV_finishset(L, t, s2v(L->top.p - 1), s2v(L->top.p - 2), hres);
    L->top.p -= 2;  /* pop value and key */
  }
  lum_unlock(L);  /* lock done by caller */
}


LUM_API void lum_setglobal (lum_State *L, const char *name) {
  TValue gt;
  lum_lock(L);  /* unlock done in 'auxsetstr' */
  getGlobalTable(L, &gt);
  auxsetstr(L, &gt, name);
}


LUM_API void lum_settable (lum_State *L, int idx) {
  TValue *t;
  int hres;
  lum_lock(L);
  api_checkpop(L, 2);
  t = index2value(L, idx);
  lumV_fastset(t, s2v(L->top.p - 2), s2v(L->top.p - 1), hres, lumH_pset);
  if (hres == HOK) {
    lumV_finishfastset(L, t, s2v(L->top.p - 1));
  }
  else
    lumV_finishset(L, t, s2v(L->top.p - 2), s2v(L->top.p - 1), hres);
  L->top.p -= 2;  /* pop index and value */
  lum_unlock(L);
}


LUM_API void lum_setfield (lum_State *L, int idx, const char *k) {
  lum_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, index2value(L, idx), k);
}


LUM_API void lum_seti (lum_State *L, int idx, lum_Integer n) {
  TValue *t;
  int hres;
  lum_lock(L);
  api_checkpop(L, 1);
  t = index2value(L, idx);
  lumV_fastseti(t, n, s2v(L->top.p - 1), hres);
  if (hres == HOK)
    lumV_finishfastset(L, t, s2v(L->top.p - 1));
  else {
    TValue temp;
    setivalue(&temp, n);
    lumV_finishset(L, t, &temp, s2v(L->top.p - 1), hres);
  }
  L->top.p--;  /* pop value */
  lum_unlock(L);
}


static void aux_rawset (lum_State *L, int idx, TValue *key, int n) {
  Table *t;
  lum_lock(L);
  api_checkpop(L, n);
  t = gettable(L, idx);
  lumH_set(L, t, key, s2v(L->top.p - 1));
  invalidateTMcache(t);
  lumC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p -= n;
  lum_unlock(L);
}


LUM_API void lum_rawset (lum_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->top.p - 2), 2);
}


LUM_API void lum_rawsetp (lum_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}


LUM_API void lum_rawseti (lum_State *L, int idx, lum_Integer n) {
  Table *t;
  lum_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  lumH_setint(L, t, n, s2v(L->top.p - 1));
  lumC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p--;
  lum_unlock(L);
}


LUM_API int lum_setmetatable (lum_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lum_lock(L);
  api_checkpop(L, 1);
  obj = index2value(L, objindex);
  if (ttisnil(s2v(L->top.p - 1)))
    mt = NULL;
  else {
    api_check(L, ttistable(s2v(L->top.p - 1)), "table expected");
    mt = hvalue(s2v(L->top.p - 1));
  }
  switch (ttype(obj)) {
    case LUM_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        lumC_objbarrier(L, gcvalue(obj), mt);
        lumC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUM_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        lumC_objbarrier(L, uvalue(obj), mt);
        lumC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttype(obj)] = mt;
      break;
    }
  }
  L->top.p--;
  lum_unlock(L);
  return 1;
}


LUM_API int lum_setiuservalue (lum_State *L, int idx, int n) {
  TValue *o;
  int res;
  lum_lock(L);
  api_checkpop(L, 1);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->nuvalue)))
    res = 0;  /* 'n' not in [1, uvalue(o)->nuvalue] */
  else {
    setobj(L, &uvalue(o)->uv[n - 1].uv, s2v(L->top.p - 1));
    lumC_barrierback(L, gcvalue(o), s2v(L->top.p - 1));
    res = 1;
  }
  L->top.p--;
  lum_unlock(L);
  return res;
}


/*
** 'load' and 'call' functions (run Lum code)
*/


#define checkresults(L,na,nr) \
     (api_check(L, (nr) == LUM_MULTRET \
               || (L->ci->top.p - L->top.p >= (nr) - (na)), \
	"results from function overflow current stack size"), \
      api_check(L, LUM_MULTRET <= (nr) && (nr) <= MAXRESULTS,  \
                   "invalid number of results"))


LUM_API void lum_callk (lum_State *L, int nargs, int nresults,
                        lum_KContext ctx, lum_KFunction k) {
  StkId func;
  lum_lock(L);
  api_check(L, k == NULL || !isLum(L->ci),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->status == LUM_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top.p - (nargs+1);
  if (k != NULL && yieldable(L)) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    lumD_call(L, func, nresults);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    lumD_callnoyield(L, func, nresults);  /* just do the call */
  adjustresults(L, nresults);
  lum_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (lum_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  lumD_callnoyield(L, c->func, c->nresults);
}



LUM_API int lum_pcallk (lum_State *L, int nargs, int nresults, int errfunc,
                        lum_KContext ctx, lum_KFunction k) {
  struct CallS c;
  TStatus status;
  ptrdiff_t func;
  lum_lock(L);
  api_check(L, k == NULL || !isLum(L->ci),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->status == LUM_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2stack(L, errfunc);
    api_check(L, ttisfunction(s2v(o)), "error handler must be a function");
    func = savestack(L, o);
  }
  c.func = L->top.p - (nargs+1);  /* function to be called */
  if (k == NULL || !yieldable(L)) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = lumD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->u2.funcidx = cast_int(savestack(L, c.func));
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;  /* function can do error recovery */
    lumD_call(L, c.func, nresults);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUM_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  lum_unlock(L);
  return APIstatus(status);
}


LUM_API int lum_load (lum_State *L, lum_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  TStatus status;
  lum_lock(L);
  if (!chunkname) chunkname = "?";
  lumZ_init(L, &z, reader, data);
  status = lumD_protectedparser(L, &z, chunkname, mode);
  if (status == LUM_OK) {  /* no errors? */
    LClosure *f = clLvalue(s2v(L->top.p - 1));  /* get new function */
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      TValue gt;
      getGlobalTable(L, &gt);
      /* set global table as 1st upvalue of 'f' (may be LUM_ENV) */
      setobj(L, f->upvals[0]->v.p, &gt);
      lumC_barrier(L, f->upvals[0], &gt);
    }
  }
  lum_unlock(L);
  return APIstatus(status);
}


/*
** Dump a Lum function, calling 'writer' to write its parts. Ensure
** the stack returns with its original size.
*/
LUM_API int lum_dump (lum_State *L, lum_Writer writer, void *data, int strip) {
  int status;
  ptrdiff_t otop = savestack(L, L->top.p);  /* original top */
  TValue *f = s2v(L->top.p - 1);  /* function to be dumped */
  lum_lock(L);
  api_checkpop(L, 1);
  api_check(L, isLfunction(f), "Lum function expected");
  status = lumU_dump(L, clLvalue(f)->p, writer, data, strip);
  L->top.p = restorestack(L, otop);  /* restore top */
  lum_unlock(L);
  return status;
}


LUM_API int lum_status (lum_State *L) {
  return APIstatus(L->status);
}


/*
** Garbage-collection function
*/
LUM_API int lum_gc (lum_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  global_State *g = G(L);
  if (g->gcstp & (GCSTPGC | GCSTPCLS))  /* internal stop? */
    return -1;  /* all options are invalid when stopped */
  lum_lock(L);
  va_start(argp, what);
  switch (what) {
    case LUM_GCSTOP: {
      g->gcstp = GCSTPUSR;  /* stopped by the user */
      break;
    }
    case LUM_GCRESTART: {
      lumE_setdebt(g, 0);
      g->gcstp = 0;  /* (other bits must be zero here) */
      break;
    }
    case LUM_GCCOLLECT: {
      lumC_fullgc(L, 0);
      break;
    }
    case LUM_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUM_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUM_GCSTEP: {
      lu_byte oldstp = g->gcstp;
      l_mem n = cast(l_mem, va_arg(argp, size_t));
      int work = 0;  /* true if GC did some work */
      g->gcstp = 0;  /* allow GC to run (other bits must be zero here) */
      if (n <= 0)
        n = g->GCdebt;  /* force to run one basic step */
      lumE_setdebt(g, g->GCdebt - n);
      lumC_condGC(L, (void)0, work = 1);
      if (work && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      g->gcstp = oldstp;  /* restore previous state */
      break;
    }
    case LUM_GCISRUNNING: {
      res = gcrunning(g);
      break;
    }
    case LUM_GCGEN: {
      res = (g->gckind == KGC_INC) ? LUM_GCINC : LUM_GCGEN;
      lumC_changemode(L, KGC_GENMINOR);
      break;
    }
    case LUM_GCINC: {
      res = (g->gckind == KGC_INC) ? LUM_GCINC : LUM_GCGEN;
      lumC_changemode(L, KGC_INC);
      break;
    }
    case LUM_GCPARAM: {
      int param = va_arg(argp, int);
      int value = va_arg(argp, int);
      api_check(L, 0 <= param && param < LUM_GCPN, "invalid parameter");
      res = cast_int(lumO_applyparam(g->gcparams[param], 100));
      if (value >= 0)
        g->gcparams[param] = lumO_codeparam(cast_uint(value));
      break;
    }
    default: res = -1;  /* invalid option */
  }
  va_end(argp);
  lum_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


LUM_API int lum_error (lum_State *L) {
  TValue *errobj;
  lum_lock(L);
  errobj = s2v(L->top.p - 1);
  api_checkpop(L, 1);
  /* error object is the memory error message? */
  if (ttisshrstring(errobj) && eqshrstr(tsvalue(errobj), G(L)->memerrmsg))
    lumM_error(L);  /* raise a memory error */
  else
    lumG_errormsg(L);  /* raise a regular error */
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}


LUM_API int lum_next (lum_State *L, int idx) {
  Table *t;
  int more;
  lum_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  more = lumH_next(L, t, L->top.p - 1);
  if (more)
    api_incr_top(L);
  else  /* no more elements */
    L->top.p--;  /* pop key */
  lum_unlock(L);
  return more;
}


LUM_API void lum_toclose (lum_State *L, int idx) {
  StkId o;
  lum_lock(L);
  o = index2stack(L, idx);
  api_check(L, L->tbclist.p < o, "given index below or equal a marked one");
  lumF_newtbcupval(L, o);  /* create new to-be-closed upvalue */
  L->ci->callstatus |= CIST_TBC;  /* mark that function has TBC slots */
  lum_unlock(L);
}


LUM_API void lum_concat (lum_State *L, int n) {
  lum_lock(L);
  api_checknelems(L, n);
  if (n > 0) {
    lumV_concat(L, n);
    lumC_checkGC(L);
  }
  else {  /* nothing to concatenate */
    setsvalue2s(L, L->top.p, lumS_newlstr(L, "", 0));  /* push empty string */
    api_incr_top(L);
  }
  lum_unlock(L);
}


LUM_API void lum_len (lum_State *L, int idx) {
  TValue *t;
  lum_lock(L);
  t = index2value(L, idx);
  lumV_objlen(L, L->top.p, t);
  api_incr_top(L);
  lum_unlock(L);
}


LUM_API lum_Alloc lum_getallocf (lum_State *L, void **ud) {
  lum_Alloc f;
  lum_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lum_unlock(L);
  return f;
}


LUM_API void lum_setallocf (lum_State *L, lum_Alloc f, void *ud) {
  lum_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lum_unlock(L);
}


void lum_setwarnf (lum_State *L, lum_WarnFunction f, void *ud) {
  lum_lock(L);
  G(L)->ud_warn = ud;
  G(L)->warnf = f;
  lum_unlock(L);
}


void lum_warning (lum_State *L, const char *msg, int tocont) {
  lum_lock(L);
  lumE_warning(L, msg, tocont);
  lum_unlock(L);
}



LUM_API void *lum_newuserdatauv (lum_State *L, size_t size, int nuvalue) {
  Udata *u;
  lum_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < SHRT_MAX, "invalid value");
  u = lumS_newudata(L, size, cast(unsigned short, nuvalue));
  setuvalue(L, s2v(L->top.p), u);
  api_incr_top(L);
  lumC_checkGC(L);
  lum_unlock(L);
  return getudatamem(u);
}



static const char *aux_upvalue (TValue *fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttypetag(fi)) {
    case LUM_VCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(cast_uint(n) - 1u < cast_uint(f->nupvalues)))
        return NULL;  /* 'n' not in [1, f->nupvalues] */
      *val = &f->upvalue[n-1];
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case LUM_VLCL: {  /* Lum closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(cast_uint(n) - 1u  < cast_uint(p->sizeupvalues)))
        return NULL;  /* 'n' not in [1, p->sizeupvalues] */
      *val = f->upvals[n-1]->v.p;
      if (owner) *owner = obj2gco(f->upvals[n - 1]);
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


LUM_API const char *lum_getupvalue (lum_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  lum_lock(L);
  name = aux_upvalue(index2value(L, funcindex), n, &val, NULL);
  if (name) {
    setobj2s(L, L->top.p, val);
    api_incr_top(L);
  }
  lum_unlock(L);
  return name;
}


LUM_API const char *lum_setupvalue (lum_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  GCObject *owner = NULL;  /* to avoid warnings */
  TValue *fi;
  lum_lock(L);
  fi = index2value(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top.p--;
    setobj(L, val, s2v(L->top.p));
    lumC_barrier(L, owner, val);
  }
  lum_unlock(L);
  return name;
}


static UpVal **getupvalref (lum_State *L, int fidx, int n, LClosure **pf) {
  static const UpVal *const nullup = NULL;
  LClosure *f;
  TValue *fi = index2value(L, fidx);
  api_check(L, ttisLclosure(fi), "Lum function expected");
  f = clLvalue(fi);
  if (pf) *pf = f;
  if (1 <= n && n <= f->p->sizeupvalues)
    return &f->upvals[n - 1];  /* get its upvalue pointer */
  else
    return (UpVal**)&nullup;
}


LUM_API void *lum_upvalueid (lum_State *L, int fidx, int n) {
  TValue *fi = index2value(L, fidx);
  switch (ttypetag(fi)) {
    case LUM_VLCL: {  /* lum closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUM_VCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (1 <= n && n <= f->nupvalues)
        return &f->upvalue[n - 1];
      /* else */
    }  /* FALLTHROUGH */
    case LUM_VLCF:
      return NULL;  /* light C functions have no upvalues */
    default: {
      api_check(L, 0, "function expected");
      return NULL;
    }
  }
}


LUM_API void lum_upvaluejoin (lum_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  api_check(L, *up1 != NULL && *up2 != NULL, "invalid upvalue index");
  *up1 = *up2;
  lumC_objbarrier(L, f1, *up1);
}


