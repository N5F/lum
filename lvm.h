/*
** $Id: lvm.h $
** Lum virtual machine
** See Copyright Notice in lum.h
*/

#ifndef lvm_h
#define lvm_h


#include "ldo.h"
#include "lobject.h"
#include "ltm.h"


#if !defined(LUM_NOCVTN2S)
#define cvt2str(o)	ttisnumber(o)
#else
#define cvt2str(o)	0	/* no conversion from numbers to strings */
#endif


#if !defined(LUM_NOCVTS2N)
#define cvt2num(o)	ttisstring(o)
#else
#define cvt2num(o)	0	/* no conversion from strings to numbers */
#endif


/*
** You can define LUM_FLOORN2I if you want to convert floats to integers
** by flooring them (instead of raising an error if they are not
** integral values)
*/
#if !defined(LUM_FLOORN2I)
#define LUM_FLOORN2I		F2Ieq
#endif


/*
** Rounding modes for float->integer coercion
 */
typedef enum {
  F2Ieq,     /* no rounding; accepts only integral values */
  F2Ifloor,  /* takes the floor of the number */
  F2Iceil    /* takes the ceiling of the number */
} F2Imod;


/* convert an object to a float (including string coercion) */
#define tonumber(o,n) \
	(ttisfloat(o) ? (*(n) = fltvalue(o), 1) : lumV_tonumber_(o,n))


/* convert an object to a float (without string coercion) */
#define tonumberns(o,n) \
	(ttisfloat(o) ? ((n) = fltvalue(o), 1) : \
	(ttisinteger(o) ? ((n) = cast_num(ivalue(o)), 1) : 0))


/* convert an object to an integer (including string coercion) */
#define tointeger(o,i) \
  (l_likely(ttisinteger(o)) ? (*(i) = ivalue(o), 1) \
                          : lumV_tointeger(o,i,LUM_FLOORN2I))


/* convert an object to an integer (without string coercion) */
#define tointegerns(o,i) \
  (l_likely(ttisinteger(o)) ? (*(i) = ivalue(o), 1) \
                          : lumV_tointegerns(o,i,LUM_FLOORN2I))


#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))

#define lumV_rawequalobj(t1,t2)		lumV_equalobj(NULL,t1,t2)


/*
** fast track for 'gettable'
*/
#define lumV_fastget(t,k,res,f, tag) \
  (tag = (!ttistable(t) ? LUM_VNOTABLE : f(hvalue(t), k, res)))


/*
** Special case of 'lumV_fastget' for integers, inlining the fast case
** of 'lumH_getint'.
*/
#define lumV_fastgeti(t,k,res,tag) \
  if (!ttistable(t)) tag = LUM_VNOTABLE; \
  else { lumH_fastgeti(hvalue(t), k, res, tag); }


#define lumV_fastset(t,k,val,hres,f) \
  (hres = (!ttistable(t) ? HNOTATABLE : f(hvalue(t), k, val)))

#define lumV_fastseti(t,k,val,hres) \
  if (!ttistable(t)) hres = HNOTATABLE; \
  else { lumH_fastseti(hvalue(t), k, val, hres); }


/*
** Finish a fast set operation (when fast set succeeds).
*/
#define lumV_finishfastset(L,t,v)	lumC_barrierback(L, gcvalue(t), v)


/*
** Shift right is the same as shift left with a negative 'y'
*/
#define lumV_shiftr(x,y)	lumV_shiftl(x,intop(-, 0, y))



LUMI_FUNC int lumV_equalobj (lum_State *L, const TValue *t1, const TValue *t2);
LUMI_FUNC int lumV_lessthan (lum_State *L, const TValue *l, const TValue *r);
LUMI_FUNC int lumV_lessequal (lum_State *L, const TValue *l, const TValue *r);
LUMI_FUNC int lumV_tonumber_ (const TValue *obj, lum_Number *n);
LUMI_FUNC int lumV_tointeger (const TValue *obj, lum_Integer *p, F2Imod mode);
LUMI_FUNC int lumV_tointegerns (const TValue *obj, lum_Integer *p,
                                F2Imod mode);
LUMI_FUNC int lumV_flttointeger (lum_Number n, lum_Integer *p, F2Imod mode);
LUMI_FUNC lu_byte lumV_finishget (lum_State *L, const TValue *t, TValue *key,
                                                StkId val, lu_byte tag);
LUMI_FUNC void lumV_finishset (lum_State *L, const TValue *t, TValue *key,
                                             TValue *val, int aux);
LUMI_FUNC void lumV_finishOp (lum_State *L);
LUMI_FUNC void lumV_execute (lum_State *L, CallInfo *ci);
LUMI_FUNC void lumV_concat (lum_State *L, int total);
LUMI_FUNC lum_Integer lumV_idiv (lum_State *L, lum_Integer x, lum_Integer y);
LUMI_FUNC lum_Integer lumV_mod (lum_State *L, lum_Integer x, lum_Integer y);
LUMI_FUNC lum_Number lumV_modf (lum_State *L, lum_Number x, lum_Number y);
LUMI_FUNC lum_Integer lumV_shiftl (lum_Integer x, lum_Integer y);
LUMI_FUNC void lumV_objlen (lum_State *L, StkId ra, const TValue *rb);

#endif
