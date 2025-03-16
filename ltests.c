/*
** $Id: ltests.c $
** Internal Module for Debugging of the Lum Implementation
** See Copyright Notice in lum.h
*/

#define ltests_c
#define LUM_CORE

#include "lprefix.h"


#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lapi.h"
#include "lauxlib.h"
#include "lcode.h"
#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lumlib.h"



/*
** The whole module only makes sense with LUM_DEBUG on
*/
#if defined(LUM_DEBUG)


void *l_Trick = 0;


#define obj_at(L,k)	s2v(L->ci->func.p + (k))


static int runC (lum_State *L, lum_State *L1, const char *pc);


static void setnameval (lum_State *L, const char *name, int val) {
  lum_pushinteger(L, val);
  lum_setfield(L, -2, name);
}


static void pushobject (lum_State *L, const TValue *o) {
  setobj2s(L, L->top.p, o);
  api_incr_top(L);
}


static void badexit (const char *fmt, const char *s1, const char *s2) {
  fprintf(stderr, fmt, s1);
  if (s2)
    fprintf(stderr, "extra info: %s\n", s2);
  /* avoid assertion failures when exiting */
  l_memcontrol.numblocks = l_memcontrol.total = 0;
  exit(EXIT_FAILURE);
}


static int tpanic (lum_State *L) {
  const char *msg = (lum_type(L, -1) == LUM_TSTRING)
                  ? lum_tostring(L, -1)
                  : "error object is not a string";
  return (badexit("PANIC: unprotected error in call to Lum API (%s)\n",
                   msg, NULL),
          0);  /* do not return to Lum */
}


/*
** Warning function for tests. First, it concatenates all parts of
** a warning in buffer 'buff'. Then, it has three modes:
** - 0.normal: messages starting with '#' are shown on standard output;
** - other messages abort the tests (they represent real warning
** conditions; the standard tests should not generate these conditions
** unexpectedly);
** - 1.allow: all messages are shown;
** - 2.store: all warnings go to the global '_WARN';
*/
static void warnf (void *ud, const char *msg, int tocont) {
  lum_State *L = cast(lum_State *, ud);
  static char buff[200] = "";  /* should be enough for tests... */
  static int onoff = 0;
  static int mode = 0;  /* start in normal mode */
  static int lasttocont = 0;
  if (!lasttocont && !tocont && *msg == '@') {  /* control message? */
    if (buff[0] != '\0')
      badexit("Control warning during warning: %s\naborting...\n", msg, buff);
    if (strcmp(msg, "@off") == 0)
      onoff = 0;
    else if (strcmp(msg, "@on") == 0)
      onoff = 1;
    else if (strcmp(msg, "@normal") == 0)
      mode = 0;
    else if (strcmp(msg, "@allow") == 0)
      mode = 1;
    else if (strcmp(msg, "@store") == 0)
      mode = 2;
    else
      badexit("Invalid control warning in test mode: %s\naborting...\n",
              msg, NULL);
    return;
  }
  lasttocont = tocont;
  if (strlen(msg) >= sizeof(buff) - strlen(buff))
    badexit("warnf-buffer overflow (%s)\n", msg, buff);
  strcat(buff, msg);  /* add new message to current warning */
  if (!tocont) {  /* message finished? */
    lum_unlock(L);
    lumL_checkstack(L, 1, "warn stack space");
    lum_getglobal(L, "_WARN");
    if (!lum_toboolean(L, -1))
      lum_pop(L, 1);  /* ok, no previous unexpected warning */
    else {
      badexit("Unhandled warning in store mode: %s\naborting...\n",
              lum_tostring(L, -1), buff);
    }
    lum_lock(L);
    switch (mode) {
      case 0: {  /* normal */
        if (buff[0] != '#' && onoff)  /* unexpected warning? */
          badexit("Unexpected warning in test mode: %s\naborting...\n",
                  buff, NULL);
      }  /* FALLTHROUGH */
      case 1: {  /* allow */
        if (onoff)
          fprintf(stderr, "Lum warning: %s\n", buff);  /* print warning */
        break;
      }
      case 2: {  /* store */
        lum_unlock(L);
        lumL_checkstack(L, 1, "warn stack space");
        lum_pushstring(L, buff);
        lum_setglobal(L, "_WARN");  /* assign message to global '_WARN' */
        lum_lock(L);
        break;
      }
    }
    buff[0] = '\0';  /* prepare buffer for next warning */
  }
}


/*
** {======================================================================
** Controlled version for realloc.
** =======================================================================
*/

#define MARK		0x55  /* 01010101 (a nice pattern) */

typedef union Header {
  LUMI_MAXALIGN;
  struct {
    size_t size;
    int type;
  } d;
} Header;


#if !defined(EXTERNMEMCHECK)

/* full memory check */
#define MARKSIZE	16  /* size of marks after each block */
#define fillmem(mem,size)	memset(mem, -MARK, size)

#else

/* external memory check: don't do it twice */
#define MARKSIZE	0
#define fillmem(mem,size)	/* empty */

#endif


Memcontrol l_memcontrol =
  {0, 0UL, 0UL, 0UL, 0UL, (~0UL),
   {0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL}};


static void freeblock (Memcontrol *mc, Header *block) {
  if (block) {
    size_t size = block->d.size;
    int i;
    for (i = 0; i < MARKSIZE; i++)  /* check marks after block */
      lum_assert(*(cast_charp(block + 1) + size + i) == MARK);
    mc->objcount[block->d.type]--;
    fillmem(block, sizeof(Header) + size + MARKSIZE);  /* erase block */
    free(block);  /* actually free block */
    mc->numblocks--;  /* update counts */
    mc->total -= size;
  }
}


void *debug_realloc (void *ud, void *b, size_t oldsize, size_t size) {
  Memcontrol *mc = cast(Memcontrol *, ud);
  Header *block = cast(Header *, b);
  int type;
  if (mc->memlimit == 0) {  /* first time? */
    char *limit = getenv("MEMLIMIT");  /* initialize memory limit */
    mc->memlimit = limit ? strtoul(limit, NULL, 10) : ULONG_MAX;
  }
  if (block == NULL) {
    type = (oldsize < LUM_NUMTYPES) ? cast_int(oldsize) : 0;
    oldsize = 0;
  }
  else {
    block--;  /* go to real header */
    type = block->d.type;
    lum_assert(oldsize == block->d.size);
  }
  if (size == 0) {
    freeblock(mc, block);
    return NULL;
  }
  if (mc->failnext) {
    mc->failnext = 0;
    return NULL;  /* fake a single memory allocation error */
  }
  if (mc->countlimit != ~0UL && size != oldsize) {  /* count limit in use? */
    if (mc->countlimit == 0)
      return NULL;  /* fake a memory allocation error */
    mc->countlimit--;
  }
  if (size > oldsize && mc->total+size-oldsize > mc->memlimit)
    return NULL;  /* fake a memory allocation error */
  else {
    Header *newblock;
    int i;
    size_t commonsize = (oldsize < size) ? oldsize : size;
    size_t realsize = sizeof(Header) + size + MARKSIZE;
    if (realsize < size) return NULL;  /* arithmetic overflow! */
    newblock = cast(Header *, malloc(realsize));  /* alloc a new block */
    if (newblock == NULL)
      return NULL;  /* really out of memory? */
    if (block) {
      memcpy(newblock + 1, block + 1, commonsize);  /* copy old contents */
      freeblock(mc, block);  /* erase (and check) old copy */
    }
    /* initialize new part of the block with something weird */
    fillmem(cast_charp(newblock + 1) + commonsize, size - commonsize);
    /* initialize marks after block */
    for (i = 0; i < MARKSIZE; i++)
      *(cast_charp(newblock + 1) + size + i) = MARK;
    newblock->d.size = size;
    newblock->d.type = type;
    mc->total += size;
    if (mc->total > mc->maxmem)
      mc->maxmem = mc->total;
    mc->numblocks++;
    mc->objcount[type]++;
    return newblock + 1;
  }
}


/* }====================================================================== */



/*
** {=====================================================================
** Functions to check memory consistency.
** Most of these checks are done through asserts, so this code does
** not make sense with asserts off. For this reason, it uses 'assert'
** directly, instead of 'lum_assert'.
** ======================================================================
*/

#include <assert.h>

