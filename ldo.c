/*
** $Id: ldo.c $
** Stack and Call structure of Lum
** See Copyright Notice in lum.h
*/

#define ldo_c
#define LUM_CORE

#include "lprefix.h"


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"



#define errorstatus(s)	((s) > LUM_YIELD)


/*
** these macros allow user-specific actions when a thread is
** resumed/yielded.
*/
#if !defined(lumi_userstateresume)
#define lumi_userstateresume(L,n)	((void)L)
#endif

#if !defined(lumi_userstateyield)
#define lumi_userstateyield(L,n)	((void)L)
#endif


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUMI_THROW/LUMI_TRY define how Lum does exception handling. By
** default, Lum handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUMI_THROW)				/* { */

#if defined(__cplusplus) && !defined(LUM_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define LUMI_THROW(L,c)		throw(c)
#define LUMI_TRY(L,c,f,ud) \
    try { (f)(L, ud); } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define lumi_jmpbuf		int  /* dummy field */

#elif defined(LUM_USE_POSIX)				/* }{ */

/* in POSIX, try _longjmp/_setjmp (more efficient) */
#define LUMI_THROW(L,c)		_longjmp((c)->b, 1)
#define LUMI_TRY(L,c,f,ud)	if (_setjmp((c)->b) == 0) ((f)(L, ud))
#define lumi_jmpbuf		jmp_buf

#else							/* }{ */

/* ISO C handling with long jumps */
#define LUMI_THROW(L,c)		longjmp((c)->b, 1)
#define LUMI_TRY(L,c,f,ud)	if (setjmp((c)->b) == 0) ((f)(L, ud))
#define lumi_jmpbuf		jmp_buf

#endif							/* } */

#endif							/* } */



/* chain list of long jump buffers */
struct lum_longjmp {
  struct lum_longjmp *previous;
  lumi_jmpbuf b;
  volatile TStatus status;  /* error code */
};


void lumD_seterrorobj (lum_State *L, TStatus errcode, StkId oldtop) {
  switch (errcode) {
    case LUM_ERRMEM: {  /* memory error? */
      setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
      break;
    }
    case LUM_ERRERR: {
      setsvalue2s(L, oldtop, lumS_newliteral(L, "error in error handling"));
      break;
    }
    default: {
      lum_assert(errorstatus(errcode));  /* must be a real error */
      if (!ttisnil(s2v(L->top.p - 1))) {  /* error object is not nil? */
        setobjs2s(L, oldtop, L->top.p - 1);  /* move it to 'oldtop' */
      }
      else  /* change it to a proper message */
        setsvalue2s(L, oldtop, lumS_newliteral(L, "<error object is nil>"));
      break;
    }
  }
  L->top.p = oldtop + 1;  /* top goes back to old top plus error object */
}


l_noret lumD_throw (lum_State *L, TStatus errcode) {
  if (L->errorJmp) {  /* thread has an error handler? */
    L->errorJmp->status = errcode;  /* set status */
    LUMI_THROW(L, L->errorJmp);  /* jump to it */
  }
  else {  /* thread has no error handler */
    global_State *g = G(L);
    lum_State *mainth = mainthread(g);
    errcode = lumE_resetthread(L, errcode);  /* close all upvalues */
    L->status = errcode;
    if (mainth->errorJmp) {  /* main thread has a handler? */
      setobjs2s(L, mainth->top.p++, L->top.p - 1);  /* copy error obj. */
      lumD_throw(mainth, errcode);  /* re-throw in main thread */
    }
    else {  /* no handler at all; abort */
      if (g->panic) {  /* panic function? */
        lum_unlock(L);
        g->panic(L);  /* call panic function (last chance to jump out) */
      }
      abort();
    }
  }
}


TStatus lumD_rawrunprotected (lum_State *L, Pfunc f, void *ud) {
  l_uint32 oldnCcalls = L->nCcalls;
  struct lum_longjmp lj;
  lj.status = LUM_OK;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  LUMI_TRY(L, &lj, f, ud);  /* call 'f' catching errors */
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }====================================================== */


/*
** {==================================================================
** Stack reallocation
** ===================================================================
*/

/* some stack space for error handling */
#define STACKERRSPACE	200


/* maximum stack size that respects size_t */
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

/*
** Minimum between LUMI_MAXSTACK and MAXSTACK_BYSIZET
** (Maximum size for the stack must respect size_t.)
*/
#define MAXSTACK	cast_int(LUMI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? LUMI_MAXSTACK : MAXSTACK_BYSIZET)


/* stack size with extra space for error handling */
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


