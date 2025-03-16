/*
** $Id: lobject.c $
** Some generic functions over Lum objects
** See Copyright Notice in lum.h
*/

#define lobject_c
#define LUM_CORE

#include "lprefix.h"


#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"


/*
** Computes ceil(log2(x))
*/
lu_byte lumO_ceillog2 (unsigned int x) {
  static const lu_byte log_2[256] = {  /* log_2[i - 1] = ceil(log2(i)) */
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return cast_byte(l + log_2[x]);
}

/*
** Encodes 'p'% as a floating-point byte, represented as (eeeexxxx).
** The exponent is represented using excess-7. Mimicking IEEE 754, the
** representation normalizes the number when possible, assuming an extra
** 1 before the mantissa (xxxx) and adding one to the exponent (eeee)
** to signal that. So, the real value is (1xxxx) * 2^(eeee - 7 - 1) if
** eeee != 0, and (xxxx) * 2^-7 otherwise (subnormal numbers).
*/
lu_byte lumO_codeparam (unsigned int p) {
  if (p >= (cast(lu_mem, 0x1F) << (0xF - 7 - 1)) * 100u)  /* overflow? */
    return 0xFF;  /* return maximum value */
  else {
    p = (cast(l_uint32, p) * 128 + 99) / 100;  /* round up the division */
    if (p < 0x10) {  /* subnormal number? */
      /* exponent bits are already zero; nothing else to do */
      return cast_byte(p);
    }
    else {  /* p >= 0x10 implies ceil(log2(p + 1)) >= 5 */
      /* preserve 5 bits in 'p' */
      unsigned log = lumO_ceillog2(p + 1) - 5u;
      return cast_byte(((p >> log) - 0x10) | ((log + 1) << 4));
    }
  }
}


/*
** Computes 'p' times 'x', where 'p' is a floating-point byte. Roughly,
** we have to multiply 'x' by the mantissa and then shift accordingly to
** the exponent.  If the exponent is positive, both the multiplication
** and the shift increase 'x', so we have to care only about overflows.
** For negative exponents, however, multiplying before the shift keeps
** more significant bits, as long as the multiplication does not
** overflow, so we check which order is best.
*/
l_mem lumO_applyparam (lu_byte p, l_mem x) {
  unsigned int m = p & 0xF;  /* mantissa */
  int e = (p >> 4);  /* exponent */
  if (e > 0) {  /* normalized? */
    e--;  /* correct exponent */
    m += 0x10;  /* correct mantissa; maximum value is 0x1F */
  }
  e -= 7;  /* correct excess-7 */
  if (e >= 0) {
    if (x < (MAX_LMEM / 0x1F) >> e)  /* no overflow? */
      return (x * m) << e;  /* order doesn't matter here */
    else  /* real overflow */
      return MAX_LMEM;
  }
  else {  /* negative exponent */
    e = -e;
    if (x < MAX_LMEM / 0x1F)  /* multiplication cannot overflow? */
      return (x * m) >> e;  /* multiplying first gives more precision */
    else if ((x >> e) <  MAX_LMEM / 0x1F)  /* cannot overflow after shift? */
      return (x >> e) * m;
    else  /* real overflow */
      return MAX_LMEM;
  }
}


static lum_Integer intarith (lum_State *L, int op, lum_Integer v1,
                                                   lum_Integer v2) {
  switch (op) {
    case LUM_OPADD: return intop(+, v1, v2);
    case LUM_OPSUB:return intop(-, v1, v2);
    case LUM_OPMUL:return intop(*, v1, v2);
    case LUM_OPMOD: return lumV_mod(L, v1, v2);
    case LUM_OPIDIV: return lumV_idiv(L, v1, v2);
    case LUM_OPBAND: return intop(&, v1, v2);
    case LUM_OPBOR: return intop(|, v1, v2);
    case LUM_OPBXOR: return intop(^, v1, v2);
    case LUM_OPSHL: return lumV_shiftl(v1, v2);
    case LUM_OPSHR: return lumV_shiftr(v1, v2);
    case LUM_OPUNM: return intop(-, 0, v1);
    case LUM_OPBNOT: return intop(^, ~l_castS2U(0), v1);
    default: lum_assert(0); return 0;
  }
}


static lum_Number numarith (lum_State *L, int op, lum_Number v1,
                                                  lum_Number v2) {
  switch (op) {
    case LUM_OPADD: return lumi_numadd(L, v1, v2);
    case LUM_OPSUB: return lumi_numsub(L, v1, v2);
    case LUM_OPMUL: return lumi_nummul(L, v1, v2);
    case LUM_OPDIV: return lumi_numdiv(L, v1, v2);
    case LUM_OPPOW: return lumi_numpow(L, v1, v2);
    case LUM_OPIDIV: return lumi_numidiv(L, v1, v2);
    case LUM_OPUNM: return lumi_numunm(L, v1);
    case LUM_OPMOD: return lumV_modf(L, v1, v2);
    default: lum_assert(0); return 0;
  }
}


int lumO_rawarith (lum_State *L, int op, const TValue *p1, const TValue *p2,
                   TValue *res) {
  switch (op) {
    case LUM_OPBAND: case LUM_OPBOR: case LUM_OPBXOR:
    case LUM_OPSHL: case LUM_OPSHR:
    case LUM_OPBNOT: {  /* operate only on integers */
      lum_Integer i1; lum_Integer i2;
      if (tointegerns(p1, &i1) && tointegerns(p2, &i2)) {
        setivalue(res, intarith(L, op, i1, i2));
        return 1;
      }
      else return 0;  /* fail */
    }
    case LUM_OPDIV: case LUM_OPPOW: {  /* operate only on floats */
      lum_Number n1; lum_Number n2;
      if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;  /* fail */
    }
    default: {  /* other operations */
      lum_Number n1; lum_Number n2;
      if (ttisinteger(p1) && ttisinteger(p2)) {
        setivalue(res, intarith(L, op, ivalue(p1), ivalue(p2)));
        return 1;
      }
      else if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;  /* fail */
    }
  }
}


void lumO_arith (lum_State *L, int op, const TValue *p1, const TValue *p2,
                 StkId res) {
  if (!lumO_rawarith(L, op, p1, p2, s2v(res))) {
    /* could not perform raw operation; try metamethod */
    lumT_trybinTM(L, p1, p2, res, cast(TMS, (op - LUM_OPADD) + TM_ADD));
  }
}


lu_byte lumO_hexavalue (int c) {
  lum_assert(lisxdigit(c));
  if (lisdigit(c)) return cast_byte(c - '0');
  else return cast_byte((ltolower(c) - 'a') + 10);
}


static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}



