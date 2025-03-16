/*
** $Id: loadlib.c $
** Dynamic library loader for Lum
** See Copyright Notice in lum.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Windows, and a stub for other
** systems.
*/

#define loadlib_c
#define LUM_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


/*
** LUM_CSUBSEP is the character that replaces dots in submodule names
** when searching for a C loader.
** LUM_LSUBSEP is the character that replaces dots in submodule names
** when searching for a Lum loader.
*/
#if !defined(LUM_CSUBSEP)
#define LUM_CSUBSEP		LUM_DIRSEP
#endif

#if !defined(LUM_LSUBSEP)
#define LUM_LSUBSEP		LUM_DIRSEP
#endif


/* prefix for open functions in C libraries */
#define LUM_POF		"lumopen_"

/* separator for open functions in C libraries */
#define LUM_OFSEP	"_"


/*
** key for table in the registry that keeps handles
** for all loaded C libraries
*/
static const char *const CLIBS = "_CLIBS";

#define LIB_FAIL	"open"


#define setprogdir(L)           ((void)0)


/* cast void* to a Lum function */
#define cast_Lfunc(p)	cast(lum_CFunction, cast_func(p))


/*
** system-dependent functions
*/

/*
** unload library 'lib'
*/
static void lsys_unloadlib (void *lib);

/*
** load C library in file 'path'. If 'seeglb', load with all names in
** the library global.
** Returns the library; in case of error, returns NULL plus an
** error string in the stack.
*/
static void *lsys_load (lum_State *L, const char *path, int seeglb);

/*
** Try to find a function named 'sym' in library 'lib'.
** Returns the function; in case of error, returns NULL plus an
** error string in the stack.
*/
static lum_CFunction lsys_sym (lum_State *L, void *lib, const char *sym);




#if defined(LUM_USE_DLOPEN)	/* { */
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface,
** which is available in all POSIX systems.
** =========================================================================
*/

#include <dlfcn.h>


static void lsys_unloadlib (void *lib) {
  dlclose(lib);
}


static void *lsys_load (lum_State *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (l_unlikely(lib == NULL))
    lum_pushstring(L, dlerror());
  return lib;
}


static lum_CFunction lsys_sym (lum_State *L, void *lib, const char *sym) {
  lum_CFunction f = cast_Lfunc(dlsym(lib, sym));
  if (l_unlikely(f == NULL))
    lum_pushstring(L, dlerror());
  return f;
}

/* }====================================================== */



#elif defined(LUM_DL_DLL)	/* }{ */
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

#include <windows.h>


/*
** optional flags for LoadLibraryEx
*/
#if !defined(LUM_LLE_FLAGS)
#define LUM_LLE_FLAGS	0
#endif


#undef setprogdir


/*
** Replace in the path (on the top of the stack) any occurrence
** of LUM_EXEC_DIR with the executable's path.
*/
static void setprogdir (lum_State *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileNameA(NULL, buff, nsize);  /* get exec. name */
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    lumL_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';  /* cut name on the last '\\' to get the path */
    lumL_gsub(L, lum_tostring(L, -1), LUM_EXEC_DIR, buff);
    lum_remove(L, -2);  /* remove original string */
  }
}




static void pusherror (lum_State *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, 0, buffer, sizeof(buffer)/sizeof(char), NULL))
    lum_pushstring(L, buffer);
  else
    lum_pushfstring(L, "system error %d\n", error);
}

static void lsys_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}


static void *lsys_load (lum_State *L, const char *path, int seeglb) {
  HMODULE lib = LoadLibraryExA(path, NULL, LUM_LLE_FLAGS);
  (void)(seeglb);  /* not used: symbols are 'global' by default */
  if (lib == NULL) pusherror(L);
  return lib;
}


static lum_CFunction lsys_sym (lum_State *L, void *lib, const char *sym) {
  lum_CFunction f = cast_Lfunc(GetProcAddress((HMODULE)lib, sym));
  if (f == NULL) pusherror(L);
  return f;
}

/* }====================================================== */


#else				/* }{ */
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Lum installation"


static void lsys_unloadlib (void *lib) {
  (void)(lib);  /* not used */
}


static void *lsys_load (lum_State *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);  /* not used */
  lum_pushliteral(L, DLMSG);
  return NULL;
}


static lum_CFunction lsys_sym (lum_State *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);  /* not used */
  lum_pushliteral(L, DLMSG);
  return NULL;
}

/* }====================================================== */
#endif				/* } */


/*
** {==================================================================
** Set Paths
** ===================================================================
*/

