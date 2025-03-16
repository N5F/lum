/*
** $Id: ldebug.c $
** Debug Interface
** See Copyright Notice in lum.h
*/

#define ldebug_c
#define LUM_CORE

#include "lprefix.h"


#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "lum.h"

#include "lapi.h"
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



#define LumClosure(f)		((f) != NULL && (f)->c.tt == LUM_VLCL)


static const char *funcnamefromcall (lum_State *L, CallInfo *ci,
                                                   const char **name);


static int currentpc (CallInfo *ci) {
  lum_assert(isLum(ci));
  return pcRel(ci->u.l.savedpc, ci_func(ci)->p);
}


/*
** Get a "base line" to find the line corresponding to an instruction.
** Base lines are regularly placed at MAXIWTHABS intervals, so usually
** an integer division gets the right place. When the source file has
** large sequences of empty/comment lines, it may need extra entries,
** so the original estimate needs a correction.
** If the original estimate is -1, the initial 'if' ensures that the
** 'while' will run at least once.
** The assertion that the estimate is a lower bound for the correct base
** is valid as long as the debug info has been generated with the same
** value for MAXIWTHABS or smaller. (Previous releases use a little
** smaller value.)
*/
static int getbaseline (const Proto *f, int pc, int *basepc) {
  if (f->sizeabslineinfo == 0 || pc < f->abslineinfo[0].pc) {
    *basepc = -1;  /* start from the beginning */
    return f->linedefined;
  }
  else {
    int i = pc / MAXIWTHABS - 1;  /* get an estimate */
    /* estimate must be a lower bound of the correct base */
    lum_assert(i < 0 ||
              (i < f->sizeabslineinfo && f->abslineinfo[i].pc <= pc));
    while (i + 1 < f->sizeabslineinfo && pc >= f->abslineinfo[i + 1].pc)
      i++;  /* low estimate; adjust it */
    *basepc = f->abslineinfo[i].pc;
    return f->abslineinfo[i].line;
  }
}


/*
** Get the line corresponding to instruction 'pc' in function 'f';
** first gets a base line and from there does the increments until
** the desired instruction.
*/
int lumG_getfuncline (const Proto *f, int pc) {
  if (f->lineinfo == NULL)  /* no debug information? */
    return -1;
  else {
    int basepc;
    int baseline = getbaseline(f, pc, &basepc);
    while (basepc++ < pc) {  /* walk until given instruction */
      lum_assert(f->lineinfo[basepc] != ABSLINEINFO);
      baseline += f->lineinfo[basepc];  /* correct line */
    }
    return baseline;
  }
}


static int getcurrentline (CallInfo *ci) {
  return lumG_getfuncline(ci_func(ci)->p, currentpc(ci));
}


/*
** Set 'trap' for all active Lum frames.
** This function can be called during a signal, under "reasonable"
** assumptions. A new 'ci' is completely linked in the list before it
** becomes part of the "active" list, and we assume that pointers are
** atomic; see comment in next function.
** (A compiler doing interprocedural optimizations could, theoretically,
** reorder memory writes in such a way that the list could be
** temporarily broken while inserting a new element. We simply assume it
** has no good reasons to do that.)
*/
static void settraps (CallInfo *ci) {
  for (; ci != NULL; ci = ci->previous)
    if (isLum(ci))
      ci->u.l.trap = 1;
}


/*
** This function can be called during a signal, under "reasonable"
** assumptions.
** Fields 'basehookcount' and 'hookcount' (set by 'resethookcount')
** are for debug only, and it is no problem if they get arbitrary
** values (causes at most one wrong hook call). 'hookmask' is an atomic
** value. We assume that pointers are atomic too (e.g., gcc ensures that
** for all platforms where it runs). Moreover, 'hook' is always checked
** before being called (see 'lumD_hook').
*/
LUM_API void lum_sethook (lum_State *L, lum_Hook func, int mask, int count) {
  if (func == NULL || mask == 0) {  /* turn off hooks? */
    mask = 0;
    func = NULL;
  }
  L->hook = func;
  L->basehookcount = count;
  resethookcount(L);
  L->hookmask = cast_byte(mask);
  if (mask)
    settraps(L->ci);  /* to trace inside 'lumV_execute' */
}