/*
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before a stack reallocation, all pointers
** should be changed to offsets, and after the reallocation they should
** be changed back to pointers. As during the reallocation the pointers
** are invalid, the reallocation cannot run emergency collections.
** Alternatively, we can use the old address after the deallocation.
** That is not strict ISO C, but seems to work fine everywhere.
** The following macro chooses how strict is the code.
*/
#if !defined(LUMI_STRICT_ADDRESS)
#define LUMI_STRICT_ADDRESS	0
#endif

#if LUMI_STRICT_ADDRESS
/*
** Change all pointers to the stack into offsets.
*/
static void relstack (lum_State *L) {
  CallInfo *ci;
  UpVal *up;
  L->top.offset = savestack(L, L->top.p);
  L->tbclist.offset = savestack(L, L->tbclist.p);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.offset = savestack(L, uplevel(up));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.offset = savestack(L, ci->top.p);
    ci->func.offset = savestack(L, ci->func.p);
  }
}


/*
** Change back all offsets into pointers.
*/
static void correctstack (lum_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  UNUSED(oldstack);
  L->top.p = restorestack(L, L->top.offset);
  L->tbclist.p = restorestack(L, L->tbclist.offset);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(restorestack(L, up->v.offset));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = restorestack(L, ci->top.offset);
    ci->func.p = restorestack(L, ci->func.offset);
    if (isLum(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'lumV_execute' */
  }
}

#else
/*
** Assume that it is fine to use an address after its deallocation,
** as long as we do not dereference it.
*/

static void relstack (lum_State *L) { UNUSED(L); }  /* do nothing */


/*
** Correct pointers into 'oldstack' to point into 'L->stack'.
*/
static void correctstack (lum_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  StkId newstack = L->stack.p;
  if (oldstack == newstack)
    return;
  L->top.p = L->top.p - oldstack + newstack;
  L->tbclist.p = L->tbclist.p - oldstack + newstack;
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(uplevel(up) - oldstack + newstack);
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = ci->top.p - oldstack + newstack;
    ci->func.p = ci->func.p - oldstack + newstack;
    if (isLum(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'lumV_execute' */
  }
}
#endif


/*
** Reallocate the stack to a new size, correcting all pointers into it.
** In case of allocation error, raise an error or return false according
** to 'raiseerror'.
*/
int lumD_reallocstack (lum_State *L, int newsize, int raiseerror) {
  int oldsize = stacksize(L);
  int i;
  StkId newstack;
  StkId oldstack = L->stack.p;
  lu_byte oldgcstop = G(L)->gcstopem;
  lum_assert(newsize <= MAXSTACK || newsize == ERRORSTACKSIZE);
  relstack(L);  /* change pointers to offsets */
  G(L)->gcstopem = 1;  /* stop emergency collection */
  newstack = lumM_reallocvector(L, oldstack, oldsize + EXTRA_STACK,
                                   newsize + EXTRA_STACK, StackValue);
  G(L)->gcstopem = oldgcstop;  /* restore emergency collection */
  if (l_unlikely(newstack == NULL)) {  /* reallocation failed? */
    correctstack(L, oldstack);  /* change offsets back to pointers */
    if (raiseerror)
      lumM_error(L);
    else return 0;  /* do not raise an error */
  }
  L->stack.p = newstack;
  correctstack(L, oldstack);  /* change offsets back to pointers */
  L->stack_last.p = L->stack.p + newsize;
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i)); /* erase new segment */
  return 1;
}


/*
** Try to grow the stack by at least 'n' elements. When 'raiseerror'
** is true, raises any error; otherwise, return 0 in case of errors.
*/
int lumD_growstack (lum_State *L, int n, int raiseerror) {
  int size = stacksize(L);
  if (l_unlikely(size > MAXSTACK)) {
    /* if stack is larger than maximum, thread is already using the
       extra space reserved for errors, that is, thread is handling
       a stack error; cannot grow further than that. */
    lum_assert(stacksize(L) == ERRORSTACKSIZE);
    if (raiseerror)
      lumD_throw(L, LUM_ERRERR);  /* error inside message handler */
    return 0;  /* if not 'raiseerror', just signal it */
  }
  else if (n < MAXSTACK) {  /* avoids arithmetic overflows */
    int newsize = 2 * size;  /* tentative new size */
    int needed = cast_int(L->top.p - L->stack.p) + n;
    if (newsize > MAXSTACK)  /* cannot cross the limit */
      newsize = MAXSTACK;
    if (newsize < needed)  /* but must respect what was asked for */
      newsize = needed;
    if (l_likely(newsize <= MAXSTACK))
      return lumD_reallocstack(L, newsize, raiseerror);
  }
  /* else stack overflow */
  /* add extra size to be able to handle the error message */
  lumD_reallocstack(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    lumG_runerror(L, "stack overflow");
  return 0;
}


/*
** Compute how much of the stack is being used, by computing the
** maximum top of all call frames in the stack and the current top.
*/
static int stackinuse (lum_State *L) {
  CallInfo *ci;
  int res;
  StkId lim = L->top.p;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    if (lim < ci->top.p) lim = ci->top.p;
  }
  lum_assert(lim <= L->stack_last.p + EXTRA_STACK);
  res = cast_int(lim - L->stack.p) + 1;  /* part of stack in use */
  if (res < LUM_MINSTACK)
    res = LUM_MINSTACK;  /* ensure a minimum size */
  return res;
}