/*
** {==================================================================
** Lum's implementation for 'lum_strx2number'
** ===================================================================
*/

#if !defined(lum_strx2number)

/* maximum number of significant digits to read (to avoid overflows
   even with single floats) */
#define MAXSIGDIG	30

/*
** convert a hexadecimal numeric string to a number, following
** C99 specification for 'strtod'
*/
static lum_Number lum_strx2number (const char *s, char **endptr) {
  int dot = lum_getlocaledecpoint();
  lum_Number r = l_mathop(0.0);  /* result (accumulator) */
  int sigdig = 0;  /* number of significant digits */
  int nosigdig = 0;  /* number of non-significant digits */
  int e = 0;  /* exponent correction */
  int neg;  /* 1 if number is negative */
  int hasdot = 0;  /* true after seen a dot */
  *endptr = cast_charp(s);  /* nothing is valid yet */
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces */
  neg = isneg(&s);  /* check sign */
  if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))  /* check '0x' */
    return l_mathop(0.0);  /* invalid format (no '0x') */
  for (s += 2; ; s++) {  /* skip '0x' and read numeral */
    if (*s == dot) {
      if (hasdot) break;  /* second dot? stop loop */
      else hasdot = 1;
    }
    else if (lisxdigit(cast_uchar(*s))) {
      if (sigdig == 0 && *s == '0')  /* non-significant digit (zero)? */
        nosigdig++;
      else if (++sigdig <= MAXSIGDIG)  /* can read it without overflow? */
          r = (r * l_mathop(16.0)) + lumO_hexavalue(*s);
      else e++;  /* too many digits; ignore, but still count for exponent */
      if (hasdot) e--;  /* decimal digit? correct exponent */
    }
    else break;  /* neither a dot nor a digit */
  }
  if (nosigdig + sigdig == 0)  /* no digits? */
    return l_mathop(0.0);  /* invalid format */
  *endptr = cast_charp(s);  /* valid up to here */
  e *= 4;  /* each digit multiplies/divides value by 2^4 */
  if (*s == 'p' || *s == 'P') {  /* exponent part? */
    int exp1 = 0;  /* exponent value */
    int neg1;  /* exponent sign */
    s++;  /* skip 'p' */
    neg1 = isneg(&s);  /* sign */
    if (!lisdigit(cast_uchar(*s)))
      return l_mathop(0.0);  /* invalid; must have at least one digit */
    while (lisdigit(cast_uchar(*s)))  /* read exponent */
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    e += exp1;
    *endptr = cast_charp(s);  /* valid up to here */
  }
  if (neg) r = -r;
  return l_mathop(ldexp)(r, e);
}

