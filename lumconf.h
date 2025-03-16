/*
** $Id: lumconf.h $
** Configuration file for Lum
** See Copyright Notice in lum.h
*/


#ifndef lumconf_h
#define lumconf_h

#include <limits.h>
#include <stddef.h>


/*
** ===================================================================
** General Configuration File for Lum
**
** Some definitions here can be changed externally, through the compiler
** (e.g., with '-D' options): They are commented out or protected
** by '#if !defined' guards. However, several other definitions
** should be changed directly here, either because they affect the
** Lum ABI (by making the changes here, you ensure that all software
** connected to Lum, such as C libraries, will be compiled with the same
** configuration); or because they are seldom changed.
**
** Search for "@@" to find all configurable definitions.
** ===================================================================
*/


/*
** {====================================================================
** System Configuration: macros to adapt (if needed) Lum to some
** particular platform, for instance restricting it to C89.
** =====================================================================
*/

/*
@@ LUM_USE_C89 controls the use of non-ISO-C89 features.
** Define it if you want Lum to avoid the use of a few C99 features
** or Windows-specific features on Windows.
*/
/* #define LUM_USE_C89 */


/*
** By default, Lum on Windows use (some) specific Windows features
*/
#if !defined(LUM_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#define LUM_USE_WINDOWS  /* enable goodies for regular Windows */
#endif


#if defined(LUM_USE_WINDOWS)
#define LUM_DL_DLL	/* enable support for DLL */
#define LUM_USE_C89	/* broadly, Windows is C89 */
#endif


/*
** When Posix DLL ('LUM_USE_DLOPEN') is enabled, the Lum stand-alone
** application will try to dynamically link a 'readline' facility
** for its REPL.  In that case, LUM_READLINELIB is the name of the
** library it will look for those facilities.  If lum.c cannot open
** the specified library, it will generate a warning and then run
** without 'readline'.  If that macro is not defined, lum.c will not
** use 'readline'.
*/
#if defined(LUM_USE_LINUX)
#define LUM_USE_POSIX
#define LUM_USE_DLOPEN		/* needs an extra library: -ldl */
#define LUM_READLINELIB		"libreadline.so"
#endif


#if defined(LUM_USE_MACOSX)
#define LUM_USE_POSIX
#define LUM_USE_DLOPEN		/* MacOS does not need -ldl */
#define LUM_READLINELIB		"libedit.dylib"
#endif


#if defined(LUM_USE_IOS)
#define LUM_USE_POSIX
#define LUM_USE_DLOPEN
#endif


#if defined(LUM_USE_C89) && defined(LUM_USE_POSIX)
#error "Posix is not compatible with C89"
#endif


/*
@@ LUMI_IS32INT is true iff 'int' has (at least) 32 bits.
*/
#define LUMI_IS32INT	((UINT_MAX >> 30) >= 3)

/* }================================================================== */



/*
** {==================================================================
** Configuration for Number types. These options should not be
** set externally, because any other code connected to Lum must
** use the same configuration.
** ===================================================================
*/

/*
@@ LUM_INT_TYPE defines the type for Lum integers.
@@ LUM_FLOAT_TYPE defines the type for Lum floats.
** Lum should work fine with any mix of these options supported
** by your C compiler. The usual configurations are 64-bit integers
** and 'double' (the default), 32-bit integers and 'float' (for
** restricted platforms), and 'long'/'double' (for C compilers not
** compliant with C99, which may not have support for 'long long').
*/

/* predefined options for LUM_INT_TYPE */
#define LUM_INT_INT		1
#define LUM_INT_LONG		2
#define LUM_INT_LONGLONG	3

/* predefined options for LUM_FLOAT_TYPE */
#define LUM_FLOAT_FLOAT		1
#define LUM_FLOAT_DOUBLE	2
#define LUM_FLOAT_LONGDOUBLE	3


/* Default configuration ('long long' and 'double', for 64-bit Lum) */
#define LUM_INT_DEFAULT		LUM_INT_LONGLONG
#define LUM_FLOAT_DEFAULT	LUM_FLOAT_DOUBLE


