/*
** $Id: ltests.h $
** Internal Header for Debugging of the Lum Implementation
** See Copyright Notice in lum.h
*/

#ifndef ltests_h
#define ltests_h


#include <stdio.h>
#include <stdlib.h>

/* test Lum with compatibility code */
#define LUM_COMPAT_MATHLIB
#define LUM_COMPAT_LT_LE


#define LUM_DEBUG


/* turn on assertions */
#define LUMI_ASSERT


/* to avoid warnings, and to make sure value is really unused */
#define UNUSED(x)       (x=0, (void)(x))


/* test for sizes in 'l_sprintf' (make sure whole buffer is available) */
#undef l_sprintf
#if !defined(LUM_USE_C89)
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), snprintf(s,sz,f,i))
#else
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), sprintf(s,f,i))
#endif


/* get a chance to test code without jump tables */
#define LUM_USE_JUMPTABLE	0


/* use 32-bit integers in random generator */
#define LUM_RAND32


/* test stack reallocation with strict address use */
#define LUMI_STRICT_ADDRESS	1


/* memory-allocator control variables */
typedef struct Memcontrol {
  int failnext;
  unsigned long numblocks;
  unsigned long total;
  unsigned long maxmem;
  unsigned long memlimit;
  unsigned long countlimit;
  unsigned long objcount[LUM_NUMTYPES];
} Memcontrol;

LUM_API Memcontrol l_memcontrol;


#define lumi_tracegc(L,f)		lumi_tracegctest(L, f)
LUMI_FUNC void lumi_tracegctest (lum_State *L, int first);


/*
** generic variable for debug tricks
*/
extern void *l_Trick;


/*
** Function to traverse and check all memory used by Lum
*/
LUMI_FUNC int lum_checkmemory (lum_State *L);

/*
** Function to print an object GC-friendly
*/
struct GCObject;
LUMI_FUNC void lum_printobj (lum_State *L, struct GCObject *o);


/*
** Function to print a value
*/
struct TValue;
LUMI_FUNC void lum_printvalue (struct TValue *v);

/*
** Function to print the stack
*/
LUMI_FUNC void lum_printstack (lum_State *L);


/* test for lock/unlock */

struct L_EXTRA { int lock; int *plock; };
#undef LUM_EXTRASPACE
#define LUM_EXTRASPACE	sizeof(struct L_EXTRA)
#define getlock(l)	cast(struct L_EXTRA*, lum_getextraspace(l))
#define lumi_userstateopen(l)  \
	(getlock(l)->lock = 0, getlock(l)->plock = &(getlock(l)->lock))
#define lumi_userstateclose(l)  \
  lum_assert(getlock(l)->lock == 1 && getlock(l)->plock == &(getlock(l)->lock))
#define lumi_userstatethread(l,l1) \
  lum_assert(getlock(l1)->plock == getlock(l)->plock)
#define lumi_userstatefree(l,l1) \
  lum_assert(getlock(l)->plock == getlock(l1)->plock)
#define lum_lock(l)     lum_assert((*getlock(l)->plock)++ == 0)
#define lum_unlock(l)   lum_assert(--(*getlock(l)->plock) == 0)



LUM_API int lumB_opentests (lum_State *L);

LUM_API void *debug_realloc (void *ud, void *block,
                             size_t osize, size_t nsize);

#if defined(lum_c)
#define lumL_newstate()  \
	lum_newstate(debug_realloc, &l_memcontrol, lumL_makeseed(NULL))
#define lumi_openlibs(L)  \
  {  lumL_openlibs(L); \
     lumL_requiref(L, "T", lumB_opentests, 1); \
     lum_pop(L, 1); }
#endif



/* change some sizes to give some bugs a chance */

#undef LUML_BUFFERSIZE
#define LUML_BUFFERSIZE		23
#define MINSTRTABSIZE		2
#define MAXIWTHABS		3

#define STRCACHE_N	23
#define STRCACHE_M	5

#undef LUMI_USER_ALIGNMENT_T
#define LUMI_USER_ALIGNMENT_T   union { char b[sizeof(void*) * 8]; }


/*
** This one is not compatible with tests for opcode optimizations,
** as it blocks some optimizations
#define MAXINDEXRK	0
*/


/*
** Reduce maximum stack size to make stack-overflow tests run faster.
** (But value is still large enough to overflow smaller integers.)
*/
#undef LUMI_MAXSTACK
#define LUMI_MAXSTACK   68000


/* test mode uses more stack space */
#undef LUMI_MAXCCALLS
#define LUMI_MAXCCALLS	180


/* force Lum to use its own implementations */
#undef lum_strx2number
#undef lum_number2strx


#endif

