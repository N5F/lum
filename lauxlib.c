/*
** $Id: lauxlib.c $
** Auxiliary functions for building Lum libraries
** See Copyright Notice in lum.h
*/

#define lauxlib_c
#define LUM_LIB

#include "lprefix.h"


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
** This file uses only the official API of Lum.
** Any function declared here could be written as an application function.
*/

#include "lum.h"

#include "lauxlib.h"
#include "llimits.h"


/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	10	/* size of the first part of the stack */
#define LEVELS2	11	/* size of the second part of the stack */



/*
** Search for 'objidx' in table at index -1. ('objidx' must be an
** absolute index.) Return 1 + string at top if it found a good name.
*/
static int findfield (lum_State *L, int objidx, int level) {
  if (level == 0 || !lum_istable(L, -1))
    return 0;  /* not found */
  lum_pushnil(L);  /* start 'next' loop */
  while (lum_next(L, -2)) {  /* for each pair in table */
    if (lum_type(L, -2) == LUM_TSTRING) {  /* ignore non-string keys */
      if (lum_rawequal(L, objidx, -1)) {  /* found object? */
        lum_pop(L, 1);  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        /* stack: lib_name, lib_table, field_name (top) */
        lum_pushliteral(L, ".");  /* place '.' between the two names */
        lum_replace(L, -3);  /* (in the slot occupied by table) */
        lum_concat(L, 3);  /* lib_name.field_name */
        return 1;
      }
    }
    lum_pop(L, 1);  /* remove value */
  }
  return 0;  /* not found */
}


/*
** Search for a name for a function in all loaded modules
*/
static int pushglobalfuncname (lum_State *L, lum_Debug *ar) {
  int top = lum_gettop(L);
  lum_getinfo(L, "f", ar);  /* push function */
  lum_getfield(L, LUM_REGISTRYINDEX, LUM_LOADED_TABLE);
  lumL_checkstack(L, 6, "not enough stack");  /* slots for 'findfield' */
  if (findfield(L, top + 1, 2)) {
    const char *name = lum_tostring(L, -1);
    if (strncmp(name, LUM_GNAME ".", 3) == 0) {  /* name start with '_G.'? */
      lum_pushstring(L, name + 3);  /* push name without prefix */
      lum_remove(L, -2);  /* remove original name */
    }
    lum_copy(L, -1, top + 1);  /* copy name to proper place */
    lum_settop(L, top + 1);  /* remove table "loaded" and name copy */
    return 1;
  }
  else {
    lum_settop(L, top);  /* remove function and global table */
    return 0;
  }
}


static void pushfuncname (lum_State *L, lum_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {  /* try first a global name */
    lum_pushfstring(L, "function '%s'", lum_tostring(L, -1));
    lum_remove(L, -2);  /* remove name */
  }
  else if (*ar->namewhat != '\0')  /* is there a name from code? */
    lum_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  /* use it */
  else if (*ar->what == 'm')  /* main? */
      lum_pushliteral(L, "main chunk");
  else if (*ar->what != 'C')  /* for Lum functions, use <file:line> */
    lum_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
  else  /* nothing left... */
    lum_pushliteral(L, "?");
}


