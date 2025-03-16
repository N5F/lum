/*
** $Id: lutf8lib.c $
** Standard library for UTF-8 manipulation
** See Copyright Notice in lum.h
*/

#define lutf8lib_c
#define LUM_LIB

#include "lprefix.h"


#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


#define MAXUNICODE	0x10FFFFu

#define MAXUTF		0x7FFFFFFFu


#define MSGInvalid	"invalid UTF-8 code"


#define iscont(c)	(((c) & 0xC0) == 0x80)
#define iscontp(p)	iscont(*(p))


/* from strlib */
/* translate a relative string position: negative means back from end */
static lum_Integer u_posrelat (lum_Integer pos, size_t len) {
  if (pos >= 0) return pos;
  else if (0u - (size_t)pos > len) return 0;
  else return (lum_Integer)len + pos + 1;
}


/*
** Decode one UTF-8 sequence, returning NULL if byte sequence is
** invalid.  The array 'limits' stores the minimum value for each
** sequence length, to check for overlong representations. Its first
** entry forces an error for non-ascii bytes with no continuation
** bytes (count == 0).
*/
static const char *utf8_decode (const char *s, l_uint32 *val, int strict) {
  static const l_uint32 limits[] =
        {~(l_uint32)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
  unsigned int c = (unsigned char)s[0];
  l_uint32 res = 0;  /* final result */
  if (c < 0x80)  /* ascii? */
    res = c;
  else {
    int count = 0;  /* to count number of continuation bytes */
    for (; c & 0x40; c <<= 1) {  /* while it needs continuation bytes... */
      unsigned int cc = (unsigned char)s[++count];  /* read next byte */
      if (!iscont(cc))  /* not a continuation byte? */
        return NULL;  /* invalid byte sequence */
      res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
    }
    res |= ((l_uint32)(c & 0x7F) << (count * 5));  /* add first byte */
    if (count > 5 || res > MAXUTF || res < limits[count])
      return NULL;  /* invalid byte sequence */
    s += count;  /* skip continuation bytes read */
  }
  if (strict) {
    /* check for invalid code points; too large or surrogates */
    if (res > MAXUNICODE || (0xD800u <= res && res <= 0xDFFFu))
      return NULL;
  }
  if (val) *val = res;
  return s + 1;  /* +1 to include first byte */
}


/*
** utf8len(s [, i [, j [, lax]]]) --> number of characters that
** start in the range [i,j], or nil + current position if 's' is not
** well formed in that interval
*/
static int utflen (lum_State *L) {
  lum_Integer n = 0;  /* counter for the number of characters */
  size_t len;  /* string length in bytes */
  const char *s = lumL_checklstring(L, 1, &len);
  lum_Integer posi = u_posrelat(lumL_optinteger(L, 2, 1), len);
  lum_Integer posj = u_posrelat(lumL_optinteger(L, 3, -1), len);
  int lax = lum_toboolean(L, 4);
  lumL_argcheck(L, 1 <= posi && --posi <= (lum_Integer)len, 2,
                   "initial position out of bounds");
  lumL_argcheck(L, --posj < (lum_Integer)len, 3,
                   "final position out of bounds");
  while (posi <= posj) {
    const char *s1 = utf8_decode(s + posi, NULL, !lax);
    if (s1 == NULL) {  /* conversion error? */
      lumL_pushfail(L);  /* return fail ... */
      lum_pushinteger(L, posi + 1);  /* ... and current position */
      return 2;
    }
    posi = ct_diff2S(s1 - s);
    n++;
  }
  lum_pushinteger(L, n);
  return 1;
}


/*
** codepoint(s, [i, [j [, lax]]]) -> returns codepoints for all
** characters that start in the range [i,j]
*/
static int codepoint (lum_State *L) {
  size_t len;
  const char *s = lumL_checklstring(L, 1, &len);
  lum_Integer posi = u_posrelat(lumL_optinteger(L, 2, 1), len);
  lum_Integer pose = u_posrelat(lumL_optinteger(L, 3, posi), len);
  int lax = lum_toboolean(L, 4);
  int n;
  const char *se;
  lumL_argcheck(L, posi >= 1, 2, "out of bounds");
  lumL_argcheck(L, pose <= (lum_Integer)len, 3, "out of bounds");
  if (posi > pose) return 0;  /* empty interval; return no values */
  if (pose - posi >= INT_MAX)  /* (lum_Integer -> int) overflow? */
    return lumL_error(L, "string slice too long");
  n = (int)(pose -  posi) + 1;  /* upper bound for number of returns */
  lumL_checkstack(L, n, "string slice too long");
  n = 0;  /* count the number of returns */
  se = s + pose;  /* string end */
  for (s += posi - 1; s < se;) {
    l_uint32 code;
    s = utf8_decode(s, &code, !lax);
    if (s == NULL)
      return lumL_error(L, MSGInvalid);
    lum_pushinteger(L, l_castU2S(code));
    n++;
  }
  return n;
}


static void pushutfchar (lum_State *L, int arg) {
  lum_Unsigned code = (lum_Unsigned)lumL_checkinteger(L, arg);
  lumL_argcheck(L, code <= MAXUTF, arg, "value out of range");
  lum_pushfstring(L, "%U", (long)code);
}


/*
** utfchar(n1, n2, ...)  -> char(n1)..char(n2)...
*/
static int utfchar (lum_State *L) {
  int n = lum_gettop(L);  /* number of arguments */
  if (n == 1)  /* optimize common case of single char */
    pushutfchar(L, 1);
  else {
    int i;
    lumL_Buffer b;
    lumL_buffinit(L, &b);
    for (i = 1; i <= n; i++) {
      pushutfchar(L, i);
      lumL_addvalue(&b);
    }
    lumL_pushresult(&b);
  }
  return 1;
}


/*
** offset(s, n, [i])  -> indices where n-th character counting from
**   position 'i' starts and ends; 0 means character at 'i'.
*/
static int byteoffset (lum_State *L) {
  size_t len;
  const char *s = lumL_checklstring(L, 1, &len);
  lum_Integer n  = lumL_checkinteger(L, 2);
  lum_Integer posi = (n >= 0) ? 1 : cast_st2S(len) + 1;
  posi = u_posrelat(lumL_optinteger(L, 3, posi), len);
  lumL_argcheck(L, 1 <= posi && --posi <= (lum_Integer)len, 3,
                   "position out of bounds");
  if (n == 0) {
    /* find beginning of current byte sequence */
    while (posi > 0 && iscontp(s + posi)) posi--;
  }
  else {
    if (iscontp(s + posi))
      return lumL_error(L, "initial position is a continuation byte");
    if (n < 0) {
      while (n < 0 && posi > 0) {  /* move back */
        do {  /* find beginning of previous character */
          posi--;
        } while (posi > 0 && iscontp(s + posi));
        n++;
      }
    }
    else {
      n--;  /* do not move for 1st character */
      while (n > 0 && posi < (lum_Integer)len) {
        do {  /* find beginning of next character */
          posi++;
        } while (iscontp(s + posi));  /* (cannot pass final '\0') */
        n--;
      }
    }
  }
  if (n != 0) {  /* did not find given character? */
    lumL_pushfail(L);
    return 1;
  }
  lum_pushinteger(L, posi + 1);  /* initial position */
  if ((s[posi] & 0x80) != 0) {  /* multi-byte character? */
    do {
      posi++;
    } while (iscontp(s + posi + 1));  /* skip to final byte */
  }
  /* else one-byte character: final position is the initial one */
  lum_pushinteger(L, posi + 1);  /* 'posi' now is the final position */
  return 2;
}


static int iter_aux (lum_State *L, int strict) {
  size_t len;
  const char *s = lumL_checklstring(L, 1, &len);
  lum_Unsigned n = (lum_Unsigned)lum_tointeger(L, 2);
  if (n < len) {
    while (iscontp(s + n)) n++;  /* go to next character */
  }
  if (n >= len)  /* (also handles original 'n' being negative) */
    return 0;  /* no more codepoints */
  else {
    l_uint32 code;
    const char *next = utf8_decode(s + n, &code, strict);
    if (next == NULL || iscontp(next))
      return lumL_error(L, MSGInvalid);
    lum_pushinteger(L, l_castU2S(n + 1));
    lum_pushinteger(L, l_castU2S(code));
    return 2;
  }
}


static int iter_auxstrict (lum_State *L) {
  return iter_aux(L, 1);
}

static int iter_auxlax (lum_State *L) {
  return iter_aux(L, 0);
}


static int iter_codes (lum_State *L) {
  int lax = lum_toboolean(L, 2);
  const char *s = lumL_checkstring(L, 1);
  lumL_argcheck(L, !iscontp(s), 1, MSGInvalid);
  lum_pushcfunction(L, lax ? iter_auxlax : iter_auxstrict);
  lum_pushvalue(L, 1);
  lum_pushinteger(L, 0);
  return 3;
}


/* pattern to match a single UTF-8 character */
#define UTF8PATT	"[\0-\x7F\xC2-\xFD][\x80-\xBF]*"


static const lumL_Reg funcs[] = {
  {"offset", byteoffset},
  {"codepoint", codepoint},
  {"char", utfchar},
  {"len", utflen},
  {"codes", iter_codes},
  /* placeholders */
  {"charpattern", NULL},
  {NULL, NULL}
};


LUMMOD_API int lumopen_utf8 (lum_State *L) {
  lumL_newlib(L, funcs);
  lum_pushlstring(L, UTF8PATT, sizeof(UTF8PATT)/sizeof(char) - 1);
  lum_setfield(L, -2, "charpattern");
  return 1;
}