#endif
/* }====================================================== */


/* maximum length of a numeral to be converted to a number */
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM	200
#endif

/*
** Convert string 's' to a Lum number (put in 'result'). Return NULL on
** fail or the address of the ending '\0' on success. ('mode' == 'x')
** means a hexadecimal numeral.
*/
static const char *l_str2dloc (const char *s, lum_Number *result, int mode) {
  char *endptr;
  *result = (mode == 'x') ? lum_strx2number(s, &endptr)  /* try to convert */
                          : lum_str2number(s, &endptr);
  if (endptr == s) return NULL;  /* nothing recognized? */
  while (lisspace(cast_uchar(*endptr))) endptr++;  /* skip trailing spaces */
  return (*endptr == '\0') ? endptr : NULL;  /* OK iff no trailing chars */
}


/*
** Convert string 's' to a Lum number (put in 'result') handling the
** current locale.
** This function accepts both the current locale or a dot as the radix
** mark. If the conversion fails, it may mean number has a dot but
** locale accepts something else. In that case, the code copies 's'
** to a buffer (because 's' is read-only), changes the dot to the
** current locale radix mark, and tries to convert again.
** The variable 'mode' checks for special characters in the string:
** - 'n' means 'inf' or 'nan' (which should be rejected)
** - 'x' means a hexadecimal numeral
** - '.' just optimizes the search for the common case (no special chars)
*/
static const char *l_str2d (const char *s, lum_Number *result) {
  const char *endptr;
  const char *pmode = strpbrk(s, ".xXnN");  /* look for special chars */
  int mode = pmode ? ltolower(cast_uchar(*pmode)) : 0;
  if (mode == 'n')  /* reject 'inf' and 'nan' */
    return NULL;
  endptr = l_str2dloc(s, result, mode);  /* try to convert */
  if (endptr == NULL) {  /* failed? may be a different locale */
    char buff[L_MAXLENNUM + 1];
    const char *pdot = strchr(s, '.');
    if (pdot == NULL || strlen(s) > L_MAXLENNUM)
      return NULL;  /* string too long or no dot; fail */
    strcpy(buff, s);  /* copy string to buffer */
    buff[pdot - s] = lum_getlocaledecpoint();  /* correct decimal point */
    endptr = l_str2dloc(buff, result, mode);  /* try again */
    if (endptr != NULL)
      endptr = s + (endptr - buff);  /* make relative to 's' */
  }
  return endptr;
}


#define MAXBY10		cast(lum_Unsigned, LUM_MAXINTEGER / 10)
#define MAXLASTD	cast_int(LUM_MAXINTEGER % 10)

static const char *l_str2int (const char *s, lum_Integer *result) {
  lum_Unsigned a = 0;
  int empty = 1;
  int neg;
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces */
  neg = isneg(&s);
  if (s[0] == '0' &&
      (s[1] == 'x' || s[1] == 'X')) {  /* hex? */
    s += 2;  /* skip '0x' */
    for (; lisxdigit(cast_uchar(*s)); s++) {
      a = a * 16 + lumO_hexavalue(*s);
      empty = 0;
    }
  }
  else {  /* decimal */
    for (; lisdigit(cast_uchar(*s)); s++) {
      int d = *s - '0';
      if (a >= MAXBY10 && (a > MAXBY10 || d > MAXLASTD + neg))  /* overflow? */
        return NULL;  /* do not accept it (as integer) */
      a = a * 10 + cast_uint(d);
      empty = 0;
    }
  }
  while (lisspace(cast_uchar(*s))) s++;  /* skip trailing spaces */
  if (empty || *s != '\0') return NULL;  /* something wrong in the numeral */
  else {
    *result = l_castU2S((neg) ? 0u - a : a);
    return s;
  }
}


