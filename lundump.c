/*
** $Id: lundump.c $
** load precompiled Lum chunks
** See Copyright Notice in lum.h
*/

#define lundump_c
#define LUM_CORE

#include "lprefix.h"


#include <limits.h>
#include <string.h>

#include "lum.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "ltable.h"
#include "lundump.h"
#include "lzio.h"


#if !defined(lumi_verifycode)
#define lumi_verifycode(L,f)  /* empty */
#endif


typedef struct {
  lum_State *L;
  ZIO *Z;
  const char *name;
  Table *h;  /* list for string reuse */
  size_t offset;  /* current position relative to beginning of dump */
  lum_Integer nstr;  /* number of strings in the list */
  lu_byte fixed;  /* dump is fixed in memory */
} LoadState;


static l_noret error (LoadState *S, const char *why) {
  lumO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
  lumD_throw(S->L, LUM_ERRSYNTAX);
}


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
#define loadVector(S,b,n)	loadBlock(S,b,cast_sizet(n)*sizeof((b)[0]))

static void loadBlock (LoadState *S, void *b, size_t size) {
  if (lumZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");
  S->offset += size;
}


static void loadAlign (LoadState *S, unsigned align) {
  unsigned padding = align - cast_uint(S->offset % align);
  if (padding < align) {  /* (padding == align) means no padding */
    lum_Integer paddingContent;
    loadBlock(S, &paddingContent, padding);
    lum_assert(S->offset % align == 0);
  }
}


#define getaddr(S,n,t)	cast(t *, getaddr_(S,cast_sizet(n) * sizeof(t)))

static const void *getaddr_ (LoadState *S, size_t size) {
  const void *block = lumZ_getaddr(S->Z, size);
  S->offset += size;
  if (block == NULL)
    error(S, "truncated fixed buffer");
  return block;
}


#define loadVar(S,x)		loadVector(S,&x,1)


static lu_byte loadByte (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  S->offset++;
  return cast_byte(b);
}


static size_t loadVarint (LoadState *S, size_t limit) {
  size_t x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte(S);
    if (x > limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) != 0);
  return x;
}


static size_t loadSize (LoadState *S) {
  return loadVarint(S, MAX_SIZE);
}


static int loadInt (LoadState *S) {
  return cast_int(loadVarint(S, cast_sizet(INT_MAX)));
}



static lum_Number loadNumber (LoadState *S) {
  lum_Number x;
  loadVar(S, x);
  return x;
}


static lum_Integer loadInteger (LoadState *S) {
  lum_Integer x;
  loadVar(S, x);
  return x;
}


/*
** Load a nullable string into slot 'sl' from prototype 'p'. The
** assignment to the slot and the barrier must be performed before any
** possible GC activity, to anchor the string. (Both 'loadVector' and
** 'lumH_setint' can call the GC.)
*/
static void loadString (LoadState *S, Proto *p, TString **sl) {
  lum_State *L = S->L;
  TString *ts;
  TValue sv;
  size_t size = loadSize(S);
  if (size == 0) {  /* no string? */
    lum_assert(*sl == NULL);  /* must be prefilled */
    return;
  }
  else if (size == 1) {  /* previously saved string? */
    lum_Integer idx = cast(lum_Integer, loadSize(S));  /* get its index */
    TValue stv;
    lumH_getint(S->h, idx, &stv);  /* get its value */
    *sl = ts = tsvalue(&stv);
    lumC_objbarrier(L, p, ts);
    return;  /* do not save it again */
  }
  else if ((size -= 2) <= LUMI_MAXSHORTLEN) {  /* short string? */
    char buff[LUMI_MAXSHORTLEN + 1];  /* extra space for '\0' */
    loadVector(S, buff, size + 1);  /* load string into buffer */
    *sl = ts = lumS_newlstr(L, buff, size);  /* create string */
    lumC_objbarrier(L, p, ts);
  }
  else if (S->fixed) {  /* for a fixed buffer, use a fixed string */
    const char *s = getaddr(S, size + 1, char);  /* get content address */
    *sl = ts = lumS_newextlstr(L, s, size, NULL, NULL);
    lumC_objbarrier(L, p, ts);
  }
  else {  /* create internal copy */
    *sl = ts = lumS_createlngstrobj(L, size);  /* create string */
    lumC_objbarrier(L, p, ts);
    loadVector(S, getlngstr(ts), size + 1);  /* load directly in final place */
  }
  /* add string to list of saved strings */
  S->nstr++;
  setsvalue(L, &sv, ts);
  lumH_setint(L, S->h, S->nstr, &sv);
  lumC_objbarrierback(L, obj2gco(S->h), ts);
}


static void loadCode (LoadState *S, Proto *f) {
  int n = loadInt(S);
  loadAlign(S, sizeof(f->code[0]));
  if (S->fixed) {
    f->code = getaddr(S, n, Instruction);
    f->sizecode = n;
  }
  else {
    f->code = lumM_newvectorchecked(S->L, n, Instruction);
    f->sizecode = n;
    loadVector(S, f->code, n);
  }
}


static void loadFunction(LoadState *S, Proto *f);


