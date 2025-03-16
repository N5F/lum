/*
** $Id: ldebug.h $
** Auxiliary functions from Debug Interface module
** See Copyright Notice in lum.h
*/

#ifndef ldebug_h
#define ldebug_h


#include "lstate.h"


#define pcRel(pc, p)	(cast_int((pc) - (p)->code) - 1)


/* Active Lum function (given call info) */
#define ci_func(ci)		(clLvalue(s2v((ci)->func.p)))


#define resethookcount(L)	(L->hookcount = L->basehookcount)

/*
** mark for entries in 'lineinfo' array that has absolute information in
** 'abslineinfo' array
*/
#define ABSLINEINFO	(-0x80)


/*
** MAXimum number of successive Instructions WiTHout ABSolute line
** information. (A power of two allows fast divisions.)
*/
#if !defined(MAXIWTHABS)
#define MAXIWTHABS	128
#endif


LUMI_FUNC int lumG_getfuncline (const Proto *f, int pc);
LUMI_FUNC const char *lumG_findlocal (lum_State *L, CallInfo *ci, int n,
                                                    StkId *pos);
LUMI_FUNC l_noret lumG_typeerror (lum_State *L, const TValue *o,
                                                const char *opname);
LUMI_FUNC l_noret lumG_callerror (lum_State *L, const TValue *o);
LUMI_FUNC l_noret lumG_forerror (lum_State *L, const TValue *o,
                                               const char *what);
LUMI_FUNC l_noret lumG_concaterror (lum_State *L, const TValue *p1,
                                                  const TValue *p2);
LUMI_FUNC l_noret lumG_opinterror (lum_State *L, const TValue *p1,
                                                 const TValue *p2,
                                                 const char *msg);
LUMI_FUNC l_noret lumG_tointerror (lum_State *L, const TValue *p1,
                                                 const TValue *p2);
LUMI_FUNC l_noret lumG_ordererror (lum_State *L, const TValue *p1,
                                                 const TValue *p2);
LUMI_FUNC l_noret lumG_runerror (lum_State *L, const char *fmt, ...);
LUMI_FUNC const char *lumG_addinfo (lum_State *L, const char *msg,
                                                  TString *src, int line);
LUMI_FUNC l_noret lumG_errormsg (lum_State *L);
LUMI_FUNC int lumG_traceexec (lum_State *L, const Instruction *pc);
LUMI_FUNC int lumG_tracecall (lum_State *L);


#endif