size_t lumO_str2num (const char *s, TValue *o) {
  lum_Integer i; lum_Number n;
  const char *e;
  if ((e = l_str2int(s, &i)) != NULL) {  /* try as an integer */
    setivalue(o, i);
  }
  else if ((e = l_str2d(s, &n)) != NULL) {  /* else try as a float */
    setfltvalue(o, n);
  }
  else
    return 0;  /* conversion failed */
  return ct_diff2sz(e - s) + 1;  /* success; return string size */
}


int lumO_utf8esc (char *buff, unsigned long x) {
  int n = 1;  /* number of bytes put in buffer (backwards) */
  lum_assert(x <= 0x7FFFFFFFu);
  if (x < 0x80)  /* ascii? */
    buff[UTF8BUFFSZ - 1] = cast_char(x);
  else {  /* need continuation bytes */
    unsigned int mfb = 0x3f;  /* maximum that fits in first byte */
    do {  /* add continuation bytes */
      buff[UTF8BUFFSZ - (n++)] = cast_char(0x80 | (x & 0x3f));
      x >>= 6;  /* remove added bits */
      mfb >>= 1;  /* now there is one less bit available in first byte */
    } while (x > mfb);  /* still needs continuation byte? */
    buff[UTF8BUFFSZ - n] = cast_char((~mfb << 1) | x);  /* add first byte */
  }
  return n;
}


/*
** The size of the buffer for the conversion of a number to a string
** 'LUM_N2SBUFFSZ' must be enough to accommodate both LUM_INTEGER_FMT
** and LUM_NUMBER_FMT.  For a long long int, this is 19 digits plus a
** sign and a final '\0', adding to 21. For a long double, it can go to
** a sign, the dot, an exponent letter, an exponent sign, 4 exponent
** digits, the final '\0', plus the significant digits, which are
** approximately the *_DIG attribute.
*/
#if LUM_N2SBUFFSZ < (20 + l_floatatt(DIG))
#error "invalid value for LUM_N2SBUFFSZ"
#endif


/*
** Convert a float to a string, adding it to a buffer. First try with
** a not too large number of digits, to avoid noise (for instance,
** 1.1 going to "1.1000000000000001"). If that lose precision, so
** that reading the result back gives a different number, then do the
** conversion again with extra precision. Moreover, if the numeral looks
** like an integer (without a decimal point or an exponent), add ".0" to
** its end.
*/
static int tostringbuffFloat (lum_Number n, char *buff) {
  /* first conversion */
  int len = l_sprintf(buff, LUM_N2SBUFFSZ, LUM_NUMBER_FMT,
                            (LUMI_UACNUMBER)n);
  lum_Number check = lum_str2number(buff, NULL);  /* read it back */
  if (check != n) {  /* not enough precision? */
    /* convert again with more precision */
    len = l_sprintf(buff, LUM_N2SBUFFSZ, LUM_NUMBER_FMT_N,
                          (LUMI_UACNUMBER)n);
  }
  /* looks like an integer? */
  if (buff[strspn(buff, "-0123456789")] == '\0') {
    buff[len++] = lum_getlocaledecpoint();
    buff[len++] = '0';  /* adds '.0' to result */
  }
  return len;
}


/*
** Convert a number object to a string, adding it to a buffer.
*/
unsigned lumO_tostringbuff (const TValue *obj, char *buff) {
  int len;
  lum_assert(ttisnumber(obj));
  if (ttisinteger(obj))
    len = lum_integer2str(buff, LUM_N2SBUFFSZ, ivalue(obj));
  else
    len = tostringbuffFloat(fltvalue(obj), buff);
  lum_assert(len < LUM_N2SBUFFSZ);
  return cast_uint(len);
}


/*
** Convert a number object to a Lum string, replacing the value at 'obj'
*/
void lumO_tostring (lum_State *L, TValue *obj) {
  char buff[LUM_N2SBUFFSZ];
  unsigned len = lumO_tostringbuff(obj, buff);
  setsvalue(L, obj, lumS_newlstr(L, buff, len));
}




/*
** {==================================================================
** 'lumO_pushvfstring'
** ===================================================================
*/