/*
** LUM_PATH_VAR and LUM_CPATH_VAR are the names of the environment
** variables that Lum check to set its paths.
*/
#if !defined(LUM_PATH_VAR)
#define LUM_PATH_VAR    "LUM_PATH"
#endif

#if !defined(LUM_CPATH_VAR)
#define LUM_CPATH_VAR   "LUM_CPATH"
#endif



/*
** return registry.LUM_NOENV as a boolean
*/
static int noenv (lum_State *L) {
  int b;
  lum_getfield(L, LUM_REGISTRYINDEX, "LUM_NOENV");
  b = lum_toboolean(L, -1);
  lum_pop(L, 1);  /* remove value */
  return b;
}


/*
** Set a path. (If using the default path, assume it is a string
** literal in C and create it as an external string.)
*/
static void setpath (lum_State *L, const char *fieldname,
                                   const char *envname,
                                   const char *dft) {
  const char *dftmark;
  const char *nver = lum_pushfstring(L, "%s%s", envname, LUM_VERSUFFIX);
  const char *path = getenv(nver);  /* try versioned name */
  if (path == NULL)  /* no versioned environment variable? */
    path = getenv(envname);  /* try unversioned name */
  if (path == NULL || noenv(L))  /* no environment variable? */
    lum_pushexternalstring(L, dft, strlen(dft), NULL, NULL);  /* use default */
  else if ((dftmark = strstr(path, LUM_PATH_SEP LUM_PATH_SEP)) == NULL)
    lum_pushstring(L, path);  /* nothing to change */
  else {  /* path contains a ";;": insert default path in its place */
    size_t len = strlen(path);
    lumL_Buffer b;
    lumL_buffinit(L, &b);
    if (path < dftmark) {  /* is there a prefix before ';;'? */
      lumL_addlstring(&b, path, ct_diff2sz(dftmark - path));  /* add it */
      lumL_addchar(&b, *LUM_PATH_SEP);
    }
    lumL_addstring(&b, dft);  /* add default */
    if (dftmark < path + len - 2) {  /* is there a suffix after ';;'? */
      lumL_addchar(&b, *LUM_PATH_SEP);
      lumL_addlstring(&b, dftmark + 2, ct_diff2sz((path + len - 2) - dftmark));
    }
    lumL_pushresult(&b);
  }
  setprogdir(L);
  lum_setfield(L, -3, fieldname);  /* package[fieldname] = path value */
  lum_pop(L, 1);  /* pop versioned variable name ('nver') */
}

/* }================================================================== */


/*
** return registry.CLIBS[path]
*/
static void *checkclib (lum_State *L, const char *path) {
  void *plib;
  lum_getfield(L, LUM_REGISTRYINDEX, CLIBS);
  lum_getfield(L, -1, path);
  plib = lum_touserdata(L, -1);  /* plib = CLIBS[path] */
  lum_pop(L, 2);  /* pop CLIBS table and 'plib' */
  return plib;
}


/*
** registry.CLIBS[path] = plib        -- for queries
** registry.CLIBS[#CLIBS + 1] = plib  -- also keep a list of all libraries
*/
static void addtoclib (lum_State *L, const char *path, void *plib) {
  lum_getfield(L, LUM_REGISTRYINDEX, CLIBS);
  lum_pushlightuserdata(L, plib);
  lum_pushvalue(L, -1);
  lum_setfield(L, -3, path);  /* CLIBS[path] = plib */
  lum_rawseti(L, -2, lumL_len(L, -2) + 1);  /* CLIBS[#CLIBS + 1] = plib */
  lum_pop(L, 1);  /* pop CLIBS table */
}


/*
** __gc tag method for CLIBS table: calls 'lsys_unloadlib' for all lib
** handles in list CLIBS
*/
static int gctm (lum_State *L) {
  lum_Integer n = lumL_len(L, 1);
  for (; n >= 1; n--) {  /* for each handle, in reverse order */
    lum_rawgeti(L, 1, n);  /* get handle CLIBS[n] */
    lsys_unloadlib(lum_touserdata(L, -1));
    lum_pop(L, 1);  /* pop handle */
  }
  return 0;
}



/* error codes for 'lookforfunc' */
#define ERRLIB		1
#define ERRFUNC		2

