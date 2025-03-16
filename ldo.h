/*
** $Id: ldo.h $
** Stack and Call structure of Lum
** See Copyright Notice in lum.h
*/

#ifndef ldo_h
#define ldo_h


#include "llimits.h"
#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** It also allows the running of one GC step when the stack is
** reallocated.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/

#if !defined(HARDSTACKTESTS)
#define condmovestack(L,pre,pos)	((void)0)
#else
/* realloc stack keeping its size */
#define condmovestack(L,pre,pos)  \
  { int sz_ = stacksize(L); pre; lumD_reallocstack((L), sz_, 0); pos; }
#endif

#define lumD_checkstackaux(L,n,pre,pos)  \
	if (l_unlikely(L->stack_last.p - L->top.p <= (n))) \
	  { pre; lumD_growstack(L, n, 1); pos; } \
	else { condmovestack(L,pre,pos); }

/* In general, 'pre'/'pos' are empty (nothing to save) */
#define lumD_checkstack(L,n)	lumD_checkstackaux(L,n,(void)0,(void)0)



#define savestack(L,pt)		(cast_charp(pt) - cast_charp(L->stack.p))
#define restorestack(L,n)	cast(StkId, cast_charp(L->stack.p) + (n))


/* macro to check stack size, preserving 'p' */
#define checkstackp(L,n,p)  \
  lumD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p),  /* save 'p' */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/*
** Maximum depth for nested C calls, syntactical nested non-terminals,
** and other features implemented through recursion in C. (Value must
** fit in a 16-bit unsigned integer. It must also be compatible with
** the size of the C stack.)
*/
#if !defined(LUMI_MAXCCALLS)
#define LUMI_MAXCCALLS		200
#endif


/* type of protected functions, to be ran by 'runprotected' */
typedef void (*Pfunc) (lum_State *L, void *ud);

LUMI_FUNC void lumD_seterrorobj (lum_State *L, TStatus errcode, StkId oldtop);
LUMI_FUNC TStatus lumD_protectedparser (lum_State *L, ZIO *z,
                                                  const char *name,
                                                  const char *mode);
LUMI_FUNC void lumD_hook (lum_State *L, int event, int line,
                                        int fTransfer, int nTransfer);
LUMI_FUNC void lumD_hookcall (lum_State *L, CallInfo *ci);
LUMI_FUNC int lumD_pretailcall (lum_State *L, CallInfo *ci, StkId func,
                                              int narg1, int delta);
LUMI_FUNC CallInfo *lumD_precall (lum_State *L, StkId func, int nResults);
LUMI_FUNC void lumD_call (lum_State *L, StkId func, int nResults);
LUMI_FUNC void lumD_callnoyield (lum_State *L, StkId func, int nResults);
LUMI_FUNC TStatus lumD_closeprotected (lum_State *L, ptrdiff_t level,
                                                     TStatus status);
LUMI_FUNC TStatus lumD_pcall (lum_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
LUMI_FUNC void lumD_poscall (lum_State *L, CallInfo *ci, int nres);
LUMI_FUNC int lumD_reallocstack (lum_State *L, int newsize, int raiseerror);
LUMI_FUNC int lumD_growstack (lum_State *L, int n, int raiseerror);
LUMI_FUNC void lumD_shrinkstack (lum_State *L);
LUMI_FUNC void lumD_inctop (lum_State *L);

LUMI_FUNC l_noret lumD_throw (lum_State *L, TStatus errcode);
LUMI_FUNC TStatus lumD_rawrunprotected (lum_State *L, Pfunc f, void *ud);

#endif