/*
** Size for buffer space used by 'lumO_pushvfstring'. It should be
** (LUM_IDSIZE + LUM_N2SBUFFSZ) + a minimal space for basic messages,
** so that 'lumG_addinfo' can work directly on the static buffer.
*/
#define BUFVFS		cast_uint(LUM_IDSIZE + LUM_N2SBUFFSZ + 95)

/*
** Buffer used by 'lumO_pushvfstring'. 'err' signals an error while
** building result (memory error [1] or buffer overflow [2]).
*/
typedef struct BuffFS {
  lum_State *L;
  char *b;
  size_t buffsize;
  size_t blen;  /* length of string in 'buff' */
  int err;
  char space[BUFVFS];  /* initial buffer */
} BuffFS;


static void initbuff (lum_State *L, BuffFS *buff) {
  buff->L = L;
  buff->b = buff->space;
  buff->buffsize = sizeof(buff->space);
  buff->blen = 0;
  buff->err = 0;
}


/*
** Push final result from 'lumO_pushvfstring'. This function may raise
** errors explicitly or through memory errors, so it must run protected.
*/
static void pushbuff (lum_State *L, void *ud) {
  BuffFS *buff = cast(BuffFS*, ud);
  switch (buff->err) {
    case 1:  /* memory error */
      lumD_throw(L, LUM_ERRMEM);
      break;
    case 2:  /* length overflow: Add "..." at the end of result */
      if (buff->buffsize - buff->blen < 3)
        strcpy(buff->b + buff->blen - 3, "...");  /* 'blen' must be > 3 */
      else {  /* there is enough space left for the "..." */
        strcpy(buff->b + buff->blen, "...");
        buff->blen += 3;
      }
      /* FALLTHROUGH */
    default: {  /* no errors, but it can raise one creating the new string */
      TString *ts = lumS_newlstr(L, buff->b, buff->blen);
      setsvalue2s(L, L->top.p, ts);
      L->top.p++;
    }
  }
}


static const char *clearbuff (BuffFS *buff) {
  lum_State *L = buff->L;
  const char *res;
  if (lumD_rawrunprotected(L, pushbuff, buff) != LUM_OK)  /* errors? */
    res = NULL;  /* error message is on the top of the stack */
  else
    res = getstr(tsvalue(s2v(L->top.p - 1)));
  if (buff->b != buff->space)  /* using dynamic buffer? */
    lumM_freearray(L, buff->b, buff->buffsize);  /* free it */
  return res;
}


static void addstr2buff (BuffFS *buff, const char *str, size_t slen) {
  size_t left = buff->buffsize - buff->blen;  /* space left in the buffer */
  if (buff->err)  /* do nothing else after an error */
    return;
  if (slen > left) {  /* new string doesn't fit into current buffer? */
    if (slen > ((MAX_SIZE/2) - buff->blen)) {  /* overflow? */
      memcpy(buff->b + buff->blen, str, left);  /* copy what it can */
      buff->blen = buff->buffsize;
      buff->err = 2;  /* doesn't add anything else */
      return;
    }
    else {
      size_t newsize = buff->buffsize + slen;  /* limited to MAX_SIZE/2 */
      char *newb =
        (buff->b == buff->space)  /* still using static space? */
        ? lumM_reallocvector(buff->L, NULL, 0, newsize, char)
        : lumM_reallocvector(buff->L, buff->b, buff->buffsize, newsize,
                                                               char);
      if (newb == NULL) {  /* allocation error? */
        buff->err = 1;  /* signal a memory error */
        return;
      }
      if (buff->b == buff->space)  /* new buffer (not reallocated)? */
        memcpy(newb, buff->b, buff->blen);  /* copy previous content */
      buff->b = newb;  /* set new (larger) buffer... */
      buff->buffsize = newsize;  /* ...and its new size */
    }
  }
  memcpy(buff->b + buff->blen, str, slen);  /* copy new content */
  buff->blen += slen;
}


/*
** Add a numeral to the buffer.
*/
static void addnum2buff (BuffFS *buff, TValue *num) {
  char numbuff[LUM_N2SBUFFSZ];
  unsigned len = lumO_tostringbuff(num, numbuff);
  addstr2buff(buff, numbuff, len);
}