/*
** If stack size is more than 3 times the current use, reduce that size
** to twice the current use. (So, the final stack size is at most 2/3 the
** previous size, and half of its entries are empty.)
** As a particular case, if stack was handling a stack overflow and now
** it is not, 'max' (limited by MAXSTACK) will be smaller than
** stacksize (equal to ERRORSTACKSIZE in this case), and so the stack
** will be reduced to a "regular" size.
*/
void lumD_shrinkstack (lum_State *L) {
  int inuse = stackinuse(L);
  int max = (inuse > MAXSTACK / 3) ? MAXSTACK : inuse * 3;
  /* if thread is currently not handling a stack overflow and its
     size is larger than maximum "reasonable" size, shrink it */
  if (inuse <= MAXSTACK && stacksize(L) > max) {
    int nsize = (inuse > MAXSTACK / 2) ? MAXSTACK : inuse * 2;
    lumD_reallocstack(L, nsize, 0);  /* ok if that fails */
  }
  else  /* don't change stack */
    condmovestack(L,(void)0,(void)0);  /* (change only for debugging) */
  lumE_shrinkCI(L);  /* shrink CI list */
}


void lumD_inctop (lum_State *L) {
  L->top.p++;
  lumD_checkstack(L, 1);
}

/* }================================================================== */


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which trigger this
** function, can be changed asynchronously by signals.)
*/
void lumD_hook (lum_State *L, int event, int line,
                              int ftransfer, int ntransfer) {
  lum_Hook hook = L->hook;
  if (hook && L->allowhook) {  /* make sure there is a hook */
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top.p);  /* preserve original 'top' */
    ptrdiff_t ci_top = savestack(L, ci->top.p);  /* idem for 'ci->top' */
    lum_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    L->transferinfo.ftransfer = ftransfer;
    L->transferinfo.ntransfer = ntransfer;
    if (isLum(ci) && L->top.p < ci->top.p)
      L->top.p = ci->top.p;  /* protect entire activation register */
    lumD_checkstack(L, LUM_MINSTACK);  /* ensure minimum stack size */
    if (ci->top.p < L->top.p + LUM_MINSTACK)
      ci->top.p = L->top.p + LUM_MINSTACK;
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= CIST_HOOKED;
    lum_unlock(L);
    (*hook)(L, &ar);
    lum_lock(L);
    lum_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top.p = restorestack(L, ci_top);
    L->top.p = restorestack(L, top);
    ci->callstatus &= ~CIST_HOOKED;
  }
}


/*
** Executes a call hook for Lum functions. This function is called
** whenever 'hookmask' is not zero, so it checks whether call hooks are
** active.
*/
void lumD_hookcall (lum_State *L, CallInfo *ci) {
  L->oldpc = 0;  /* set 'oldpc' for new function */
  if (L->hookmask & LUM_MASKCALL) {  /* is call hook on? */
    int event = (ci->callstatus & CIST_TAIL) ? LUM_HOOKTAILCALL
                                             : LUM_HOOKCALL;
    Proto *p = ci_func(ci)->p;
    ci->u.l.savedpc++;  /* hooks assume 'pc' is already incremented */
    lumD_hook(L, event, -1, 1, p->numparams);
    ci->u.l.savedpc--;  /* correct 'pc' */
  }
}


/*
** Executes a return hook for Lum and C functions and sets/corrects
** 'oldpc'. (Note that this correction is needed by the line hook, so it
** is done even when return hooks are off.)
*/
static void rethook (lum_State *L, CallInfo *ci, int nres) {
  if (L->hookmask & LUM_MASKRET) {  /* is return hook on? */
    StkId firstres = L->top.p - nres;  /* index of first result */
    int delta = 0;  /* correction for vararg functions */
    int ftransfer;
    if (isLum(ci)) {
      Proto *p = ci_func(ci)->p;
      if (p->flag & PF_ISVARARG)
        delta = ci->u.l.nextraargs + p->numparams + 1;
    }
    ci->func.p += delta;  /* if vararg, back to virtual 'func' */
    ftransfer = cast_int(firstres - ci->func.p);
    lumD_hook(L, LUM_HOOKRET, -1, ftransfer, nres);  /* call it */
    ci->func.p -= delta;
  }
  if (isLum(ci = ci->previous))
    L->oldpc = pcRel(ci->u.l.savedpc, ci_func(ci)->p);  /* set 'oldpc' */
}