/*
** Look for a C function named 'sym' in a dynamically loaded library
** 'path'.
** First, check whether the library is already loaded; if not, try
** to load it.
** Then, if 'sym' is '*', return true (as library has been loaded).
** Otherwise, look for symbol 'sym' in the library and push a
** C function with that symbol.
** Return 0 and 'true' or a function in the stack; in case of
** errors, return an error code and an error message in the stack.
*/
static int lookforfunc (lum_State *L, const char *path, const char *sym) {
  void *reg = checkclib(L, path);  /* check loaded C libraries */
  if (reg == NULL) {  /* must load library? */
    reg = lsys_load(L, path, *sym == '*');  /* global symbols if 'sym'=='*' */
    if (reg == NULL) return ERRLIB;  /* unable to load library */
    addtoclib(L, path, reg);
  }
  if (*sym == '*') {  /* loading only library (no function)? */
    lum_pushboolean(L, 1);  /* return 'true' */
    return 0;  /* no errors */
  }
  else {
    lum_CFunction f = lsys_sym(L, reg, sym);
    if (f == NULL)
      return ERRFUNC;  /* unable to find function */
    lum_pushcfunction(L, f);  /* else create new function */
    return 0;  /* no errors */
  }
}


static int ll_loadlib (lum_State *L) {
  const char *path = lumL_checkstring(L, 1);
  const char *init = lumL_checkstring(L, 2);
  int stat = lookforfunc(L, path, init);
  if (l_likely(stat == 0))  /* no errors? */
    return 1;  /* return the loaded function */
  else {  /* error; error message is on stack top */
    lumL_pushfail(L);
    lum_insert(L, -2);
    lum_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;  /* return fail, error message, and where */
  }
}



/*
** {======================================================
** 'require' function
** =======================================================
*/


static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  /* try to open file */
  if (f == NULL) return 0;  /* open failed */
  fclose(f);
  return 1;
}


/*
** Get the next name in '*path' = 'name1;name2;name3;...', changing
** the ending ';' to '\0' to create a zero-terminated string. Return
** NULL when list ends.
*/
static const char *getnextfilename (char **path, char *end) {
  char *sep;
  char *name = *path;
  if (name == end)
    return NULL;  /* no more names */
  else if (*name == '\0') {  /* from previous iteration? */
    *name = *LUM_PATH_SEP;  /* restore separator */
    name++;  /* skip it */
  }
  sep = strchr(name, *LUM_PATH_SEP);  /* find next separator */
  if (sep == NULL)  /* separator not found? */
    sep = end;  /* name goes until the end */
  *sep = '\0';  /* finish file name */
  *path = sep;  /* will start next search from here */
  return name;
}


/*
** Given a path such as ";blabla.so;blublu.so", pushes the string
**
** no file 'blabla.so'
**	no file 'blublu.so'
*/
static void pusherrornotfound (lum_State *L, const char *path) {
  lumL_Buffer b;
  lumL_buffinit(L, &b);
  lumL_addstring(&b, "no file '");
  lumL_addgsub(&b, path, LUM_PATH_SEP, "'\n\tno file '");
  lumL_addstring(&b, "'");
  lumL_pushresult(&b);
}


static const char *searchpath (lum_State *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  lumL_Buffer buff;
  char *pathname;  /* path with name inserted */
  char *endpathname;  /* its end */
  const char *filename;
  /* separator is non-empty and appears in 'name'? */
  if (*sep != '\0' && strchr(name, *sep) != NULL)
    name = lumL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
  lumL_buffinit(L, &buff);
  /* add path to the buffer, replacing marks ('?') with the file name */
  lumL_addgsub(&buff, path, LUM_PATH_MARK, name);
  lumL_addchar(&buff, '\0');
  pathname = lumL_buffaddr(&buff);  /* writable list of file names */
  endpathname = pathname + lumL_bufflen(&buff) - 1;
  while ((filename = getnextfilename(&pathname, endpathname)) != NULL) {
    if (readable(filename))  /* does file exist and is readable? */
      return lum_pushstring(L, filename);  /* save and return name */
  }
  lumL_pushresult(&buff);  /* push path to create error message */
  pusherrornotfound(L, lum_tostring(L, -1));  /* create error message */
  return NULL;  /* not found */
}


static int ll_searchpath (lum_State *L) {
  const char *f = searchpath(L, lumL_checkstring(L, 1),
                                lumL_checkstring(L, 2),
                                lumL_optstring(L, 3, "."),
                                lumL_optstring(L, 4, LUM_DIRSEP));
  if (f != NULL) return 1;
  else {  /* error message is on top of the stack */
    lumL_pushfail(L);
    lum_insert(L, -2);
    return 2;  /* return fail + error message */
  }
}


