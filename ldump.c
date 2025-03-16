/*
** $Id: ldump.c $
** save precompiled Lum chunks
** See Copyright Notice in lum.h
*/

#define ldump_c
#define LUM_CORE

#include "lprefix.h"


#include <limits.h>
#include <stddef.h>

#include "lum.h"

#include "lapi.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "lundump.h"


typedef struct {
  lum_State *L;
  lum_Writer writer;
  void *data;
  size_t offset;  /* current position relative to beginning of dump */
  int strip;
  int status;
  Table *h;  /* table to track saved strings */
  lum_Integer nstr;  /* counter for counting saved strings */
} DumpState;


/*
** All high-level dumps go through dumpVector; you can change it to
** change the endianness of the result
*/
#define dumpVector(D,v,n)	dumpBlock(D,v,(n)*sizeof((v)[0]))

#define dumpLiteral(D, s)	dumpBlock(D,s,sizeof(s) - sizeof(char))


/*
** Dump the block of memory pointed by 'b' with given 'size'.
** 'b' should not be NULL, except for the last call signaling the end
** of the dump.
*/
static void dumpBlock (DumpState *D, const void *b, size_t size) {
  if (D->status == 0) {  /* do not write anything after an error */
    lum_unlock(D->L);
    D->status = (*D->writer)(D->L, b, size, D->data);
    lum_lock(D->L);
    D->offset += size;
  }
}


/*
** Dump enough zeros to ensure that current position is a multiple of
** 'align'.
*/
static void dumpAlign (DumpState *D, unsigned align) {
  unsigned padding = align - cast_uint(D->offset % align);
  if (padding < align) {  /* padding == align means no padding */
    static lum_Integer paddingContent = 0;
    lum_assert(align <= sizeof(lum_Integer));
    dumpBlock(D, &paddingContent, padding);
  }
  lum_assert(D->offset % align == 0);
}


#define dumpVar(D,x)		dumpVector(D,&x,1)


static void dumpByte (DumpState *D, int y) {
  lu_byte x = (lu_byte)y;
  dumpVar(D, x);
}


/*
** size for 'dumpVarint' buffer: each byte can store up to 7 bits.
** (The "+6" rounds up the division.)
*/
#define DIBS    ((sizeof(size_t) * CHAR_BIT + 6) / 7)

/*
** Dumps an unsigned integer using the MSB Varint encoding
*/
static void dumpVarint (DumpState *D, size_t x) {
  lu_byte buff[DIBS];
  unsigned n = 1;
  buff[DIBS - 1] = x & 0x7f;  /* fill least-significant byte */
  while ((x >>= 7) != 0)  /* fill other bytes in reverse order */
    buff[DIBS - (++n)] = cast_byte((x & 0x7f) | 0x80);
  dumpVector(D, buff + DIBS - n, n);
}


static void dumpSize (DumpState *D, size_t sz) {
  dumpVarint(D, sz);
}

static void dumpInt (DumpState *D, int x) {
  lum_assert(x >= 0);
  dumpVarint(D, cast(size_t, x));
}


static void dumpNumber (DumpState *D, lum_Number x) {
  dumpVar(D, x);
}


static void dumpInteger (DumpState *D, lum_Integer x) {
  dumpVar(D, x);
}


/*
** Dump a String. First dump its "size": size==0 means NULL;
** size==1 is followed by an index and means "reuse saved string with
** that index"; size>=2 is followed by the string contents with real
** size==size-2 and means that string, which will be saved with
** the next available index.
*/
static void dumpString (DumpState *D, TString *ts) {
  if (ts == NULL)
    dumpSize(D, 0);
  else {
    TValue idx;
    int tag = lumH_getstr(D->h, ts, &idx);
    if (!tagisempty(tag)) {  /* string already saved? */
      dumpSize(D, 1);  /* reuse a saved string */
      dumpSize(D, cast_sizet(ivalue(&idx)));  /* index of saved string */
    }
    else {  /* must write and save the string */
      TValue key, value;  /* to save the string in the hash */
      size_t size;
      const char *s = getlstr(ts, size);
      dumpSize(D, size + 2);
      dumpVector(D, s, size + 1);  /* include ending '\0' */
      D->nstr++;  /* one more saved string */
      setsvalue(D->L, &key, ts);  /* the string is the key */
      setivalue(&value, D->nstr);  /* its index is the value */
      lumH_set(D->L, D->h, &key, &value);  /* h[ts] = nstr */
      /* integer value does not need barrier */
    }
  }
}