/*
** Check GC invariants. For incremental mode, a black object cannot
** point to a white one. For generational mode, really old objects
** cannot point to young objects. Both old1 and touched2 objects
** cannot point to new objects (but can point to survivals).
** (Threads and open upvalues, despite being marked "really old",
** continue to be visited in all collections, and therefore can point to
** new objects. They, and only they, are old but gray.)
*/
static int testobjref1 (global_State *g, GCObject *f, GCObject *t) {
  if (isdead(g,t)) return 0;
  if (issweepphase(g))
    return 1;  /* no invariants */
  else if (g->gckind != KGC_GENMINOR)
    return !(isblack(f) && iswhite(t));  /* basic incremental invariant */
  else {  /* generational mode */
    if ((getage(f) == G_OLD && isblack(f)) && !isold(t))
      return 0;
    if ((getage(f) == G_OLD1 || getage(f) == G_TOUCHED2) &&
         getage(t) == G_NEW)
      return 0;
    return 1;
  }
}


static void printobj (global_State *g, GCObject *o) {
  printf("||%s(%p)-%c%c(%02X)||",
           ttypename(novariant(o->tt)), (void *)o,
           isdead(g,o) ? 'd' : isblack(o) ? 'b' : iswhite(o) ? 'w' : 'g',
           "ns01oTt"[getage(o)], o->marked);
  if (o->tt == LUM_VSHRSTR || o->tt == LUM_VLNGSTR)
    printf(" '%s'", getstr(gco2ts(o)));
}


void lum_printobj (lum_State *L, struct GCObject *o) {
  printobj(G(L), o);
}


void lum_printvalue (TValue *v) {
  switch (ttype(v)) {
    case LUM_TNUMBER: {
      char buff[LUM_N2SBUFFSZ];
      unsigned len = lumO_tostringbuff(v, buff);
      buff[len] = '\0';
      printf("%s", buff);
      break;
    }
    case LUM_TSTRING: {
      printf("'%s'", getstr(tsvalue(v)));
      break;
    }
    case LUM_TBOOLEAN: {
      printf("%s", (!l_isfalse(v) ? "true" : "false"));
      break;
    }
    case LUM_TLIGHTUSERDATA: {
      printf("light udata: %p", pvalue(v));
      break;
    }
    case LUM_TNIL: {
      printf("nil");
      break;
    }
    default: {
      if (ttislcf(v))
        printf("light C function: %p", fvalue(v));
      else  /* must be collectable */
        printf("%s: %p", ttypename(ttype(v)), gcvalue(v));
      break;
    }
  }
}


static int testobjref (global_State *g, GCObject *f, GCObject *t) {
  int r1 = testobjref1(g, f, t);
  if (!r1) {
    printf("%d(%02X) - ", g->gcstate, g->currentwhite);
    printobj(g, f);
    printf("  ->  ");
    printobj(g, t);
    printf("\n");
  }
  return r1;
}


static void checkobjref (global_State *g, GCObject *f, GCObject *t) {
    assert(testobjref(g, f, t));
}


/*
** Version where 't' can be NULL. In that case, it should not apply the
** macro 'obj2gco' over the object. ('t' may have several types, so this
** definition must be a macro.)  Most checks need this version, because
** the check may run while an object is still being created.
*/
#define checkobjrefN(g,f,t)	{ if (t) checkobjref(g,f,obj2gco(t)); }


static void checkvalref (global_State *g, GCObject *f, const TValue *t) {
  assert(!iscollectable(t) || (righttt(t) && testobjref(g, f, gcvalue(t))));
}


static void checktable (global_State *g, Table *h) {
  unsigned int i;
  unsigned int asize = h->asize;
  Node *n, *limit = gnode(h, sizenode(h));
  GCObject *hgc = obj2gco(h);
  checkobjrefN(g, hgc, h->metatable);
  for (i = 0; i < asize; i++) {
    TValue aux;
    arr2obj(h, i, &aux);
    checkvalref(g, hgc, &aux);
  }
  for (n = gnode(h, 0); n < limit; n++) {
    if (!isempty(gval(n))) {
      TValue k;
      getnodekey(mainthread(g), &k, n);
      assert(!keyisnil(n));
      checkvalref(g, hgc, &k);
      checkvalref(g, hgc, gval(n));
    }
  }
}


static void checkudata (global_State *g, Udata *u) {
  int i;
  GCObject *hgc = obj2gco(u);
  checkobjrefN(g, hgc, u->metatable);
  for (i = 0; i < u->nuvalue; i++)
    checkvalref(g, hgc, &u->uv[i].uv);
}


static void checkproto (global_State *g, Proto *f) {
  int i;
  GCObject *fgc = obj2gco(f);
  checkobjrefN(g, fgc, f->source);
  for (i=0; i<f->sizek; i++) {
    if (iscollectable(f->k + i))
      checkobjref(g, fgc, gcvalue(f->k + i));
  }
  for (i=0; i<f->sizeupvalues; i++)
    checkobjrefN(g, fgc, f->upvalues[i].name);
  for (i=0; i<f->sizep; i++)
    checkobjrefN(g, fgc, f->p[i]);
  for (i=0; i<f->sizelocvars; i++)
    checkobjrefN(g, fgc, f->locvars[i].varname);
}


static void checkCclosure (global_State *g, CClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  for (i = 0; i < cl->nupvalues; i++)
    checkvalref(g, clgc, &cl->upvalue[i]);
}


static void checkLclosure (global_State *g, LClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  checkobjrefN(g, clgc, cl->p);
  for (i=0; i<cl->nupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    if (uv) {
      checkobjrefN(g, clgc, uv);
      if (!upisopen(uv))
        checkvalref(g, obj2gco(uv), uv->v.p);
    }
  }
}


static int lum_checkpc (CallInfo *ci) {
  if (!isLum(ci)) return 1;
  else {
    StkId f = ci->func.p;
    Proto *p = clLvalue(s2v(f))->p;
    return p->code <= ci->u.l.savedpc &&
           ci->u.l.savedpc <= p->code + p->sizecode;
  }
}


static void checkstack (global_State *g, lum_State *L1) {
  StkId o;
  CallInfo *ci;
  UpVal *uv;
  assert(!isdead(g, L1));
  if (L1->stack.p == NULL) {  /* incomplete thread? */
    assert(L1->openupval == NULL && L1->ci == NULL);
    return;
  }
  for (uv = L1->openupval; uv != NULL; uv = uv->u.open.next)
    assert(upisopen(uv));  /* must be open */
  assert(L1->top.p <= L1->stack_last.p);
  assert(L1->tbclist.p <= L1->top.p);
  for (ci = L1->ci; ci != NULL; ci = ci->previous) {
    assert(ci->top.p <= L1->stack_last.p);
    assert(lum_checkpc(ci));
  }
  for (o = L1->stack.p; o < L1->stack_last.p; o++)
    checkliveness(L1, s2v(o));  /* entire stack must have valid values */
}


static void checkrefs (global_State *g, GCObject *o) {
  switch (o->tt) {
    case LUM_VUSERDATA: {
      checkudata(g, gco2u(o));
      break;
    }
    case LUM_VUPVAL: {
      checkvalref(g, o, gco2upv(o)->v.p);
      break;
    }
    case LUM_VTABLE: {
      checktable(g, gco2t(o));
      break;
    }
    case LUM_VTHREAD: {
      checkstack(g, gco2th(o));
      break;
    }
    case LUM_VLCL: {
      checkLclosure(g, gco2lcl(o));
      break;
    }
    case LUM_VCCL: {
      checkCclosure(g, gco2ccl(o));
      break;
    }
    case LUM_VPROTO: {
      checkproto(g, gco2p(o));
      break;
    }
    case LUM_VSHRSTR:
    case LUM_VLNGSTR: {
      assert(!isgray(o));  /* strings are never gray */
      break;
    }
    default: assert(0);
  }
}


/*
** Check consistency of an object:
** - Dead objects can only happen in the 'allgc' list during a sweep
** phase (controlled by the caller through 'maybedead').
** - During pause, all objects must be white.
** - In generational mode:
**   * objects must be old enough for their lists ('listage').
**   * old objects cannot be white.
**   * old objects must be black, except for 'touched1', 'old0',
**     threads, and open upvalues.
**   * 'touched1' objects must be gray.
*/
static void checkobject (global_State *g, GCObject *o, int maybedead,
                         int listage) {
  if (isdead(g, o))
    assert(maybedead);
  else {
    assert(g->gcstate != GCSpause || iswhite(o));
    if (g->gckind == KGC_GENMINOR) {  /* generational mode? */
      assert(getage(o) >= listage);
      if (isold(o)) {
        assert(!iswhite(o));
        assert(isblack(o) ||
        getage(o) == G_TOUCHED1 ||
        getage(o) == G_OLD0 ||
        o->tt == LUM_VTHREAD ||
        (o->tt == LUM_VUPVAL && upisopen(gco2upv(o))));
      }
      assert(getage(o) != G_TOUCHED1 || isgray(o));
    }
    checkrefs(g, o);
  }
}


