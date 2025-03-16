/*
** $Id: lstring.h $
** String table (keep all strings handled by Lum)
** See Copyright Notice in lum.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"


/*
** Maximum length for short strings, that is, strings that are
** internalized. (Cannot be smaller than reserved words or tags for
** metamethods, as these strings must be internalized;
** #("function") = 8, #("__newindex") = 10.)
*/
#if !defined(LUMI_MAXSHORTLEN)
#define LUMI_MAXSHORTLEN	40
#endif


/*
** Size of a short TString: Size of the header plus space for the string
** itself (including final '\0').
*/
#define sizestrshr(l)  \
	(offsetof(TString, contents) + ((l) + 1) * sizeof(char))


#define lumS_newliteral(L, s)	(lumS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	(strisshr(s) && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUM_VSHRSTR, (a) == (b))


LUMI_FUNC unsigned lumS_hash (const char *str, size_t l, unsigned seed);
LUMI_FUNC unsigned lumS_hashlongstr (TString *ts);
LUMI_FUNC int lumS_eqlngstr (TString *a, TString *b);
LUMI_FUNC void lumS_resize (lum_State *L, int newsize);
LUMI_FUNC void lumS_clearcache (global_State *g);
LUMI_FUNC void lumS_init (lum_State *L);
LUMI_FUNC void lumS_remove (lum_State *L, TString *ts);
LUMI_FUNC Udata *lumS_newudata (lum_State *L, size_t s,
                                              unsigned short nuvalue);
LUMI_FUNC TString *lumS_newlstr (lum_State *L, const char *str, size_t l);
LUMI_FUNC TString *lumS_new (lum_State *L, const char *str);
LUMI_FUNC TString *lumS_createlngstrobj (lum_State *L, size_t l);
LUMI_FUNC TString *lumS_newextlstr (lum_State *L,
		const char *s, size_t len, lum_Alloc falloc, void *ud);
LUMI_FUNC size_t lumS_sizelngstr (size_t len, int kind);

#endif