/*
@@ LUM_32BITS enables Lum with 32-bit integers and 32-bit floats.
*/
#define LUM_32BITS	0


/*
@@ LUM_C89_NUMBERS ensures that Lum uses the largest types available for
** C89 ('long' and 'double'); Windows always has '__int64', so it does
** not need to use this case.
*/
#if defined(LUM_USE_C89) && !defined(LUM_USE_WINDOWS)
#define LUM_C89_NUMBERS		1
#else
#define LUM_C89_NUMBERS		0
#endif


#if LUM_32BITS		/* { */
/*
** 32-bit integers and 'float'
*/
#if LUMI_IS32INT  /* use 'int' if big enough */
#define LUM_INT_TYPE	LUM_INT_INT
#else  /* otherwise use 'long' */
#define LUM_INT_TYPE	LUM_INT_LONG
#endif
#define LUM_FLOAT_TYPE	LUM_FLOAT_FLOAT

#elif LUM_C89_NUMBERS	/* }{ */
/*
** largest types available for C89 ('long' and 'double')
*/
#define LUM_INT_TYPE	LUM_INT_LONG
#define LUM_FLOAT_TYPE	LUM_FLOAT_DOUBLE

#else		/* }{ */
/* use defaults */

#define LUM_INT_TYPE	LUM_INT_DEFAULT
#define LUM_FLOAT_TYPE	LUM_FLOAT_DEFAULT

#endif				/* } */


/* }================================================================== */



/*
** {==================================================================
** Configuration for Paths.
** ===================================================================
*/

/*
** LUM_PATH_SEP is the character that separates templates in a path.
** LUM_PATH_MARK is the string that marks the substitution points in a
** template.
** LUM_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
*/
#define LUM_PATH_SEP            ";"
#define LUM_PATH_MARK           "?"
#define LUM_EXEC_DIR            "!"


/*
@@ LUM_PATH_DEFAULT is the default path that Lum uses to look for
** Lum libraries.
@@ LUM_CPATH_DEFAULT is the default path that Lum uses to look for
** C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/

#define LUM_VDIR	LUM_VERSION_MAJOR "." LUM_VERSION_MINOR
#if defined(_WIN32)	/* { */
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define LUM_LDIR	"!\\lum\\"
#define LUM_CDIR	"!\\"
#define LUM_SHRDIR	"!\\..\\share\\lum\\" LUM_VDIR "\\"

#if !defined(LUM_PATH_DEFAULT)
#define LUM_PATH_DEFAULT  \
		LUM_LDIR"?.lum;"  LUM_LDIR"?\\init.lum;" \
		LUM_CDIR"?.lum;"  LUM_CDIR"?\\init.lum;" \
		LUM_SHRDIR"?.lum;" LUM_SHRDIR"?\\init.lum;" \
		".\\?.lum;" ".\\?\\init.lum"
#endif

#if !defined(LUM_CPATH_DEFAULT)
#define LUM_CPATH_DEFAULT \
		LUM_CDIR"?.dll;" \
		LUM_CDIR"..\\lib\\lum\\" LUM_VDIR "\\?.dll;" \
		LUM_CDIR"loadall.dll;" ".\\?.dll"
#endif

#else			/* }{ */

#define LUM_ROOT	"/usr/local/"
#define LUM_LDIR	LUM_ROOT "share/lum/" LUM_VDIR "/"
#define LUM_CDIR	LUM_ROOT "lib/lum/" LUM_VDIR "/"

#if !defined(LUM_PATH_DEFAULT)
#define LUM_PATH_DEFAULT  \
		LUM_LDIR"?.lum;"  LUM_LDIR"?/init.lum;" \
		LUM_CDIR"?.lum;"  LUM_CDIR"?/init.lum;" \
		"./?.lum;" "./?/init.lum"
#endif

#if !defined(LUM_CPATH_DEFAULT)
#define LUM_CPATH_DEFAULT \
		LUM_CDIR"?.so;" LUM_CDIR"loadall.so;" "./?.so"
#endif

#endif			/* } */


/*
@@ LUM_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Lum automatically uses "\".)
*/
#if !defined(LUM_DIRSEP)