LUM_API lum_Hook lum_gethook (lum_State *L) {
  return L->hook;
}


LUM_API int lum_gethookmask (lum_State *L) {
  return L->hookmask;
}


LUM_API int lum_gethookcount (lum_State *L) {
  return L->basehookcount;
}


LUM_API int lum_getstack (lum_State *L, int level, lum_Debug *ar) {
  int status;
  CallInfo *ci;
  if (level < 0) return 0;  /* invalid (negative) level */
  lum_lock(L);
  for (ci = L->ci; level > 0 && ci != &L->base_ci; ci = ci->previous)
    level--;
  if (level == 0 && ci != &L->base_ci) {  /* level found? */
    status = 1;
    ar->i_ci = ci;
  }
  else status = 0;  /* no such level */
  lum_unlock(L);
  return status;
}


static const char *upvalname (const Proto *p, int uv) {
  TString *s = check_exp(uv < p->sizeupvalues, p->upvalues[uv].name);
  if (s == NULL) return "?";
  else return getstr(s);
}


static const char *findvararg (CallInfo *ci, int n, StkId *pos) {
  if (clLvalue(s2v(ci->func.p))->p->flag & PF_ISVARARG) {
    int nextra = ci->u.l.nextraargs;
    if (n >= -nextra) {  /* 'n' is negative */
      *pos = ci->func.p - nextra - (n + 1);
      return "(vararg)";  /* generic name for any vararg */
    }
  }
  return NULL;  /* no such vararg */
}


const char *lumG_findlocal (lum_State *L, CallInfo *ci, int n, StkId *pos) {
  StkId base = ci->func.p + 1;
  const char *name = NULL;
  if (isLum(ci)) {
    if (n < 0)  /* access to vararg values? */
      return findvararg(ci, n, pos);
    else
      name = lumF_getlocalname(ci_func(ci)->p, n, currentpc(ci));
  }
  if (name == NULL) {  /* no 'standard' name? */
    StkId limit = (ci == L->ci) ? L->top.p : ci->next->func.p;
    if (limit - base >= n && n > 0) {  /* is 'n' inside 'ci' stack? */
      /* generic name for any valid slot */
      name = isLum(ci) ? "(temporary)" : "(C temporary)";
    }
    else
      return NULL;  /* no name */
  }
  if (pos)
    *pos = base + (n - 1);
  return name;
}


LUM_API const char *lum_getlocal (lum_State *L, const lum_Debug *ar, int n) {
  const char *name;
  lum_lock(L);
  if (ar == NULL) {  /* information about non-active function? */
    if (!isLfunction(s2v(L->top.p - 1)))  /* not a Lum function? */
      name = NULL;
    else  /* consider live variables at function start (parameters) */
      name = lumF_getlocalname(clLvalue(s2v(L->top.p - 1))->p, n, 0);
  }
  else {  /* active function; get information through 'ar' */
    StkId pos = NULL;  /* to avoid warnings */
    name = lumG_findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      setobjs2s(L, L->top.p, pos);
      api_incr_top(L);
    }
  }
  lum_unlock(L);
  return name;
}


LUM_API const char *lum_setlocal (lum_State *L, const lum_Debug *ar, int n) {
  StkId pos = NULL;  /* to avoid warnings */
  const char *name;
  lum_lock(L);
  name = lumG_findlocal(L, ar->i_ci, n, &pos);
  if (name) {
    api_checkpop(L, 1);
    setobjs2s(L, pos, L->top.p - 1);
    L->top.p--;  /* pop value */
  }
  lum_unlock(L);
  return name;
}