/*
** Check whether 'func' has a '__call' metafield. If so, put it in the
** stack, below original 'func', so that 'lumD_precall' can call it.
** Raise an error if there is no '__call' metafield.
** Bits CIST_CCMT in status count how many _call metamethods were
** invoked and how many corresponding extra arguments were pushed.
** (This count will be saved in the 'callstatus' of the call).
**  Raise an error if this counter overflows.
*/
static unsigned tryfuncTM (lum_State *L, StkId func, unsigned status) {
  const TValue *tm;
  StkId p;
  tm = lumT_gettmbyobj(L, s2v(func), TM_CALL);
  if (l_unlikely(ttisnil(tm)))  /* no metamethod? */
    lumG_callerror(L, s2v(func));
  for (p = L->top.p; p > func; p--)  /* open space for metamethod */
    setobjs2s(L, p, p-1);
  L->top.p++;  /* stack space pre-allocated by the caller */
  setobj2s(L, func, tm);  /* metamethod is the new function to be called */
  if ((status & MAX_CCMT) == MAX_CCMT)  /* is counter full? */
    lumG_runerror(L, "'__call' chain too long");
  return status + (1u << CIST_CCMT);  /* increment counter */
}


/* Generic case for 'moveresult' */
l_sinline void genmoveresults (lum_State *L, StkId res, int nres,
                                             int wanted) {
  StkId firstresult = L->top.p - nres;  /* index of first result */
  int i;
  if (nres > wanted)  /* extra results? */
    nres = wanted;  /* don't need them */
  for (i = 0; i < nres; i++)  /* move all results to correct place */
    setobjs2s(L, res + i, firstresult + i);
  for (; i < wanted; i++)  /* complete wanted number of results */
    setnilvalue(s2v(res + i));
  L->top.p = res + wanted;  /* top points after the last result */
}


/*
** Given 'nres' results at 'firstResult', move 'fwanted-1' of them
** to 'res'.  Handle most typical cases (zero results for commands,
** one result for expressions, multiple results for tail calls/single
** parameters) separated. The flag CIST_TBC in 'fwanted', if set,
** forces the swicth to go to the default case.
*/
l_sinline void moveresults (lum_State *L, StkId res, int nres,
                                          l_uint32 fwanted) {
  switch (fwanted) {  /* handle typical cases separately */
    case 0 + 1:  /* no values needed */
      L->top.p = res;
      return;
    case 1 + 1:  /* one value needed */
      if (nres == 0)   /* no results? */
        setnilvalue(s2v(res));  /* adjust with nil */
      else  /* at least one result */
        setobjs2s(L, res, L->top.p - nres);  /* move it to proper place */
      L->top.p = res + 1;
      return;
    case LUM_MULTRET + 1:
      genmoveresults(L, res, nres, nres);  /* we want all results */
      break;
    default: {  /* two/more results and/or to-be-closed variables */
      int wanted = get_nresults(fwanted);
      if (fwanted & CIST_TBC) {  /* to-be-closed variables? */
        L->ci->u2.nres = nres;
        L->ci->callstatus |= CIST_CLSRET;  /* in case of yields */
        res = lumF_close(L, res, CLOSEKTOP, 1);
        L->ci->callstatus &= ~CIST_CLSRET;
        if (L->hookmask) {  /* if needed, call hook after '__close's */
          ptrdiff_t savedres = savestack(L, res);
          rethook(L, L->ci, nres);
          res = restorestack(L, savedres);  /* hook can move stack */
        }
        if (wanted == LUM_MULTRET)
          wanted = nres;  /* we want all results */
      }
      genmoveresults(L, res, nres, wanted);
      break;
    }
  }
}


/*
** Finishes a function call: calls hook if necessary, moves current
** number of results to proper place, and returns to previous call
** info. If function has to close variables, hook must be called after
** that.
*/
void lumD_poscall (lum_State *L, CallInfo *ci, int nres) {
  l_uint32 fwanted = ci->callstatus & (CIST_TBC | CIST_NRESULTS);
  if (l_unlikely(L->hookmask) && !(fwanted & CIST_TBC))
    rethook(L, ci, nres);
  /* move results to proper place */
  moveresults(L, ci->func.p, nres, fwanted);
  /* function cannot be in any of these cases when returning */
  lum_assert(!(ci->callstatus &
        (CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_CLSRET)));
  L->ci = ci->previous;  /* back to caller (after closing variables) */
}



#define next_ci(L)  (L->ci->next ? L->ci->next : lumE_extendCI(L))