#if defined(_WIN32)
#define LUM_DIRSEP	"\\"
#else
#define LUM_DIRSEP	"/"
#endif

#endif


/*
** LUM_IGMARK is a mark to ignore all after it when building the
** module name (e.g., used to build the lumopen_ function name).
** Typically, the suffix after the mark is the module version,
** as in "mod-v1.2.so".
*/
#define LUM_IGMARK		"-"

/* }================================================================== */


/*
** {==================================================================
** Marks for exported symbols in the C code
** ===================================================================
*/

/*
@@ LUM_API is a mark for all core API functions.
@@ LUMLIB_API is a mark for all auxiliary library functions.
@@ LUMMOD_API is a mark for all standard library opening functions.
** CHANGE them if you need to define those functions in some special way.
** For instance, if you want to create one Windows DLL with the core and
** the libraries, you may want to use the following definition (define
** LUM_BUILD_AS_DLL to get it).
*/
#if defined(LUM_BUILD_AS_DLL)	/* { */

#if defined(LUM_CORE) || defined(LUM_LIB)	/* { */
#define LUM_API __declspec(dllexport)
#else						/* }{ */
#define LUM_API __declspec(dllimport)
#endif						/* } */

#else				/* }{ */

#define LUM_API		extern

#endif				/* } */


/*
** More often than not the libs go together with the core.
*/
#define LUMLIB_API	LUM_API
#define LUMMOD_API	LUM_API


/*
@@ LUMI_FUNC is a mark for all extern functions that are not to be
** exported to outside modules.
@@ LUMI_DDEF and LUMI_DDEC are marks for all extern (const) variables,
** none of which to be exported to outside modules (LUMI_DDEF for
** definitions and LUMI_DDEC for declarations).
** CHANGE them if you need to mark them in some special way. Elf/gcc
** (versions 3.2 and later) mark them as "hidden" to optimize access
** when Lum is compiled as a shared library. Not all elf targets support
** this attribute. Unfortunately, gcc does not offer a way to check
** whether the target offers that support, and those without support
** give a warning about it. To avoid these warnings, change to the
** default definition.
*/
#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
    defined(__ELF__)		/* { */
#define LUMI_FUNC	__attribute__((visibility("internal"))) extern
#else				/* }{ */
#define LUMI_FUNC	extern
#endif				/* } */

#define LUMI_DDEC(dec)	LUMI_FUNC dec
#define LUMI_DDEF	/* empty */

/* }================================================================== */


/*
** {==================================================================
** Compatibility with previous versions
** ===================================================================
*/

/*
@@ LUM_COMPAT_5_3 controls other macros for compatibility with Lum 5.3.
** You can define it to get all options, or change specific options
** to fit your specific needs.
*/
#if defined(LUM_COMPAT_5_3)	/* { */

/*
@@ LUM_COMPAT_MATHLIB controls the presence of several deprecated
** functions in the mathematical library.
** (These functions were already officially removed in 5.3;
** nevertheless they are still available here.)
*/
#define LUM_COMPAT_MATHLIB

/*
@@ LUM_COMPAT_APIINTCASTS controls the presence of macros for
** manipulating other integer types (lum_pushunsigned, lum_tounsigned,
** lumL_checkint, lumL_checklong, etc.)
** (These macros were also officially removed in 5.3, but they are still
** available here.)
*/
#define LUM_COMPAT_APIINTCASTS


/*
@@ LUM_COMPAT_LT_LE controls the emulation of the '__le' metamethod
** using '__lt'.
*/
#define LUM_COMPAT_LT_LE


/*
@@ The following macros supply trivial compatibility for some
** changes in the API. The macros themselves document how to
** change your code to avoid using them.
** (Once more, these macros were officially removed in 5.3, but they are
** still available here.)
*/
#define lum_strlen(L,i)		lum_rawlen(L, (i))

#define lum_objlen(L,i)		lum_rawlen(L, (i))

#define lum_equal(L,idx1,idx2)		lum_compare(L,(idx1),(idx2),LUM_OPEQ)
#define lum_lessthan(L,idx1,idx2)	lum_compare(L,(idx1),(idx2),LUM_OPLT)

#endif				/* } */

/* }================================================================== */