static l_mem checkgraylist (global_State *g, GCObject *o) {
  int total = 0;  /* count number of elements in the list */
  cast_void(g);  /* better to keep it if we need to print an object */
  while (o) {
    assert(!!isgray(o) ^ (getage(o) == G_TOUCHED2));
    assert(!testbit(o->marked, TESTBIT));
    if (keepinvariant(g))
      l_setbit(o->marked, TESTBIT);  /* mark that object is in a gray list */
    total++;
    switch (o->tt) {
      case LUM_VTABLE: o = gco2t(o)->gclist; break;
      case LUM_VLCL: o = gco2lcl(o)->gclist; break;
      case LUM_VCCL: o = gco2ccl(o)->gclist; break;
      case LUM_VTHREAD: o = gco2th(o)->gclist; break;
      case LUM_VPROTO: o = gco2p(o)->gclist; break;
      case LUM_VUSERDATA:
        assert(gco2u(o)->nuvalue > 0);
        o = gco2u(o)->gclist;
        break;
      default: assert(0);  /* other objects cannot be in a gray list */
    }
  }
  return total;
}


/*
** Check objects in gray lists.
*/
static l_mem checkgrays (global_State *g) {
  l_mem total = 0;  /* count number of elements in all lists */
  if (!keepinvariant(g)) return total;
  total += checkgraylist(g, g->gray);
  total += checkgraylist(g, g->grayagain);
  total += checkgraylist(g, g->weak);
  total += checkgraylist(g, g->allweak);
  total += checkgraylist(g, g->ephemeron);
  return total;
}


/*
** Check whether 'o' should be in a gray list. If so, increment
** 'count' and check its TESTBIT. (It must have been previously set by
** 'checkgraylist'.)
*/
static void incifingray (global_State *g, GCObject *o, l_mem *count) {
  if (!keepinvariant(g))
    return;  /* gray lists not being kept in these phases */
  if (o->tt == LUM_VUPVAL) {
    /* only open upvalues can be gray */
    assert(!isgray(o) || upisopen(gco2upv(o)));
    return;  /* upvalues are never in gray lists */
  }
  /* these are the ones that must be in gray lists */
  if (isgray(o) || getage(o) == G_TOUCHED2) {
    (*count)++;
    assert(testbit(o->marked, TESTBIT));
    resetbit(o->marked, TESTBIT);  /* prepare for next cycle */
  }
}


static l_mem checklist (global_State *g, int maybedead, int tof,
  GCObject *newl, GCObject *survival, GCObject *old, GCObject *reallyold) {
  GCObject *o;
  l_mem total = 0;  /* number of object that should be in  gray lists */
  for (o = newl; o != survival; o = o->next) {
    checkobject(g, o, maybedead, G_NEW);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = survival; o != old; o = o->next) {
    checkobject(g, o, 0, G_SURVIVAL);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = old; o != reallyold; o = o->next) {
    checkobject(g, o, 0, G_OLD1);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = reallyold; o != NULL; o = o->next) {
    checkobject(g, o, 0, G_OLD);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  return total;
}


int lum_checkmemory (lum_State *L) {
  global_State *g = G(L);
  GCObject *o;
  int maybedead;
  l_mem totalin;  /* total of objects that are in gray lists */
  l_mem totalshould;  /* total of objects that should be in gray lists */
  if (keepinvariant(g)) {
    assert(!iswhite(mainthread(g)));
    assert(!iswhite(gcvalue(&g->l_registry)));
  }
  assert(!isdead(g, gcvalue(&g->l_registry)));
  assert(g->sweepgc == NULL || issweepphase(g));
  totalin = checkgrays(g);

  /* check 'fixedgc' list */
  for (o = g->fixedgc; o != NULL; o = o->next) {
    assert(o->tt == LUM_VSHRSTR && isgray(o) && getage(o) == G_OLD);
  }

  /* check 'allgc' list */
  maybedead = (GCSatomic < g->gcstate && g->gcstate <= GCSswpallgc);
  totalshould = checklist(g, maybedead, 0, g->allgc,
                             g->survival, g->old1, g->reallyold);

  /* check 'finobj' list */
  totalshould += checklist(g, 0, 1, g->finobj,
                              g->finobjsur, g->finobjold1, g->finobjrold);

  /* check 'tobefnz' list */
  for (o = g->tobefnz; o != NULL; o = o->next) {
    checkobject(g, o, 0, G_NEW);
    incifingray(g, o, &totalshould);
    assert(tofinalize(o));
    assert(o->tt == LUM_VUSERDATA || o->tt == LUM_VTABLE);
  }
  if (keepinvariant(g))
    assert(totalin == totalshould);
  return 0;
}

/* }====================================================== */



/*
** {======================================================
** Disassembler
** =======================================================
*/


static char *buildop (Proto *p, int pc, char *buff) {
  char *obuff = buff;
  Instruction i = p->code[pc];
  OpCode o = GET_OPCODE(i);
  const char *name = opnames[o];
  int line = lumG_getfuncline(p, pc);
  int lineinfo = (p->lineinfo != NULL) ? p->lineinfo[pc] : 0;
  if (lineinfo == ABSLINEINFO)
    buff += sprintf(buff, "(__");
  else
    buff += sprintf(buff, "(%2d", lineinfo);
  buff += sprintf(buff, " - %4d) %4d - ", line, pc);
  switch (getOpMode(o)) {
    case iABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              GETARG_A(i), GETARG_B(i), GETARG_C(i),
              GETARG_k(i) ? " (k)" : "");
      break;
    case ivABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              GETARG_A(i), GETARG_vB(i), GETARG_vC(i),
              GETARG_k(i) ? " (k)" : "");
      break;
    case iABx:
      sprintf(buff, "%-12s%4d %4d", name, GETARG_A(i), GETARG_Bx(i));
      break;
    case iAsBx:
      sprintf(buff, "%-12s%4d %4d", name, GETARG_A(i), GETARG_sBx(i));
      break;
    case iAx:
      sprintf(buff, "%-12s%4d", name, GETARG_Ax(i));
      break;
    case isJ:
      sprintf(buff, "%-12s%4d", name, GETARG_sJ(i));
      break;
  }
  return obuff;
}


#if 0
void lumI_printcode (Proto *pt, int size) {
  int pc;
  for (pc=0; pc<size; pc++) {
    char buff[100];
    printf("%s\n", buildop(pt, pc, buff));
  }
  printf("-------\n");
}


void lumI_printinst (Proto *pt, int pc) {
  char buff[100];
  printf("%s\n", buildop(pt, pc, buff));
}
#endif


static int listcode (lum_State *L) {
  int pc;
  Proto *p;
  lumL_argcheck(L, lum_isfunction(L, 1) && !lum_iscfunction(L, 1),
                 1, "Lum function expected");
  p = getproto(obj_at(L, 1));
  lum_newtable(L);
  setnameval(L, "maxstack", p->maxstacksize);
  setnameval(L, "numparams", p->numparams);
  for (pc=0; pc<p->sizecode; pc++) {
    char buff[100];
    lum_pushinteger(L, pc+1);
    lum_pushstring(L, buildop(p, pc, buff));
    lum_settable(L, -3);
  }
  return 1;
}


static int printcode (lum_State *L) {
  int pc;
  Proto *p;
  lumL_argcheck(L, lum_isfunction(L, 1) && !lum_iscfunction(L, 1),
                 1, "Lum function expected");
  p = getproto(obj_at(L, 1));
  printf("maxstack: %d\n", p->maxstacksize);
  printf("numparams: %d\n", p->numparams);
  for (pc=0; pc<p->sizecode; pc++) {
    char buff[100];
    printf("%s\n", buildop(p, pc, buff));
  }
  return 0;
}


static int listk (lum_State *L) {
  Proto *p;
  int i;
  lumL_argcheck(L, lum_isfunction(L, 1) && !lum_iscfunction(L, 1),
                 1, "Lum function expected");
  p = getproto(obj_at(L, 1));
  lum_createtable(L, p->sizek, 0);
  for (i=0; i<p->sizek; i++) {
    pushobject(L, p->k+i);
    lum_rawseti(L, -2, i+1);
  }
  return 1;
}