static const char *findfile (lum_State *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  const char *path;
  lum_getfield(L, lum_upvalueindex(1), pname);
  path = lum_tostring(L, -1);
  if (l_unlikely(path == NULL))
    lumL_error(L, "'package.%s' must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}


static int checkload (lum_State *L, int stat, const char *filename) {
  if (l_likely(stat)) {  /* module loaded successfully? */
    lum_pushstring(L, filename);  /* will be 2nd argument to module */
    return 2;  /* return open function and file name */
  }
  else
    return lumL_error(L, "error loading module '%s' from file '%s':\n\t%s",
                          lum_tostring(L, 1), filename, lum_tostring(L, -1));
}


static int searcher_Lum (lum_State *L) {
  const char *filename;
  const char *name = lumL_checkstring(L, 1);
  filename = findfile(L, name, "path", LUM_LSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (lumL_loadfile(L, filename) == LUM_OK), filename);
}


/*
** Try to find a load function for module 'modname' at file 'filename'.
** First, change '.' to '_' in 'modname'; then, if 'modname' has
** the form X-Y (that is, it has an "ignore mark"), build a function
** name "lumopen_X" and look for it. (For compatibility, if that
** fails, it also tries "lumopen_Y".) If there is no ignore mark,
** look for a function named "lumopen_modname".
*/
static int loadfunc (lum_State *L, const char *filename, const char *modname) {
  const char *openfunc;
  const char *mark;
  modname = lumL_gsub(L, modname, ".", LUM_OFSEP);
  mark = strchr(modname, *LUM_IGMARK);
  if (mark) {
    int stat;
    openfunc = lum_pushlstring(L, modname, ct_diff2sz(mark - modname));
    openfunc = lum_pushfstring(L, LUM_POF"%s", openfunc);
    stat = lookforfunc(L, filename, openfunc);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;  /* else go ahead and try old-style name */
  }
  openfunc = lum_pushfstring(L, LUM_POF"%s", modname);
  return lookforfunc(L, filename, openfunc);
}


static int searcher_C (lum_State *L) {
  const char *name = lumL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", LUM_CSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}


static int searcher_Croot (lum_State *L) {
  const char *filename;
  const char *name = lumL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;  /* is root */
  lum_pushlstring(L, name, ct_diff2sz(p - name));
  filename = findfile(L, lum_tostring(L, -1), "cpath", LUM_CSUBSEP);
  if (filename == NULL) return 1;  /* root not found */
  if ((stat = loadfunc(L, filename, name)) != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);  /* real error */
    else {  /* open function not found */
      lum_pushfstring(L, "no module '%s' in file '%s'", name, filename);
      return 1;
    }
  }
  lum_pushstring(L, filename);  /* will be 2nd argument to module */
  return 2;
}


static int searcher_preload (lum_State *L) {
  const char *name = lumL_checkstring(L, 1);
  lum_getfield(L, LUM_REGISTRYINDEX, LUM_PRELOAD_TABLE);
  if (lum_getfield(L, -1, name) == LUM_TNIL) {  /* not found? */
    lum_pushfstring(L, "no field package.preload['%s']", name);
    return 1;
  }
  else {
    lum_pushliteral(L, ":preload:");
    return 2;
  }
}


static void findloader (lum_State *L, const char *name) {
  int i;
  lumL_Buffer msg;  /* to build error message */
  /* push 'package.searchers' to index 3 in the stack */
  if (l_unlikely(lum_getfield(L, lum_upvalueindex(1), "searchers")
                 != LUM_TTABLE))
    lumL_error(L, "'package.searchers' must be a table");
  lumL_buffinit(L, &msg);
  lumL_addstring(&msg, "\n\t");  /* error-message prefix for first message */
  /*  iterate over available searchers to find a loader */
  for (i = 1; ; i++) {
    if (l_unlikely(lum_rawgeti(L, 3, i) == LUM_TNIL)) {  /* no more searchers? */
      lum_pop(L, 1);  /* remove nil */
      lumL_buffsub(&msg, 2);  /* remove last prefix */
      lumL_pushresult(&msg);  /* create error message */
      lumL_error(L, "module '%s' not found:%s", name, lum_tostring(L, -1));
    }
    lum_pushstring(L, name);
    lum_call(L, 1, 2);  /* call it */
    if (lum_isfunction(L, -2))  /* did it find a loader? */
      return;  /* module loader found */
    else if (lum_isstring(L, -2)) {  /* searcher returned error message? */
      lum_pop(L, 1);  /* remove extra return */
      lumL_addvalue(&msg);  /* concatenate error message */
      lumL_addstring(&msg, "\n\t");  /* prefix for next message */
    }
    else  /* no error message */
      lum_pop(L, 2);  /* remove both returns */
  }
}


static int ll_require (lum_State *L) {
  const char *name = lumL_checkstring(L, 1);
  lum_settop(L, 1);  /* LOADED table will be at index 2 */
  lum_getfield(L, LUM_REGISTRYINDEX, LUM_LOADED_TABLE);
  lum_getfield(L, 2, name);  /* LOADED[name] */
  if (lum_toboolean(L, -1))  /* is it there? */
    return 1;  /* package is already loaded */
  /* else must load package */
  lum_pop(L, 1);  /* remove 'getfield' result */
  findloader(L, name);
  lum_rotate(L, -2, 1);  /* function <-> loader data */
  lum_pushvalue(L, 1);  /* name is 1st argument to module loader */
  lum_pushvalue(L, -3);  /* loader data is 2nd argument */
  /* stack: ...; loader data; loader function; mod. name; loader data */
  lum_call(L, 2, 1);  /* run loader to load module */
  /* stack: ...; loader data; result from loader */
  if (!lum_isnil(L, -1))  /* non-nil return? */
    lum_setfield(L, 2, name);  /* LOADED[name] = returned value */
  else
    lum_pop(L, 1);  /* pop nil */
  if (lum_getfield(L, 2, name) == LUM_TNIL) {   /* module set no value? */
    lum_pushboolean(L, 1);  /* use true as result */
    lum_copy(L, -1, -2);  /* replace loader result */
    lum_setfield(L, 2, name);  /* LOADED[name] = true */
  }
  lum_rotate(L, -2, 1);  /* loader data <-> module result  */
  return 2;  /* return module result and loader data */
}

/* }====================================================== */




static const lumL_Reg pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"searchpath", ll_searchpath},
  /* placeholders */
  {"preload", NULL},
  {"cpath", NULL},
  {"path", NULL},
  {"searchers", NULL},
  {"loaded", NULL},
  {NULL, NULL}
};