static int lastlevel (lum_State *L) {
  lum_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (lum_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (lum_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


LUMLIB_API void lumL_traceback (lum_State *L, lum_State *L1,
                                const char *msg, int level) {
  lumL_Buffer b;
  lum_Debug ar;
  int last = lastlevel(L1);
  int limit2show = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  lumL_buffinit(L, &b);
  if (msg) {
    lumL_addstring(&b, msg);
    lumL_addchar(&b, '\n');
  }
  lumL_addstring(&b, "stack traceback:");
  while (lum_getstack(L1, level++, &ar)) {
    if (limit2show-- == 0) {  /* too many levels? */
      int n = last - level - LEVELS2 + 1;  /* number of levels to skip */
      lum_pushfstring(L, "\n\t...\t(skipping %d levels)", n);
      lumL_addvalue(&b);  /* add warning about skip */
      level += n;  /* and skip to last levels */
    }
    else {
      lum_getinfo(L1, "Slnt", &ar);
      if (ar.currentline <= 0)
        lum_pushfstring(L, "\n\t%s: in ", ar.short_src);
      else
        lum_pushfstring(L, "\n\t%s:%d: in ", ar.short_src, ar.currentline);
      lumL_addvalue(&b);
      pushfuncname(L, &ar);
      lumL_addvalue(&b);
      if (ar.istailcall)
        lumL_addstring(&b, "\n\t(...tail calls...)");
    }
  }
  lumL_pushresult(&b);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

LUMLIB_API int lumL_argerror (lum_State *L, int arg, const char *extramsg) {
  lum_Debug ar;
  const char *argword;
  if (!lum_getstack(L, 0, &ar))  /* no stack frame? */
    return lumL_error(L, "bad argument #%d (%s)", arg, extramsg);
  lum_getinfo(L, "nt", &ar);
  if (arg <= ar.extraargs)  /* error in an extra argument? */
    argword =  "extra argument";
  else {
    arg -= ar.extraargs;  /* do not count extra arguments */
    if (strcmp(ar.namewhat, "method") == 0) {  /* colon syntax? */
      arg--;  /* do not count (extra) self argument */
      if (arg == 0)  /* error in self argument? */
        return lumL_error(L, "calling '%s' on bad self (%s)",
                               ar.name, extramsg);
      /* else go through; error in a regular argument */
    }
    argword = "argument";
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? lum_tostring(L, -1) : "?";
  return lumL_error(L, "bad %s #%d to '%s' (%s)",
                       argword, arg, ar.name, extramsg);
}


LUMLIB_API int lumL_typeerror (lum_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  /* name for the type of the actual argument */
  if (lumL_getmetafield(L, arg, "__name") == LUM_TSTRING)
    typearg = lum_tostring(L, -1);  /* use the given type name */
  else if (lum_type(L, arg) == LUM_TLIGHTUSERDATA)
    typearg = "light userdata";  /* special name for messages */
  else
    typearg = lumL_typename(L, arg);  /* standard name */
  msg = lum_pushfstring(L, "%s expected, got %s", tname, typearg);
  return lumL_argerror(L, arg, msg);
}


static void tag_error (lum_State *L, int arg, int tag) {
  lumL_typeerror(L, arg, lum_typename(L, tag));
}


/*
** The use of 'lum_pushfstring' ensures this function does not
** need reserved stack space when called.
*/
LUMLIB_API void lumL_where (lum_State *L, int level) {
  lum_Debug ar;
  if (lum_getstack(L, level, &ar)) {  /* check function at level */
    lum_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      lum_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  lum_pushfstring(L, "");  /* else, no information available... */
}


/*
** Again, the use of 'lum_pushvfstring' ensures this function does
** not need reserved stack space when called. (At worst, it generates
** a memory error instead of the given message.)
*/
LUMLIB_API int lumL_error (lum_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  lumL_where(L, 1);
  lum_pushvfstring(L, fmt, argp);
  va_end(argp);
  lum_concat(L, 2);
  return lum_error(L);
}


LUMLIB_API int lumL_fileresult (lum_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to Lum API may change this value */
  if (stat) {
    lum_pushboolean(L, 1);
    return 1;
  }
  else {
    const char *msg;
    lumL_pushfail(L);
    msg = (en != 0) ? strerror(en) : "(no extra info)";
    if (fname)
      lum_pushfstring(L, "%s: %s", fname, msg);
    else
      lum_pushstring(L, msg);
    lum_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)	/* { */

#if defined(LUM_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */


LUMLIB_API int lumL_execresult (lum_State *L, int stat) {
  if (stat != 0 && errno != 0)  /* error with an 'errno'? */
    return lumL_fileresult(L, 0, NULL);
  else {
    const char *what = "exit";  /* type of termination */
    l_inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      lum_pushboolean(L, 1);
    else
      lumL_pushfail(L);
    lum_pushstring(L, what);
    lum_pushinteger(L, stat);
    return 3;  /* return true/fail,what,code */
  }
}

/* }====================================================== */



/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

LUMLIB_API int lumL_newmetatable (lum_State *L, const char *tname) {
  if (lumL_getmetatable(L, tname) != LUM_TNIL)  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  lum_pop(L, 1);
  lum_createtable(L, 0, 2);  /* create metatable */
  lum_pushstring(L, tname);
  lum_setfield(L, -2, "__name");  /* metatable.__name = tname */
  lum_pushvalue(L, -1);
  lum_setfield(L, LUM_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}


LUMLIB_API void lumL_setmetatable (lum_State *L, const char *tname) {
  lumL_getmetatable(L, tname);
  lum_setmetatable(L, -2);
}


LUMLIB_API void *lumL_testudata (lum_State *L, int ud, const char *tname) {
  void *p = lum_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (lum_getmetatable(L, ud)) {  /* does it have a metatable? */
      lumL_getmetatable(L, tname);  /* get correct metatable */
      if (!lum_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      lum_pop(L, 2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}


LUMLIB_API void *lumL_checkudata (lum_State *L, int ud, const char *tname) {
  void *p = lumL_testudata(L, ud, tname);
  lumL_argexpected(L, p != NULL, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

LUMLIB_API int lumL_checkoption (lum_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? lumL_optstring(L, arg, def) :
                             lumL_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return lumL_argerror(L, arg,
                       lum_pushfstring(L, "invalid option '%s'", name));
}


/*
** Ensures the stack has at least 'space' extra slots, raising an error
** if it cannot fulfill the request. (The error handling needs a few
** extra slots to format the error message. In case of an error without
** this extra space, Lum will generate the same 'stack overflow' error,
** but without 'msg'.)
*/
LUMLIB_API void lumL_checkstack (lum_State *L, int space, const char *msg) {
  if (l_unlikely(!lum_checkstack(L, space))) {
    if (msg)
      lumL_error(L, "stack overflow (%s)", msg);
    else
      lumL_error(L, "stack overflow");
  }
}


LUMLIB_API void lumL_checktype (lum_State *L, int arg, int t) {
  if (l_unlikely(lum_type(L, arg) != t))
    tag_error(L, arg, t);
}


LUMLIB_API void lumL_checkany (lum_State *L, int arg) {
  if (l_unlikely(lum_type(L, arg) == LUM_TNONE))
    lumL_argerror(L, arg, "value expected");
}


LUMLIB_API const char *lumL_checklstring (lum_State *L, int arg, size_t *len) {
  const char *s = lum_tolstring(L, arg, len);
  if (l_unlikely(!s)) tag_error(L, arg, LUM_TSTRING);
  return s;
}


LUMLIB_API const char *lumL_optlstring (lum_State *L, int arg,
                                        const char *def, size_t *len) {
  if (lum_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return lumL_checklstring(L, arg, len);
}


LUMLIB_API lum_Number lumL_checknumber (lum_State *L, int arg) {
  int isnum;
  lum_Number d = lum_tonumberx(L, arg, &isnum);
  if (l_unlikely(!isnum))
    tag_error(L, arg, LUM_TNUMBER);
  return d;
}


LUMLIB_API lum_Number lumL_optnumber (lum_State *L, int arg, lum_Number def) {
  return lumL_opt(L, lumL_checknumber, arg, def);
}


static void interror (lum_State *L, int arg) {
  if (lum_isnumber(L, arg))
    lumL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, LUM_TNUMBER);
}


LUMLIB_API lum_Integer lumL_checkinteger (lum_State *L, int arg) {
  int isnum;
  lum_Integer d = lum_tointegerx(L, arg, &isnum);
  if (l_unlikely(!isnum)) {
    interror(L, arg);
  }
  return d;
}


LUMLIB_API lum_Integer lumL_optinteger (lum_State *L, int arg,
                                                      lum_Integer def) {
  return lumL_opt(L, lumL_checkinteger, arg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/* userdata to box arbitrary data */
typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;


/* Resize the buffer used by a box. Optimize for the common case of
** resizing to the old size. (For instance, __gc will resize the box
** to 0 even after it was closed. 'pushresult' may also resize it to a
** final size that is equal to the one set when the buffer was created.)
*/
static void *resizebox (lum_State *L, int idx, size_t newsize) {
  UBox *box = (UBox *)lum_touserdata(L, idx);
  if (box->bsize == newsize)  /* not changing size? */
    return box->box;  /* keep the buffer */
  else {
    void *ud;
    lum_Alloc allocf = lum_getallocf(L, &ud);
    void *temp = allocf(ud, box->box, box->bsize, newsize);
    if (l_unlikely(temp == NULL && newsize > 0)) {  /* allocation error? */
      lum_pushliteral(L, "not enough memory");
      lum_error(L);  /* raise a memory error */
    }
    box->box = temp;
    box->bsize = newsize;
    return temp;
  }
}


static int boxgc (lum_State *L) {
  resizebox(L, 1, 0);
  return 0;
}


static const lumL_Reg boxmt[] = {  /* box metamethods */
  {"__gc", boxgc},
  {"__close", boxgc},
  {NULL, NULL}
};


static void newbox (lum_State *L) {
  UBox *box = (UBox *)lum_newuserdatauv(L, sizeof(UBox), 0);
  box->box = NULL;
  box->bsize = 0;
  if (lumL_newmetatable(L, "_UBOX*"))  /* creating metatable? */
    lumL_setfuncs(L, boxmt, 0);  /* set its metamethods */
  lum_setmetatable(L, -2);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->init.b)


/*
** Whenever buffer is accessed, slot 'idx' must either be a box (which
** cannot be NULL) or it is a placeholder for the buffer.
*/
#define checkbufferlevel(B,idx)  \
  lum_assert(buffonstack(B) ? lum_touserdata(B->L, idx) != NULL  \
                            : lum_touserdata(B->L, idx) == (void*)B)


/*
** Compute new size for buffer 'B', enough to accommodate extra 'sz'
** bytes plus one for a terminating zero. (The test for "not big enough"
** also gets the case when the computation of 'newsize' overflows.)
*/
static size_t newbuffsize (lumL_Buffer *B, size_t sz) {
  size_t newsize = (B->size / 2) * 3;  /* buffer size * 1.5 */
  if (l_unlikely(sz > MAX_SIZE - B->n - 1))
    return cast_sizet(lumL_error(B->L, "resulting string too large"));
  if (newsize < B->n + sz + 1 || newsize > MAX_SIZE) {
    /* newsize was not big enough or too big */
    newsize = B->n + sz + 1;
  }
  return newsize;
}


/*
** Returns a pointer to a free area with at least 'sz' bytes in buffer
** 'B'. 'boxidx' is the relative position in the stack where is the
** buffer's box or its placeholder.
*/
static char *prepbuffsize (lumL_Buffer *B, size_t sz, int boxidx) {
  checkbufferlevel(B, boxidx);
  if (B->size - B->n >= sz)  /* enough space? */
    return B->b + B->n;
  else {
    lum_State *L = B->L;
    char *newbuff;
    size_t newsize = newbuffsize(B, sz);
    /* create larger buffer */
    if (buffonstack(B))  /* buffer already has a box? */
      newbuff = (char *)resizebox(L, boxidx, newsize);  /* resize it */
    else {  /* no box yet */
      lum_remove(L, boxidx);  /* remove placeholder */
      newbox(L);  /* create a new box */
      lum_insert(L, boxidx);  /* move box to its intended position */
      lum_toclose(L, boxidx);
      newbuff = (char *)resizebox(L, boxidx, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));  /* copy original content */
    }
    B->b = newbuff;
    B->size = newsize;
    return newbuff + B->n;
  }
}

/*
** returns a pointer to a free area with at least 'sz' bytes
*/
LUMLIB_API char *lumL_prepbuffsize (lumL_Buffer *B, size_t sz) {
  return prepbuffsize(B, sz, -1);
}


LUMLIB_API void lumL_addlstring (lumL_Buffer *B, const char *s, size_t l) {
  if (l > 0) {  /* avoid 'memcpy' when 's' can be NULL */
    char *b = prepbuffsize(B, l, -1);
    memcpy(b, s, l * sizeof(char));
    lumL_addsize(B, l);
  }
}


LUMLIB_API void lumL_addstring (lumL_Buffer *B, const char *s) {
  lumL_addlstring(B, s, strlen(s));
}


LUMLIB_API void lumL_pushresult (lumL_Buffer *B) {
  lum_State *L = B->L;
  checkbufferlevel(B, -1);
  if (!buffonstack(B))  /* using static buffer? */
    lum_pushlstring(L, B->b, B->n);  /* save result as regular string */
  else {  /* reuse buffer already allocated */
    UBox *box = (UBox *)lum_touserdata(L, -1);
    void *ud;
    lum_Alloc allocf = lum_getallocf(L, &ud);  /* function to free buffer */
    size_t len = B->n;  /* final string length */
    char *s;
    resizebox(L, -1, len + 1);  /* adjust box size to content size */
    s = (char*)box->box;  /* final buffer address */
    s[len] = '\0';  /* add ending zero */
    /* clear box, as Lum will take control of the buffer */
    box->bsize = 0;  box->box = NULL;
    lum_pushexternalstring(L, s, len, allocf, ud);
    lum_closeslot(L, -2);  /* close the box */
    lum_gc(L, LUM_GCSTEP, len);
  }
  lum_remove(L, -2);  /* remove box or placeholder from the stack */
}


LUMLIB_API void lumL_pushresultsize (lumL_Buffer *B, size_t sz) {
  lumL_addsize(B, sz);
  lumL_pushresult(B);
}


/*
** 'lumL_addvalue' is the only function in the Buffer system where the
** box (if existent) is not on the top of the stack. So, instead of
** calling 'lumL_addlstring', it replicates the code using -2 as the
** last argument to 'prepbuffsize', signaling that the box is (or will
** be) below the string being added to the buffer. (Box creation can
** trigger an emergency GC, so we should not remove the string from the
** stack before we have the space guaranteed.)
*/
LUMLIB_API void lumL_addvalue (lumL_Buffer *B) {
  lum_State *L = B->L;
  size_t len;
  const char *s = lum_tolstring(L, -1, &len);
  char *b = prepbuffsize(B, len, -2);
  memcpy(b, s, len * sizeof(char));
  lumL_addsize(B, len);
  lum_pop(L, 1);  /* pop string */
}


LUMLIB_API void lumL_buffinit (lum_State *L, lumL_Buffer *B) {
  B->L = L;
  B->b = B->init.b;
  B->n = 0;
  B->size = LUML_BUFFERSIZE;
  lum_pushlightuserdata(L, (void*)B);  /* push placeholder */
}


LUMLIB_API char *lumL_buffinitsize (lum_State *L, lumL_Buffer *B, size_t sz) {
  lumL_buffinit(L, B);
  return prepbuffsize(B, sz, -1);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/*
** The previously freed references form a linked list: t[1] is the index
** of a first free index, t[t[1]] is the index of the second element,
** etc. A zero signals the end of the list.
*/
LUMLIB_API int lumL_ref (lum_State *L, int t) {
  int ref;
  if (lum_isnil(L, -1)) {
    lum_pop(L, 1);  /* remove from stack */
    return LUM_REFNIL;  /* 'nil' has a unique fixed reference */
  }
  t = lum_absindex(L, t);
  if (lum_rawgeti(L, t, 1) == LUM_TNUMBER)  /* already initialized? */
    ref = (int)lum_tointeger(L, -1);  /* ref = t[1] */
  else {  /* first access */
    lum_assert(!lum_toboolean(L, -1));  /* must be nil or false */
    ref = 0;  /* list is empty */
    lum_pushinteger(L, 0);  /* initialize as an empty list */
    lum_rawseti(L, t, 1);  /* ref = t[1] = 0 */
  }
  lum_pop(L, 1);  /* remove element from stack */
  if (ref != 0) {  /* any free element? */
    lum_rawgeti(L, t, ref);  /* remove it from list */
    lum_rawseti(L, t, 1);  /* (t[1] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)lum_rawlen(L, t) + 1;  /* get a new reference */
  lum_rawseti(L, t, ref);
  return ref;
}


LUMLIB_API void lumL_unref (lum_State *L, int t, int ref) {
  if (ref >= 0) {
    t = lum_absindex(L, t);
    lum_rawgeti(L, t, 1);
    lum_assert(lum_isinteger(L, -1));
    lum_rawseti(L, t, ref);  /* t[ref] = t[1] */
    lum_pushinteger(L, ref);
    lum_rawseti(L, t, 1);  /* t[1] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  unsigned n;  /* number of pre-read characters */
  FILE *f;  /* file being read */
  char buff[BUFSIZ];  /* area for reading file */
} LoadF;


static const char *getF (lum_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (lum_State *L, const char *what, int fnameindex) {
  int err = errno;
  const char *filename = lum_tostring(L, fnameindex) + 1;
  if (err != 0)
    lum_pushfstring(L, "cannot %s %s: %s", what, filename, strerror(err));
  else
    lum_pushfstring(L, "cannot %s %s", what, filename);
  lum_remove(L, fnameindex);
  return LUM_ERRFILE;
}


/*
** Skip an optional BOM at the start of a stream. If there is an
** incomplete BOM (the first character is correct but the rest is
** not), returns the first character anyway to force an error
** (as no chunk can start with 0xEF).
*/
static int skipBOM (FILE *f) {
  int c = getc(f);  /* read first character */
  if (c == 0xEF && getc(f) == 0xBB && getc(f) == 0xBF)  /* correct BOM? */
    return getc(f);  /* ignore BOM and return next char */
  else  /* no (valid) BOM */
    return c;  /* return first character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (FILE *f, int *cp) {
  int c = *cp = skipBOM(f);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(f);
    } while (c != EOF && c != '\n');
    *cp = getc(f);  /* next character after comment, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


LUMLIB_API int lumL_loadfilex (lum_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = lum_gettop(L) + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    lum_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    lum_pushfstring(L, "@%s", filename);
    errno = 0;
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  lf.n = 0;
  if (skipcomment(lf.f, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add newline to correct line numbers */
  if (c == LUM_SIGNATURE[0]) {  /* binary file? */
    lf.n = 0;  /* remove possible newline */
    if (filename) {  /* "real" file? */
      errno = 0;
      lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
      if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
      skipcomment(lf.f, &c);  /* re-read initial portion */
    }
  }
  if (c != EOF)
    lf.buff[lf.n++] = cast_char(c);  /* 'c' is the first character */
  status = lum_load(L, getF, &lf, lum_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  errno = 0;  /* no useful error number until here */
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    lum_settop(L, fnameindex);  /* ignore results from 'lum_load' */
    return errfile(L, "read", fnameindex);
  }
  lum_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (lum_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


LUMLIB_API int lumL_loadbufferx (lum_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return lum_load(L, getS, &ls, name, mode);
}


LUMLIB_API int lumL_loadstring (lum_State *L, const char *s) {
  return lumL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



LUMLIB_API int lumL_getmetafield (lum_State *L, int obj, const char *event) {
  if (!lum_getmetatable(L, obj))  /* no metatable? */
    return LUM_TNIL;
  else {
    int tt;
    lum_pushstring(L, event);
    tt = lum_rawget(L, -2);
    if (tt == LUM_TNIL)  /* is metafield nil? */
      lum_pop(L, 2);  /* remove metatable and metafield */
    else
      lum_remove(L, -2);  /* remove only metatable */
    return tt;  /* return metafield type */
  }
}


LUMLIB_API int lumL_callmeta (lum_State *L, int obj, const char *event) {
  obj = lum_absindex(L, obj);
  if (lumL_getmetafield(L, obj, event) == LUM_TNIL)  /* no metafield? */
    return 0;
  lum_pushvalue(L, obj);
  lum_call(L, 1, 1);
  return 1;
}


LUMLIB_API lum_Integer lumL_len (lum_State *L, int idx) {
  lum_Integer l;
  int isnum;
  lum_len(L, idx);
  l = lum_tointegerx(L, -1, &isnum);
  if (l_unlikely(!isnum))
    lumL_error(L, "object length is not an integer");
  lum_pop(L, 1);  /* remove object */
  return l;
}


LUMLIB_API const char *lumL_tolstring (lum_State *L, int idx, size_t *len) {
  idx = lum_absindex(L,idx);
  if (lumL_callmeta(L, idx, "__tostring")) {  /* metafield? */
    if (!lum_isstring(L, -1))
      lumL_error(L, "'__tostring' must return a string");
  }
  else {
    switch (lum_type(L, idx)) {
      case LUM_TNUMBER: {
        char buff[LUM_N2SBUFFSZ];
        lum_numbertocstring(L, idx, buff);
        lum_pushstring(L, buff);
        break;
      }
      case LUM_TSTRING:
        lum_pushvalue(L, idx);
        break;
      case LUM_TBOOLEAN:
        lum_pushstring(L, (lum_toboolean(L, idx) ? "true" : "false"));
        break;
      case LUM_TNIL:
        lum_pushliteral(L, "nil");
        break;
      default: {
        int tt = lumL_getmetafield(L, idx, "__name");  /* try name */
        const char *kind = (tt == LUM_TSTRING) ? lum_tostring(L, -1) :
                                                 lumL_typename(L, idx);
        lum_pushfstring(L, "%s: %p", kind, lum_topointer(L, idx));
        if (tt != LUM_TNIL)
          lum_remove(L, -2);  /* remove '__name' */
        break;
      }
    }
  }
  return lum_tolstring(L, -1, len);
}


/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
LUMLIB_API void lumL_setfuncs (lum_State *L, const lumL_Reg *l, int nup) {
  lumL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    if (l->func == NULL)  /* placeholder? */
      lum_pushboolean(L, 0);
    else {
      int i;
      for (i = 0; i < nup; i++)  /* copy upvalues to the top */
        lum_pushvalue(L, -nup);
      lum_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    }
    lum_setfield(L, -(nup + 2), l->name);
  }
  lum_pop(L, nup);  /* remove upvalues */
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
LUMLIB_API int lumL_getsubtable (lum_State *L, int idx, const char *fname) {
  if (lum_getfield(L, idx, fname) == LUM_TTABLE)
    return 1;  /* table already there */
  else {
    lum_pop(L, 1);  /* remove previous result */
    idx = lum_absindex(L, idx);
    lum_newtable(L);
    lum_pushvalue(L, -1);  /* copy to be left at top */
    lum_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** Stripped-down 'require': After checking "loaded" table, calls 'openf'
** to open a module, registers the result in 'package.loaded' table and,
** if 'glb' is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
LUMLIB_API void lumL_requiref (lum_State *L, const char *modname,
                               lum_CFunction openf, int glb) {
  lumL_getsubtable(L, LUM_REGISTRYINDEX, LUM_LOADED_TABLE);
  lum_getfield(L, -1, modname);  /* LOADED[modname] */
  if (!lum_toboolean(L, -1)) {  /* package not already loaded? */
    lum_pop(L, 1);  /* remove field */
    lum_pushcfunction(L, openf);
    lum_pushstring(L, modname);  /* argument to open function */
    lum_call(L, 1, 1);  /* call 'openf' to open module */
    lum_pushvalue(L, -1);  /* make copy of module (call result) */
    lum_setfield(L, -3, modname);  /* LOADED[modname] = module */
  }
  lum_remove(L, -2);  /* remove LOADED table */
  if (glb) {
    lum_pushvalue(L, -1);  /* copy of module */
    lum_setglobal(L, modname);  /* _G[modname] = module */
  }
}


LUMLIB_API void lumL_addgsub (lumL_Buffer *b, const char *s,
                                     const char *p, const char *r) {
  const char *wild;
  size_t l = strlen(p);
  while ((wild = strstr(s, p)) != NULL) {
    lumL_addlstring(b, s, ct_diff2sz(wild - s));  /* push prefix */
    lumL_addstring(b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after 'p' */
  }
  lumL_addstring(b, s);  /* push last suffix */
}


LUMLIB_API const char *lumL_gsub (lum_State *L, const char *s,
                                  const char *p, const char *r) {
  lumL_Buffer b;
  lumL_buffinit(L, &b);
  lumL_addgsub(&b, s, p, r);
  lumL_pushresult(&b);
  return lum_tostring(L, -1);
}


static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}


/*
** Standard panic function just prints an error message. The test
** with 'lum_type' avoids possible memory errors in 'lum_tostring'.
*/
static int panic (lum_State *L) {
  const char *msg = (lum_type(L, -1) == LUM_TSTRING)
                  ? lum_tostring(L, -1)
                  : "error object is not a string";
  lum_writestringerror("PANIC: unprotected error in call to Lum API (%s)\n",
                        msg);
  return 0;  /* return to Lum to abort */
}


/*
** Warning functions:
** warnfoff: warning system is off
** warnfon: ready to start a new message
** warnfcont: previous message is to be continued
*/
static void warnfoff (void *ud, const char *message, int tocont);
static void warnfon (void *ud, const char *message, int tocont);
static void warnfcont (void *ud, const char *message, int tocont);


/*
** Check whether message is a control message. If so, execute the
** control or ignore it if unknown.
*/
static int checkcontrol (lum_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')  /* not a control message? */
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      lum_setwarnf(L, warnfoff, L);  /* turn warnings off */
    else if (strcmp(message, "on") == 0)
      lum_setwarnf(L, warnfon, L);   /* turn warnings on */
    return 1;  /* it was a control message */
  }
}


static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol((lum_State *)ud, message, tocont);
}


/*
** Writes the message and handle 'tocont', finishing the message
** if needed and setting the next warn function.
*/
static void warnfcont (void *ud, const char *message, int tocont) {
  lum_State *L = (lum_State *)ud;
  lum_writestringerror("%s", message);  /* write message */
  if (tocont)  /* not the last part? */
    lum_setwarnf(L, warnfcont, L);  /* to be continued */
  else {  /* last part */
    lum_writestringerror("%s", "\n");  /* finish message with end-of-line */
    lum_setwarnf(L, warnfon, L);  /* next call is a new message */
  }
}


static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol((lum_State *)ud, message, tocont))  /* control message? */
    return;  /* nothing else to be done */
  lum_writestringerror("%s", "Lum warning: ");  /* start a new warning */
  warnfcont(ud, message, tocont);  /* finish processing */
}



/*
** A function to compute an unsigned int with some level of
** randomness. Rely on Address Space Layout Randomization (if present)
** and the current time.
*/
#if !defined(lumi_makeseed)

#include <time.h>


/* Size for the buffer, in bytes */
#define BUFSEEDB	(sizeof(void*) + sizeof(time_t))

/* Size for the buffer in int's, rounded up */
#define BUFSEED		((BUFSEEDB + sizeof(int) - 1) / sizeof(int))

/*
** Copy the contents of variable 'v' into the buffer pointed by 'b'.
** (The '&b[0]' disguises 'b' to fix an absurd warning from clang.)
*/
#define addbuff(b,v)	(memcpy(&b[0], &(v), sizeof(v)), b += sizeof(v))


static unsigned int lumi_makeseed (void) {
  unsigned int buff[BUFSEED];
  unsigned int res;
  unsigned int i;
  time_t t = time(NULL);
  char *b = (char*)buff;
  addbuff(b, b);  /* local variable's address */
  addbuff(b, t);  /* time */
  /* fill (rare but possible) remain of the buffer with zeros */
  memset(b, 0, sizeof(buff) - BUFSEEDB);
  res = buff[0];
  for (i = 1; i < BUFSEED; i++)
    res ^= (res >> 3) + (res << 7) + buff[i];
  return res;
}

#endif


LUMLIB_API unsigned int lumL_makeseed (lum_State *L) {
  (void)L;  /* unused */
  return lumi_makeseed();
}


LUMLIB_API lum_State *lumL_newstate (void) {
  lum_State *L = lum_newstate(l_alloc, NULL, lumi_makeseed());
  if (l_likely(L)) {
    lum_atpanic(L, &panic);
    lum_setwarnf(L, warnfoff, L);  /* default is warnings off */
  }
  return L;
}


LUMLIB_API void lumL_checkversion_ (lum_State *L, lum_Number ver, size_t sz) {
  lum_Number v = lum_version(L);
  if (sz != LUML_NUMSIZES)  /* check numeric types */
    lumL_error(L, "core and library have incompatible numeric types");
  else if (v != ver)
    lumL_error(L, "version mismatch: app. needs %f, Lum core provides %f",
                  (LUMI_UACNUMBER)ver, (LUMI_UACNUMBER)v);
}