/*
** Allocate and initialize CallInfo structure. At this point, the
** only valid fields in the call status are number of results,
** CIST_C (if it's a C function), and number of extra arguments.
** (All these bit-fields fit in 16-bit values.)
*/
l_sinline CallInfo *prepCallInfo (lum_State *L, StkId func, unsigned status,
                                                StkId top) {
  CallInfo *ci = L->ci = next_ci(L);  /* new frame */
  ci->func.p = func;
  lum_assert((status & ~(CIST_NRESULTS | CIST_C | MAX_CCMT)) == 0);
  ci->callstatus = status;
  ci->top.p = top;
  return ci;
}


/*
** precall for C functions
*/
l_sinline int precallC (lum_State *L, StkId func, unsigned status,
                                            lum_CFunction f) {
  int n;  /* number of returns */
  CallInfo *ci;
  checkstackp(L, LUM_MINSTACK, func);  /* ensure minimum stack size */
  L->ci = ci = prepCallInfo(L, func, status | CIST_C,
                               L->top.p + LUM_MINSTACK);
  lum_assert(ci->top.p <= L->stack_last.p);
  if (l_unlikely(L->hookmask & LUM_MASKCALL)) {
    int narg = cast_int(L->top.p - func) - 1;
    lumD_hook(L, LUM_HOOKCALL, -1, 1, narg);
  }
  lum_unlock(L);
  n = (*f)(L);  /* do the actual call */
  lum_lock(L);
  api_checknelems(L, n);
  lumD_poscall(L, ci, n);
  return n;
}


/*
** Prepare a function for a tail call, building its call info on top
** of the current call info. 'narg1' is the number of arguments plus 1
** (so that it includes the function itself). Return the number of
** results, if it was a C function, or -1 for a Lum function.
*/
int lumD_pretailcall (lum_State *L, CallInfo *ci, StkId func,
                                    int narg1, int delta) {
  unsigned status = LUM_MULTRET + 1;
 retry:
  switch (ttypetag(s2v(func))) {
    case LUM_VCCL:  /* C closure */
      return precallC(L, func, status, clCvalue(s2v(func))->f);
    case LUM_VLCF:  /* light C function */
      return precallC(L, func, status, fvalue(s2v(func)));
    case LUM_VLCL: {  /* Lum function */
      Proto *p = clLvalue(s2v(func))->p;
      int fsize = p->maxstacksize;  /* frame size */
      int nfixparams = p->numparams;
      int i;
      checkstackp(L, fsize - delta, func);
      ci->func.p -= delta;  /* restore 'func' (if vararg) */
      for (i = 0; i < narg1; i++)  /* move down function and arguments */
        setobjs2s(L, ci->func.p + i, func + i);
      func = ci->func.p;  /* moved-down function */
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));  /* complete missing arguments */
      ci->top.p = func + 1 + fsize;  /* top for new function */
      lum_assert(ci->top.p <= L->stack_last.p);
      ci->u.l.savedpc = p->code;  /* starting point */
      ci->callstatus |= CIST_TAIL;
      L->top.p = func + narg1;  /* set top */
      return -1;
    }
    default: {  /* not a function */
      checkstackp(L, 1, func);  /* space for metamethod */
      status = tryfuncTM(L, func, status);  /* try '__call' metamethod */
      narg1++;
      goto retry;  /* try again */
    }
  }
}


/*
** Prepares the call to a function (C or Lum). For C functions, also do
** the call. The function to be called is at '*func'.  The arguments
** are on the stack, right after the function.  Returns the CallInfo
** to be executed, if it was a Lum function. Otherwise (a C function)
** returns NULL, with all the results on the stack, starting at the
** original function position.
*/
CallInfo *lumD_precall (lum_State *L, StkId func, int nresults) {
  unsigned status = cast_uint(nresults + 1);
  lum_assert(status <= MAXRESULTS + 1);
 retry:
  switch (ttypetag(s2v(func))) {
    case LUM_VCCL:  /* C closure */
      precallC(L, func, status, clCvalue(s2v(func))->f);
      return NULL;
    case LUM_VLCF:  /* light C function */
      precallC(L, func, status, fvalue(s2v(func)));
      return NULL;
    case LUM_VLCL: {  /* Lum function */
      CallInfo *ci;
      Proto *p = clLvalue(s2v(func))->p;
      int narg = cast_int(L->top.p - func) - 1;  /* number of real arguments */
      int nfixparams = p->numparams;
      int fsize = p->maxstacksize;  /* frame size */
      checkstackp(L, fsize, func);
      L->ci = ci = prepCallInfo(L, func, status, func + 1 + fsize);
      ci->u.l.savedpc = p->code;  /* starting point */
      for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));  /* complete missing arguments */
      lum_assert(ci->top.p <= L->stack_last.p);
      return ci;
    }
    default: {  /* not a function */
      checkstackp(L, 1, func);  /* space for metamethod */
      status = tryfuncTM(L, func, status);  /* try '__call' metamethod */
      goto retry;  /* try again with metamethod */
    }
  }
}