static const lumL_Reg ll_funcs[] = {
  {"require", ll_require},
  {NULL, NULL}
};


static void createsearcherstable (lum_State *L) {
  static const lum_CFunction searchers[] = {
    searcher_preload,
    searcher_Lum,
    searcher_C,
    searcher_Croot,
    NULL
  };
  int i;
  /* create 'searchers' table */
  lum_createtable(L, sizeof(searchers)/sizeof(searchers[0]) - 1, 0);
  /* fill it with predefined searchers */
  for (i=0; searchers[i] != NULL; i++) {
    lum_pushvalue(L, -2);  /* set 'package' as upvalue for all searchers */
    lum_pushcclosure(L, searchers[i], 1);
    lum_rawseti(L, -2, i+1);
  }
  lum_setfield(L, -2, "searchers");  /* put it in field 'searchers' */
}


/*
** create table CLIBS to keep track of loaded C libraries,
** setting a finalizer to close all libraries when closing state.
*/
static void createclibstable (lum_State *L) {
  lumL_getsubtable(L, LUM_REGISTRYINDEX, CLIBS);  /* create CLIBS table */
  lum_createtable(L, 0, 1);  /* create metatable for CLIBS */
  lum_pushcfunction(L, gctm);
  lum_setfield(L, -2, "__gc");  /* set finalizer for CLIBS table */
  lum_setmetatable(L, -2);
}


LUMMOD_API int lumopen_package (lum_State *L) {
  createclibstable(L);
  lumL_newlib(L, pk_funcs);  /* create 'package' table */
  createsearcherstable(L);
  /* set paths */
  setpath(L, "path", LUM_PATH_VAR, LUM_PATH_DEFAULT);
  setpath(L, "cpath", LUM_CPATH_VAR, LUM_CPATH_DEFAULT);
  /* store config information */
  lum_pushliteral(L, LUM_DIRSEP "\n" LUM_PATH_SEP "\n" LUM_PATH_MARK "\n"
                     LUM_EXEC_DIR "\n" LUM_IGMARK "\n");
  lum_setfield(L, -2, "config");
  /* set field 'loaded' */
  lumL_getsubtable(L, LUM_REGISTRYINDEX, LUM_LOADED_TABLE);
  lum_setfield(L, -2, "loaded");
  /* set field 'preload' */
  lumL_getsubtable(L, LUM_REGISTRYINDEX, LUM_PRELOAD_TABLE);
  lum_setfield(L, -2, "preload");
  lum_pushglobaltable(L);
  lum_pushvalue(L, -2);  /* set 'package' as upvalue for next lib */
  lumL_setfuncs(L, ll_funcs, 1);  /* open lib into global table */
  lum_pop(L, 1);  /* pop global table */
  return 1;  /* return 'package' table */
}