static int listabslineinfo (lum_State *L) {
  Proto *p;
  int i;
  lumL_argcheck(L, lum_isfunction(L, 1) && !lum_iscfunction(L, 1),
                 1, "Lum function expected");
  p = getproto(obj_at(L, 1));
  lumL_argcheck(L, p->abslineinfo != NULL, 1, "function has no debug info");
  lum_createtable(L, 2 * p->sizeabslineinfo, 0);
  for (i=0; i < p->sizeabslineinfo; i++) {
    lum_pushinteger(L, p->abslineinfo[i].pc);
    lum_rawseti(L, -2, 2 * i + 1);
    lum_pushinteger(L, p->abslineinfo[i].line);
    lum_rawseti(L, -2, 2 * i + 2);
  }
  return 1;
}


static int listlocals (lum_State *L) {
  Proto *p;
  int pc = cast_int(lumL_checkinteger(L, 2)) - 1;
  int i = 0;
  const char *name;
  lumL_argcheck(L, lum_isfunction(L, 1) && !lum_iscfunction(L, 1),
                 1, "Lum function expected");
  p = getproto(obj_at(L, 1));
  while ((name = lumF_getlocalname(p, ++i, pc)) != NULL)
    lum_pushstring(L, name);
  return i-1;
}

/* }====================================================== */



void lum_printstack (lum_State *L) {
  int i;
  int n = lum_gettop(L);
  printf("stack: >>\n");
  for (i = 1; i <= n; i++) {
    printf("%3d: ", i);
    lum_printvalue(s2v(L->ci->func.p + i));
    printf("\n");
  }
  printf("<<\n");
}


static int get_limits (lum_State *L) {
  lum_createtable(L, 0, 5);
  setnameval(L, "IS32INT", LUMI_IS32INT);
  setnameval(L, "MAXARG_Ax", MAXARG_Ax);
  setnameval(L, "MAXARG_Bx", MAXARG_Bx);
  setnameval(L, "OFFSET_sBx", OFFSET_sBx);
  setnameval(L, "NUM_OPCODES", NUM_OPCODES);
  return 1;
}


static int mem_query (lum_State *L) {
  if (lum_isnone(L, 1)) {
    lum_pushinteger(L, cast(lum_Integer, l_memcontrol.total));
    lum_pushinteger(L, cast(lum_Integer, l_memcontrol.numblocks));
    lum_pushinteger(L, cast(lum_Integer, l_memcontrol.maxmem));
    return 3;
  }
  else if (lum_isnumber(L, 1)) {
    unsigned long limit = cast(unsigned long, lumL_checkinteger(L, 1));
    if (limit == 0) limit = ULONG_MAX;
    l_memcontrol.memlimit = limit;
    return 0;
  }
  else {
    const char *t = lumL_checkstring(L, 1);
    int i;
    for (i = LUM_NUMTYPES - 1; i >= 0; i--) {
      if (strcmp(t, ttypename(i)) == 0) {
        lum_pushinteger(L, cast(lum_Integer, l_memcontrol.objcount[i]));
        return 1;
      }
    }
    return lumL_error(L, "unknown type '%s'", t);
  }
}


static int alloc_count (lum_State *L) {
  if (lum_isnone(L, 1))
    l_memcontrol.countlimit = cast(unsigned long, ~0L);
  else
    l_memcontrol.countlimit = cast(unsigned long, lumL_checkinteger(L, 1));
  return 0;
}


static int alloc_failnext (lum_State *L) {
  UNUSED(L);
  l_memcontrol.failnext = 1;
  return 0;
}


static int settrick (lum_State *L) {
  if (ttisnil(obj_at(L, 1)))
    l_Trick = NULL;
  else
    l_Trick = gcvalue(obj_at(L, 1));
  return 0;
}


static int gc_color (lum_State *L) {
  TValue *o;
  lumL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    lum_pushstring(L, "no collectable");
  else {
    GCObject *obj = gcvalue(o);
    lum_pushstring(L, isdead(G(L), obj) ? "dead" :
                      iswhite(obj) ? "white" :
                      isblack(obj) ? "black" : "gray");
  }
  return 1;
}


static int gc_age (lum_State *L) {
  TValue *o;
  lumL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    lum_pushstring(L, "no collectable");
  else {
    static const char *gennames[] = {"new", "survival", "old0", "old1",
                                     "old", "touched1", "touched2"};
    GCObject *obj = gcvalue(o);
    lum_pushstring(L, gennames[getage(obj)]);
  }
  return 1;
}


static int gc_printobj (lum_State *L) {
  TValue *o;
  lumL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    printf("no collectable\n");
  else {
    GCObject *obj = gcvalue(o);
    printobj(G(L), obj);
    printf("\n");
  }
  return 0;
}


static const char *statenames[] = {
  "propagate", "enteratomic", "atomic", "sweepallgc", "sweepfinobj",
  "sweeptobefnz", "sweepend", "callfin", "pause", ""};

static int gc_state (lum_State *L) {
  static const int states[] = {
    GCSpropagate, GCSenteratomic, GCSatomic, GCSswpallgc, GCSswpfinobj,
    GCSswptobefnz, GCSswpend, GCScallfin, GCSpause, -1};
  int option = states[lumL_checkoption(L, 1, "", statenames)];
  global_State *g = G(L);
  if (option == -1) {
    lum_pushstring(L, statenames[g->gcstate]);
    return 1;
  }
  else {
    if (g->gckind != KGC_INC)
      lumL_error(L, "cannot change states in generational mode");
    lum_lock(L);
    if (option < g->gcstate) {  /* must cross 'pause'? */
      lumC_runtilstate(L, GCSpause, 1);  /* run until pause */
    }
    lumC_runtilstate(L, option, 0);  /* do not skip propagation state */
    lum_assert(g->gcstate == option);
    lum_unlock(L);
    return 0;
  }
}


static int tracinggc = 0;
void lumi_tracegctest (lum_State *L, int first) {
  if (!tracinggc) return;
  else {
    global_State *g = G(L);
    lum_unlock(L);
    g->gcstp = GCSTPGC;
    lum_checkstack(L, 10);
    lum_getfield(L, LUM_REGISTRYINDEX, "tracegc");
    lum_pushboolean(L, first);
    lum_call(L, 1, 0);
    g->gcstp = 0;
    lum_lock(L);
  }
}


static int tracegc (lum_State *L) {
  if (lum_isnil(L, 1))
    tracinggc = 0;
  else {
    tracinggc = 1;
    lum_setfield(L, LUM_REGISTRYINDEX, "tracegc");
  }
  return 0;
}


static int hash_query (lum_State *L) {
  if (lum_isnone(L, 2)) {
    lumL_argcheck(L, lum_type(L, 1) == LUM_TSTRING, 1, "string expected");
    lum_pushinteger(L, cast_int(tsvalue(obj_at(L, 1))->hash));
  }
  else {
    TValue *o = obj_at(L, 1);
    Table *t;
    lumL_checktype(L, 2, LUM_TTABLE);
    t = hvalue(obj_at(L, 2));
    lum_pushinteger(L, cast(lum_Integer, lumH_mainposition(t, o) - t->node));
  }
  return 1;
}


static int stacklevel (lum_State *L) {
  int a = 0;
  lum_pushinteger(L, cast(lum_Integer, L->top.p - L->stack.p));
  lum_pushinteger(L, stacksize(L));
  lum_pushinteger(L, cast(lum_Integer, L->nCcalls));
  lum_pushinteger(L, L->nci);
  lum_pushinteger(L, (lum_Integer)(size_t)&a);
  return 5;
}


static int table_query (lum_State *L) {
  const Table *t;
  int i = cast_int(lumL_optinteger(L, 2, -1));
  unsigned int asize;
  lumL_checktype(L, 1, LUM_TTABLE);
  t = hvalue(obj_at(L, 1));
  asize = t->asize;
  if (i == -1) {
    lum_pushinteger(L, cast(lum_Integer, asize));
    lum_pushinteger(L, cast(lum_Integer, allocsizenode(t)));
    lum_pushinteger(L, cast(lum_Integer, asize > 0 ? *lenhint(t) : 0));
    return 3;
  }
  else if (cast_uint(i) < asize) {
    lum_pushinteger(L, i);
    if (!tagisempty(*getArrTag(t, i)))
      arr2obj(t, cast_uint(i), s2v(L->top.p));
    else
      setnilvalue(s2v(L->top.p));
    api_incr_top(L);
    lum_pushnil(L);
  }
  else if (cast_uint(i -= cast_int(asize)) < sizenode(t)) {
    TValue k;
    getnodekey(L, &k, gnode(t, i));
    if (!isempty(gval(gnode(t, i))) ||
        ttisnil(&k) ||
        ttisnumber(&k)) {
      pushobject(L, &k);
    }
    else
      lum_pushliteral(L, "<undef>");
    if (!isempty(gval(gnode(t, i))))
      pushobject(L, gval(gnode(t, i)));
    else
      lum_pushnil(L);
    lum_pushinteger(L, gnext(&t->node[i]));
  }
  return 3;
}


