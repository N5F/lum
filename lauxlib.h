/*
** $Id: lauxlib.h $
** Auxiliary functions for building Lum libraries
** See Copyright Notice in lum.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "lumconf.h"
#include "lum.h"


/* global table */
#define LUM_GNAME	"_G"


typedef struct lumL_Buffer lumL_Buffer;


/* extra error code for 'lumL_loadfilex' */
#define LUM_ERRFILE     (LUM_ERRERR+1)


/* key, in the registry, for table of loaded modules */
#define LUM_LOADED_TABLE	"_LOADED"


/* key, in the registry, for table of preloaded loaders */
#define LUM_PRELOAD_TABLE	"_PRELOAD"


typedef struct lumL_Reg {
  const char *name;
  lum_CFunction func;
} lumL_Reg;


#define LUML_NUMSIZES	(sizeof(lum_Integer)*16 + sizeof(lum_Number))

LUMLIB_API void (lumL_checkversion_) (lum_State *L, lum_Number ver, size_t sz);
#define lumL_checkversion(L)  \
	  lumL_checkversion_(L, LUM_VERSION_NUM, LUML_NUMSIZES)

LUMLIB_API int (lumL_getmetafield) (lum_State *L, int obj, const char *e);
LUMLIB_API int (lumL_callmeta) (lum_State *L, int obj, const char *e);
LUMLIB_API const char *(lumL_tolstring) (lum_State *L, int idx, size_t *len);
LUMLIB_API int (lumL_argerror) (lum_State *L, int arg, const char *extramsg);
LUMLIB_API int (lumL_typeerror) (lum_State *L, int arg, const char *tname);
LUMLIB_API const char *(lumL_checklstring) (lum_State *L, int arg,
                                                          size_t *l);
LUMLIB_API const char *(lumL_optlstring) (lum_State *L, int arg,
                                          const char *def, size_t *l);
LUMLIB_API lum_Number (lumL_checknumber) (lum_State *L, int arg);
LUMLIB_API lum_Number (lumL_optnumber) (lum_State *L, int arg, lum_Number def);

LUMLIB_API lum_Integer (lumL_checkinteger) (lum_State *L, int arg);
LUMLIB_API lum_Integer (lumL_optinteger) (lum_State *L, int arg,
                                          lum_Integer def);

LUMLIB_API void (lumL_checkstack) (lum_State *L, int sz, const char *msg);
LUMLIB_API void (lumL_checktype) (lum_State *L, int arg, int t);
LUMLIB_API void (lumL_checkany) (lum_State *L, int arg);

LUMLIB_API int   (lumL_newmetatable) (lum_State *L, const char *tname);
LUMLIB_API void  (lumL_setmetatable) (lum_State *L, const char *tname);
LUMLIB_API void *(lumL_testudata) (lum_State *L, int ud, const char *tname);
LUMLIB_API void *(lumL_checkudata) (lum_State *L, int ud, const char *tname);

LUMLIB_API void (lumL_where) (lum_State *L, int lvl);
LUMLIB_API int (lumL_error) (lum_State *L, const char *fmt, ...);

LUMLIB_API int (lumL_checkoption) (lum_State *L, int arg, const char *def,
                                   const char *const lst[]);

LUMLIB_API int (lumL_fileresult) (lum_State *L, int stat, const char *fname);
LUMLIB_API int (lumL_execresult) (lum_State *L, int stat);


/* predefined references */
#define LUM_NOREF       (-2)
#define LUM_REFNIL      (-1)

LUMLIB_API int (lumL_ref) (lum_State *L, int t);
LUMLIB_API void (lumL_unref) (lum_State *L, int t, int ref);

LUMLIB_API int (lumL_loadfilex) (lum_State *L, const char *filename,
                                               const char *mode);

#define lumL_loadfile(L,f)	lumL_loadfilex(L,f,NULL)