/*
** Call a function (C or Lum) through C. 'inc' can be 1 (increment
** number of recursive invocations in the C stack) or nyci (the same
** plus increment number of non-yieldable calls).
** This function can be called with some use of EXTRA_STACK, so it should
** check the stack before doing anything else. 'lumD_precall' already
** does that.
*/
l_sinline void ccall (lum_State *L, StkId func, int nResults, l_uint32 inc) {
  CallInfo *ci;
  L->nCcalls += inc;
  if (l_unlikely(getCcalls(L) >= LUMI_MAXCCALLS)) {
    checkstackp(L, 0, func);  /* free any use of EXTRA_STACK */
    lumE_checkcstack(L);
  }
  if ((ci = lumD_precall(L, func, nResults)) != NULL) {  /* Lum function? */
    ci->callstatus |= CIST_FRESH;  /* mark that it is a "fresh" execute */
    lumV_execute(L, ci);  /* call it */
  }
  L->nCcalls -= inc;
}


/*
** External interface for 'ccall'
*/
void lumD_call (lum_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, 1);
}


/*
** Similar to 'lumD_call', but does not allow yields during the call.
*/
void lumD_callnoyield (lum_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, nyci);
}


/*
** Finish the job of 'lum_pcallk' after it was interrupted by an yield.
** (The caller, 'finishCcall', does the final call to 'adjustresults'.)
** The main job is to complete the 'lumD_pcall' called by 'lum_pcallk'.
** If a '__close' method yields here, eventually control will be back
** to 'finishCcall' (when that '__close' method finally returns) and
** 'finishpcallk' will run again and close any still pending '__close'
** methods. Similarly, if a '__close' method errs, 'precover' calls
** 'unroll' which calls ''finishCcall' and we are back here again, to
** close any pending '__close' methods.
** Note that, up to the call to 'lumF_close', the corresponding
** 'CallInfo' is not modified, so that this repeated run works like the
** first one (except that it has at least one less '__close' to do). In
** particular, field CIST_RECST preserves the error status across these
** multiple runs, changing only if there is a new error.
*/
static TStatus finishpcallk (lum_State *L,  CallInfo *ci) {
  TStatus status = getcistrecst(ci);  /* get original status */
  if (l_likely(status == LUM_OK))  /* no error? */
    status = LUM_YIELD;  /* was interrupted by an yield */
  else {  /* error */
    StkId func = restorestack(L, ci->u2.funcidx);
    L->allowhook = getoah(ci);  /* restore 'allowhook' */
    func = lumF_close(L, func, status, 1);  /* can yield or raise an error */
    lumD_seterrorobj(L, status, func);
    lumD_shrinkstack(L);   /* restore stack size in case of overflow */
    setcistrecst(ci, LUM_OK);  /* clear original status */
  }
  ci->callstatus &= ~CIST_YPCALL;
  L->errfunc = ci->u.c.old_errfunc;
  /* if it is here, there were errors or yields; unlike 'lum_pcallk',
     do not change status */
  return status;
}