static int gc_query (lum_State *L) {
  global_State *g = G(L);
  lum_pushstring(L, g->gckind == KGC_INC ? "inc"
                  : g->gckind == KGC_GENMAJOR ? "genmajor"
                  : "genminor");
  lum_pushstring(L, statenames[g->gcstate]);
  lum_pushinteger(L, cast_st2S(gettotalbytes(g)));
  lum_pushinteger(L, cast_st2S(g->GCdebt));
  lum_pushinteger(L, cast_st2S(g->GCmarked));
  lum_pushinteger(L, cast_st2S(g->GCmajorminor));
  return 6;
}


static int test_codeparam (lum_State *L) {
  lum_Integer p = lumL_checkinteger(L, 1);
  lum_pushinteger(L, lumO_codeparam(cast_uint(p)));
  return 1;
}


static int test_applyparam (lum_State *L) {
  lum_Integer p = lumL_checkinteger(L, 1);
  lum_Integer x = lumL_checkinteger(L, 2);
  lum_pushinteger(L, cast(lum_Integer, lumO_applyparam(cast_byte(p), x)));
  return 1;
}


static int string_query (lum_State *L) {
  stringtable *tb = &G(L)->strt;
  int s = cast_int(lumL_optinteger(L, 1, 0)) - 1;
  if (s == -1) {
    lum_pushinteger(L ,tb->size);
    lum_pushinteger(L ,tb->nuse);
    return 2;
  }
  else if (s < tb->size) {
    TString *ts;
    int n = 0;
    for (ts = tb->hash[s]; ts != NULL; ts = ts->u.hnext) {
      setsvalue2s(L, L->top.p, ts);
      api_incr_top(L);
      n++;
    }
    return n;
  }
  else return 0;
}


static int getreftable (lum_State *L) {
  if (lum_istable(L, 2))  /* is there a table as second argument? */
    return 2;  /* use it as the table */
  else
    return LUM_REGISTRYINDEX;  /* default is to use the register */
}


static int tref (lum_State *L) {
  int t = getreftable(L);
  int level = lum_gettop(L);
  lumL_checkany(L, 1);
  lum_pushvalue(L, 1);
  lum_pushinteger(L, lumL_ref(L, t));
  cast_void(level);  /* to avoid warnings */
  lum_assert(lum_gettop(L) == level+1);  /* +1 for result */
  return 1;
}


static int getref (lum_State *L) {
  int t = getreftable(L);
  int level = lum_gettop(L);
  lum_rawgeti(L, t, lumL_checkinteger(L, 1));
  cast_void(level);  /* to avoid warnings */
  lum_assert(lum_gettop(L) == level+1);
  return 1;
}

static int unref (lum_State *L) {
  int t = getreftable(L);
  int level = lum_gettop(L);
  lumL_unref(L, t, cast_int(lumL_checkinteger(L, 1)));
  cast_void(level);  /* to avoid warnings */
  lum_assert(lum_gettop(L) == level);
  return 0;
}


static int upvalue (lum_State *L) {
  int n = cast_int(lumL_checkinteger(L, 2));
  lumL_checktype(L, 1, LUM_TFUNCTION);
  if (lum_isnone(L, 3)) {
    const char *name = lum_getupvalue(L, 1, n);
    if (name == NULL) return 0;
    lum_pushstring(L, name);
    return 2;
  }
  else {
    const char *name = lum_setupvalue(L, 1, n);
    lum_pushstring(L, name);
    return 1;
  }
}


static int newuserdata (lum_State *L) {
  size_t size = cast_sizet(lumL_optinteger(L, 1, 0));
  int nuv = cast_int(lumL_optinteger(L, 2, 0));
  char *p = cast_charp(lum_newuserdatauv(L, size, nuv));
  while (size--) *p++ = '\0';
  return 1;
}


static int pushuserdata (lum_State *L) {
  lum_Integer u = lumL_checkinteger(L, 1);
  lum_pushlightuserdata(L, cast_voidp(cast_sizet(u)));
  return 1;
}


static int udataval (lum_State *L) {
  lum_pushinteger(L, cast(lum_Integer, cast(size_t, lum_touserdata(L, 1))));
  return 1;
}


static int doonnewstack (lum_State *L) {
  lum_State *L1 = lum_newthread(L);
  size_t l;
  const char *s = lumL_checklstring(L, 1, &l);
  int status = lumL_loadbuffer(L1, s, l, s);
  if (status == LUM_OK)
    status = lum_pcall(L1, 0, 0, 0);
  lum_pushinteger(L, status);
  return 1;
}


static int s2d (lum_State *L) {
  lum_pushnumber(L, cast_num(*cast(const double *, lumL_checkstring(L, 1))));
  return 1;
}


static int d2s (lum_State *L) {
  double d = cast(double, lumL_checknumber(L, 1));
  lum_pushlstring(L, cast_charp(&d), sizeof(d));
  return 1;
}


static int num2int (lum_State *L) {
  lum_pushinteger(L, lum_tointeger(L, 1));
  return 1;
}


static int makeseed (lum_State *L) {
  lum_pushinteger(L, cast(lum_Integer, lumL_makeseed(L)));
  return 1;
}


static int newstate (lum_State *L) {
  void *ud;
  lum_Alloc f = lum_getallocf(L, &ud);
  lum_State *L1 = lum_newstate(f, ud, 0);
  if (L1) {
    lum_atpanic(L1, tpanic);
    lum_pushlightuserdata(L, L1);
  }
  else
    lum_pushnil(L);
  return 1;
}


static lum_State *getstate (lum_State *L) {
  lum_State *L1 = cast(lum_State *, lum_touserdata(L, 1));
  lumL_argcheck(L, L1 != NULL, 1, "state expected");
  return L1;
}


static int loadlib (lum_State *L) {
  lum_State *L1 = getstate(L);
  int load = cast_int(lumL_checkinteger(L, 2));
  int preload = cast_int(lumL_checkinteger(L, 3));
  lumL_openselectedlibs(L1, load, preload);
  lumL_requiref(L1, "T", lumB_opentests, 0);
  lum_assert(lum_type(L1, -1) == LUM_TTABLE);
  /* 'requiref' should not reload module already loaded... */
  lumL_requiref(L1, "T", NULL, 1);  /* seg. fault if it reloads */
  /* ...but should return the same module */
  lum_assert(lum_compare(L1, -1, -2, LUM_OPEQ));
  return 0;
}

static int closestate (lum_State *L) {
  lum_State *L1 = getstate(L);
  lum_close(L1);
  return 0;
}

static int doremote (lum_State *L) {
  lum_State *L1 = getstate(L);
  size_t lcode;
  const char *code = lumL_checklstring(L, 2, &lcode);
  int status;
  lum_settop(L1, 0);
  status = lumL_loadbuffer(L1, code, lcode, code);
  if (status == LUM_OK)
    status = lum_pcall(L1, 0, LUM_MULTRET, 0);
  if (status != LUM_OK) {
    lum_pushnil(L);
    lum_pushstring(L, lum_tostring(L1, -1));
    lum_pushinteger(L, status);
    return 3;
  }
  else {
    int i = 0;
    while (!lum_isnone(L1, ++i))
      lum_pushstring(L, lum_tostring(L1, i));
    lum_pop(L1, i-1);
    return i-1;
  }
}


static int log2_aux (lum_State *L) {
  unsigned int x = (unsigned int)lumL_checkinteger(L, 1);
  lum_pushinteger(L, lumO_ceillog2(x));
  return 1;
}


struct Aux { jmp_buf jb; const char *paniccode; lum_State *L; };

/*
** does a long-jump back to "main program".
*/
static int panicback (lum_State *L) {
  struct Aux *b;
  lum_checkstack(L, 1);  /* open space for 'Aux' struct */
  lum_getfield(L, LUM_REGISTRYINDEX, "_jmpbuf");  /* get 'Aux' struct */
  b = (struct Aux *)lum_touserdata(L, -1);
  lum_pop(L, 1);  /* remove 'Aux' struct */
  runC(b->L, L, b->paniccode);  /* run optional panic code */
  longjmp(b->jb, 1);
  return 1;  /* to avoid warnings */
}

