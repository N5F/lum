/*
** $Id: ltm.h $
** Tag methods
** See Copyright Notice in lum.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/
typedef enum {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,  /* last tag method with fast access */
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_MOD,
  TM_POW,
  TM_DIV,
  TM_IDIV,
  TM_BAND,
  TM_BOR,
  TM_BXOR,
  TM_SHL,
  TM_SHR,
  TM_UNM,
  TM_BNOT,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_CLOSE,
  TM_N		/* number of elements in the enum */
} TMS;


/*
** Mask with 1 in all fast-access methods. A 1 in any of these bits
** in the flag of a (meta)table means the metatable does not have the
** corresponding metamethod field. (Bit 6 of the flag indicates that
** the table is using the dummy node; bit 7 is used for 'isrealasize'.)
*/
#define maskflags	cast_byte(~(~0u << (TM_EQ + 1)))


/*
** Test whether there is no tagmethod.
** (Because tagmethods use raw accesses, the result may be an "empty" nil.)
*/
#define notm(tm)	ttisnil(tm)

#define checknoTM(mt,e)	((mt) == NULL || (mt)->flags & (1u<<(e)))

#define gfasttm(g,mt,e)  \
  (checknoTM(mt, e) ? NULL : lumT_gettm(mt, e, (g)->tmname[e]))

#define fasttm(l,mt,e)	gfasttm(G(l), mt, e)

#define ttypename(x)	lumT_typenames_[(x) + 1]

LUMI_DDEC(const char *const lumT_typenames_[LUM_TOTALTYPES];)


LUMI_FUNC const char *lumT_objtypename (lum_State *L, const TValue *o);

LUMI_FUNC const TValue *lumT_gettm (Table *events, TMS event, TString *ename);
LUMI_FUNC const TValue *lumT_gettmbyobj (lum_State *L, const TValue *o,
                                                       TMS event);
LUMI_FUNC void lumT_init (lum_State *L);

LUMI_FUNC void lumT_callTM (lum_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
LUMI_FUNC lu_byte lumT_callTMres (lum_State *L, const TValue *f,
                               const TValue *p1, const TValue *p2, StkId p3);
LUMI_FUNC void lumT_trybinTM (lum_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUMI_FUNC void lumT_tryconcatTM (lum_State *L);
LUMI_FUNC void lumT_trybinassocTM (lum_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
LUMI_FUNC void lumT_trybiniTM (lum_State *L, const TValue *p1, lum_Integer i2,
                               int inv, StkId res, TMS event);
LUMI_FUNC int lumT_callorderTM (lum_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
LUMI_FUNC int lumT_callorderiTM (lum_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

LUMI_FUNC void lumT_adjustvarargs (lum_State *L, int nfixparams,
                                   struct CallInfo *ci, const Proto *p);
LUMI_FUNC void lumT_getvarargs (lum_State *L, struct CallInfo *ci,
                                              StkId where, int wanted);


#endif
