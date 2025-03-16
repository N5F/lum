/*
** $Id: lum.h $
** Lum - A Scripting Language
** Lua.org, PUC-Rio, Brazil (www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lum_h
#define lum_h

#include <stdarg.h>
#include <stddef.h>


#define LUM_COPYRIGHT	LUM_RELEASE "  Copyright (C) 1994-2025 Lua.org, PUC-Rio"
#define LUM_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


#define LUM_VERSION_MAJOR_N	5
#define LUM_VERSION_MINOR_N	5
#define LUM_VERSION_RELEASE_N	0

#define LUM_VERSION_NUM  (LUM_VERSION_MAJOR_N * 100 + LUM_VERSION_MINOR_N)
#define LUM_VERSION_RELEASE_NUM  (LUM_VERSION_NUM * 100 + LUM_VERSION_RELEASE_N)


#include "lumconf.h"


/* mark for precompiled code ('<esc>Lum') */
#define LUM_SIGNATURE	"\x1bLum"

/* option for multiple returns in 'lum_pcall' and 'lum_call' */
#define LUM_MULTRET	(-1)


/*
** Pseudo-indices
** (-LUMI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
#define LUM_REGISTRYINDEX	(-LUMI_MAXSTACK - 1000)
#define lum_upvalueindex(i)	(LUM_REGISTRYINDEX - (i))


/* thread status */
#define LUM_OK		0
#define LUM_YIELD	1
#define LUM_ERRRUN	2
#define LUM_ERRSYNTAX	3
#define LUM_ERRMEM	4
#define LUM_ERRERR	5


typedef struct lum_State lum_State;


/*
** basic types
*/
#define LUM_TNONE		(-1)

#define LUM_TNIL		0
#define LUM_TBOOLEAN		1
#define LUM_TLIGHTUSERDATA	2
#define LUM_TNUMBER		3
#define LUM_TSTRING		4
#define LUM_TTABLE		5
#define LUM_TFUNCTION		6
#define LUM_TUSERDATA		7
#define LUM_TTHREAD		8

#define LUM_NUMTYPES		9



/* minimum Lum stack available to a C function */
#define LUM_MINSTACK	20


/* predefined values in the registry */
/* index 1 is reserved for the reference mechanism */
#define LUM_RIDX_GLOBALS	2
#define LUM_RIDX_MAINTHREAD	3
#define LUM_RIDX_LAST		3


/* type of numbers in Lum */
typedef LUM_NUMBER lum_Number;


/* type for integer functions */
typedef LUM_INTEGER lum_Integer;

/* unsigned integer type */
typedef LUM_UNSIGNED lum_Unsigned;

/* type for continuation-function contexts */
typedef LUM_KCONTEXT lum_KContext;


/*
** Type for C functions registered with Lum
*/
typedef int (*lum_CFunction) (lum_State *L);