static void funcinfo (lum_Debug *ar, Closure *cl) {
  if (!LumClosure(cl)) {
    ar->source = "=[C]";
    ar->srclen = LL("=[C]");
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    const Proto *p = cl->l.p;
    if (p->source) {
      ar->source = getlstr(p->source, ar->srclen);
    }
    else {
      ar->source = "=?";
      ar->srclen = LL("=?");
    }
    ar->linedefined = p->linedefined;
    ar->lastlinedefined = p->lastlinedefined;
    ar->what = (ar->linedefined == 0) ? "main" : "Lum";
  }
  lumO_chunkid(ar->short_src, ar->source, ar->srclen);
}


static int nextline (const Proto *p, int currentline, int pc) {
  if (p->lineinfo[pc] != ABSLINEINFO)
    return currentline + p->lineinfo[pc];
  else
    return lumG_getfuncline(p, pc);
}


static void collectvalidlines (lum_State *L, Closure *f) {
  if (!LumClosure(f)) {
    setnilvalue(s2v(L->top.p));
    api_incr_top(L);
  }
  else {
    const Proto *p = f->l.p;
    int currentline = p->linedefined;
    Table *t = lumH_new(L);  /* new table to store active lines */
    sethvalue2s(L, L->top.p, t);  /* push it on stack */
    api_incr_top(L);
    if (p->lineinfo != NULL) {  /* proto with debug information? */
      int i;
      TValue v;
      setbtvalue(&v);  /* boolean 'true' to be the value of all indices */
      if (!(p->flag & PF_ISVARARG))  /* regular function? */
        i = 0;  /* consider all instructions */
      else {  /* vararg function */
        lum_assert(GET_OPCODE(p->code[0]) == OP_VARARGPREP);
        currentline = nextline(p, currentline, 0);
        i = 1;  /* skip first instruction (OP_VARARGPREP) */
      }
      for (; i < p->sizelineinfo; i++) {  /* for each instruction */
        currentline = nextline(p, currentline, i);  /* get its line */
        lumH_setint(L, t, currentline, &v);  /* table[line] = true */
      }
    }
  }
}


static const char *getfuncname (lum_State *L, CallInfo *ci, const char **name) {
  /* calling function is a known function? */
  if (ci != NULL && !(ci->callstatus & CIST_TAIL))
    return funcnamefromcall(L, ci->previous, name);
  else return NULL;  /* no way to find a name */
}


static int auxgetinfo (lum_State *L, const char *what, lum_Debug *ar,
                       Closure *f, CallInfo *ci) {
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (ci && isLum(ci)) ? getcurrentline(ci) : -1;
        break;
      }
      case 'u': {
        ar->nups = (f == NULL) ? 0 : f->c.nupvalues;
        if (!LumClosure(f)) {
          ar->isvararg = 1;
          ar->nparams = 0;
        }
        else {
          ar->isvararg = (f->l.p->flag & PF_ISVARARG) ? 1 : 0;
          ar->nparams = f->l.p->numparams;
        }
        break;
      }
      case 't': {
        if (ci != NULL) {
          ar->istailcall = !!(ci->callstatus & CIST_TAIL);
          ar->extraargs =
                   cast_uchar((ci->callstatus & MAX_CCMT) >> CIST_CCMT);
        }
        else {
          ar->istailcall = 0;
          ar->extraargs = 0;
        }
        break;
      }
      case 'n': {
        ar->namewhat = getfuncname(L, ci, &ar->name);
        if (ar->namewhat == NULL) {
          ar->namewhat = "";  /* not found */
          ar->name = NULL;
        }
        break;
      }
      case 'r': {
        if (ci == NULL || !(ci->callstatus & CIST_HOOKED))
          ar->ftransfer = ar->ntransfer = 0;
        else {
          ar->ftransfer = L->transferinfo.ftransfer;
          ar->ntransfer = L->transferinfo.ntransfer;
        }
        break;
      }
      case 'L':
      case 'f':  /* handled by lum_getinfo */
        break;
      default: status = 0;  /* invalid option */
    }
  }
  return status;
}