/*
** Completes the execution of a C function interrupted by an yield.
** The interruption must have happened while the function was either
** closing its tbc variables in 'moveresults' or executing
** 'lum_callk'/'lum_pcallk'. In the first case, it just redoes
** 'lumD_poscall'. In the second case, the call to 'finishpcallk'
** finishes the interrupted execution of 'lum_pcallk'.  After that, it
** calls the continuation of the interrupted function and finally it
** completes the job of the 'lumD_call' that called the function.  In
** the call to 'adjustresults', we do not know the number of results
** of the function called by 'lum_callk'/'lum_pcallk', so we are
** conservative and use LUM_MULTRET (always adjust).
*/
static void finishCcall (lum_State *L, CallInfo *ci) {
  int n;  /* actual number of results from C function */
  if (ci->callstatus & CIST_CLSRET) {  /* was closing TBC variable? */
    lum_assert(ci->callstatus & CIST_TBC);
    n = ci->u2.nres;  /* just redo 'lumD_poscall' */
    /* don't need to reset CIST_CLSRET, as it will be set again anyway */
  }
  else {
    TStatus status = LUM_YIELD;  /* default if there were no errors */
    lum_KFunction kf = ci->u.c.k;  /* continuation function */
    /* must have a continuation and must be able to call it */
    lum_assert(kf != NULL && yieldable(L));
    if (ci->callstatus & CIST_YPCALL)   /* was inside a 'lum_pcallk'? */
      status = finishpcallk(L, ci);  /* finish it */
    adjustresults(L, LUM_MULTRET);  /* finish 'lum_callk' */
    lum_unlock(L);
    n = (*kf)(L, APIstatus(status), ci->u.c.ctx);  /* call continuation */
    lum_lock(L);
    api_checknelems(L, n);
  }
  lumD_poscall(L, ci, n);  /* finish 'lumD_call' */
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop).
*/
static void unroll (lum_State *L, void *ud) {
  CallInfo *ci;
  UNUSED(ud);
  while ((ci = L->ci) != &L->base_ci) {  /* something in the stack */
    if (!isLum(ci))  /* C function? */
      finishCcall(L, ci);  /* complete its execution */
    else {  /* Lum function */
      lumV_finishOp(L);  /* finish interrupted instruction */
      lumV_execute(L, ci);  /* execute down to higher C 'boundary' */
    }
  }
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallInfo *findpcall (lum_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


/*
** Signal an error in the call to 'lum_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error (lum_State *L, const char *msg, int narg) {
  api_checkpop(L, narg);
  L->top.p -= narg;  /* remove args from the stack */
  setsvalue2s(L, L->top.p, lumS_new(L, msg));  /* push error message */
  api_incr_top(L);
  lum_unlock(L);
  return LUM_ERRRUN;
}


/*
** Do the work for 'lum_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume (lum_State *L, void *ud) {
  int n = *(cast(int*, ud));  /* number of arguments */
  StkId firstArg = L->top.p - n;  /* first argument */
  CallInfo *ci = L->ci;
  if (L->status == LUM_OK)  /* starting a coroutine? */
    ccall(L, firstArg - 1, LUM_MULTRET, 0);  /* just call its body */
  else {  /* resuming from previous yield */
    lum_assert(L->status == LUM_YIELD);
    L->status = LUM_OK;  /* mark that it is running (again) */
    if (isLum(ci)) {  /* yielded inside a hook? */
      /* undo increment made by 'lumG_traceexec': instruction was not
         executed yet */
      lum_assert(ci->callstatus & CIST_HOOKYIELD);
      ci->u.l.savedpc--;
      L->top.p = firstArg;  /* discard arguments */
      lumV_execute(L, ci);  /* just continue running Lum code */
    }
    else {  /* 'common' yield */
      if (ci->u.c.k != NULL) {  /* does it have a continuation function? */
        lum_unlock(L);
        n = (*ci->u.c.k)(L, LUM_YIELD, ci->u.c.ctx); /* call continuation */
        lum_lock(L);
        api_checknelems(L, n);
      }
      lumD_poscall(L, ci, n);  /* finish 'lumD_call' */
    }
    unroll(L, NULL);  /* run continuation */
  }
}


/*
** Unrolls a coroutine in protected mode while there are recoverable
** errors, that is, errors inside a protected call. (Any error
** interrupts 'unroll', and this loop protects it again so it can
** continue.) Stops with a normal end (status == LUM_OK), an yield
** (status == LUM_YIELD), or an unprotected error ('findpcall' doesn't
** find a recover point).
*/
static TStatus precover (lum_State *L, TStatus status) {
  CallInfo *ci;
  while (errorstatus(status) && (ci = findpcall(L)) != NULL) {
    L->ci = ci;  /* go down to recovery functions */
    setcistrecst(ci, status);  /* status to finish 'pcall' */
    status = lumD_rawrunprotected(L, unroll, NULL);
  }
  return status;
}


LUM_API int lum_resume (lum_State *L, lum_State *from, int nargs,
                                      int *nresults) {
  TStatus status;
  lum_lock(L);
  if (L->status == LUM_OK) {  /* may be starting a coroutine */
    if (L->ci != &L->base_ci)  /* not in base level? */
      return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    else if (L->top.p - (L->ci->func.p + 1) == nargs)  /* no function? */
      return resume_error(L, "cannot resume dead coroutine", nargs);
  }
  else if (L->status != LUM_YIELD)  /* ended with errors? */
    return resume_error(L, "cannot resume dead coroutine", nargs);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  if (getCcalls(L) >= LUMI_MAXCCALLS)
    return resume_error(L, "C stack overflow", nargs);
  L->nCcalls++;
  lumi_userstateresume(L, nargs);
  api_checkpop(L, (L->status == LUM_OK) ? nargs + 1 : nargs);
  status = lumD_rawrunprotected(L, resume, &nargs);
   /* continue running after recoverable errors */
  status = precover(L, status);
  if (l_likely(!errorstatus(status)))
    lum_assert(status == L->status);  /* normal end or yield */
  else {  /* unrecoverable error */
    L->status = status;  /* mark thread as 'dead' */
    lumD_seterrorobj(L, status, L->top.p);  /* push error message */
    L->ci->top.p = L->top.p;
  }
  *nresults = (status == LUM_YIELD) ? L->ci->u2.nyield
                                    : cast_int(L->top.p - (L->ci->func.p + 1));
  lum_unlock(L);
  return APIstatus(status);
}


