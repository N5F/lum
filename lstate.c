/*
** $Id: lstate.c $
** Global State
** See Copyright Notice in lum.h
*/

#define lstate_c
#define LUM_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

#include "lum.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** these macros allow user-specific actions when a thread is
** created/deleted
*/
#if !defined(lumi_userstateopen)
#define lumi_userstateopen(L)		((void)L)
#endif

#if !defined(lumi_userstateclose)
#define lumi_userstateclose(L)		((void)L)
#endif

#if !defined(lumi_userstatethread)
#define lumi_userstatethread(L,L1)	((void)L)
#endif

#if !defined(lumi_userstatefree)
#define lumi_userstatefree(L,L1)	((void)L)
#endif


/*
** set GCdebt to a new value keeping the real number of allocated
** objects (GCtotalobjs - GCdebt) invariant and avoiding overflows in
** 'GCtotalobjs'.
*/
void lumE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);
  lum_assert(tb > 0);
  if (debt > MAX_LMEM - tb)
    debt = MAX_LMEM - tb;  /* will make GCtotalbytes == MAX_LMEM */
  g->GCtotalbytes = tb + debt;
  g->GCdebt = debt;
}


CallInfo *lumE_extendCI (lum_State *L) {
  CallInfo *ci;
  lum_assert(L->ci->next == NULL);
  ci = lumM_new(L, CallInfo);
  lum_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  ci->u.l.trap = 0;
  L->nci++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
static void freeCI (lum_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    lumM_free(L, ci);
    L->nci--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread,
** keeping the first one.
*/
void lumE_shrinkCI (lum_State *L) {
  CallInfo *ci = L->ci->next;  /* first free CallInfo */
  CallInfo *next;
  if (ci == NULL)
    return;  /* no extra elements */
  while ((next = ci->next) != NULL) {  /* two extra elements? */
    CallInfo *next2 = next->next;  /* next's next */
    ci->next = next2;  /* remove next from the list */
    L->nci--;
    lumM_free(L, next);  /* free next */
    if (next2 == NULL)
      break;  /* no more elements */
    else {
      next2->previous = ci;
      ci = next2;  /* continue */
    }
  }
}


/*
** Called when 'getCcalls(L)' larger or equal to LUMI_MAXCCALLS.
** If equal, raises an overflow error. If value is larger than
** LUMI_MAXCCALLS (which means it is handling an overflow) but
** not much larger, does not report an error (to allow overflow
** handling to work).
*/
void lumE_checkcstack (lum_State *L) {
  if (getCcalls(L) == LUMI_MAXCCALLS)
    lumG_runerror(L, "C stack overflow");
  else if (getCcalls(L) >= (LUMI_MAXCCALLS / 10 * 11))
    lumD_throw(L, LUM_ERRERR);  /* error while handling stack error */
}


LUMI_FUNC void lumE_incCstack (lum_State *L) {
  L->nCcalls++;
  if (l_unlikely(getCcalls(L) >= LUMI_MAXCCALLS))
    lumE_checkcstack(L);
}


static void stack_init (lum_State *L1, lum_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack.p = lumM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  L1->tbclist.p = L1->stack.p;
  for (i = 0; i < BASIC_STACK_SIZE + EXTRA_STACK; i++)
    setnilvalue(s2v(L1->stack.p + i));  /* erase new stack */
  L1->top.p = L1->stack.p;
  L1->stack_last.p = L1->stack.p + BASIC_STACK_SIZE;
  /* initialize first ci */
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = CIST_C;
  ci->func.p = L1->top.p;
  ci->u.c.k = NULL;
  setnilvalue(s2v(L1->top.p));  /* 'function' entry for this 'ci' */
  L1->top.p++;
  ci->top.p = L1->top.p + LUM_MINSTACK;
  L1->ci = ci;
}


static void freestack (lum_State *L) {
  if (L->stack.p == NULL)
    return;  /* stack not completely built yet */
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  freeCI(L);
  lum_assert(L->nci == 0);
  /* free stack */
  lumM_freearray(L, L->stack.p, cast_sizet(stacksize(L) + EXTRA_STACK));
}


/*
** Create registry table and its predefined values
*/
static void init_registry (lum_State *L, global_State *g) {
  /* create registry */
  TValue aux;
  Table *registry = lumH_new(L);
  sethvalue(L, &g->l_registry, registry);
  lumH_resize(L, registry, LUM_RIDX_LAST, 0);
  /* registry[1] = false */
  setbfvalue(&aux);
  lumH_setint(L, registry, 1, &aux);
  /* registry[LUM_RIDX_MAINTHREAD] = L */
  setthvalue(L, &aux, L);
  lumH_setint(L, registry, LUM_RIDX_MAINTHREAD, &aux);
  /* registry[LUM_RIDX_GLOBALS] = new table (table of globals) */
  sethvalue(L, &aux, lumH_new(L));
  lumH_setint(L, registry, LUM_RIDX_GLOBALS, &aux);
}


/*
** open parts of the state that may cause memory-allocation errors.
*/
static void f_lumopen (lum_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  lumS_init(L);
  lumT_init(L);
  lumX_init(L);
  g->gcstp = 0;  /* allow gc */
  setnilvalue(&g->nilvalue);  /* now state is complete */
  lumi_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread (lum_State *L, global_State *g) {
  G(L) = g;
  L->stack.p = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->twups = L;  /* thread has no upvalues */
  L->nCcalls = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->status = LUM_OK;
  L->errfunc = 0;
  L->oldpc = 0;
}


lu_mem lumE_threadsize (lum_State *L) {
  lu_mem sz = cast(lu_mem, sizeof(LX))
            + cast_uint(L->nci) * sizeof(CallInfo);
  if (L->stack.p != NULL)
    sz += cast_uint(stacksize(L) + EXTRA_STACK) * sizeof(StackValue);
  return sz;
}


static void close_state (lum_State *L) {
  global_State *g = G(L);
  if (!completestate(g))  /* closing a partially built state? */
    lumC_freeallobjects(L);  /* just collect its objects */
  else {  /* closing a fully built state */
    L->ci = &L->base_ci;  /* unwind CallInfo list */
    lumD_closeprotected(L, 1, LUM_OK);  /* close all upvalues */
    lumC_freeallobjects(L);  /* collect all objects */
    lumi_userstateclose(L);
  }
  lumM_freearray(L, G(L)->strt.hash, cast_sizet(G(L)->strt.size));
  freestack(L);
  lum_assert(gettotalbytes(g) == sizeof(global_State));
  (*g->frealloc)(g->ud, g, sizeof(global_State), 0);  /* free main block */
}


LUM_API lum_State *lum_newthread (lum_State *L) {
  global_State *g = G(L);
  GCObject *o;
  lum_State *L1;
  lum_lock(L);
  lumC_checkGC(L);
  /* create new thread */
  o = lumC_newobjdt(L, LUM_TTHREAD, sizeof(LX), offsetof(LX, l));
  L1 = gco2th(o);
  /* anchor it on L stack */
  setthvalue2s(L, L->top.p, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(lum_getextraspace(L1), lum_getextraspace(mainthread(g)),
         LUM_EXTRASPACE);
  lumi_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  lum_unlock(L);
  return L1;
}


void lumE_freethread (lum_State *L, lum_State *L1) {
  LX *l = fromstate(L1);
  lumF_closeupval(L1, L1->stack.p);  /* close all upvalues */
  lum_assert(L1->openupval == NULL);
  lumi_userstatefree(L, L1);
  freestack(L1);
  lumM_free(L, l);
}


TStatus lumE_resetthread (lum_State *L, TStatus status) {
  CallInfo *ci = L->ci = &L->base_ci;  /* unwind CallInfo list */
  setnilvalue(s2v(L->stack.p));  /* 'function' entry for basic 'ci' */
  ci->func.p = L->stack.p;
  ci->callstatus = CIST_C;
  if (status == LUM_YIELD)
    status = LUM_OK;
  L->status = LUM_OK;  /* so it can run __close metamethods */
  status = lumD_closeprotected(L, 1, status);
  if (status != LUM_OK)  /* errors? */
    lumD_seterrorobj(L, status, L->stack.p + 1);
  else
    L->top.p = L->stack.p + 1;
  ci->top.p = L->top.p + LUM_MINSTACK;
  lumD_reallocstack(L, cast_int(ci->top.p - L->stack.p), 0);
  return status;
}


LUM_API int lum_closethread (lum_State *L, lum_State *from) {
  TStatus status;
  lum_lock(L);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  status = lumE_resetthread(L, L->status);
  lum_unlock(L);
  return APIstatus(status);
}


LUM_API lum_State *lum_newstate (lum_Alloc f, void *ud, unsigned seed) {
  int i;
  lum_State *L;
  global_State *g = cast(global_State*,
                       (*f)(ud, NULL, LUM_TTHREAD, sizeof(global_State)));
  if (g == NULL) return NULL;
  L = &g->mainth.l;
  L->tt = LUM_VTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = lumC_white(g);
  preinit_thread(L, g);
  g->allgc = obj2gco(L);  /* by now, only object is the main thread */
  L->next = NULL;
  incnny(L);  /* main thread is always non yieldable */
  g->frealloc = f;
  g->ud = ud;
  g->warnf = NULL;
  g->ud_warn = NULL;
  g->seed = seed;
  g->gcstp = GCSTPGC;  /* no GC while building state */
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->gcstopem = 0;
  g->gcemergency = 0;
  g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->firstold1 = g->survival = g->old1 = g->reallyold = NULL;
  g->finobjsur = g->finobjold1 = g->finobjrold = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->GCtotalbytes = sizeof(global_State);
  g->GCmarked = 0;
  g->GCdebt = 0;
  setivalue(&g->nilvalue, 0);  /* to signal that state is not yet built */
  setgcparam(g, PAUSE, LUMI_GCPAUSE);
  setgcparam(g, STEPMUL, LUMI_GCMUL);
  setgcparam(g, STEPSIZE, LUMI_GCSTEPSIZE);
  setgcparam(g, MINORMUL, LUMI_GENMINORMUL);
  setgcparam(g, MINORMAJOR, LUMI_MINORMAJOR);
  setgcparam(g, MAJORMINOR, LUMI_MAJORMINOR);
  for (i=0; i < LUM_NUMTYPES; i++) g->mt[i] = NULL;
  if (lumD_rawrunprotected(L, f_lumopen, NULL) != LUM_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


LUM_API void lum_close (lum_State *L) {
  lum_lock(L);
  L = mainthread(G(L));  /* only the main thread can be closed */
  close_state(L);
}


void lumE_warning (lum_State *L, const char *msg, int tocont) {
  lum_WarnFunction wf = G(L)->warnf;
  if (wf != NULL)
    wf(G(L)->ud_warn, msg, tocont);
}


/*
** Generate a warning from an error message
*/
void lumE_warnerror (lum_State *L, const char *where) {
  TValue *errobj = s2v(L->top.p - 1);  /* error object */
  const char *msg = (ttisstring(errobj))
                  ? getstr(tsvalue(errobj))
                  : "error object is not a string";
  /* produce warning "error in %s (%s)" (where, msg) */
  lumE_warning(L, "error in ", 1);
  lumE_warning(L, where, 1);
  lumE_warning(L, " (", 1);
  lumE_warning(L, msg, 1);
  lumE_warning(L, ")", 0);
}

