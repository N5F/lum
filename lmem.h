/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lum.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lum.h"


#define lumM_error(L)	lumD_throw(L, LUM_ERRMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define lumM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

#define lumM_checksize(L,n,e)  \
	(lumM_testsize(n,e) ? lumM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
#define lumM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_int((MAX_SIZET/sizeof(t))))


/*
** Arrays of chars do not need any test
*/
#define lumM_reallocvchar(L,b,on,n)  \
  cast_charp(lumM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

#define lumM_freemem(L, b, s)	lumM_free_(L, (b), (s))
#define lumM_free(L, b)		lumM_free_(L, (b), sizeof(*(b)))
#define lumM_freearray(L, b, n)   lumM_free_(L, (b), (n)*sizeof(*(b)))

#define lumM_new(L,t)		cast(t*, lumM_malloc_(L, sizeof(t), 0))
#define lumM_newvector(L,n,t)  \
	cast(t*, lumM_malloc_(L, cast_sizet(n)*sizeof(t), 0))
#define lumM_newvectorchecked(L,n,t) \
  (lumM_checksize(L,n,sizeof(t)), lumM_newvector(L,n,t))

#define lumM_newobject(L,tag,s)	lumM_malloc_(L, (s), tag)

#define lumM_newblock(L, size)	lumM_newvector(L, size, char)

#define lumM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, lumM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         lumM_limitN(limit,t),e)))

#define lumM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, lumM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

#define lumM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, lumM_shrinkvector_(L, v, &(size), fs, sizeof(t))))

LUMI_FUNC l_noret lumM_toobig (lum_State *L);

/* not to be called directly */
LUMI_FUNC void *lumM_realloc_ (lum_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUMI_FUNC void *lumM_saferealloc_ (lum_State *L, void *block, size_t oldsize,
                                                              size_t size);
LUMI_FUNC void lumM_free_ (lum_State *L, void *block, size_t osize);
LUMI_FUNC void *lumM_growaux_ (lum_State *L, void *block, int nelems,
                               int *size, unsigned size_elem, int limit,
                               const char *what);
LUMI_FUNC void *lumM_shrinkvector_ (lum_State *L, void *block, int *nelem,
                                    int final_n, unsigned size_elem);
LUMI_FUNC void *lumM_malloc_ (lum_State *L, size_t size, int tag);

#endif