LUMLIB_API int (lumL_loadbufferx) (lum_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
LUMLIB_API int (lumL_loadstring) (lum_State *L, const char *s);

LUMLIB_API lum_State *(lumL_newstate) (void);

LUMLIB_API unsigned lumL_makeseed (lum_State *L);

LUMLIB_API lum_Integer (lumL_len) (lum_State *L, int idx);

LUMLIB_API void (lumL_addgsub) (lumL_Buffer *b, const char *s,
                                     const char *p, const char *r);
LUMLIB_API const char *(lumL_gsub) (lum_State *L, const char *s,
                                    const char *p, const char *r);

LUMLIB_API void (lumL_setfuncs) (lum_State *L, const lumL_Reg *l, int nup);

LUMLIB_API int (lumL_getsubtable) (lum_State *L, int idx, const char *fname);

LUMLIB_API void (lumL_traceback) (lum_State *L, lum_State *L1,
                                  const char *msg, int level);

LUMLIB_API void (lumL_requiref) (lum_State *L, const char *modname,
                                 lum_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define lumL_newlibtable(L,l)	\
  lum_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define lumL_newlib(L,l)  \
  (lumL_checkversion(L), lumL_newlibtable(L,l), lumL_setfuncs(L,l,0))

#define lumL_argcheck(L, cond,arg,extramsg)	\
	((void)(lumi_likely(cond) || lumL_argerror(L, (arg), (extramsg))))

#define lumL_argexpected(L,cond,arg,tname)	\
	((void)(lumi_likely(cond) || lumL_typeerror(L, (arg), (tname))))

#define lumL_checkstring(L,n)	(lumL_checklstring(L, (n), NULL))
#define lumL_optstring(L,n,d)	(lumL_optlstring(L, (n), (d), NULL))

#define lumL_typename(L,i)	lum_typename(L, lum_type(L,(i)))

#define lumL_dofile(L, fn) \
	(lumL_loadfile(L, fn) || lum_pcall(L, 0, LUM_MULTRET, 0))

#define lumL_dostring(L, s) \
	(lumL_loadstring(L, s) || lum_pcall(L, 0, LUM_MULTRET, 0))

#define lumL_getmetatable(L,n)	(lum_getfield(L, LUM_REGISTRYINDEX, (n)))

#define lumL_opt(L,f,n,d)	(lum_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define lumL_loadbuffer(L,s,sz,n)	lumL_loadbufferx(L,s,sz,n,NULL)


/*
** Perform arithmetic operations on lum_Integer values with wrap-around
** semantics, as the Lum core does.
*/
#define lumL_intop(op,v1,v2)  \
	((lum_Integer)((lum_Unsigned)(v1) op (lum_Unsigned)(v2)))


/* push the value used to represent failure/error */
#if defined(LUM_FAILISFALSE)
#define lumL_pushfail(L)	lum_pushboolean(L, 0)
#else
#define lumL_pushfail(L)	lum_pushnil(L)
#endif



/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

struct lumL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  lum_State *L;
  union {
    LUMI_MAXALIGN;  /* ensure maximum alignment for buffer */
    char b[LUML_BUFFERSIZE];  /* initial buffer */
  } init;
};


#define lumL_bufflen(bf)	((bf)->n)
#define lumL_buffaddr(bf)	((bf)->b)


#define lumL_addchar(B,c) \
  ((void)((B)->n < (B)->size || lumL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define lumL_addsize(B,s)	((B)->n += (s))

#define lumL_buffsub(B,s)	((B)->n -= (s))

LUMLIB_API void (lumL_buffinit) (lum_State *L, lumL_Buffer *B);
LUMLIB_API char *(lumL_prepbuffsize) (lumL_Buffer *B, size_t sz);
LUMLIB_API void (lumL_addlstring) (lumL_Buffer *B, const char *s, size_t l);
LUMLIB_API void (lumL_addstring) (lumL_Buffer *B, const char *s);
LUMLIB_API void (lumL_addvalue) (lumL_Buffer *B);
LUMLIB_API void (lumL_pushresult) (lumL_Buffer *B);
LUMLIB_API void (lumL_pushresultsize) (lumL_Buffer *B, size_t sz);
LUMLIB_API char *(lumL_buffinitsize) (lum_State *L, lumL_Buffer *B, size_t sz);

#define lumL_prepbuffer(B)	lumL_prepbuffsize(B, LUML_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'LUM_FILEHANDLE' and
** initial structure 'lumL_Stream' (it may contain other fields
** after that initial structure).
*/

#define LUM_FILEHANDLE          "FILE*"


typedef struct lumL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  lum_CFunction closef;  /* to close stream (NULL for closed streams) */
} lumL_Stream;

/* }====================================================== */


/*
** {============================================================
** Compatibility with deprecated conversions
** =============================================================
*/
#if defined(LUM_COMPAT_APIINTCASTS)

#define lumL_checkunsigned(L,a)	((lum_Unsigned)lumL_checkinteger(L,a))
#define lumL_optunsigned(L,a,d)	\
	((lum_Unsigned)lumL_optinteger(L,a,(lum_Integer)(d)))

#define lumL_checkint(L,n)	((int)lumL_checkinteger(L, (n)))
#define lumL_optint(L,n,d)	((int)lumL_optinteger(L, (n), (d)))

#define lumL_checklong(L,n)	((long)lumL_checkinteger(L, (n)))
#define lumL_optlong(L,n,d)	((long)lumL_optinteger(L, (n), (d)))

#endif
/* }============================================================ */



#endif