/*
** {==================================================================
** Configuration for Numbers (low-level part).
** Change these definitions if no predefined LUM_FLOAT_* / LUM_INT_*
** satisfy your needs.
** ===================================================================
*/

/*
@@ LUMI_UACNUMBER is the result of a 'default argument promotion'
@@ over a floating number.
@@ l_floatatt(x) corrects float attribute 'x' to the proper float type
** by prefixing it with one of FLT/DBL/LDBL.
@@ LUM_NUMBER_FRMLEN is the length modifier for writing floats.
@@ LUM_NUMBER_FMT is the format for writing floats with the maximum
** number of digits that respects tostring(tonumber(numeral)) == numeral.
** (That would be floor(log10(2^n)), where n is the number of bits in
** the float mantissa.)
@@ LUM_NUMBER_FMT_N is the format for writing floats with the minimum
** number of digits that ensures tonumber(tostring(number)) == number.
** (That would be LUM_NUMBER_FMT+2.)
@@ l_mathop allows the addition of an 'l' or 'f' to all math operations.
@@ l_floor takes the floor of a float.
@@ lum_str2number converts a decimal numeral to a number.
*/


/* The following definitions are good for most cases here */

#define l_floor(x)		(l_mathop(floor)(x))


/*
@@ lum_numbertointeger converts a float number with an integral value
** to an integer, or returns 0 if float is not within the range of
** a lum_Integer.  (The range comparisons are tricky because of
** rounding. The tests here assume a two-complement representation,
** where MININTEGER always has an exact representation as a float;
** MAXINTEGER may not have one, and therefore its conversion to float
** may have an ill-defined value.)
*/
#define lum_numbertointeger(n,p) \
  ((n) >= (LUM_NUMBER)(LUM_MININTEGER) && \
   (n) < -(LUM_NUMBER)(LUM_MININTEGER) && \
      (*(p) = (LUM_INTEGER)(n), 1))


/* now the variable definitions */

#if LUM_FLOAT_TYPE == LUM_FLOAT_FLOAT		/* { single float */

#define LUM_NUMBER	float

#define l_floatatt(n)		(FLT_##n)

#define LUMI_UACNUMBER	double

#define LUM_NUMBER_FRMLEN	""
#define LUM_NUMBER_FMT		"%.7g"
#define LUM_NUMBER_FMT_N	"%.9g"

#define l_mathop(op)		op##f

#define lum_str2number(s,p)	strtof((s), (p))


#elif LUM_FLOAT_TYPE == LUM_FLOAT_LONGDOUBLE	/* }{ long double */

#define LUM_NUMBER	long double

#define l_floatatt(n)		(LDBL_##n)

#define LUMI_UACNUMBER	long double

#define LUM_NUMBER_FRMLEN	"L"
#define LUM_NUMBER_FMT		"%.19Lg"
#define LUM_NUMBER_FMT_N	"%.21Lg"

#define l_mathop(op)		op##l

#define lum_str2number(s,p)	strtold((s), (p))

#elif LUM_FLOAT_TYPE == LUM_FLOAT_DOUBLE	/* }{ double */

#define LUM_NUMBER	double

#define l_floatatt(n)		(DBL_##n)

#define LUMI_UACNUMBER	double

#define LUM_NUMBER_FRMLEN	""
#define LUM_NUMBER_FMT		"%.15g"
#define LUM_NUMBER_FMT_N	"%.17g"

#define l_mathop(op)		op

#define lum_str2number(s,p)	strtod((s), (p))

#else						/* }{ */

#error "numeric float type not defined"

#endif					/* } */



/*
@@ LUM_UNSIGNED is the unsigned version of LUM_INTEGER.
@@ LUMI_UACINT is the result of a 'default argument promotion'
@@ over a LUM_INTEGER.
@@ LUM_INTEGER_FRMLEN is the length modifier for reading/writing integers.
@@ LUM_INTEGER_FMT is the format for writing integers.
@@ LUM_MAXINTEGER is the maximum value for a LUM_INTEGER.
@@ LUM_MININTEGER is the minimum value for a LUM_INTEGER.
@@ LUM_MAXUNSIGNED is the maximum value for a LUM_UNSIGNED.
@@ lum_integer2str converts an integer to a string.
*/