static void dumpCode (DumpState *D, const Proto *f) {
  dumpInt(D, f->sizecode);
  dumpAlign(D, sizeof(f->code[0]));
  lum_assert(f->code != NULL);
  dumpVector(D, f->code, cast_uint(f->sizecode));
}


static void dumpFunction (DumpState *D, const Proto *f);

static void dumpConstants (DumpState *D, const Proto *f) {
  int i;
  int n = f->sizek;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    const TValue *o = &f->k[i];
    int tt = ttypetag(o);
    dumpByte(D, tt);
    switch (tt) {
      case LUM_VNUMFLT:
        dumpNumber(D, fltvalue(o));
        break;
      case LUM_VNUMINT:
        dumpInteger(D, ivalue(o));
        break;
      case LUM_VSHRSTR:
      case LUM_VLNGSTR:
        dumpString(D, tsvalue(o));
        break;
      default:
        lum_assert(tt == LUM_VNIL || tt == LUM_VFALSE || tt == LUM_VTRUE);
    }
  }
}


static void dumpProtos (DumpState *D, const Proto *f) {
  int i;
  int n = f->sizep;
  dumpInt(D, n);
  for (i = 0; i < n; i++)
    dumpFunction(D, f->p[i]);
}


static void dumpUpvalues (DumpState *D, const Proto *f) {
  int i, n = f->sizeupvalues;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    dumpByte(D, f->upvalues[i].instack);
    dumpByte(D, f->upvalues[i].idx);
    dumpByte(D, f->upvalues[i].kind);
  }
}


static void dumpDebug (DumpState *D, const Proto *f) {
  int i, n;
  n = (D->strip) ? 0 : f->sizelineinfo;
  dumpInt(D, n);
  if (f->lineinfo != NULL)
    dumpVector(D, f->lineinfo, cast_uint(n));
  n = (D->strip) ? 0 : f->sizeabslineinfo;
  dumpInt(D, n);
  if (n > 0) {
    /* 'abslineinfo' is an array of structures of int's */
    dumpAlign(D, sizeof(int));
    dumpVector(D, f->abslineinfo, cast_uint(n));
  }
  n = (D->strip) ? 0 : f->sizelocvars;
  dumpInt(D, n);
  for (i = 0; i < n; i++) {
    dumpString(D, f->locvars[i].varname);
    dumpInt(D, f->locvars[i].startpc);
    dumpInt(D, f->locvars[i].endpc);
  }
  n = (D->strip) ? 0 : f->sizeupvalues;
  dumpInt(D, n);
  for (i = 0; i < n; i++)
    dumpString(D, f->upvalues[i].name);
}


static void dumpFunction (DumpState *D, const Proto *f) {
  dumpInt(D, f->linedefined);
  dumpInt(D, f->lastlinedefined);
  dumpByte(D, f->numparams);
  dumpByte(D, f->flag);
  dumpByte(D, f->maxstacksize);
  dumpCode(D, f);
  dumpConstants(D, f);
  dumpUpvalues(D, f);
  dumpProtos(D, f);
  dumpString(D, D->strip ? NULL : f->source);
  dumpDebug(D, f);
}


static void dumpHeader (DumpState *D) {
  dumpLiteral(D, LUM_SIGNATURE);
  dumpByte(D, LUMC_VERSION);
  dumpByte(D, LUMC_FORMAT);
  dumpLiteral(D, LUMC_DATA);
  dumpByte(D, sizeof(Instruction));
  dumpByte(D, sizeof(lum_Integer));
  dumpByte(D, sizeof(lum_Number));
  dumpInteger(D, LUMC_INT);
  dumpNumber(D, LUMC_NUM);
}


/*
** dump Lum function as precompiled chunk
*/
int lumU_dump (lum_State *L, const Proto *f, lum_Writer w, void *data,
               int strip) {
  DumpState D;
  D.h = lumH_new(L);  /* aux. table to keep strings already dumped */
  sethvalue2s(L, L->top.p, D.h);  /* anchor it */
  L->top.p++;
  D.L = L;
  D.writer = w;
  D.offset = 0;
  D.data = data;
  D.strip = strip;
  D.status = 0;
  D.nstr = 0;
  dumpHeader(&D);
  dumpByte(&D, f->sizeupvalues);
  dumpFunction(&D, f);
  dumpBlock(&D, NULL, 0);  /* signal end of dump */
  return D.status;
}

