/*
** $Id: lundump.h $
** load precompiled Lum chunks
** See Copyright Notice in lum.h
*/

#ifndef lundump_h
#define lundump_h

#include <limits.h>

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/* data to catch conversion errors */
#define LUMC_DATA	"\x19\x93\r\n\x1a\n"

#define LUMC_INT	0x5678
#define LUMC_NUM	cast_num(370.5)

/*
** Encode major-minor version in one byte, one nibble for each
*/
#define LUMC_VERSION	(LUM_VERSION_MAJOR_N*16+LUM_VERSION_MINOR_N)

#define LUMC_FORMAT	0	/* this is the official format */


/* load one chunk; from lundump.c */
LUMI_FUNC LClosure* lumU_undump (lum_State* L, ZIO* Z, const char* name,
                                               int fixed);

/* dump one chunk; from ldump.c */
LUMI_FUNC int lumU_dump (lum_State* L, const Proto* f, lum_Writer w,
                         void* data, int strip);

#endif