LUM_API int lum_getinfo (lum_State *L, const char *what, lum_Debug *ar) {
  int status;
  Closure *cl;
  CallInfo *ci;
  TValue *func;
  lum_lock(L);
  if (*what == '>') {
    ci = NULL;
    func = s2v(L->top.p - 1);
    api_check(L, ttisfunction(func), "function expected");
    what++;  /* skip the '>' */
    L->top.p--;  /* pop function */
  }
  else {
    ci = ar->i_ci;
    func = s2v(ci->func.p);
    lum_assert(ttisfunction(func));
  }
  cl = ttisclosure(func) ? clvalue(func) : NULL;
  status = auxgetinfo(L, what, ar, cl, ci);
  if (strchr(what, 'f')) {
    setobj2s(L, L->top.p, func);
    api_incr_top(L);
  }
  if (strchr(what, 'L'))
    collectvalidlines(L, cl);
  lum_unlock(L);
  return status;
}


/*
** {======================================================
** Symbolic Execution
** =======================================================
*/


static int filterpc (int pc, int jmptarget) {
  if (pc < jmptarget)  /* is code conditional (inside a jump)? */
    return -1;  /* cannot know who sets that register */
  else return pc;  /* current position sets that register */
}


/*
** Try to find last instruction before 'lastpc' that modified register 'reg'.
*/
static int findsetreg (const Proto *p, int lastpc, int reg) {
  int pc;
  int setreg = -1;  /* keep last instruction that changed 'reg' */
  int jmptarget = 0;  /* any code before this address is conditional */
  if (testMMMode(GET_OPCODE(p->code[lastpc])))
    lastpc--;  /* previous instruction was not actually executed */
  for (pc = 0; pc < lastpc; pc++) {
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);
    int change;  /* true if current instruction changed 'reg' */
    switch (op) {
      case OP_LOADNIL: {  /* set registers from 'a' to 'a+b' */
        int b = GETARG_B(i);
        change = (a <= reg && reg <= a + b);
        break;
      }
      case OP_TFORCALL: {  /* affect all regs above its base */
        change = (reg >= a + 2);
        break;
      }
      case OP_CALL:
      case OP_TAILCALL: {  /* affect all registers above base */
        change = (reg >= a);
        break;
      }
      case OP_JMP: {  /* doesn't change registers, but changes 'jmptarget' */
        int b = GETARG_sJ(i);
        int dest = pc + 1 + b;
        /* jump does not skip 'lastpc' and is larger than current one? */
        if (dest <= lastpc && dest > jmptarget)
          jmptarget = dest;  /* update 'jmptarget' */
        change = 0;
        break;
      }
      default:  /* any instruction that sets A */
        change = (testAMode(op) && reg == a);
        break;
    }
    if (change)
      setreg = filterpc(pc, jmptarget);
  }
  return setreg;
}


/*
** Find a "name" for the constant 'c'.
*/
static const char *kname (const Proto *p, int index, const char **name) {
  TValue *kvalue = &p->k[index];
  if (ttisstring(kvalue)) {
    *name = getstr(tsvalue(kvalue));
    return "constant";
  }
  else {
    *name = "?";
    return NULL;
  }
}


static const char *basicgetobjname (const Proto *p, int *ppc, int reg,
                                    const char **name) {
  int pc = *ppc;
  *name = lumF_getlocalname(p, reg + 1, pc);
  if (*name)  /* is a local? */
    return "local";
  /* else try symbolic execution */
  *ppc = pc = findsetreg(p, pc, reg);
  if (pc != -1) {  /* could find instruction? */
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_MOVE: {
        int b = GETARG_B(i);  /* move from 'b' to 'a' */
        if (b < GETARG_A(i))
          return basicgetobjname(p, ppc, b, name);  /* get name for 'b' */
        break;
      }
      case OP_GETUPVAL: {
        *name = upvalname(p, GETARG_B(i));
        return "upvalue";
      }
      case OP_LOADK: return kname(p, GETARG_Bx(i), name);
      case OP_LOADKX: return kname(p, GETARG_Ax(p->code[pc + 1]), name);
      default: break;
    }
  }
  return NULL;  /* could not find reasonable name */
}