static void loadConstants (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->k = lumM_newvectorchecked(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = loadByte(S);
    switch (t) {
      case LUM_VNIL:
        setnilvalue(o);
        break;
      case LUM_VFALSE:
        setbfvalue(o);
        break;
      case LUM_VTRUE:
        setbtvalue(o);
        break;
      case LUM_VNUMFLT:
        setfltvalue(o, loadNumber(S));
        break;
      case LUM_VNUMINT:
        setivalue(o, loadInteger(S));
        break;
      case LUM_VSHRSTR:
      case LUM_VLNGSTR: {
        lum_assert(f->source == NULL);
        loadString(S, f, &f->source);  /* use 'source' to anchor string */
        if (f->source == NULL)
          error(S, "bad format for constant string");
        setsvalue2n(S->L, o, f->source);  /* save it in the right place */
        f->source = NULL;
        break;
      }
      default: lum_assert(0);
    }
  }
}


static void loadProtos (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->p = lumM_newvectorchecked(S->L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = lumF_newproto(S->L);
    lumC_objbarrier(S->L, f, f->p[i]);
    loadFunction(S, f->p[i]);
  }
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
static void loadUpvalues (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->upvalues = lumM_newvectorchecked(S->L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)  /* make array valid for GC */
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {  /* following calls can raise errors */
    f->upvalues[i].instack = loadByte(S);
    f->upvalues[i].idx = loadByte(S);
    f->upvalues[i].kind = loadByte(S);
  }
}


static void loadDebug (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  if (S->fixed) {
    f->lineinfo = getaddr(S, n, ls_byte);
    f->sizelineinfo = n;
  }
  else {
    f->lineinfo = lumM_newvectorchecked(S->L, n, ls_byte);
    f->sizelineinfo = n;
    loadVector(S, f->lineinfo, n);
  }
  n = loadInt(S);
  if (n > 0) {
    loadAlign(S, sizeof(int));
    if (S->fixed) {
      f->abslineinfo = getaddr(S, n, AbsLineInfo);
      f->sizeabslineinfo = n;
    }
    else {
      f->abslineinfo = lumM_newvectorchecked(S->L, n, AbsLineInfo);
      f->sizeabslineinfo = n;
      loadVector(S, f->abslineinfo, n);
    }
  }
  n = loadInt(S);
  f->locvars = lumM_newvectorchecked(S->L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    loadString(S, f, &f->locvars[i].varname);
    f->locvars[i].startpc = loadInt(S);
    f->locvars[i].endpc = loadInt(S);
  }
  n = loadInt(S);
  if (n != 0)  /* does it have debug information? */
    n = f->sizeupvalues;  /* must be this many */
  for (i = 0; i < n; i++)
    loadString(S, f, &f->upvalues[i].name);
}


static void loadFunction (LoadState *S, Proto *f) {
  f->linedefined = loadInt(S);
  f->lastlinedefined = loadInt(S);
  f->numparams = loadByte(S);
  f->flag = loadByte(S) & PF_ISVARARG;  /* get only the meaningful flags */
  if (S->fixed)
    f->flag |= PF_FIXED;  /* signal that code is fixed */
  f->maxstacksize = loadByte(S);
  loadCode(S, f);
  loadConstants(S, f);
  loadUpvalues(S, f);
  loadProtos(S, f);
  loadString(S, f, &f->source);
  loadDebug(S, f);
}


static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(LUM_SIGNATURE) + sizeof(LUMC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  loadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}


static void fchecksize (LoadState *S, size_t size, const char *tname) {
  if (loadByte(S) != size)
    error(S, lumO_pushfstring(S->L, "%s size mismatch", tname));
}


#define checksize(S,t)	fchecksize(S,sizeof(t),#t)

static void checkHeader (LoadState *S) {
  /* skip 1st char (already read and checked) */
  checkliteral(S, &LUM_SIGNATURE[1], "not a binary chunk");
  if (loadByte(S) != LUMC_VERSION)
    error(S, "version mismatch");
  if (loadByte(S) != LUMC_FORMAT)
    error(S, "format mismatch");
  checkliteral(S, LUMC_DATA, "corrupted chunk");
  checksize(S, Instruction);
  checksize(S, lum_Integer);
  checksize(S, lum_Number);
  if (loadInteger(S) != LUMC_INT)
    error(S, "integer format mismatch");
  if (loadNumber(S) != LUMC_NUM)
    error(S, "float format mismatch");
}


/*
** Load precompiled chunk.
*/
LClosure *lumU_undump (lum_State *L, ZIO *Z, const char *name, int fixed) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    S.name = name + 1;
  else if (*name == LUM_SIGNATURE[0])
    S.name = "binary string";
  else
    S.name = name;
  S.L = L;
  S.Z = Z;
  S.fixed = cast_byte(fixed);
  S.offset = 1;  /* fist byte was already read */
  checkHeader(&S);
  cl = lumF_newLclosure(L, loadByte(&S));
  setclLvalue2s(L, L->top.p, cl);
  lumD_inctop(L);
  S.h = lumH_new(L);  /* create list of saved strings */
  S.nstr = 0;
  sethvalue2s(L, L->top.p, S.h);  /* anchor it */
  lumD_inctop(L);
  cl->p = lumF_newproto(L);
  lumC_objbarrier(L, cl, cl->p);
  loadFunction(&S, cl->p);
  lum_assert(cl->nupvalues == cl->p->sizeupvalues);
  lumi_verifycode(L, cl->p);
  L->top.p--;  /* pop table */
  return cl;
}

