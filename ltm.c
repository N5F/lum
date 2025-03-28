/*
** $Id: ltm.c $
** Tag methods
** See Copyright Notice in lum.h
*/

#define ltm_c
#define LUM_CORE

#include "lprefix.h"


#include <string.h>

#include "lum.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


static const char udatatypename[] = "userdata";

LUMI_DDEF const char *const lumT_typenames_[LUM_TOTALTYPES] = {
  "no value",
  "nil", "boolean", udatatypename, "number",
  "string", "table", "function", udatatypename, "thread",
  "upvalue", "proto" /* these last cases are used for tests only */
};


void lumT_init (lum_State *L) {
  static const char *const lumT_eventname[] = {  /* ORDER TM */
    "__index", "__newindex",
    "__gc", "__mode", "__len", "__eq",
    "__add", "__sub", "__mul", "__mod", "__pow",
    "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr",
    "__unm", "__bnot", "__lt", "__le",
    "__concat", "__call", "__close"
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = lumS_new(L, lumT_eventname[i]);
    lumC_fix(L, obj2gco(G(L)->tmname[i]));  /* never collect these names */
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/
const TValue *lumT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = lumH_Hgetshortstr(events, ename);
  lum_assert(event <= TM_EQ);
  if (notm(tm)) {  /* no tag method? */
    events->flags |= cast_byte(1u<<event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}


const TValue *lumT_gettmbyobj (lum_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttype(o)) {
    case LUM_TTABLE:
      mt = hvalue(o)->metatable;
      break;
    case LUM_TUSERDATA:
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(o)];
  }
  return (mt ? lumH_Hgetshortstr(mt, G(L)->tmname[event]) : &G(L)->nilvalue);
}


/*
** Return the name of the type of an object. For tables and userdata
** with metatable, use their '__name' metafield, if present.
*/
const char *lumT_objtypename (lum_State *L, const TValue *o) {
  Table *mt;
  if ((ttistable(o) && (mt = hvalue(o)->metatable) != NULL) ||
      (ttisfulluserdata(o) && (mt = uvalue(o)->metatable) != NULL)) {
    const TValue *name = lumH_Hgetshortstr(mt, lumS_new(L, "__name"));
    if (ttisstring(name))  /* is '__name' a string? */
      return getstr(tsvalue(name));  /* use it as type name */
  }
  return ttypename(ttype(o));  /* else use standard type name */
}


void lumT_callTM (lum_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, const TValue *p3) {
  StkId func = L->top.p;
  setobj2s(L, func, f);  /* push function (assume EXTRA_STACK) */
  setobj2s(L, func + 1, p1);  /* 1st argument */
  setobj2s(L, func + 2, p2);  /* 2nd argument */
  setobj2s(L, func + 3, p3);  /* 3rd argument */
  L->top.p = func + 4;
  /* metamethod may yield only when called from Lum code */
  if (isLumcode(L->ci))
    lumD_call(L, func, 0);
  else
    lumD_callnoyield(L, func, 0);
}


lu_byte lumT_callTMres (lum_State *L, const TValue *f, const TValue *p1,
                        const TValue *p2, StkId res) {
  ptrdiff_t result = savestack(L, res);
  StkId func = L->top.p;
  setobj2s(L, func, f);  /* push function (assume EXTRA_STACK) */
  setobj2s(L, func + 1, p1);  /* 1st argument */
  setobj2s(L, func + 2, p2);  /* 2nd argument */
  L->top.p += 3;
  /* metamethod may yield only when called from Lum code */
  if (isLumcode(L->ci))
    lumD_call(L, func, 1);
  else
    lumD_callnoyield(L, func, 1);
  res = restorestack(L, result);
  setobjs2s(L, res, --L->top.p);  /* move result to its place */
  return ttypetag(s2v(res));  /* return tag of the result */
}


static int callbinTM (lum_State *L, const TValue *p1, const TValue *p2,
                      StkId res, TMS event) {
  const TValue *tm = lumT_gettmbyobj(L, p1, event);  /* try first operand */
  if (notm(tm))
    tm = lumT_gettmbyobj(L, p2, event);  /* try second operand */
  if (notm(tm))
    return -1;  /* tag method not found */
  else  /* call tag method and return the tag of the result */
    return lumT_callTMres(L, tm, p1, p2, res);
}


void lumT_trybinTM (lum_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  if (l_unlikely(callbinTM(L, p1, p2, res, event) < 0)) {
    switch (event) {
      case TM_BAND: case TM_BOR: case TM_BXOR:
      case TM_SHL: case TM_SHR: case TM_BNOT: {
        if (ttisnumber(p1) && ttisnumber(p2))
          lumG_tointerror(L, p1, p2);
        else
          lumG_opinterror(L, p1, p2, "perform bitwise operation on");
      }
      /* calls never return, but to avoid warnings: *//* FALLTHROUGH */
      default:
        lumG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}


/*
** The use of 'p1' after 'callbinTM' is safe because, when a tag
** method is not found, 'callbinTM' cannot change the stack.
*/
void lumT_tryconcatTM (lum_State *L) {
  StkId p1 = L->top.p - 2;  /* first argument */
  if (l_unlikely(callbinTM(L, s2v(p1), s2v(p1 + 1), p1, TM_CONCAT) < 0))
    lumG_concaterror(L, s2v(p1), s2v(p1 + 1));
}


void lumT_trybinassocTM (lum_State *L, const TValue *p1, const TValue *p2,
                                       int flip, StkId res, TMS event) {
  if (flip)
    lumT_trybinTM(L, p2, p1, res, event);
  else
    lumT_trybinTM(L, p1, p2, res, event);
}


void lumT_trybiniTM (lum_State *L, const TValue *p1, lum_Integer i2,
                                   int flip, StkId res, TMS event) {
  TValue aux;
  setivalue(&aux, i2);
  lumT_trybinassocTM(L, p1, &aux, flip, res, event);
}


/*
** Calls an order tag method.
** For lessequal, LUM_COMPAT_LT_LE keeps compatibility with old
** behavior: if there is no '__le', try '__lt', based on l <= r iff
** !(r < l) (assuming a total order). If the metamethod yields during
** this substitution, the continuation has to know about it (to negate
** the result of r<l); bit CIST_LEQ in the call status keeps that
** information.
*/
int lumT_callorderTM (lum_State *L, const TValue *p1, const TValue *p2,
                      TMS event) {
  int tag = callbinTM(L, p1, p2, L->top.p, event);  /* try original event */
  if (tag >= 0)  /* found tag method? */
    return !tagisfalse(tag);
#if defined(LUM_COMPAT_LT_LE)
  else if (event == TM_LE) {
    /* try '!(p2 < p1)' for '(p1 <= p2)' */
    L->ci->callstatus |= CIST_LEQ;  /* mark it is doing 'lt' for 'le' */
    tag = callbinTM(L, p2, p1, L->top.p, TM_LT);
    L->ci->callstatus ^= CIST_LEQ;  /* clear mark */
    if (tag >= 0)  /* found tag method? */
      return tagisfalse(tag);
  }
#endif
  lumG_ordererror(L, p1, p2);  /* no metamethod found */
  return 0;  /* to avoid warnings */
}


int lumT_callorderiTM (lum_State *L, const TValue *p1, int v2,
                       int flip, int isfloat, TMS event) {
  TValue aux; const TValue *p2;
  if (isfloat) {
    setfltvalue(&aux, cast_num(v2));
  }
  else
    setivalue(&aux, v2);
  if (flip) {  /* arguments were exchanged? */
    p2 = p1; p1 = &aux;  /* correct them */
  }
  else
    p2 = &aux;
  return lumT_callorderTM(L, p1, p2, event);
}


void lumT_adjustvarargs (lum_State *L, int nfixparams, CallInfo *ci,
                         const Proto *p) {
  int i;
  int actual = cast_int(L->top.p - ci->func.p) - 1;  /* number of arguments */
  int nextra = actual - nfixparams;  /* number of extra arguments */
  ci->u.l.nextraargs = nextra;
  lumD_checkstack(L, p->maxstacksize + 1);
  /* copy function to the top of the stack */
  setobjs2s(L, L->top.p++, ci->func.p);
  /* move fixed parameters to the top of the stack */
  for (i = 1; i <= nfixparams; i++) {
    setobjs2s(L, L->top.p++, ci->func.p + i);
    setnilvalue(s2v(ci->func.p + i));  /* erase original parameter (for GC) */
  }
  ci->func.p += actual + 1;
  ci->top.p += actual + 1;
  lum_assert(L->top.p <= ci->top.p && ci->top.p <= L->stack_last.p);
}


void lumT_getvarargs (lum_State *L, CallInfo *ci, StkId where, int wanted) {
  int i;
  int nextra = ci->u.l.nextraargs;
  if (wanted < 0) {
    wanted = nextra;  /* get all extra arguments available */
    checkstackp(L, nextra, where);  /* ensure stack space */
    L->top.p = where + nextra;  /* next instruction will need top */
  }
  for (i = 0; i < wanted && i < nextra; i++)
    setobjs2s(L, where + i, ci->func.p - nextra + i);
  for (; i < wanted; i++)   /* complete required results with nil */
    setnilvalue(s2v(where + i));
}