/*
** this function handles only '%d', '%c', '%f', '%p', '%s', and '%%'
   conventional formats, plus Lum-specific '%I' and '%U'
*/
const char *lumO_pushvfstring (lum_State *L, const char *fmt, va_list argp) {
  BuffFS buff;  /* holds last part of the result */
  const char *e;  /* points to next '%' */
  initbuff(L, &buff);
  while ((e = strchr(fmt, '%')) != NULL) {
    addstr2buff(&buff, fmt, ct_diff2sz(e - fmt));  /* add 'fmt' up to '%' */
    switch (*(e + 1)) {  /* conversion specifier */
      case 's': {  /* zero-terminated string */
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        addstr2buff(&buff, s, strlen(s));
        break;
      }
      case 'c': {  /* an 'int' as a character */
        char c = cast_char(va_arg(argp, int));
        addstr2buff(&buff, &c, sizeof(char));
        break;
      }
      case 'd': {  /* an 'int' */
        TValue num;
        setivalue(&num, va_arg(argp, int));
        addnum2buff(&buff, &num);
        break;
      }
      case 'I': {  /* a 'lum_Integer' */
        TValue num;
        setivalue(&num, cast(lum_Integer, va_arg(argp, l_uacInt)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'f': {  /* a 'lum_Number' */
        TValue num;
        setfltvalue(&num, cast_num(va_arg(argp, l_uacNumber)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'p': {  /* a pointer */
        char bf[LUM_N2SBUFFSZ];  /* enough space for '%p' */
        void *p = va_arg(argp, void *);
        int len = lum_pointer2str(bf, LUM_N2SBUFFSZ, p);
        addstr2buff(&buff, bf, cast_uint(len));
        break;
      }
      case 'U': {  /* an 'unsigned long' as a UTF-8 sequence */
        char bf[UTF8BUFFSZ];
        int len = lumO_utf8esc(bf, va_arg(argp, unsigned long));
        addstr2buff(&buff, bf + UTF8BUFFSZ - len, cast_uint(len));
        break;
      }
      case '%': {
        addstr2buff(&buff, "%", 1);
        break;
      }
      default: {
        addstr2buff(&buff, e, 2);  /* keep unknown format in the result */
        break;
      }
    }
    fmt = e + 2;  /* skip '%' and the specifier */
  }
  addstr2buff(&buff, fmt, strlen(fmt));  /* rest of 'fmt' */
  return clearbuff(&buff);  /* empty buffer into a new string */
}


const char *lumO_pushfstring (lum_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = lumO_pushvfstring(L, fmt, argp);
  va_end(argp);
  if (msg == NULL)  /* error? */
    lumD_throw(L, LUM_ERRMEM);
  return msg;
}

/* }================================================================== */


#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

#define addstr(a,b,l)	( memcpy(a,b,(l) * sizeof(char)), a += (l) )

void lumO_chunkid (char *out, const char *source, size_t srclen) {
  size_t bufflen = LUM_IDSIZE;  /* free space in buffer */
  if (*source == '=') {  /* 'literal' source */
    if (srclen <= bufflen)  /* small enough? */
      memcpy(out, source + 1, srclen * sizeof(char));
    else {  /* truncate it */
      addstr(out, source + 1, bufflen - 1);
      *out = '\0';
    }
  }
  else if (*source == '@') {  /* file name */
    if (srclen <= bufflen)  /* small enough? */
      memcpy(out, source + 1, srclen * sizeof(char));
    else {  /* add '...' before rest of name */
      addstr(out, RETS, LL(RETS));
      bufflen -= LL(RETS);
      memcpy(out, source + 1 + srclen - bufflen, bufflen * sizeof(char));
    }
  }
  else {  /* string; format as [string "source"] */
    const char *nl = strchr(source, '\n');  /* find first new line (if any) */
    addstr(out, PRE, LL(PRE));  /* add prefix */
    bufflen -= LL(PRE RETS POS) + 1;  /* save space for prefix+suffix+'\0' */
    if (srclen < bufflen && nl == NULL) {  /* small one-line source? */
      addstr(out, source, srclen);  /* keep it */
    }
    else {
      if (nl != NULL)
        srclen = ct_diff2sz(nl - source);  /* stop at first newline */
      if (srclen > bufflen) srclen = bufflen;
      addstr(out, source, srclen);
      addstr(out, RETS, LL(RETS));
    }
    memcpy(out, POS, (LL(POS) + 1) * sizeof(char));
  }
}

