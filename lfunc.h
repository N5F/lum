/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lum.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


#define sizeCclosure(n)  \
	(offsetof(CClosure, upvalue) + sizeof(TValue) * cast_uint(n))

#define sizeLclosure(n)  \
	(offsetof(LClosure, upvals) + sizeof(UpVal *) * cast_uint(n))


/* test whether thread is in 'twups' list */
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Lum). (Value
** must fit in a VM register.)
*/
#define MAXUPVAL	255


#define upisopen(up)	((up)->v.p != &(up)->u.value)


#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v.p))


/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
#define MAXMISS		10



/* special status to close upvalues preserving the top of the stack */
#define CLOSEKTOP	(LUM_ERRERR + 1)


LUMI_FUNC Proto *lumF_newproto (lum_State *L);
LUMI_FUNC CClosure *lumF_newCclosure (lum_State *L, int nupvals);
LUMI_FUNC LClosure *lumF_newLclosure (lum_State *L, int nupvals);
LUMI_FUNC void lumF_initupvals (lum_State *L, LClosure *cl);
LUMI_FUNC UpVal *lumF_findupval (lum_State *L, StkId level);
LUMI_FUNC void lumF_newtbcupval (lum_State *L, StkId level);
LUMI_FUNC void lumF_closeupval (lum_State *L, StkId level);
LUMI_FUNC StkId lumF_close (lum_State *L, StkId level, TStatus status, int yy);
LUMI_FUNC void lumF_unlinkupval (UpVal *uv);
LUMI_FUNC lu_mem lumF_protosize (Proto *p);
LUMI_FUNC void lumF_freeproto (lum_State *L, Proto *f);
LUMI_FUNC const char *lumF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