static int checkpanic (lum_State *L) {
  struct Aux b;
  void *ud;
  lum_State *L1;
  const char *code = lumL_checkstring(L, 1);
  lum_Alloc f = lum_getallocf(L, &ud);
  b.paniccode = lumL_optstring(L, 2, "");
  b.L = L;
  L1 = lum_newstate(f, ud, 0);  /* create new state */
  if (L1 == NULL) {  /* error? */
    lum_pushstring(L, MEMERRMSG);
    return 1;
  }
  lum_atpanic(L1, panicback);  /* set its panic function */
  lum_pushlightuserdata(L1, &b);
  lum_setfield(L1, LUM_REGISTRYINDEX, "_jmpbuf");  /* store 'Aux' struct */
  if (setjmp(b.jb) == 0) {  /* set jump buffer */
    runC(L, L1, code);  /* run code unprotected */
    lum_pushliteral(L, "no errors");
  }
  else {  /* error handling */
    /* move error message to original state */
    lum_pushstring(L, lum_tostring(L1, -1));
  }
  lum_close(L1);
  return 1;
}


static int externKstr (lum_State *L) {
  size_t len;
  const char *s = lumL_checklstring(L, 1, &len);
  lum_pushexternalstring(L, s, len, NULL, NULL);
  return 1;
}


/*
** Create a buffer with the content of a given string and then
** create an external string using that buffer. Use the allocation
** function from Lum to create and free the buffer.
*/
static int externstr (lum_State *L) {
  size_t len;
  const char *s = lumL_checklstring(L, 1, &len);
  void *ud;
  lum_Alloc allocf = lum_getallocf(L, &ud);  /* get allocation function */
  /* create the buffer */
  char *buff = cast_charp((*allocf)(ud, NULL, 0, len + 1));
  if (buff == NULL) {  /* memory error? */
    lum_pushliteral(L, "not enough memory");
    lum_error(L);  /* raise a memory error */
  }
  /* copy string content to buffer, including ending 0 */
  memcpy(buff, s, (len + 1) * sizeof(char));
  /* create external string */
  lum_pushexternalstring(L, buff, len, allocf, ud);
  return 1;
}


/*
** {====================================================================
** function to test the API with C. It interprets a kind of assembler
** language with calls to the API, so the test can be driven by Lum code
** =====================================================================
*/


static void sethookaux (lum_State *L, int mask, int count, const char *code);

static const char *const delimits = " \t\n,;";

static void skip (const char **pc) {
  for (;;) {
    if (**pc != '\0' && strchr(delimits, **pc)) (*pc)++;
    else if (**pc == '#') {  /* comment? */
      while (**pc != '\n' && **pc != '\0') (*pc)++;  /* until end-of-line */
    }
    else break;
  }
}

static int getnum_aux (lum_State *L, lum_State *L1, const char **pc) {
  int res = 0;
  int sig = 1;
  skip(pc);
  if (**pc == '.') {
    res = cast_int(lum_tointeger(L1, -1));
    lum_pop(L1, 1);
    (*pc)++;
    return res;
  }
  else if (**pc == '*') {
    res = lum_gettop(L1);
    (*pc)++;
    return res;
  }
  else if (**pc == '!') {
    (*pc)++;
    if (**pc == 'G')
      res = LUM_RIDX_GLOBALS;
    else if (**pc == 'M')
      res = LUM_RIDX_MAINTHREAD;
    else lum_assert(0);
    (*pc)++;
    return res;
  }
  else if (**pc == '-') {
    sig = -1;
    (*pc)++;
  }
  if (!lisdigit(cast_uchar(**pc)))
    lumL_error(L, "number expected (%s)", *pc);
  while (lisdigit(cast_uchar(**pc))) res = res*10 + (*(*pc)++) - '0';
  return sig*res;
}

static const char *getstring_aux (lum_State *L, char *buff, const char **pc) {
  int i = 0;
  skip(pc);
  if (**pc == '"' || **pc == '\'') {  /* quoted string? */
    int quote = *(*pc)++;
    while (**pc != quote) {
      if (**pc == '\0') lumL_error(L, "unfinished string in C script");
      buff[i++] = *(*pc)++;
    }
    (*pc)++;
  }
  else {
    while (**pc != '\0' && !strchr(delimits, **pc))
      buff[i++] = *(*pc)++;
  }
  buff[i] = '\0';
  return buff;
}


static int getindex_aux (lum_State *L, lum_State *L1, const char **pc) {
  skip(pc);
  switch (*(*pc)++) {
    case 'R': return LUM_REGISTRYINDEX;
    case 'U': return lum_upvalueindex(getnum_aux(L, L1, pc));
    default: {
      int n;
      (*pc)--;  /* to read again */
      n = getnum_aux(L, L1, pc);
      if (n == 0) return 0;
      else return lum_absindex(L1, n);
    }
  }
}


static const char *const statcodes[] = {"OK", "YIELD", "ERRRUN",
    "ERRSYNTAX", MEMERRMSG, "ERRERR"};

/*
** Avoid these stat codes from being collected, to avoid possible
** memory error when pushing them.
*/
static void regcodes (lum_State *L) {
  unsigned int i;
  for (i = 0; i < sizeof(statcodes) / sizeof(statcodes[0]); i++) {
    lum_pushboolean(L, 1);
    lum_setfield(L, LUM_REGISTRYINDEX, statcodes[i]);
  }
}


#define EQ(s1)	(strcmp(s1, inst) == 0)

#define getnum		(getnum_aux(L, L1, &pc))
#define getstring	(getstring_aux(L, buff, &pc))
#define getindex	(getindex_aux(L, L1, &pc))


static int testC (lum_State *L);
static int Cfunck (lum_State *L, int status, lum_KContext ctx);

/*
** arithmetic operation encoding for 'arith' instruction
** LUM_OPIDIV  -> \
** LUM_OPSHL   -> <
** LUM_OPSHR   -> >
** LUM_OPUNM   -> _
** LUM_OPBNOT  -> !
*/
static const char ops[] = "+-*%^/\\&|~<>_!";