LUM_API int lum_isyieldable (lum_State *L) {
  return yieldable(L);
}


LUM_API int lum_yieldk (lum_State *L, int nresults, lum_KContext ctx,
                        lum_KFunction k) {
  CallInfo *ci;
  lumi_userstateyield(L, nresults);
  lum_lock(L);
  ci = L->ci;
  api_checkpop(L, nresults);
  if (l_unlikely(!yieldable(L))) {
    if (L != mainthread(G(L)))
      lumG_runerror(L, "attempt to yield across a C-call boundary");
    else
      lumG_runerror(L, "attempt to yield from outside a coroutine");
  }
  L->status = LUM_YIELD;
  ci->u2.nyield = nresults;  /* save number of results */
  if (isLum(ci)) {  /* inside a hook? */
    lum_assert(!isLumcode(ci));
    api_check(L, nresults == 0, "hooks cannot yield values");
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->u.c.k = k) != NULL)  /* is there a continuation? */
      ci->u.c.ctx = ctx;  /* save context */
    lumD_throw(L, LUM_YIELD);
  }
  lum_assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  lum_unlock(L);
  return 0;  /* return to 'lumD_hook' */
}


/*
** Auxiliary structure to call 'lumF_close' in protected mode.
*/
struct CloseP {
  StkId level;
  TStatus status;
};


/*
** Auxiliary function to call 'lumF_close' in protected mode.
*/
static void closepaux (lum_State *L, void *ud) {
  struct CloseP *pcl = cast(struct CloseP *, ud);
  lumF_close(L, pcl->level, pcl->status, 0);
}


/*
** Calls 'lumF_close' in protected mode. Return the original status
** or, in case of errors, the new status.
*/
TStatus lumD_closeprotected (lum_State *L, ptrdiff_t level, TStatus status) {
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  for (;;) {  /* keep closing upvalues until no more errors */
    struct CloseP pcl;
    pcl.level = restorestack(L, level); pcl.status = status;
    status = lumD_rawrunprotected(L, &closepaux, &pcl);
    if (l_likely(status == LUM_OK))  /* no more errors? */
      return pcl.status;
    else {  /* an error occurred; restore saved state and repeat */
      L->ci = old_ci;
      L->allowhook = old_allowhooks;
    }
  }
}


/*
** Call the C function 'func' in protected mode, restoring basic
** thread information ('allowhook', etc.) and in particular
** its stack level in case of errors.
*/
TStatus lumD_pcall (lum_State *L, Pfunc func, void *u, ptrdiff_t old_top,
                                  ptrdiff_t ef) {
  TStatus status;
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = lumD_rawrunprotected(L, func, u);
  if (l_unlikely(status != LUM_OK)) {  /* an error occurred? */
    L->ci = old_ci;
    L->allowhook = old_allowhooks;
    status = lumD_closeprotected(L, old_top, status);
    lumD_seterrorobj(L, status, restorestack(L, old_top));
    lumD_shrinkstack(L);   /* restore stack size in case of overflow */
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
struct SParser {  /* data to 'f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};


static void checkmode (lum_State *L, const char *mode, const char *x) {
  if (strchr(mode, x[0]) == NULL) {
    lumO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    lumD_throw(L, LUM_ERRSYNTAX);
  }
}


static void f_parser (lum_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  const char *mode = p->mode ? p->mode : "bt";
  int c = zgetc(p->z);  /* read first character */
  if (c == LUM_SIGNATURE[0]) {
    int fixed = 0;
    if (strchr(mode, 'B') != NULL)
      fixed = 1;
    else
      checkmode(L, mode, "binary");
    cl = lumU_undump(L, p->z, p->name, fixed);
  }
  else {
    checkmode(L, mode, "text");
    cl = lumY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  lum_assert(cl->nupvalues == cl->p->sizeupvalues);
  lumF_initupvals(L, cl);
}


TStatus lumD_protectedparser (lum_State *L, ZIO *z, const char *name,
                                            const char *mode) {
  struct SParser p;
  TStatus status;
  incnny(L);  /* cannot yield during parsing */
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  lumZ_initbuffer(L, &p.buff);
  status = lumD_pcall(L, f_parser, &p, savestack(L, L->top.p), L->errfunc);
  lumZ_freebuffer(L, &p.buff);
  lumM_freearray(L, p.dyd.actvar.arr, cast_sizet(p.dyd.actvar.size));
  lumM_freearray(L, p.dyd.gt.arr, cast_sizet(p.dyd.gt.size));
  lumM_freearray(L, p.dyd.label.arr, cast_sizet(p.dyd.label.size));
  decnny(L);
  return status;
}