/*
** Find a "name" for the register 'c'.
*/
static void rname (const Proto *p, int pc, int c, const char **name) {
  const char *what = basicgetobjname(p, &pc, c, name); /* search for 'c' */
  if (!(what && *what == 'c'))  /* did not find a constant name? */
    *name = "?";
}


/*
** Check whether table being indexed by instruction 'i' is the
** environment '_ENV'
*/
static const char *isEnv (const Proto *p, int pc, Instruction i, int isup) {
  int t = GETARG_B(i);  /* table index */
  const char *name;  /* name of indexed variable */
  if (isup)  /* is 't' an upvalue? */
    name = upvalname(p, t);
  else  /* 't' is a register */
    basicgetobjname(p, &pc, t, &name);
  return (name && strcmp(name, LUM_ENV) == 0) ? "global" : "field";
}


/*
** Extend 'basicgetobjname' to handle table accesses
*/
static const char *getobjname (const Proto *p, int lastpc, int reg,
                               const char **name) {
  const char *kind = basicgetobjname(p, &lastpc, reg, name);
  if (kind != NULL)
    return kind;
  else if (lastpc != -1) {  /* could find instruction? */
    Instruction i = p->code[lastpc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_GETTABUP: {
        int k = GETARG_C(i);  /* key index */
        kname(p, k, name);
        return isEnv(p, lastpc, i, 1);
      }
      case OP_GETTABLE: {
        int k = GETARG_C(i);  /* key index */
        rname(p, lastpc, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_GETI: {
        *name = "integer index";
        return "field";
      }
      case OP_GETFIELD: {
        int k = GETARG_C(i);  /* key index */
        kname(p, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_SELF: {
        int k = GETARG_C(i);  /* key index */
        kname(p, k, name);
        return "method";
      }
      default: break;  /* go through to return NULL */
    }
  }
  return NULL;  /* could not find reasonable name */
}


/*
** Try to find a name for a function based on the code that called it.
** (Only works when function was called by a Lum function.)
** Returns what the name is (e.g., "for iterator", "method",
** "metamethod") and sets '*name' to point to the name.
*/
static const char *funcnamefromcode (lum_State *L, const Proto *p,
                                     int pc, const char **name) {
  TMS tm = (TMS)0;  /* (initial value avoids warnings) */
  Instruction i = p->code[pc];  /* calling instruction */
  switch (GET_OPCODE(i)) {
    case OP_CALL:
    case OP_TAILCALL:
      return getobjname(p, pc, GETARG_A(i), name);  /* get function name */
    case OP_TFORCALL: {  /* for iterator */
      *name = "for iterator";
       return "for iterator";
    }
    /* other instructions can do calls through metamethods */
    case OP_SELF: case OP_GETTABUP: case OP_GETTABLE:
    case OP_GETI: case OP_GETFIELD:
      tm = TM_INDEX;
      break;
    case OP_SETTABUP: case OP_SETTABLE: case OP_SETI: case OP_SETFIELD:
      tm = TM_NEWINDEX;
      break;
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      tm = cast(TMS, GETARG_C(i));
      break;
    }
    case OP_UNM: tm = TM_UNM; break;
    case OP_BNOT: tm = TM_BNOT; break;
    case OP_LEN: tm = TM_LEN; break;
    case OP_CONCAT: tm = TM_CONCAT; break;
    case OP_EQ: tm = TM_EQ; break;
    /* no cases for OP_EQI and OP_EQK, as they don't call metamethods */
    case OP_LT: case OP_LTI: case OP_GTI: tm = TM_LT; break;
    case OP_LE: case OP_LEI: case OP_GEI: tm = TM_LE; break;
    case OP_CLOSE: case OP_RETURN: tm = TM_CLOSE; break;
    default:
      return NULL;  /* cannot find a reasonable name */
  }
  *name = getshrstr(G(L)->tmname[tm]) + 2;
  return "metamethod";
}


/*
** Try to find a name for a function based on how it was called.
*/
static const char *funcnamefromcall (lum_State *L, CallInfo *ci,
                                                   const char **name) {
  if (ci->callstatus & CIST_HOOKED) {  /* was it called inside a hook? */
    *name = "?";
    return "hook";
  }
  else if (ci->callstatus & CIST_FIN) {  /* was it called as a finalizer? */
    *name = "__gc";
    return "metamethod";  /* report it as such */
  }
  else if (isLum(ci))
    return funcnamefromcode(L, ci_func(ci)->p, currentpc(ci), name);
  else
    return NULL;
}

/* }====================================================== */



/*
** Check whether pointer 'o' points to some value in the stack frame of
** the current function and, if so, returns its index.  Because 'o' may
** not point to a value in this stack, we cannot compare it with the
** region boundaries (undefined behavior in ISO C).
*/
static int instack (CallInfo *ci, const TValue *o) {
  int pos;
  StkId base = ci->func.p + 1;
  for (pos = 0; base + pos < ci->top.p; pos++) {
    if (o == s2v(base + pos))
      return pos;
  }
  return -1;  /* not found */
}


/*
** Checks whether value 'o' came from an upvalue. (That can only happen
** with instructions OP_GETTABUP/OP_SETTABUP, which operate directly on
** upvalues.)
*/
static const char *getupvalname (CallInfo *ci, const TValue *o,
                                 const char **name) {
  LClosure *c = ci_func(ci);
  int i;
  for (i = 0; i < c->nupvalues; i++) {
    if (c->upvals[i]->v.p == o) {
      *name = upvalname(c->p, i);
      return "upvalue";
    }
  }
  return NULL;
}


static const char *formatvarinfo (lum_State *L, const char *kind,
                                                const char *name) {
  if (kind == NULL)
    return "";  /* no information */
  else
    return lumO_pushfstring(L, " (%s '%s')", kind, name);
}

/*
** Build a string with a "description" for the value 'o', such as
** "variable 'x'" or "upvalue 'y'".
*/
static const char *varinfo (lum_State *L, const TValue *o) {
  CallInfo *ci = L->ci;
  const char *name = NULL;  /* to avoid warnings */
  const char *kind = NULL;
  if (isLum(ci)) {
    kind = getupvalname(ci, o, &name);  /* check whether 'o' is an upvalue */
    if (!kind) {  /* not an upvalue? */
      int reg = instack(ci, o);  /* try a register */
      if (reg >= 0)  /* is 'o' a register? */
        kind = getobjname(ci_func(ci)->p, currentpc(ci), reg, &name);
    }
  }
  return formatvarinfo(L, kind, name);
}


/*
** Raise a type error
*/
static l_noret typeerror (lum_State *L, const TValue *o, const char *op,
                          const char *extra) {
  const char *t = lumT_objtypename(L, o);
  lumG_runerror(L, "attempt to %s a %s value%s", op, t, extra);
}


/*
** Raise a type error with "standard" information about the faulty
** object 'o' (using 'varinfo').
*/
l_noret lumG_typeerror (lum_State *L, const TValue *o, const char *op) {
  typeerror(L, o, op, varinfo(L, o));
}


/*
** Raise an error for calling a non-callable object. Try to find a name
** for the object based on how it was called ('funcnamefromcall'); if it
** cannot get a name there, try 'varinfo'.
*/
l_noret lumG_callerror (lum_State *L, const TValue *o) {
  CallInfo *ci = L->ci;
  const char *name = NULL;  /* to avoid warnings */
  const char *kind = funcnamefromcall(L, ci, &name);
  const char *extra = kind ? formatvarinfo(L, kind, name) : varinfo(L, o);
  typeerror(L, o, "call", extra);
}


l_noret lumG_forerror (lum_State *L, const TValue *o, const char *what) {
  lumG_runerror(L, "bad 'for' %s (number expected, got %s)",
                   what, lumT_objtypename(L, o));
}


l_noret lumG_concaterror (lum_State *L, const TValue *p1, const TValue *p2) {
  if (ttisstring(p1) || cvt2str(p1)) p1 = p2;
  lumG_typeerror(L, p1, "concatenate");
}


l_noret lumG_opinterror (lum_State *L, const TValue *p1,
                         const TValue *p2, const char *msg) {
  if (!ttisnumber(p1))  /* first operand is wrong? */
    p2 = p1;  /* now second is wrong */
  lumG_typeerror(L, p2, msg);
}


/*
** Error when both values are convertible to numbers, but not to integers
*/
l_noret lumG_tointerror (lum_State *L, const TValue *p1, const TValue *p2) {
  lum_Integer temp;
  if (!lumV_tointegerns(p1, &temp, LUM_FLOORN2I))
    p2 = p1;
  lumG_runerror(L, "number%s has no integer representation", varinfo(L, p2));
}


l_noret lumG_ordererror (lum_State *L, const TValue *p1, const TValue *p2) {
  const char *t1 = lumT_objtypename(L, p1);
  const char *t2 = lumT_objtypename(L, p2);
  if (strcmp(t1, t2) == 0)
    lumG_runerror(L, "attempt to compare two %s values", t1);
  else
    lumG_runerror(L, "attempt to compare %s with %s", t1, t2);
}


/* add src:line information to 'msg' */
const char *lumG_addinfo (lum_State *L, const char *msg, TString *src,
                                        int line) {
  char buff[LUM_IDSIZE];
  if (src) {
    size_t idlen;
    const char *id = getlstr(src, idlen);
    lumO_chunkid(buff, id, idlen);
  }
  else {  /* no source available; use "?" instead */
    buff[0] = '?'; buff[1] = '\0';
  }
  return lumO_pushfstring(L, "%s:%d: %s", buff, line, msg);
}


l_noret lumG_errormsg (lum_State *L) {
  if (L->errfunc != 0) {  /* is there an error handling function? */
    StkId errfunc = restorestack(L, L->errfunc);
    lum_assert(ttisfunction(s2v(errfunc)));
    setobjs2s(L, L->top.p, L->top.p - 1);  /* move argument */
    setobjs2s(L, L->top.p - 1, errfunc);  /* push function */
    L->top.p++;  /* assume EXTRA_STACK */
    lumD_callnoyield(L, L->top.p - 2, 1);  /* call it */
  }
  lumD_throw(L, LUM_ERRRUN);
}


l_noret lumG_runerror (lum_State *L, const char *fmt, ...) {
  CallInfo *ci = L->ci;
  const char *msg;
  va_list argp;
  lumC_checkGC(L);  /* error message uses memory */
  va_start(argp, fmt);
  msg = lumO_pushvfstring(L, fmt, argp);  /* format message */
  va_end(argp);
  if (msg == NULL)  /* no memory to format message? */
    lumD_throw(L, LUM_ERRMEM);
  else if (isLum(ci)) {  /* Lum function? */
    /* add source:line information */
    lumG_addinfo(L, msg, ci_func(ci)->p->source, getcurrentline(ci));
    setobjs2s(L, L->top.p - 2, L->top.p - 1);  /* remove 'msg' */
    L->top.p--;
  }
  lumG_errormsg(L);
}


/*
** Check whether new instruction 'newpc' is in a different line from
** previous instruction 'oldpc'. More often than not, 'newpc' is only
** one or a few instructions after 'oldpc' (it must be after, see
** caller), so try to avoid calling 'lumG_getfuncline'. If they are
** too far apart, there is a good chance of a ABSLINEINFO in the way,
** so it goes directly to 'lumG_getfuncline'.
*/
static int changedline (const Proto *p, int oldpc, int newpc) {
  if (p->lineinfo == NULL)  /* no debug information? */
    return 0;
  if (newpc - oldpc < MAXIWTHABS / 2) {  /* not too far apart? */
    int delta = 0;  /* line difference */
    int pc = oldpc;
    for (;;) {
      int lineinfo = p->lineinfo[++pc];
      if (lineinfo == ABSLINEINFO)
        break;  /* cannot compute delta; fall through */
      delta += lineinfo;
      if (pc == newpc)
        return (delta != 0);  /* delta computed successfully */
    }
  }
  /* either instructions are too far apart or there is an absolute line
     info in the way; compute line difference explicitly */
  return (lumG_getfuncline(p, oldpc) != lumG_getfuncline(p, newpc));
}


/*
** Traces Lum calls. If code is running the first instruction of a function,
** and function is not vararg, and it is not coming from an yield,
** calls 'lumD_hookcall'. (Vararg functions will call 'lumD_hookcall'
** after adjusting its variable arguments; otherwise, they could call
** a line/count hook before the call hook. Functions coming from
** an yield already called 'lumD_hookcall' before yielding.)
*/
int lumG_tracecall (lum_State *L) {
  CallInfo *ci = L->ci;
  Proto *p = ci_func(ci)->p;
  ci->u.l.trap = 1;  /* ensure hooks will be checked */
  if (ci->u.l.savedpc == p->code) {  /* first instruction (not resuming)? */
    if (p->flag & PF_ISVARARG)
      return 0;  /* hooks will start at VARARGPREP instruction */
    else if (!(ci->callstatus & CIST_HOOKYIELD))  /* not yielded? */
      lumD_hookcall(L, ci);  /* check 'call' hook */
  }
  return 1;  /* keep 'trap' on */
}


/*
** Traces the execution of a Lum function. Called before the execution
** of each opcode, when debug is on. 'L->oldpc' stores the last
** instruction traced, to detect line changes. When entering a new
** function, 'npci' will be zero and will test as a new line whatever
** the value of 'oldpc'.  Some exceptional conditions may return to
** a function without setting 'oldpc'. In that case, 'oldpc' may be
** invalid; if so, use zero as a valid value. (A wrong but valid 'oldpc'
** at most causes an extra call to a line hook.)
** This function is not "Protected" when called, so it should correct
** 'L->top.p' before calling anything that can run the GC.
*/
int lumG_traceexec (lum_State *L, const Instruction *pc) {
  CallInfo *ci = L->ci;
  lu_byte mask = cast_byte(L->hookmask);
  const Proto *p = ci_func(ci)->p;
  int counthook;
  if (!(mask & (LUM_MASKLINE | LUM_MASKCOUNT))) {  /* no hooks? */
    ci->u.l.trap = 0;  /* don't need to stop again */
    return 0;  /* turn off 'trap' */
  }
  pc++;  /* reference is always next instruction */
  ci->u.l.savedpc = pc;  /* save 'pc' */
  counthook = (mask & LUM_MASKCOUNT) && (--L->hookcount == 0);
  if (counthook)
    resethookcount(L);  /* reset count */
  else if (!(mask & LUM_MASKLINE))
    return 1;  /* no line hook and count != 0; nothing to be done now */
  if (ci->callstatus & CIST_HOOKYIELD) {  /* hook yielded last time? */
    ci->callstatus &= ~CIST_HOOKYIELD;  /* erase mark */
    return 1;  /* do not call hook again (VM yielded, so it did not move) */
  }
  if (!lumP_isIT(*(ci->u.l.savedpc - 1)))  /* top not being used? */
    L->top.p = ci->top.p;  /* correct top */
  if (counthook)
    lumD_hook(L, LUM_HOOKCOUNT, -1, 0, 0);  /* call count hook */
  if (mask & LUM_MASKLINE) {
    /* 'L->oldpc' may be invalid; use zero in this case */
    int oldpc = (L->oldpc < p->sizecode) ? L->oldpc : 0;
    int npci = pcRel(pc, p);
    if (npci <= oldpc ||  /* call hook when jump back (loop), */
        changedline(p, oldpc, npci)) {  /* or when enter new line */
      int newline = lumG_getfuncline(p, npci);
      lumD_hook(L, LUM_HOOKLINE, newline, 0, 0);  /* call line hook */
    }
    L->oldpc = npci;  /* 'pc' of last call to line hook */
  }
  if (L->status == LUM_YIELD) {  /* did hook yield? */
    if (counthook)
      L->hookcount = 1;  /* undo decrement to zero */
    ci->callstatus |= CIST_HOOKYIELD;  /* mark that it yielded */
    lumD_throw(L, LUM_YIELD);
  }
  return 1;  /* keep 'trap' on */
}

