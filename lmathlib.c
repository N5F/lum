/*
** $Id: lmathlib.c $
** Standard mathematical library
** See Copyright Notice in lum.h
*/

#define lmathlib_c
#define LUM_LIB

#include "lprefix.h"


#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


#undef PI
#define PI	(l_mathop(3.141592653589793238462643383279502884))


static int math_abs (lum_State *L) {
  if (lum_isinteger(L, 1)) {
    lum_Integer n = lum_tointeger(L, 1);
    if (n < 0) n = (lum_Integer)(0u - (lum_Unsigned)n);
    lum_pushinteger(L, n);
  }
  else
    lum_pushnumber(L, l_mathop(fabs)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_sin (lum_State *L) {
  lum_pushnumber(L, l_mathop(sin)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_cos (lum_State *L) {
  lum_pushnumber(L, l_mathop(cos)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_tan (lum_State *L) {
  lum_pushnumber(L, l_mathop(tan)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_asin (lum_State *L) {
  lum_pushnumber(L, l_mathop(asin)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_acos (lum_State *L) {
  lum_pushnumber(L, l_mathop(acos)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_atan (lum_State *L) {
  lum_Number y = lumL_checknumber(L, 1);
  lum_Number x = lumL_optnumber(L, 2, 1);
  lum_pushnumber(L, l_mathop(atan2)(y, x));
  return 1;
}


static int math_toint (lum_State *L) {
  int valid;
  lum_Integer n = lum_tointegerx(L, 1, &valid);
  if (l_likely(valid))
    lum_pushinteger(L, n);
  else {
    lumL_checkany(L, 1);
    lumL_pushfail(L);  /* value is not convertible to integer */
  }
  return 1;
}


static void pushnumint (lum_State *L, lum_Number d) {
  lum_Integer n;
  if (lum_numbertointeger(d, &n))  /* does 'd' fit in an integer? */
    lum_pushinteger(L, n);  /* result is integer */
  else
    lum_pushnumber(L, d);  /* result is float */
}


static int math_floor (lum_State *L) {
  if (lum_isinteger(L, 1))
    lum_settop(L, 1);  /* integer is its own floor */
  else {
    lum_Number d = l_mathop(floor)(lumL_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}


static int math_ceil (lum_State *L) {
  if (lum_isinteger(L, 1))
    lum_settop(L, 1);  /* integer is its own ceiling */
  else {
    lum_Number d = l_mathop(ceil)(lumL_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}


static int math_fmod (lum_State *L) {
  if (lum_isinteger(L, 1) && lum_isinteger(L, 2)) {
    lum_Integer d = lum_tointeger(L, 2);
    if ((lum_Unsigned)d + 1u <= 1u) {  /* special cases: -1 or 0 */
      lumL_argcheck(L, d != 0, 2, "zero");
      lum_pushinteger(L, 0);  /* avoid overflow with 0x80000... / -1 */
    }
    else
      lum_pushinteger(L, lum_tointeger(L, 1) % d);
  }
  else
    lum_pushnumber(L, l_mathop(fmod)(lumL_checknumber(L, 1),
                                     lumL_checknumber(L, 2)));
  return 1;
}


/*
** next function does not use 'modf', avoiding problems with 'double*'
** (which is not compatible with 'float*') when lum_Number is not
** 'double'.
*/
static int math_modf (lum_State *L) {
  if (lum_isinteger(L ,1)) {
    lum_settop(L, 1);  /* number is its own integer part */
    lum_pushnumber(L, 0);  /* no fractional part */
  }
  else {
    lum_Number n = lumL_checknumber(L, 1);
    /* integer part (rounds toward zero) */
    lum_Number ip = (n < 0) ? l_mathop(ceil)(n) : l_mathop(floor)(n);
    pushnumint(L, ip);
    /* fractional part (test needed for inf/-inf) */
    lum_pushnumber(L, (n == ip) ? l_mathop(0.0) : (n - ip));
  }
  return 2;
}


static int math_sqrt (lum_State *L) {
  lum_pushnumber(L, l_mathop(sqrt)(lumL_checknumber(L, 1)));
  return 1;
}


static int math_ult (lum_State *L) {
  lum_Integer a = lumL_checkinteger(L, 1);
  lum_Integer b = lumL_checkinteger(L, 2);
  lum_pushboolean(L, (lum_Unsigned)a < (lum_Unsigned)b);
  return 1;
}

static int math_log (lum_State *L) {
  lum_Number x = lumL_checknumber(L, 1);
  lum_Number res;
  if (lum_isnoneornil(L, 2))
    res = l_mathop(log)(x);
  else {
    lum_Number base = lumL_checknumber(L, 2);
#if !defined(LUM_USE_C89)
    if (base == l_mathop(2.0))
      res = l_mathop(log2)(x);
    else
#endif
    if (base == l_mathop(10.0))
      res = l_mathop(log10)(x);
    else
      res = l_mathop(log)(x)/l_mathop(log)(base);
  }
  lum_pushnumber(L, res);
  return 1;
}

static int math_exp (lum_State *L) {
  lum_pushnumber(L, l_mathop(exp)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_deg (lum_State *L) {
  lum_pushnumber(L, lumL_checknumber(L, 1) * (l_mathop(180.0) / PI));
  return 1;
}

static int math_rad (lum_State *L) {
  lum_pushnumber(L, lumL_checknumber(L, 1) * (PI / l_mathop(180.0)));
  return 1;
}


static int math_min (lum_State *L) {
  int n = lum_gettop(L);  /* number of arguments */
  int imin = 1;  /* index of current minimum value */
  int i;
  lumL_argcheck(L, n >= 1, 1, "value expected");
  for (i = 2; i <= n; i++) {
    if (lum_compare(L, i, imin, LUM_OPLT))
      imin = i;
  }
  lum_pushvalue(L, imin);
  return 1;
}


static int math_max (lum_State *L) {
  int n = lum_gettop(L);  /* number of arguments */
  int imax = 1;  /* index of current maximum value */
  int i;
  lumL_argcheck(L, n >= 1, 1, "value expected");
  for (i = 2; i <= n; i++) {
    if (lum_compare(L, imax, i, LUM_OPLT))
      imax = i;
  }
  lum_pushvalue(L, imax);
  return 1;
}


static int math_type (lum_State *L) {
  if (lum_type(L, 1) == LUM_TNUMBER)
    lum_pushstring(L, (lum_isinteger(L, 1)) ? "integer" : "float");
  else {
    lumL_checkany(L, 1);
    lumL_pushfail(L);
  }
  return 1;
}



/*
** {==================================================================
** Pseudo-Random Number Generator based on 'xoshiro256**'.
** ===================================================================
*/

/*
** This code uses lots of shifts. ANSI C does not allow shifts greater
** than or equal to the width of the type being shifted, so some shifts
** are written in convoluted ways to match that restriction. For
** preprocessor tests, it assumes a width of 32 bits, so the maximum
** shift there is 31 bits.
*/


/* number of binary digits in the mantissa of a float */
#define FIGS	l_floatatt(MANT_DIG)

#if FIGS > 64
/* there are only 64 random bits; use them all */
#undef FIGS
#define FIGS	64
#endif


/*
** LUM_RAND32 forces the use of 32-bit integers in the implementation
** of the PRN generator (mainly for testing).
*/
#if !defined(LUM_RAND32) && !defined(Rand64)

/* try to find an integer type with at least 64 bits */

#if ((ULONG_MAX >> 31) >> 31) >= 3

/* 'long' has at least 64 bits */
#define Rand64		unsigned long
#define SRand64		long

#elif !defined(LUM_USE_C89) && defined(LLONG_MAX)

/* there is a 'long long' type (which must have at least 64 bits) */
#define Rand64		unsigned long long
#define SRand64		long long

#elif ((LUM_MAXUNSIGNED >> 31) >> 31) >= 3

/* 'lum_Unsigned' has at least 64 bits */
#define Rand64		lum_Unsigned
#define SRand64		lum_Integer

#endif

#endif


#if defined(Rand64)  /* { */

/*
** Standard implementation, using 64-bit integers.
** If 'Rand64' has more than 64 bits, the extra bits do not interfere
** with the 64 initial bits, except in a right shift. Moreover, the
** final result has to discard the extra bits.
*/

/* avoid using extra bits when needed */
#define trim64(x)	((x) & 0xffffffffffffffffu)


/* rotate left 'x' by 'n' bits */
static Rand64 rotl (Rand64 x, int n) {
  return (x << n) | (trim64(x) >> (64 - n));
}

static Rand64 nextrand (Rand64 *state) {
  Rand64 state0 = state[0];
  Rand64 state1 = state[1];
  Rand64 state2 = state[2] ^ state0;
  Rand64 state3 = state[3] ^ state1;
  Rand64 res = rotl(state1 * 5, 7) * 9;
  state[0] = state0 ^ state3;
  state[1] = state1 ^ state2;
  state[2] = state2 ^ (state1 << 17);
  state[3] = rotl(state3, 45);
  return res;
}


/*
** Convert bits from a random integer into a float in the
** interval [0,1), getting the higher FIG bits from the
** random unsigned integer and converting that to a float.
** Some old Microsoft compilers cannot cast an unsigned long
** to a floating-point number, so we use a signed long as an
** intermediary. When lum_Number is float or double, the shift ensures
** that 'sx' is non negative; in that case, a good compiler will remove
** the correction.
*/

/* must throw out the extra (64 - FIGS) bits */
#define shift64_FIG	(64 - FIGS)

/* 2^(-FIGS) == 2^-1 / 2^(FIGS-1) */
#define scaleFIG	(l_mathop(0.5) / ((Rand64)1 << (FIGS - 1)))

static lum_Number I2d (Rand64 x) {
  SRand64 sx = (SRand64)(trim64(x) >> shift64_FIG);
  lum_Number res = (lum_Number)(sx) * scaleFIG;
  if (sx < 0)
    res += l_mathop(1.0);  /* correct the two's complement if negative */
  lum_assert(0 <= res && res < 1);
  return res;
}

/* convert a 'Rand64' to a 'lum_Unsigned' */
#define I2UInt(x)	((lum_Unsigned)trim64(x))

/* convert a 'lum_Unsigned' to a 'Rand64' */
#define Int2I(x)	((Rand64)(x))


#else	/* no 'Rand64'   }{ */

/*
** Use two 32-bit integers to represent a 64-bit quantity.
*/
typedef struct Rand64 {
  l_uint32 h;  /* higher half */
  l_uint32 l;  /* lower half */
} Rand64;


/*
** If 'l_uint32' has more than 32 bits, the extra bits do not interfere
** with the 32 initial bits, except in a right shift and comparisons.
** Moreover, the final result has to discard the extra bits.
*/

/* avoid using extra bits when needed */
#define trim32(x)	((x) & 0xffffffffu)


/*
** basic operations on 'Rand64' values
*/

/* build a new Rand64 value */
static Rand64 packI (l_uint32 h, l_uint32 l) {
  Rand64 result;
  result.h = h;
  result.l = l;
  return result;
}

/* return i << n */
static Rand64 Ishl (Rand64 i, int n) {
  lum_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)), i.l << n);
}

/* i1 ^= i2 */
static void Ixor (Rand64 *i1, Rand64 i2) {
  i1->h ^= i2.h;
  i1->l ^= i2.l;
}

/* return i1 + i2 */
static Rand64 Iadd (Rand64 i1, Rand64 i2) {
  Rand64 result = packI(i1.h + i2.h, i1.l + i2.l);
  if (trim32(result.l) < trim32(i1.l))  /* carry? */
    result.h++;
  return result;
}

/* return i * 5 */
static Rand64 times5 (Rand64 i) {
  return Iadd(Ishl(i, 2), i);  /* i * 5 == (i << 2) + i */
}

/* return i * 9 */
static Rand64 times9 (Rand64 i) {
  return Iadd(Ishl(i, 3), i);  /* i * 9 == (i << 3) + i */
}

/* return 'i' rotated left 'n' bits */
static Rand64 rotl (Rand64 i, int n) {
  lum_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)),
               (trim32(i.h) >> (32 - n)) | (i.l << n));
}

/* for offsets larger than 32, rotate right by 64 - offset */
static Rand64 rotl1 (Rand64 i, int n) {
  lum_assert(n > 32 && n < 64);
  n = 64 - n;
  return packI((trim32(i.h) >> n) | (i.l << (32 - n)),
               (i.h << (32 - n)) | (trim32(i.l) >> n));
}

/*
** implementation of 'xoshiro256**' algorithm on 'Rand64' values
*/
static Rand64 nextrand (Rand64 *state) {
  Rand64 res = times9(rotl(times5(state[1]), 7));
  Rand64 t = Ishl(state[1], 17);
  Ixor(&state[2], state[0]);
  Ixor(&state[3], state[1]);
  Ixor(&state[1], state[2]);
  Ixor(&state[0], state[3]);
  Ixor(&state[2], t);
  state[3] = rotl1(state[3], 45);
  return res;
}


/*
** Converts a 'Rand64' into a float.
*/

/* an unsigned 1 with proper type */
#define UONE		((l_uint32)1)


#if FIGS <= 32

/* 2^(-FIGS) */
#define scaleFIG       (l_mathop(0.5) / (UONE << (FIGS - 1)))

/*
** get up to 32 bits from higher half, shifting right to
** throw out the extra bits.
*/
static lum_Number I2d (Rand64 x) {
  lum_Number h = (lum_Number)(trim32(x.h) >> (32 - FIGS));
  return h * scaleFIG;
}

#else	/* 32 < FIGS <= 64 */

/* 2^(-FIGS) = 1.0 / 2^30 / 2^3 / 2^(FIGS-33) */
#define scaleFIG  \
    (l_mathop(1.0) / (UONE << 30) / l_mathop(8.0) / (UONE << (FIGS - 33)))

/*
** use FIGS - 32 bits from lower half, throwing out the other
** (32 - (FIGS - 32)) = (64 - FIGS) bits
*/
#define shiftLOW	(64 - FIGS)

/*
** higher 32 bits go after those (FIGS - 32) bits: shiftHI = 2^(FIGS - 32)
*/
#define shiftHI		((lum_Number)(UONE << (FIGS - 33)) * l_mathop(2.0))


static lum_Number I2d (Rand64 x) {
  lum_Number h = (lum_Number)trim32(x.h) * shiftHI;
  lum_Number l = (lum_Number)(trim32(x.l) >> shiftLOW);
  return (h + l) * scaleFIG;
}

#endif


/* convert a 'Rand64' to a 'lum_Unsigned' */
static lum_Unsigned I2UInt (Rand64 x) {
  return (((lum_Unsigned)trim32(x.h) << 31) << 1) | (lum_Unsigned)trim32(x.l);
}

/* convert a 'lum_Unsigned' to a 'Rand64' */
static Rand64 Int2I (lum_Unsigned n) {
  return packI((l_uint32)((n >> 31) >> 1), (l_uint32)n);
}

#endif  /* } */


/*
** A state uses four 'Rand64' values.
*/
typedef struct {
  Rand64 s[4];
} RanState;


/*
** Project the random integer 'ran' into the interval [0, n].
** Because 'ran' has 2^B possible values, the projection can only be
** uniform when the size of the interval is a power of 2 (exact
** division). Otherwise, to get a uniform projection into [0, n], we
** first compute 'lim', the smallest Mersenne number not smaller than
** 'n'. We then project 'ran' into the interval [0, lim].  If the result
** is inside [0, n], we are done. Otherwise, we try with another 'ran',
** until we have a result inside the interval.
*/
static lum_Unsigned project (lum_Unsigned ran, lum_Unsigned n,
                             RanState *state) {
  if ((n & (n + 1)) == 0)  /* is 'n + 1' a power of 2? */
    return ran & n;  /* no bias */
  else {
    lum_Unsigned lim = n;
    /* compute the smallest (2^b - 1) not smaller than 'n' */
    lim |= (lim >> 1);
    lim |= (lim >> 2);
    lim |= (lim >> 4);
    lim |= (lim >> 8);
    lim |= (lim >> 16);
#if (LUM_MAXUNSIGNED >> 31) >= 3
    lim |= (lim >> 32);  /* integer type has more than 32 bits */
#endif
    lum_assert((lim & (lim + 1)) == 0  /* 'lim + 1' is a power of 2, */
      && lim >= n  /* not smaller than 'n', */
      && (lim >> 1) < n);  /* and it is the smallest one */
    while ((ran &= lim) > n)  /* project 'ran' into [0..lim] */
      ran = I2UInt(nextrand(state->s));  /* not inside [0..n]? try again */
    return ran;
  }
}


static int math_random (lum_State *L) {
  lum_Integer low, up;
  lum_Unsigned p;
  RanState *state = (RanState *)lum_touserdata(L, lum_upvalueindex(1));
  Rand64 rv = nextrand(state->s);  /* next pseudo-random value */
  switch (lum_gettop(L)) {  /* check number of arguments */
    case 0: {  /* no arguments */
      lum_pushnumber(L, I2d(rv));  /* float between 0 and 1 */
      return 1;
    }
    case 1: {  /* only upper limit */
      low = 1;
      up = lumL_checkinteger(L, 1);
      if (up == 0) {  /* single 0 as argument? */
        lum_pushinteger(L, l_castU2S(I2UInt(rv)));  /* full random integer */
        return 1;
      }
      break;
    }
    case 2: {  /* lower and upper limits */
      low = lumL_checkinteger(L, 1);
      up = lumL_checkinteger(L, 2);
      break;
    }
    default: return lumL_error(L, "wrong number of arguments");
  }
  /* random integer in the interval [low, up] */
  lumL_argcheck(L, low <= up, 1, "interval is empty");
  /* project random integer into the interval [0, up - low] */
  p = project(I2UInt(rv), (lum_Unsigned)up - (lum_Unsigned)low, state);
  lum_pushinteger(L, l_castU2S(p) + low);
  return 1;
}


static void setseed (lum_State *L, Rand64 *state,
                     lum_Unsigned n1, lum_Unsigned n2) {
  int i;
  state[0] = Int2I(n1);
  state[1] = Int2I(0xff);  /* avoid a zero state */
  state[2] = Int2I(n2);
  state[3] = Int2I(0);
  for (i = 0; i < 16; i++)
    nextrand(state);  /* discard initial values to "spread" seed */
  lum_pushinteger(L, l_castU2S(n1));
  lum_pushinteger(L, l_castU2S(n2));
}


static int math_randomseed (lum_State *L) {
  RanState *state = (RanState *)lum_touserdata(L, lum_upvalueindex(1));
  lum_Unsigned n1, n2;
  if (lum_isnone(L, 1)) {
    n1 = lumL_makeseed(L);  /* "random" seed */
    n2 = I2UInt(nextrand(state->s));  /* in case seed is not that random... */
  }
  else {
    n1 = l_castS2U(lumL_checkinteger(L, 1));
    n2 = l_castS2U(lumL_optinteger(L, 2, 0));
  }
  setseed(L, state->s, n1, n2);
  return 2;  /* return seeds */
}


static const lumL_Reg randfuncs[] = {
  {"random", math_random},
  {"randomseed", math_randomseed},
  {NULL, NULL}
};


/*
** Register the random functions and initialize their state.
*/
static void setrandfunc (lum_State *L) {
  RanState *state = (RanState *)lum_newuserdatauv(L, sizeof(RanState), 0);
  setseed(L, state->s, lumL_makeseed(L), 0);  /* initialize with random seed */
  lum_pop(L, 2);  /* remove pushed seeds */
  lumL_setfuncs(L, randfuncs, 1);
}

/* }================================================================== */


/*
** {==================================================================
** Deprecated functions (for compatibility only)
** ===================================================================
*/
#if defined(LUM_COMPAT_MATHLIB)

static int math_cosh (lum_State *L) {
  lum_pushnumber(L, l_mathop(cosh)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_sinh (lum_State *L) {
  lum_pushnumber(L, l_mathop(sinh)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_tanh (lum_State *L) {
  lum_pushnumber(L, l_mathop(tanh)(lumL_checknumber(L, 1)));
  return 1;
}

static int math_pow (lum_State *L) {
  lum_Number x = lumL_checknumber(L, 1);
  lum_Number y = lumL_checknumber(L, 2);
  lum_pushnumber(L, l_mathop(pow)(x, y));
  return 1;
}

static int math_frexp (lum_State *L) {
  int e;
  lum_pushnumber(L, l_mathop(frexp)(lumL_checknumber(L, 1), &e));
  lum_pushinteger(L, e);
  return 2;
}

static int math_ldexp (lum_State *L) {
  lum_Number x = lumL_checknumber(L, 1);
  int ep = (int)lumL_checkinteger(L, 2);
  lum_pushnumber(L, l_mathop(ldexp)(x, ep));
  return 1;
}

static int math_log10 (lum_State *L) {
  lum_pushnumber(L, l_mathop(log10)(lumL_checknumber(L, 1)));
  return 1;
}

#endif
/* }================================================================== */



static const lumL_Reg mathlib[] = {
  {"abs",   math_abs},
  {"acos",  math_acos},
  {"asin",  math_asin},
  {"atan",  math_atan},
  {"ceil",  math_ceil},
  {"cos",   math_cos},
  {"deg",   math_deg},
  {"exp",   math_exp},
  {"tointeger", math_toint},
  {"floor", math_floor},
  {"fmod",   math_fmod},
  {"ult",   math_ult},
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",   math_modf},
  {"rad",   math_rad},
  {"sin",   math_sin},
  {"sqrt",  math_sqrt},
  {"tan",   math_tan},
  {"type", math_type},
#if defined(LUM_COMPAT_MATHLIB)
  {"atan2", math_atan},
  {"cosh",   math_cosh},
  {"sinh",   math_sinh},
  {"tanh",   math_tanh},
  {"pow",   math_pow},
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
  {"log10", math_log10},
#endif
  /* placeholders */
  {"random", NULL},
  {"randomseed", NULL},
  {"pi", NULL},
  {"huge", NULL},
  {"maxinteger", NULL},
  {"mininteger", NULL},
  {NULL, NULL}
};


/*
** Open math library
*/
LUMMOD_API int lumopen_math (lum_State *L) {
  lumL_newlib(L, mathlib);
  lum_pushnumber(L, PI);
  lum_setfield(L, -2, "pi");
  lum_pushnumber(L, (lum_Number)HUGE_VAL);
  lum_setfield(L, -2, "huge");
  lum_pushinteger(L, LUM_MAXINTEGER);
  lum_setfield(L, -2, "maxinteger");
  lum_pushinteger(L, LUM_MININTEGER);
  lum_setfield(L, -2, "mininteger");
  setrandfunc(L);
  return 1;
}