static int runC (lum_State *L, lum_State *L1, const char *pc) {
  char buff[300];
  int status = 0;
  if (pc == NULL) return lumL_error(L, "attempt to runC null script");
  for (;;) {
    const char *inst = getstring;
    if EQ("") return 0;
    else if EQ("absindex") {
      lum_pushinteger(L1, getindex);
    }
    else if EQ("append") {
      int t = getindex;
      int i = cast_int(lum_rawlen(L1, t));
      lum_rawseti(L1, t, i + 1);
    }
    else if EQ("arith") {
      int op;
      skip(&pc);
      op = cast_int(strchr(ops, *pc++) - ops);
      lum_arith(L1, op);
    }
    else if EQ("call") {
      int narg = getnum;
      int nres = getnum;
      lum_call(L1, narg, nres);
    }
    else if EQ("callk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      lum_callk(L1, narg, nres, i, Cfunck);
    }
    else if EQ("checkstack") {
      int sz = getnum;
      const char *msg = getstring;
      if (*msg == '\0')
        msg = NULL;  /* to test 'lumL_checkstack' with no message */
      lumL_checkstack(L1, sz, msg);
    }
    else if EQ("rawcheckstack") {
      int sz = getnum;
      lum_pushboolean(L1, lum_checkstack(L1, sz));
    }
    else if EQ("compare") {
      const char *opt = getstring;  /* EQ, LT, or LE */
      int op = (opt[0] == 'E') ? LUM_OPEQ
                               : (opt[1] == 'T') ? LUM_OPLT : LUM_OPLE;
      int a = getindex;
      int b = getindex;
      lum_pushboolean(L1, lum_compare(L1, a, b, op));
    }
    else if EQ("concat") {
      lum_concat(L1, getnum);
    }
    else if EQ("copy") {
      int f = getindex;
      lum_copy(L1, f, getindex);
    }
    else if EQ("func2num") {
      lum_CFunction func = lum_tocfunction(L1, getindex);
      lum_pushinteger(L1, cast(lum_Integer, cast(size_t, func)));
    }
    else if EQ("getfield") {
      int t = getindex;
      int tp = lum_getfield(L1, t, getstring);
      lum_assert(tp == lum_type(L1, -1));
    }
    else if EQ("getglobal") {
      lum_getglobal(L1, getstring);
    }
    else if EQ("getmetatable") {
      if (lum_getmetatable(L1, getindex) == 0)
        lum_pushnil(L1);
    }
    else if EQ("gettable") {
      int tp = lum_gettable(L1, getindex);
      lum_assert(tp == lum_type(L1, -1));
    }
    else if EQ("gettop") {
      lum_pushinteger(L1, lum_gettop(L1));
    }
    else if EQ("gsub") {
      int a = getnum; int b = getnum; int c = getnum;
      lumL_gsub(L1, lum_tostring(L1, a),
                    lum_tostring(L1, b),
                    lum_tostring(L1, c));
    }
    else if EQ("insert") {
      lum_insert(L1, getnum);
    }
    else if EQ("iscfunction") {
      lum_pushboolean(L1, lum_iscfunction(L1, getindex));
    }
    else if EQ("isfunction") {
      lum_pushboolean(L1, lum_isfunction(L1, getindex));
    }
    else if EQ("isnil") {
      lum_pushboolean(L1, lum_isnil(L1, getindex));
    }
    else if EQ("isnull") {
      lum_pushboolean(L1, lum_isnone(L1, getindex));
    }
    else if EQ("isnumber") {
      lum_pushboolean(L1, lum_isnumber(L1, getindex));
    }
    else if EQ("isstring") {
      lum_pushboolean(L1, lum_isstring(L1, getindex));
    }
    else if EQ("istable") {
      lum_pushboolean(L1, lum_istable(L1, getindex));
    }
    else if EQ("isudataval") {
      lum_pushboolean(L1, lum_islightuserdata(L1, getindex));
    }
    else if EQ("isuserdata") {
      lum_pushboolean(L1, lum_isuserdata(L1, getindex));
    }
    else if EQ("len") {
      lum_len(L1, getindex);
    }
    else if EQ("Llen") {
      lum_pushinteger(L1, lumL_len(L1, getindex));
    }
    else if EQ("loadfile") {
      lumL_loadfile(L1, lumL_checkstring(L1, getnum));
    }
    else if EQ("loadstring") {
      size_t slen;
      const char *s = lumL_checklstring(L1, getnum, &slen);
      const char *name = getstring;
      const char *mode = getstring;
      lumL_loadbufferx(L1, s, slen, name, mode);
    }
    else if EQ("newmetatable") {
      lum_pushboolean(L1, lumL_newmetatable(L1, getstring));
    }
    else if EQ("newtable") {
      lum_newtable(L1);
    }
    else if EQ("newthread") {
      lum_newthread(L1);
    }
    else if EQ("resetthread") {
      lum_pushinteger(L1, lum_resetthread(L1));  /* deprecated */
    }
    else if EQ("newuserdata") {
      lum_newuserdata(L1, cast_sizet(getnum));
    }
    else if EQ("next") {
      lum_next(L1, -2);
    }
    else if EQ("objsize") {
      lum_pushinteger(L1, l_castU2S(lum_rawlen(L1, getindex)));
    }
    else if EQ("pcall") {
      int narg = getnum;
      int nres = getnum;
      status = lum_pcall(L1, narg, nres, getnum);
    }
    else if EQ("pcallk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      status = lum_pcallk(L1, narg, nres, 0, i, Cfunck);
    }
    else if EQ("pop") {
      lum_pop(L1, getnum);
    }
    else if EQ("printstack") {
      int n = getnum;
      if (n != 0) {
        lum_printvalue(s2v(L->ci->func.p + n));
        printf("\n");
      }
      else lum_printstack(L1);
    }
    else if EQ("print") {
      const char *msg = getstring;
      printf("%s\n", msg);
    }
    else if EQ("warningC") {
      const char *msg = getstring;
      lum_warning(L1, msg, 1);
    }
    else if EQ("warning") {
      const char *msg = getstring;
      lum_warning(L1, msg, 0);
    }
    else if EQ("pushbool") {
      lum_pushboolean(L1, getnum);
    }
    else if EQ("pushcclosure") {
      lum_pushcclosure(L1, testC, getnum);
    }
    else if EQ("pushint") {
      lum_pushinteger(L1, getnum);
    }
    else if EQ("pushnil") {
      lum_pushnil(L1);
    }
    else if EQ("pushnum") {
      lum_pushnumber(L1, (lum_Number)getnum);
    }
    else if EQ("pushstatus") {
      lum_pushstring(L1, statcodes[status]);
    }
    else if EQ("pushstring") {
      lum_pushstring(L1, getstring);
    }
    else if EQ("pushupvalueindex") {
      lum_pushinteger(L1, lum_upvalueindex(getnum));
    }
    else if EQ("pushvalue") {
      lum_pushvalue(L1, getindex);
    }
    else if EQ("pushfstringI") {
      lum_pushfstring(L1, lum_tostring(L, -2), (int)lum_tointeger(L, -1));
    }
    else if EQ("pushfstringS") {
      lum_pushfstring(L1, lum_tostring(L, -2), lum_tostring(L, -1));
    }
    else if EQ("pushfstringP") {
      lum_pushfstring(L1, lum_tostring(L, -2), lum_topointer(L, -1));
    }
    else if EQ("rawget") {
      int t = getindex;
      lum_rawget(L1, t);
    }
    else if EQ("rawgeti") {
      int t = getindex;
      lum_rawgeti(L1, t, getnum);
    }
    else if EQ("rawgetp") {
      int t = getindex;
      lum_rawgetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("rawset") {
      int t = getindex;
      lum_rawset(L1, t);
    }
    else if EQ("rawseti") {
      int t = getindex;
      lum_rawseti(L1, t, getnum);
    }
    else if EQ("rawsetp") {
      int t = getindex;
      lum_rawsetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("remove") {
      lum_remove(L1, getnum);
    }
    else if EQ("replace") {
      lum_replace(L1, getindex);
    }
    else if EQ("resume") {
      int i = getindex;
      int nres;
      status = lum_resume(lum_tothread(L1, i), L, getnum, &nres);
    }
    else if EQ("traceback") {
      const char *msg = getstring;
      int level = getnum;
      lumL_traceback(L1, L1, msg, level);
    }
    else if EQ("threadstatus") {
      lum_pushstring(L1, statcodes[lum_status(L1)]);
    }
    else if EQ("alloccount") {
      l_memcontrol.countlimit = cast_uint(getnum);
    }
    else if EQ("return") {
      int n = getnum;
      if (L1 != L) {
        int i;
        for (i = 0; i < n; i++) {
          int idx = -(n - i);
          switch (lum_type(L1, idx)) {
            case LUM_TBOOLEAN:
              lum_pushboolean(L, lum_toboolean(L1, idx));
              break;
            default:
              lum_pushstring(L, lum_tostring(L1, idx));
              break;
          }
        }
      }
      return n;
    }
    else if EQ("rotate") {
      int i = getindex;
      lum_rotate(L1, i, getnum);
    }
    else if EQ("setfield") {
      int t = getindex;
      const char *s = getstring;
      lum_setfield(L1, t, s);
    }
    else if EQ("seti") {
      int t = getindex;
      lum_seti(L1, t, getnum);
    }
    else if EQ("setglobal") {
      const char *s = getstring;
      lum_setglobal(L1, s);
    }
    else if EQ("sethook") {
      int mask = getnum;
      int count = getnum;
      const char *s = getstring;
      sethookaux(L1, mask, count, s);
    }
    else if EQ("setmetatable") {
      int idx = getindex;
      lum_setmetatable(L1, idx);
    }
    else if EQ("settable") {
      lum_settable(L1, getindex);
    }
    else if EQ("settop") {
      lum_settop(L1, getnum);
    }
    else if EQ("testudata") {
      int i = getindex;
      lum_pushboolean(L1, lumL_testudata(L1, i, getstring) != NULL);
    }
    else if EQ("error") {
      lum_error(L1);
    }
    else if EQ("abort") {
      abort();
    }
    else if EQ("throw") {
#if defined(__cplusplus)
static struct X { int x; } x;
      throw x;
#else
      lumL_error(L1, "C++");
#endif
      break;
    }
    else if EQ("tobool") {
      lum_pushboolean(L1, lum_toboolean(L1, getindex));
    }
    else if EQ("tocfunction") {
      lum_pushcfunction(L1, lum_tocfunction(L1, getindex));
    }
    else if EQ("tointeger") {
      lum_pushinteger(L1, lum_tointeger(L1, getindex));
    }
    else if EQ("tonumber") {
      lum_pushnumber(L1, lum_tonumber(L1, getindex));
    }
    else if EQ("topointer") {
      lum_pushlightuserdata(L1, cast_voidp(lum_topointer(L1, getindex)));
    }
    else if EQ("touserdata") {
      lum_pushlightuserdata(L1, lum_touserdata(L1, getindex));
    }
    else if EQ("tostring") {
      const char *s = lum_tostring(L1, getindex);
      const char *s1 = lum_pushstring(L1, s);
      cast_void(s1);  /* to avoid warnings */
      lum_longassert((s == NULL && s1 == NULL) || strcmp(s, s1) == 0);
    }
    else if EQ("Ltolstring") {
      lumL_tolstring(L1, getindex, NULL);
    }
    else if EQ("type") {
      lum_pushstring(L1, lumL_typename(L1, getnum));
    }
    else if EQ("xmove") {
      int f = getindex;
      int t = getindex;
      lum_State *fs = (f == 0) ? L1 : lum_tothread(L1, f);
      lum_State *ts = (t == 0) ? L1 : lum_tothread(L1, t);
      int n = getnum;
      if (n == 0) n = lum_gettop(fs);
      lum_xmove(fs, ts, n);
    }
    else if EQ("isyieldable") {
      lum_pushboolean(L1, lum_isyieldable(lum_tothread(L1, getindex)));
    }
    else if EQ("yield") {
      return lum_yield(L1, getnum);
    }
    else if EQ("yieldk") {
      int nres = getnum;
      int i = getindex;
      return lum_yieldk(L1, nres, i, Cfunck);
    }
    else if EQ("toclose") {
      lum_toclose(L1, getnum);
    }
    else if EQ("closeslot") {
      lum_closeslot(L1, getnum);
    }
    else if EQ("argerror") {
      int arg = getnum;
      lumL_argerror(L1, arg, getstring);
    }
    else lumL_error(L, "unknown instruction %s", buff);
  }
  return 0;
}


static int testC (lum_State *L) {
  lum_State *L1;
  const char *pc;
  if (lum_isuserdata(L, 1)) {
    L1 = getstate(L);
    pc = lumL_checkstring(L, 2);
  }
  else if (lum_isthread(L, 1)) {
    L1 = lum_tothread(L, 1);
    pc = lumL_checkstring(L, 2);
  }
  else {
    L1 = L;
    pc = lumL_checkstring(L, 1);
  }
  return runC(L, L1, pc);
}


static int Cfunc (lum_State *L) {
  return runC(L, L, lum_tostring(L, lum_upvalueindex(1)));
}


static int Cfunck (lum_State *L, int status, lum_KContext ctx) {
  lum_pushstring(L, statcodes[status]);
  lum_setglobal(L, "status");
  lum_pushinteger(L, cast(lum_Integer, ctx));
  lum_setglobal(L, "ctx");
  return runC(L, L, lum_tostring(L, cast_int(ctx)));
}


static int makeCfunc (lum_State *L) {
  lumL_checkstring(L, 1);
  lum_pushcclosure(L, Cfunc, lum_gettop(L));
  return 1;
}


/* }====================================================== */


/*
** {======================================================
** tests for C hooks
** =======================================================
*/

/*
** C hook that runs the C script stored in registry.C_HOOK[L]
*/
static void Chook (lum_State *L, lum_Debug *ar) {
  const char *scpt;
  const char *const events [] = {"call", "ret", "line", "count", "tailcall"};
  lum_getfield(L, LUM_REGISTRYINDEX, "C_HOOK");
  lum_pushlightuserdata(L, L);
  lum_gettable(L, -2);  /* get C_HOOK[L] (script saved by sethookaux) */
  scpt = lum_tostring(L, -1);  /* not very religious (string will be popped) */
  lum_pop(L, 2);  /* remove C_HOOK and script */
  lum_pushstring(L, events[ar->event]);  /* may be used by script */
  lum_pushinteger(L, ar->currentline);  /* may be used by script */
  runC(L, L, scpt);  /* run script from C_HOOK[L] */
}


/*
** sets 'registry.C_HOOK[L] = scpt' and sets 'Chook' as a hook
*/
static void sethookaux (lum_State *L, int mask, int count, const char *scpt) {
  if (*scpt == '\0') {  /* no script? */
    lum_sethook(L, NULL, 0, 0);  /* turn off hooks */
    return;
  }
  lum_getfield(L, LUM_REGISTRYINDEX, "C_HOOK");  /* get C_HOOK table */
  if (!lum_istable(L, -1)) {  /* no hook table? */
    lum_pop(L, 1);  /* remove previous value */
    lum_newtable(L);  /* create new C_HOOK table */
    lum_pushvalue(L, -1);
    lum_setfield(L, LUM_REGISTRYINDEX, "C_HOOK");  /* register it */
  }
  lum_pushlightuserdata(L, L);
  lum_pushstring(L, scpt);
  lum_settable(L, -3);  /* C_HOOK[L] = script */
  lum_sethook(L, Chook, mask, count);
}


static int sethook (lum_State *L) {
  if (lum_isnoneornil(L, 1))
    lum_sethook(L, NULL, 0, 0);  /* turn off hooks */
  else {
    const char *scpt = lumL_checkstring(L, 1);
    const char *smask = lumL_checkstring(L, 2);
    int count = cast_int(lumL_optinteger(L, 3, 0));
    int mask = 0;
    if (strchr(smask, 'c')) mask |= LUM_MASKCALL;
    if (strchr(smask, 'r')) mask |= LUM_MASKRET;
    if (strchr(smask, 'l')) mask |= LUM_MASKLINE;
    if (count > 0) mask |= LUM_MASKCOUNT;
    sethookaux(L, mask, count, scpt);
  }
  return 0;
}


static int coresume (lum_State *L) {
  int status, nres;
  lum_State *co = lum_tothread(L, 1);
  lumL_argcheck(L, co, 1, "coroutine expected");
  status = lum_resume(co, L, 0, &nres);
  if (status != LUM_OK && status != LUM_YIELD) {
    lum_pushboolean(L, 0);
    lum_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lum_pushboolean(L, 1);
    return 1;
  }
}

/* }====================================================== */



static const struct lumL_Reg tests_funcs[] = {
  {"checkmemory", lum_checkmemory},
  {"closestate", closestate},
  {"d2s", d2s},
  {"doonnewstack", doonnewstack},
  {"doremote", doremote},
  {"gccolor", gc_color},
  {"gcage", gc_age},
  {"gcstate", gc_state},
  {"tracegc", tracegc},
  {"pobj", gc_printobj},
  {"getref", getref},
  {"hash", hash_query},
  {"log2", log2_aux},
  {"limits", get_limits},
  {"listcode", listcode},
  {"printcode", printcode},
  {"listk", listk},
  {"listabslineinfo", listabslineinfo},
  {"listlocals", listlocals},
  {"loadlib", loadlib},
  {"checkpanic", checkpanic},
  {"newstate", newstate},
  {"newuserdata", newuserdata},
  {"num2int", num2int},
  {"makeseed", makeseed},
  {"pushuserdata", pushuserdata},
  {"gcquery", gc_query},
  {"querystr", string_query},
  {"querytab", table_query},
  {"codeparam", test_codeparam},
  {"applyparam", test_applyparam},
  {"ref", tref},
  {"resume", coresume},
  {"s2d", s2d},
  {"sethook", sethook},
  {"stacklevel", stacklevel},
  {"testC", testC},
  {"makeCfunc", makeCfunc},
  {"totalmem", mem_query},
  {"alloccount", alloc_count},
  {"allocfailnext", alloc_failnext},
  {"trick", settrick},
  {"udataval", udataval},
  {"unref", unref},
  {"upvalue", upvalue},
  {"externKstr", externKstr},
  {"externstr", externstr},
  {NULL, NULL}
};


static void checkfinalmem (void) {
  lum_assert(l_memcontrol.numblocks == 0);
  lum_assert(l_memcontrol.total == 0);
}


int lumB_opentests (lum_State *L) {
  void *ud;
  lum_Alloc f = lum_getallocf(L, &ud);
  lum_atpanic(L, &tpanic);
  lum_setwarnf(L, &warnf, L);
  lum_pushboolean(L, 0);
  lum_setglobal(L, "_WARN");  /* _WARN = false */
  regcodes(L);
  atexit(checkfinalmem);
  lum_assert(f == debug_realloc && ud == cast_voidp(&l_memcontrol));
  lum_setallocf(L, f, ud);  /* exercise this function */
  lumL_newlib(L, tests_funcs);
  return 1;
}

#endif

