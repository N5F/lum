/*
** $Id: loslib.c $
** Standard Operating System library
** See Copyright Notice in lum.h
*/

#define loslib_c
#define LUM_LIB

#include "lprefix.h"


#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


/*
** {==================================================================
** List of valid conversion specifiers for the 'strftime' function;
** options are grouped by length; group of length 2 start with '||'.
** ===================================================================
*/
#if !defined(LUM_STRFTIMEOPTIONS)	/* { */

#if defined(LUM_USE_WINDOWS)
#define LUM_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"  /* two-char options */
#elif defined(LUM_USE_C89)  /* ANSI C 89 (only 1-char options) */
#define LUM_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYZ%"
#else  /* C99 specification */
#define LUM_STRFTIMEOPTIONS  "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"  /* two-char options */
#endif

#endif					/* } */
/* }================================================================== */


/*
** {==================================================================
** Configuration for time-related stuff
** ===================================================================
*/

/*
** type to represent time_t in Lum
*/
#if !defined(LUM_NUMTIME)	/* { */

#define l_timet			lum_Integer
#define l_pushtime(L,t)		lum_pushinteger(L,(lum_Integer)(t))
#define l_gettime(L,arg)	lumL_checkinteger(L, arg)

#else				/* }{ */

#define l_timet			lum_Number
#define l_pushtime(L,t)		lum_pushnumber(L,(lum_Number)(t))
#define l_gettime(L,arg)	lumL_checknumber(L, arg)

#endif				/* } */


#if !defined(l_gmtime)		/* { */
/*
** By default, Lum uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(LUM_USE_POSIX)	/* { */

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else				/* }{ */

/* ISO C definitions */
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif				/* } */

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Configuration for 'tmpnam':
** By default, Lum uses tmpnam except when POSIX is available, where
** it uses mkstemp.
** ===================================================================
*/
#if !defined(lum_tmpnam)	/* { */

#if defined(LUM_USE_POSIX)	/* { */

#include <unistd.h>

#define LUM_TMPNAMBUFSIZE	32

#if !defined(LUM_TMPNAMTEMPLATE)
#define LUM_TMPNAMTEMPLATE	"/tmp/lum_XXXXXX"
#endif

