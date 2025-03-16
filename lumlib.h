/*
** $Id: lumlib.h $
** Lum standard libraries
** See Copyright Notice in lum.h
*/


#ifndef lumlib_h
#define lumlib_h

#include "lum.h"


/* version suffix for environment variable names */
#define LUM_VERSUFFIX          "_" LUM_VERSION_MAJOR "_" LUM_VERSION_MINOR

#define LUM_GLIBK		1
LUMMOD_API int (lumopen_base) (lum_State *L);

#define LUM_LOADLIBNAME	"package"
#define LUM_LOADLIBK	(LUM_GLIBK << 1)
LUMMOD_API int (lumopen_package) (lum_State *L);


#define LUM_COLIBNAME	"coroutine"
#define LUM_COLIBK	(LUM_LOADLIBK << 1)
LUMMOD_API int (lumopen_coroutine) (lum_State *L);

#define LUM_DBLIBNAME	"debug"
#define LUM_DBLIBK	(LUM_COLIBK << 1)
LUMMOD_API int (lumopen_debug) (lum_State *L);

#define LUM_IOLIBNAME	"io"
#define LUM_IOLIBK	(LUM_DBLIBK << 1)
LUMMOD_API int (lumopen_io) (lum_State *L);

#define LUM_MATHLIBNAME	"math"
#define LUM_MATHLIBK	(LUM_IOLIBK << 1)
LUMMOD_API int (lumopen_math) (lum_State *L);

#define LUM_OSLIBNAME	"os"
#define LUM_OSLIBK	(LUM_MATHLIBK << 1)
LUMMOD_API int (lumopen_os) (lum_State *L);

#define LUM_STRLIBNAME	"string"
#define LUM_STRLIBK	(LUM_OSLIBK << 1)
LUMMOD_API int (lumopen_string) (lum_State *L);

#define LUM_TABLIBNAME	"table"
#define LUM_TABLIBK	(LUM_STRLIBK << 1)
LUMMOD_API int (lumopen_table) (lum_State *L);

#define LUM_UTF8LIBNAME	"utf8"
#define LUM_UTF8LIBK	(LUM_TABLIBK << 1)
LUMMOD_API int (lumopen_utf8) (lum_State *L);


/* open selected libraries */
LUMLIB_API void (lumL_openselectedlibs) (lum_State *L, int load, int preload);

/* open all libraries */
#define lumL_openlibs(L)	lumL_openselectedlibs(L, ~0, 0)


#endif