/* The following definitions are good for most cases here */

#define LUM_INTEGER_FMT		"%" LUM_INTEGER_FRMLEN "d"

#define LUMI_UACINT		LUM_INTEGER

#define lum_integer2str(s,sz,n)  \
	l_sprintf((s), sz, LUM_INTEGER_FMT, (LUMI_UACINT)(n))

/*
** use LUMI_UACINT here to avoid problems with promotions (which
** can turn a comparison between unsigneds into a signed comparison)
*/
#define LUM_UNSIGNED		unsigned LUMI_UACINT


/* now the variable definitions */

#if LUM_INT_TYPE == LUM_INT_INT		/* { int */

#define LUM_INTEGER		int
#define LUM_INTEGER_FRMLEN	""

#define LUM_MAXINTEGER		INT_MAX
#define LUM_MININTEGER		INT_MIN

#define LUM_MAXUNSIGNED		UINT_MAX

#elif LUM_INT_TYPE == LUM_INT_LONG	/* }{ long */

#define LUM_INTEGER		long
#define LUM_INTEGER_FRMLEN	"l"

#define LUM_MAXINTEGER		LONG_MAX
#define LUM_MININTEGER		LONG_MIN

#define LUM_MAXUNSIGNED		ULONG_MAX

#elif LUM_INT_TYPE == LUM_INT_LONGLONG	/* }{ long long */

/* use presence of macro LLONG_MAX as proxy for C99 compliance */
#if defined(LLONG_MAX)		/* { */
/* use ISO C99 stuff */

#define LUM_INTEGER		long long
#define LUM_INTEGER_FRMLEN	"ll"

#define LUM_MAXINTEGER		LLONG_MAX
#define LUM_MININTEGER		LLONG_MIN

#define LUM_MAXUNSIGNED		ULLONG_MAX

#elif defined(LUM_USE_WINDOWS) /* }{ */
/* in Windows, can use specific Windows types */

#define LUM_INTEGER		__int64
#define LUM_INTEGER_FRMLEN	"I64"

#define LUM_MAXINTEGER		_I64_MAX
#define LUM_MININTEGER		_I64_MIN

#define LUM_MAXUNSIGNED		_UI64_MAX

#else				/* }{ */

#error "Compiler does not support 'long long'. Use option '-DLUM_32BITS' \
  or '-DLUM_C89_NUMBERS' (see file 'lumconf.h' for details)"

#endif				/* } */

#else				/* }{ */

#error "numeric integer type not defined"

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Dependencies with C99 and other C details
** ===================================================================
*/

/*
@@ l_sprintf is equivalent to 'snprintf' or 'sprintf' in C89.
** (All uses in Lum have only one format item.)
*/
#if !defined(LUM_USE_C89)
#define l_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define l_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif


/*
@@ lum_strx2number converts a hexadecimal numeral to a number.
** In C99, 'strtod' does that conversion. Otherwise, you can
** leave 'lum_strx2number' undefined and Lum will provide its own
** implementation.
*/
#if !defined(LUM_USE_C89)
#define lum_strx2number(s,p)		lum_str2number(s,p)
#endif


/*
@@ lum_pointer2str converts a pointer to a readable string in a
** non-specified way.
*/
#define lum_pointer2str(buff,sz,p)	l_sprintf(buff,sz,"%p",p)


/*
@@ lum_number2strx converts a float to a hexadecimal numeral.
** In C99, 'sprintf' (with format specifiers '%a'/'%A') does that.
** Otherwise, you can leave 'lum_number2strx' undefined and Lum will
** provide its own implementation.
*/
#if !defined(LUM_USE_C89)
#define lum_number2strx(L,b,sz,f,n)  \
	((void)L, l_sprintf(b,sz,f,(LUMI_UACNUMBER)(n)))
#endif


/*
** 'strtof' and 'opf' variants for math functions are not valid in
** C89. Otherwise, the macro 'HUGE_VALF' is a good proxy for testing the
** availability of these variants. ('math.h' is already included in
** all files that use these macros.)
*/
#if defined(LUM_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef l_mathop  /* variants not available */
#undef lum_str2number
#define l_mathop(op)		(lum_Number)op  /* no variant */
#define lum_str2number(s,p)	((lum_Number)strtod((s), (p)))
#endif


