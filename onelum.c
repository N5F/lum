/*
** Lum core, libraries, and interpreter in a single file.
** Compiling just this file generates a complete Lum stand-alone
** program:
**
** $ gcc -O2 -std=c99 -o lum onelum.c -lm
**
** or
**
** $ gcc -O2 -std=c89 -DLUM_USE_C89 -o lum onelum.c -lm
**
*/

/* default is to build the full interpreter */
#ifndef MAKE_LIB
#ifndef MAKE_LUMC
#ifndef MAKE_LUM
#define MAKE_LUM
#endif
#endif
#endif


/*
** Choose suitable platform-specific features. Default is no
** platform-specific features. Some of these options may need extra
** libraries such as -ldl -lreadline -lncurses
*/
#if 0
#define LUM_USE_LINUX
#define LUM_USE_MACOSX
#define LUM_USE_POSIX
#define LUM_ANSI
#endif


/* no need to change anything below this line ----------------------------- */

#include "lprefix.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* setup for lumconf.h */
#define LUM_CORE
#define LUM_LIB
#define ltable_c
#define lvm_c
#include "lumconf.h"

/* do not export internal symbols */
#undef LUMI_FUNC
#undef LUMI_DDEC
#undef LUMI_DDEF
#define LUMI_FUNC	static
#define LUMI_DDEC(def)	/* empty */
#define LUMI_DDEF	static

/* core -- used by all */
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"

/* auxiliary library -- used by all */
#include "lauxlib.c"

/* standard library  -- not used by lumc */
#ifndef MAKE_LUMC
#include "lbaselib.c"
#include "lcorolib.c"
#include "ldblib.c"
#include "liolib.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
#include "linit.c"
#endif

/* lum */
#ifdef MAKE_LUM
#include "lum.c"
#endif

/* lumc */
#ifdef MAKE_LUMC
#include "lumc.c"
#endif
