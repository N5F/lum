/*
** $Id: ltable.h $
** Lum tables (hash)
** See Copyright Notice in lum.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


#define gnode(t,i)	(&(t)->node[i])
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->u.next)


/*
** Clear all bits of fast-access metamethods, which means that the table
** may have any of these metamethods. (First access that fails after the
** clearing will set the bit again.)
*/
#define invalidateTMcache(t)	((t)->flags &= cast_byte(~maskflags))


/*
** Bit BITDUMMY set in 'flags' means the table is using the dummy node
** for its hash part.
*/

#define BITDUMMY		(1 << 6)
#define NOTBITDUMMY		cast_byte(~BITDUMMY)
#define isdummy(t)		((t)->flags & BITDUMMY)

#define setnodummy(t)		((t)->flags &= NOTBITDUMMY)
#define setdummy(t)		((t)->flags |= BITDUMMY)



/* allocated size for hash nodes */
#define allocsizenode(t)	(isdummy(t) ? 0 : sizenode(t))


/* returns the Node, given the value of a table entry */
#define nodefromval(v)	cast(Node *, (v))



#define lumH_fastgeti(t,k,res,tag) \
  { Table *h = t; lum_Unsigned u = l_castS2U(k) - 1u; \
    if ((u < h->asize)) { \
      tag = *getArrTag(h, u); \
      if (!tagisempty(tag)) { farr2val(h, u, tag, res); }} \
    else { tag = lumH_getint(h, (k), res); }}


#define lumH_fastseti(t,k,val,hres) \
  { Table *h = t; lum_Unsigned u = l_castS2U(k) - 1u; \
    if ((u < h->asize)) { \
      lu_byte *tag = getArrTag(h, u); \
      if (checknoTM(h->metatable, TM_NEWINDEX) || !tagisempty(*tag)) \
        { fval2arr(h, u, tag, val); hres = HOK; } \
      else hres = ~cast_int(u); } \
    else { hres = lumH_psetint(h, k, val); }}


/* results from pset */
#define HOK		0
#define HNOTFOUND	1
#define HNOTATABLE	2
#define HFIRSTNODE	3

/*
** 'lumH_get*' operations set 'res', unless the value is absent, and
** return the tag of the result.
** The 'lumH_pset*' (pre-set) operations set the given value and return
** HOK, unless the original value is absent. In that case, if the key
** is really absent, they return HNOTFOUND. Otherwise, if there is a
** slot with that key but with no value, 'lumH_pset*' return an encoding
** of where the key is (usually called 'hres'). (pset cannot set that
** value because there might be a metamethod.) If the slot is in the
** hash part, the encoding is (HFIRSTNODE + hash index); if the slot is
** in the array part, the encoding is (~array index), a negative value.
** The value HNOTATABLE is used by the fast macros to signal that the
** value being indexed is not a table.
** (The size for the array part is limited by the maximum power of two
** that fits in an unsigned integer; that is INT_MAX+1. So, the C-index
** ranges from 0, which encodes to -1, to INT_MAX, which encodes to
** INT_MIN. The size of the hash part is limited by the maximum power of
** two that fits in a signed integer; that is (INT_MAX+1)/2. So, it is
** safe to add HFIRSTNODE to any index there.)
*/


/*
** The array part of a table is represented by an inverted array of
** values followed by an array of tags, to avoid wasting space with
** padding. In between them there is an unsigned int, explained later.
** The 'array' pointer points between the two arrays, so that values are
** indexed with negative indices and tags with non-negative indices.

             Values                              Tags
  --------------------------------------------------------
  ...  |   Value 1     |   Value 0     |unsigned|0|1|...
  --------------------------------------------------------
                                       ^ t->array

** All accesses to 't->array' should be through the macros 'getArrTag'
** and 'getArrVal'.
*/

/* Computes the address of the tag for the abstract C-index 'k' */
#define getArrTag(t,k)	(cast(lu_byte*, (t)->array) + sizeof(unsigned) + (k))

/* Computes the address of the value for the abstract C-index 'k' */
#define getArrVal(t,k)	((t)->array - 1 - (k))


/*
** The unsigned between the two arrays is used as a hint for #t;
** see lumH_getn. It is stored there to avoid wasting space in
** the structure Table for tables with no array part.
*/
#define lenhint(t)	cast(unsigned*, (t)->array)


/*
** Move TValues to/from arrays, using C indices
*/
#define arr2obj(h,k,val)  \
  ((val)->tt_ = *getArrTag(h,(k)), (val)->value_ = *getArrVal(h,(k)))

#define obj2arr(h,k,val)  \
  (*getArrTag(h,(k)) = (val)->tt_, *getArrVal(h,(k)) = (val)->value_)


/*
** Often, we need to check the tag of a value before moving it. The
** following macros also move TValues to/from arrays, but receive the
** precomputed tag value or address as an extra argument.
*/
#define farr2val(h,k,tag,res)  \
  ((res)->tt_ = tag, (res)->value_ = *getArrVal(h,(k)))

#define fval2arr(h,k,tag,val)  \
  (*tag = (val)->tt_, *getArrVal(h,(k)) = (val)->value_)


LUMI_FUNC lu_byte lumH_get (Table *t, const TValue *key, TValue *res);
LUMI_FUNC lu_byte lumH_getshortstr (Table *t, TString *key, TValue *res);
LUMI_FUNC lu_byte lumH_getstr (Table *t, TString *key, TValue *res);
LUMI_FUNC lu_byte lumH_getint (Table *t, lum_Integer key, TValue *res);

/* Special get for metamethods */
LUMI_FUNC const TValue *lumH_Hgetshortstr (Table *t, TString *key);

LUMI_FUNC int lumH_psetint (Table *t, lum_Integer key, TValue *val);
LUMI_FUNC int lumH_psetshortstr (Table *t, TString *key, TValue *val);
LUMI_FUNC int lumH_psetstr (Table *t, TString *key, TValue *val);
LUMI_FUNC int lumH_pset (Table *t, const TValue *key, TValue *val);

LUMI_FUNC void lumH_setint (lum_State *L, Table *t, lum_Integer key,
                                                    TValue *value);
LUMI_FUNC void lumH_set (lum_State *L, Table *t, const TValue *key,
                                                 TValue *value);

LUMI_FUNC void lumH_finishset (lum_State *L, Table *t, const TValue *key,
                                              TValue *value, int hres);
LUMI_FUNC Table *lumH_new (lum_State *L);
LUMI_FUNC void lumH_resize (lum_State *L, Table *t, unsigned nasize,
                                                    unsigned nhsize);
LUMI_FUNC void lumH_resizearray (lum_State *L, Table *t, unsigned nasize);
LUMI_FUNC lu_mem lumH_size (Table *t);
LUMI_FUNC void lumH_free (lum_State *L, Table *t);
LUMI_FUNC int lumH_next (lum_State *L, Table *t, StkId key);
LUMI_FUNC lum_Unsigned lumH_getn (Table *t);


#if defined(LUM_DEBUG)
LUMI_FUNC Node *lumH_mainposition (const Table *t, const TValue *key);
#endif


#endif