/*
@@ LUM_KCONTEXT is the type of the context ('ctx') for continuation
** functions.  It must be a numerical type; Lum will use 'intptr_t' if
** available, otherwise it will use 'ptrdiff_t' (the nearest thing to
** 'intptr_t' in C89)
*/
#define LUM_KCONTEXT	ptrdiff_t

#if !defined(LUM_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(INTPTR_MAX)  /* even in C99 this type is optional */
#undef LUM_KCONTEXT
#define LUM_KCONTEXT	intptr_t
#endif
#endif


/*
@@ lum_getlocaledecpoint gets the locale "radix character" (decimal point).
** Change that if you do not want to use C locales. (Code using this
** macro must include the header 'locale.h'.)
*/
#if !defined(lum_getlocaledecpoint)
#define lum_getlocaledecpoint()		(localeconv()->decimal_point[0])
#endif


/*
** macros to improve jump prediction, used mostly for error handling
** and debug facilities. (Some macros in the Lum API use these macros.
** Define LUM_NOBUILTIN if you do not want '__builtin_expect' in your
** code.)
*/
#if !defined(lumi_likely)

#if defined(__GNUC__) && !defined(LUM_NOBUILTIN)
#define lumi_likely(x)		(__builtin_expect(((x) != 0), 1))
#define lumi_unlikely(x)	(__builtin_expect(((x) != 0), 0))
#else
#define lumi_likely(x)		(x)
#define lumi_unlikely(x)	(x)
#endif

#endif


#if defined(LUM_CORE) || defined(LUM_LIB)
/* shorter names for Lum's own use */
#define l_likely(x)	lumi_likely(x)
#define l_unlikely(x)	lumi_unlikely(x)
#endif



/* }================================================================== */


/*
** {==================================================================
** Language Variations
** =====================================================================
*/

/*
@@ LUM_NOCVTN2S/LUM_NOCVTS2N control how Lum performs some
** coercions. Define LUM_NOCVTN2S to turn off automatic coercion from
** numbers to strings. Define LUM_NOCVTS2N to turn off automatic
** coercion from strings to numbers.
*/
/* #define LUM_NOCVTN2S */
/* #define LUM_NOCVTS2N */


/*
@@ LUM_USE_APICHECK turns on several consistency checks on the C API.
** Define it as a help when debugging C code.
*/
/* #define LUM_USE_APICHECK */

/* }================================================================== */


/*
** {==================================================================
** Macros that affect the API and must be stable (that is, must be the
** same when you compile Lum and when you compile code that links to
** Lum).
** =====================================================================
*/

/*
@@ LUMI_MAXSTACK limits the size of the Lum stack.
** CHANGE it if you need a different limit. This limit is arbitrary;
** its only purpose is to stop Lum from consuming unlimited stack
** space and to reserve some numbers for pseudo-indices.
** (It must fit into max(int)/2.)
*/
#if 1000000 < (INT_MAX / 2)
#define LUMI_MAXSTACK		1000000
#else
#define LUMI_MAXSTACK		(INT_MAX / 2u)
#endif


/*
@@ LUM_EXTRASPACE defines the size of a raw memory area associated with
** a Lum state with very fast access.
** CHANGE it if you need a different size.
*/
#define LUM_EXTRASPACE		(sizeof(void *))


/*
@@ LUM_IDSIZE gives the maximum size for the description of the source
** of a function in debug information.
** CHANGE it if you want a different size.
*/
#define LUM_IDSIZE	60


/*
@@ LUML_BUFFERSIZE is the initial buffer size used by the lauxlib
** buffer system.
*/
#define LUML_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(lum_Number)))


/*
@@ LUMI_MAXALIGN defines fields that, when used in a union, ensure
** maximum alignment for the other items in that union.
*/
#define LUMI_MAXALIGN  lum_Number n; double u; void *s; lum_Integer i; long l

/* }================================================================== */





/* =================================================================== */

/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/





#endif