/*
** Type for continuation functions
*/
typedef int (*lum_KFunction) (lum_State *L, int status, lum_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Lum chunks
*/
typedef const char * (*lum_Reader) (lum_State *L, void *ud, size_t *sz);

typedef int (*lum_Writer) (lum_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/
typedef void * (*lum_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** Type for warning functions
*/
typedef void (*lum_WarnFunction) (void *ud, const char *msg, int tocont);


/*
** Type used by the debug API to collect debug information
*/
typedef struct lum_Debug lum_Debug;


/*
** Functions to be called by the debugger in specific events
*/
typedef void (*lum_Hook) (lum_State *L, lum_Debug *ar);


/*
** generic extra include file
*/
#if defined(LUM_USER_H)
#include LUM_USER_H
#endif


/*
** RCS ident string
*/
extern const char lum_ident[];


/*
** state manipulation
*/
LUM_API lum_State *(lum_newstate) (lum_Alloc f, void *ud, unsigned seed);
LUM_API void       (lum_close) (lum_State *L);
LUM_API lum_State *(lum_newthread) (lum_State *L);
LUM_API int        (lum_closethread) (lum_State *L, lum_State *from);

LUM_API lum_CFunction (lum_atpanic) (lum_State *L, lum_CFunction panicf);


LUM_API lum_Number (lum_version) (lum_State *L);


/*
** basic stack manipulation
*/
LUM_API int   (lum_absindex) (lum_State *L, int idx);
LUM_API int   (lum_gettop) (lum_State *L);
LUM_API void  (lum_settop) (lum_State *L, int idx);
LUM_API void  (lum_pushvalue) (lum_State *L, int idx);
LUM_API void  (lum_rotate) (lum_State *L, int idx, int n);
LUM_API void  (lum_copy) (lum_State *L, int fromidx, int toidx);
LUM_API int   (lum_checkstack) (lum_State *L, int n);

LUM_API void  (lum_xmove) (lum_State *from, lum_State *to, int n);


/*
** access functions (stack -> C)
*/

LUM_API int             (lum_isnumber) (lum_State *L, int idx);
LUM_API int             (lum_isstring) (lum_State *L, int idx);
LUM_API int             (lum_iscfunction) (lum_State *L, int idx);
LUM_API int             (lum_isinteger) (lum_State *L, int idx);
LUM_API int             (lum_isuserdata) (lum_State *L, int idx);
LUM_API int             (lum_type) (lum_State *L, int idx);
LUM_API const char     *(lum_typename) (lum_State *L, int tp);

LUM_API lum_Number      (lum_tonumberx) (lum_State *L, int idx, int *isnum);
LUM_API lum_Integer     (lum_tointegerx) (lum_State *L, int idx, int *isnum);
LUM_API int             (lum_toboolean) (lum_State *L, int idx);
LUM_API const char     *(lum_tolstring) (lum_State *L, int idx, size_t *len);
LUM_API lum_Unsigned    (lum_rawlen) (lum_State *L, int idx);
LUM_API lum_CFunction   (lum_tocfunction) (lum_State *L, int idx);
LUM_API void	       *(lum_touserdata) (lum_State *L, int idx);
LUM_API lum_State      *(lum_tothread) (lum_State *L, int idx);
LUM_API const void     *(lum_topointer) (lum_State *L, int idx);


/*
** Comparison and arithmetic functions
*/

#define LUM_OPADD	0	/* ORDER TM, ORDER OP */
#define LUM_OPSUB	1
#define LUM_OPMUL	2
#define LUM_OPMOD	3
#define LUM_OPPOW	4
#define LUM_OPDIV	5
#define LUM_OPIDIV	6
#define LUM_OPBAND	7
#define LUM_OPBOR	8
#define LUM_OPBXOR	9
#define LUM_OPSHL	10
#define LUM_OPSHR	11
#define LUM_OPUNM	12
#define LUM_OPBNOT	13

LUM_API void  (lum_arith) (lum_State *L, int op);

#define LUM_OPEQ	0
#define LUM_OPLT	1
#define LUM_OPLE	2

LUM_API int   (lum_rawequal) (lum_State *L, int idx1, int idx2);
LUM_API int   (lum_compare) (lum_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
LUM_API void        (lum_pushnil) (lum_State *L);
LUM_API void        (lum_pushnumber) (lum_State *L, lum_Number n);
LUM_API void        (lum_pushinteger) (lum_State *L, lum_Integer n);
LUM_API const char *(lum_pushlstring) (lum_State *L, const char *s, size_t len);
LUM_API const char *(lum_pushexternalstring) (lum_State *L,
		const char *s, size_t len, lum_Alloc falloc, void *ud);
LUM_API const char *(lum_pushstring) (lum_State *L, const char *s);
LUM_API const char *(lum_pushvfstring) (lum_State *L, const char *fmt,
                                                      va_list argp);
LUM_API const char *(lum_pushfstring) (lum_State *L, const char *fmt, ...);
LUM_API void  (lum_pushcclosure) (lum_State *L, lum_CFunction fn, int n);
LUM_API void  (lum_pushboolean) (lum_State *L, int b);
LUM_API void  (lum_pushlightuserdata) (lum_State *L, void *p);
LUM_API int   (lum_pushthread) (lum_State *L);


/*
** get functions (Lum -> stack)
*/
LUM_API int (lum_getglobal) (lum_State *L, const char *name);
LUM_API int (lum_gettable) (lum_State *L, int idx);
LUM_API int (lum_getfield) (lum_State *L, int idx, const char *k);
LUM_API int (lum_geti) (lum_State *L, int idx, lum_Integer n);
LUM_API int (lum_rawget) (lum_State *L, int idx);
LUM_API int (lum_rawgeti) (lum_State *L, int idx, lum_Integer n);
LUM_API int (lum_rawgetp) (lum_State *L, int idx, const void *p);

LUM_API void  (lum_createtable) (lum_State *L, int narr, int nrec);
LUM_API void *(lum_newuserdatauv) (lum_State *L, size_t sz, int nuvalue);
LUM_API int   (lum_getmetatable) (lum_State *L, int objindex);
LUM_API int  (lum_getiuservalue) (lum_State *L, int idx, int n);


/*
** set functions (stack -> Lum)
*/
LUM_API void  (lum_setglobal) (lum_State *L, const char *name);
LUM_API void  (lum_settable) (lum_State *L, int idx);
LUM_API void  (lum_setfield) (lum_State *L, int idx, const char *k);
LUM_API void  (lum_seti) (lum_State *L, int idx, lum_Integer n);
LUM_API void  (lum_rawset) (lum_State *L, int idx);
LUM_API void  (lum_rawseti) (lum_State *L, int idx, lum_Integer n);
LUM_API void  (lum_rawsetp) (lum_State *L, int idx, const void *p);
LUM_API int   (lum_setmetatable) (lum_State *L, int objindex);
LUM_API int   (lum_setiuservalue) (lum_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Lum code)
*/
LUM_API void  (lum_callk) (lum_State *L, int nargs, int nresults,
                           lum_KContext ctx, lum_KFunction k);
#define lum_call(L,n,r)		lum_callk(L, (n), (r), 0, NULL)

LUM_API int   (lum_pcallk) (lum_State *L, int nargs, int nresults, int errfunc,
                            lum_KContext ctx, lum_KFunction k);
#define lum_pcall(L,n,r,f)	lum_pcallk(L, (n), (r), (f), 0, NULL)

LUM_API int   (lum_load) (lum_State *L, lum_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

LUM_API int (lum_dump) (lum_State *L, lum_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
LUM_API int  (lum_yieldk)     (lum_State *L, int nresults, lum_KContext ctx,
                               lum_KFunction k);
LUM_API int  (lum_resume)     (lum_State *L, lum_State *from, int narg,
                               int *nres);
LUM_API int  (lum_status)     (lum_State *L);
LUM_API int (lum_isyieldable) (lum_State *L);

#define lum_yield(L,n)		lum_yieldk(L, (n), 0, NULL)


/*
** Warning-related functions
*/
LUM_API void (lum_setwarnf) (lum_State *L, lum_WarnFunction f, void *ud);
LUM_API void (lum_warning)  (lum_State *L, const char *msg, int tocont);


/*
** garbage-collection options
*/

#define LUM_GCSTOP		0
#define LUM_GCRESTART		1
#define LUM_GCCOLLECT		2
#define LUM_GCCOUNT		3
#define LUM_GCCOUNTB		4
#define LUM_GCSTEP		5
#define LUM_GCISRUNNING		6
#define LUM_GCGEN		7
#define LUM_GCINC		8
#define LUM_GCPARAM		9


/*
** garbage-collection parameters
*/
/* parameters for generational mode */
#define LUM_GCPMINORMUL		0  /* control minor collections */
#define LUM_GCPMAJORMINOR	1  /* control shift major->minor */
#define LUM_GCPMINORMAJOR	2  /* control shift minor->major */

/* parameters for incremental mode */
#define LUM_GCPPAUSE		3  /* size of pause between successive GCs */
#define LUM_GCPSTEPMUL		4  /* GC "speed" */
#define LUM_GCPSTEPSIZE		5  /* GC granularity */

/* number of parameters */
#define LUM_GCPN		6


LUM_API int (lum_gc) (lum_State *L, int what, ...);


/*
** miscellaneous functions
*/

LUM_API int   (lum_error) (lum_State *L);

LUM_API int   (lum_next) (lum_State *L, int idx);

LUM_API void  (lum_concat) (lum_State *L, int n);
LUM_API void  (lum_len)    (lum_State *L, int idx);

#define LUM_N2SBUFFSZ	64
LUM_API unsigned  (lum_numbertocstring) (lum_State *L, int idx, char *buff);
LUM_API size_t  (lum_stringtonumber) (lum_State *L, const char *s);

LUM_API lum_Alloc (lum_getallocf) (lum_State *L, void **ud);
LUM_API void      (lum_setallocf) (lum_State *L, lum_Alloc f, void *ud);

LUM_API void (lum_toclose) (lum_State *L, int idx);
LUM_API void (lum_closeslot) (lum_State *L, int idx);


/*
** {==============================================================
** some useful macros
** ===============================================================
*/

#define lum_getextraspace(L)	((void *)((char *)(L) - LUM_EXTRASPACE))

#define lum_tonumber(L,i)	lum_tonumberx(L,(i),NULL)
#define lum_tointeger(L,i)	lum_tointegerx(L,(i),NULL)

#define lum_pop(L,n)		lum_settop(L, -(n)-1)

#define lum_newtable(L)		lum_createtable(L, 0, 0)

#define lum_register(L,n,f) (lum_pushcfunction(L, (f)), lum_setglobal(L, (n)))

#define lum_pushcfunction(L,f)	lum_pushcclosure(L, (f), 0)

#define lum_isfunction(L,n)	(lum_type(L, (n)) == LUM_TFUNCTION)
#define lum_istable(L,n)	(lum_type(L, (n)) == LUM_TTABLE)
#define lum_islightuserdata(L,n)	(lum_type(L, (n)) == LUM_TLIGHTUSERDATA)
#define lum_isnil(L,n)		(lum_type(L, (n)) == LUM_TNIL)
#define lum_isboolean(L,n)	(lum_type(L, (n)) == LUM_TBOOLEAN)
#define lum_isthread(L,n)	(lum_type(L, (n)) == LUM_TTHREAD)
#define lum_isnone(L,n)		(lum_type(L, (n)) == LUM_TNONE)
#define lum_isnoneornil(L, n)	(lum_type(L, (n)) <= 0)

#define lum_pushliteral(L, s)	lum_pushstring(L, "" s)

#define lum_pushglobaltable(L)  \
	((void)lum_rawgeti(L, LUM_REGISTRYINDEX, LUM_RIDX_GLOBALS))

#define lum_tostring(L,i)	lum_tolstring(L, (i), NULL)


#define lum_insert(L,idx)	lum_rotate(L, (idx), 1)

#define lum_remove(L,idx)	(lum_rotate(L, (idx), -1), lum_pop(L, 1))

#define lum_replace(L,idx)	(lum_copy(L, -1, (idx)), lum_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros
** ===============================================================
*/
#if defined(LUM_COMPAT_APIINTCASTS)

#define lum_pushunsigned(L,n)	lum_pushinteger(L, (lum_Integer)(n))
#define lum_tounsignedx(L,i,is)	((lum_Unsigned)lum_tointegerx(L,i,is))
#define lum_tounsigned(L,i)	lum_tounsignedx(L,(i),NULL)

#endif

#define lum_newuserdata(L,s)	lum_newuserdatauv(L,s,1)
#define lum_getuservalue(L,idx)	lum_getiuservalue(L,idx,1)
#define lum_setuservalue(L,idx)	lum_setiuservalue(L,idx,1)

#define lum_resetthread(L)	lum_closethread(L,NULL)

/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUM_HOOKCALL	0
#define LUM_HOOKRET	1
#define LUM_HOOKLINE	2
#define LUM_HOOKCOUNT	3
#define LUM_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUM_MASKCALL	(1 << LUM_HOOKCALL)
#define LUM_MASKRET	(1 << LUM_HOOKRET)
#define LUM_MASKLINE	(1 << LUM_HOOKLINE)
#define LUM_MASKCOUNT	(1 << LUM_HOOKCOUNT)


LUM_API int (lum_getstack) (lum_State *L, int level, lum_Debug *ar);
LUM_API int (lum_getinfo) (lum_State *L, const char *what, lum_Debug *ar);
LUM_API const char *(lum_getlocal) (lum_State *L, const lum_Debug *ar, int n);
LUM_API const char *(lum_setlocal) (lum_State *L, const lum_Debug *ar, int n);
LUM_API const char *(lum_getupvalue) (lum_State *L, int funcindex, int n);
LUM_API const char *(lum_setupvalue) (lum_State *L, int funcindex, int n);

LUM_API void *(lum_upvalueid) (lum_State *L, int fidx, int n);
LUM_API void  (lum_upvaluejoin) (lum_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

LUM_API void (lum_sethook) (lum_State *L, lum_Hook func, int mask, int count);
LUM_API lum_Hook (lum_gethook) (lum_State *L);
LUM_API int (lum_gethookmask) (lum_State *L);
LUM_API int (lum_gethookcount) (lum_State *L);


struct lum_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  const char *what;	/* (S) 'Lum', 'C', 'main', 'tail' */
  const char *source;	/* (S) */
  size_t srclen;	/* (S) */
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  unsigned char nups;	/* (u) number of upvalues */
  unsigned char nparams;/* (u) number of parameters */
  char isvararg;        /* (u) */
  unsigned char extraargs;  /* (t) number of extra arguments */
  char istailcall;	/* (t) */
  int ftransfer;   /* (r) index of first value transferred */
  int ntransfer;   /* (r) number of transferred values */
  char short_src[LUM_IDSIZE]; /* (S) */
  /* private part */
  struct CallInfo *i_ci;  /* active function */
};

/* }====================================================================== */


#define LUMI_TOSTRAUX(x)	#x
#define LUMI_TOSTR(x)		LUMI_TOSTRAUX(x)

#define LUM_VERSION_MAJOR	LUMI_TOSTR(LUM_VERSION_MAJOR_N)
#define LUM_VERSION_MINOR	LUMI_TOSTR(LUM_VERSION_MINOR_N)
#define LUM_VERSION_RELEASE	LUMI_TOSTR(LUM_VERSION_RELEASE_N)

#define LUM_VERSION	"Lum " LUM_VERSION_MAJOR "." LUM_VERSION_MINOR
#define LUM_RELEASE	LUM_VERSION "." LUM_VERSION_RELEASE


/******************************************************************************
* Copyright (C) 1994-2025 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