#define lum_tmpnam(b,e) { \
        strcpy(b, LUM_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				/* }{ */

/* ISO C definitions */
#define LUM_TMPNAMBUFSIZE	L_tmpnam
#define lum_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				/* } */

#endif				/* } */
/* }================================================================== */


#if !defined(l_system)
#if defined(LUM_USE_IOS)
/* Despite claiming to be ISO C, iOS does not implement 'system'. */
#define l_system(cmd) ((cmd) == NULL ? 0 : -1)
#else
#define l_system(cmd)	system(cmd)  /* default definition */
#endif
#endif


static int os_execute (lum_State *L) {
  const char *cmd = lumL_optstring(L, 1, NULL);
  int stat;
  errno = 0;
  stat = l_system(cmd);
  if (cmd != NULL)
    return lumL_execresult(L, stat);
  else {
    lum_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (lum_State *L) {
  const char *filename = lumL_checkstring(L, 1);
  errno = 0;
  return lumL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (lum_State *L) {
  const char *fromname = lumL_checkstring(L, 1);
  const char *toname = lumL_checkstring(L, 2);
  errno = 0;
  return lumL_fileresult(L, rename(fromname, toname) == 0, NULL);
}


static int os_tmpname (lum_State *L) {
  char buff[LUM_TMPNAMBUFSIZE];
  int err;
  lum_tmpnam(buff, err);
  if (l_unlikely(err))
    return lumL_error(L, "unable to generate a unique filename");
  lum_pushstring(L, buff);
  return 1;
}


static int os_getenv (lum_State *L) {
  lum_pushstring(L, getenv(lumL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (lum_State *L) {
  lum_pushnumber(L, ((lum_Number)clock())/(lum_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

/*
** About the overflow check: an overflow cannot occur when time
** is represented by a lum_Integer, because either lum_Integer is
** large enough to represent all int fields or it is not large enough
** to represent a time that cause a field to overflow.  However, if
** times are represented as doubles and lum_Integer is int, then the
** time 0x1.e1853b0d184f6p+55 would cause an overflow when adding 1900
** to compute the year.
*/
static void setfield (lum_State *L, const char *key, int value, int delta) {
  #if (defined(LUM_NUMTIME) && LUM_MAXINTEGER <= INT_MAX)
    if (l_unlikely(value > LUM_MAXINTEGER - delta))
      lumL_error(L, "field '%s' is out-of-bound", key);
  #endif
  lum_pushinteger(L, (lum_Integer)value + delta);
  lum_setfield(L, -2, key);
}


static void setboolfield (lum_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lum_pushboolean(L, value);
  lum_setfield(L, -2, key);
}


/*
** Set all fields from structure 'tm' in the table on top of the stack
*/
static void setallfields (lum_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}


static int getboolfield (lum_State *L, const char *key) {
  int res;
  res = (lum_getfield(L, -1, key) == LUM_TNIL) ? -1 : lum_toboolean(L, -1);
  lum_pop(L, 1);
  return res;
}


static int getfield (lum_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = lum_getfield(L, -1, key);  /* get field and its type */
  lum_Integer res = lum_tointegerx(L, -1, &isnum);
  if (!isnum) {  /* field is not an integer? */
    if (l_unlikely(t != LUM_TNIL))  /* some other value? */
      return lumL_error(L, "field '%s' is not an integer", key);
    else if (l_unlikely(d < 0))  /* absent field; no default? */
      return lumL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res))
      return lumL_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  lum_pop(L, 1);
  return (int)res;
}


static const char *checkoption (lum_State *L, const char *conv,
                                ptrdiff_t convlen, char *buff) {
  const char *option = LUM_STRFTIMEOPTIONS;
  unsigned oplen = 1;  /* length of options being checked */
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')  /* next block? */
      oplen++;  /* will check options with next length (+1) */
    else if (memcmp(conv, option, oplen) == 0) {  /* match? */
      memcpy(buff, conv, oplen);  /* copy valid option to buffer */
      buff[oplen] = '\0';
      return conv + oplen;  /* return next item */
    }
  }
  lumL_argerror(L, 1,
    lum_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static time_t l_checktime (lum_State *L, int arg) {
  l_timet t = l_gettime(L, arg);
  lumL_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}


/* maximum size for an individual 'strftime' item */
#define SIZETIMEFMT	250


static int os_date (lum_State *L) {
  size_t slen;
  const char *s = lumL_optlstring(L, 1, "%c", &slen);
  time_t t = lumL_opt(L, l_checktime, 2, time(NULL));
  const char *se = s + slen;  /* 's' end */
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip '!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    return lumL_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    lum_createtable(L, 0, 9);  /* 9 = number of fields */
    setallfields(L, stm);
  }
  else {
    char cc[4];  /* buffer for individual conversion specifiers */
    lumL_Buffer b;
    cc[0] = '%';
    lumL_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')  /* not a conversion specifier? */
        lumL_addchar(&b, *s++);
      else {
        size_t reslen;
        char *buff = lumL_prepbuffsize(&b, SIZETIMEFMT);
        s++;  /* skip '%' */
        s = checkoption(L, s, se - s, cc + 1);  /* copy specifier to 'cc' */
        reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        lumL_addsize(&b, reslen);
      }
    }
    lumL_pushresult(&b);
  }
  return 1;
}


static int os_time (lum_State *L) {
  time_t t;
  if (lum_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    lumL_checktype(L, 1, LUM_TTABLE);
    lum_settop(L, 1);  /* make sure table is at the top */
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);  /* update fields with normalized values */
  }
  if (t != (time_t)(l_timet)t || t == (time_t)(-1))
    return lumL_error(L,
                  "time result cannot be represented in this installation");
  l_pushtime(L, t);
  return 1;
}


static int os_difftime (lum_State *L) {
  time_t t1 = l_checktime(L, 1);
  time_t t2 = l_checktime(L, 2);
  lum_pushnumber(L, (lum_Number)difftime(t1, t2));
  return 1;
}

/* }====================================================== */


static int os_setlocale (lum_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = lumL_optstring(L, 1, NULL);
  int op = lumL_checkoption(L, 2, "all", catnames);
  lum_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (lum_State *L) {
  int status;
  if (lum_isboolean(L, 1))
    status = (lum_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)lumL_optinteger(L, 1, EXIT_SUCCESS);
  if (lum_toboolean(L, 2))
    lum_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static const lumL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



LUMMOD_API int lumopen_os (lum_State *L) {
  lumL_newlib(L, syslib);
  return 1;
}

