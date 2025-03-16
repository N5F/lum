/*
** $Id: lum.c $
** Lum stand-alone interpreter
** See Copyright Notice in lum.h
*/

#define lum_c

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include "lum.h"

#include "lauxlib.h"
#include "lumlib.h"
#include "llimits.h"


#if !defined(LUM_PROGNAME)
#define LUM_PROGNAME		"lum"
#endif

#if !defined(LUM_INIT_VAR)
#define LUM_INIT_VAR		"LUM_INIT"
#endif

#define LUM_INITVARVERSION	LUM_INIT_VAR LUM_VERSUFFIX


static lum_State *globalL = NULL;

static const char *progname = LUM_PROGNAME;


#if defined(LUM_USE_POSIX)   /* { */

/*
** Use 'sigaction' when available.
*/
static void setsignal (int sig, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);  /* do not mask any signal */
  sigaction(sig, &sa, NULL);
}

#else           /* }{ */

#define setsignal            signal

#endif                               /* } */


/*
** Hook set by signal function to stop the interpreter.
*/
static void lstop (lum_State *L, lum_Debug *ar) {
  (void)ar;  /* unused arg. */
  lum_sethook(L, NULL, 0, 0);  /* reset hook */
  lumL_error(L, "interrupted!");
}


/*
** Function to be called at a C signal. Because a C signal cannot
** just change a Lum state (as there is no proper synchronization),
** this function only sets a hook that, when called, will stop the
** interpreter.
*/
static void laction (int i) {
  int flag = LUM_MASKCALL | LUM_MASKRET | LUM_MASKLINE | LUM_MASKCOUNT;
  setsignal(i, SIG_DFL); /* if another SIGINT happens, terminate process */
  lum_sethook(globalL, lstop, flag, 1);
}


static void print_usage (const char *badoption) {
  lum_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    lum_writestringerror("'%s' needs argument\n", badoption);
  else
    lum_writestringerror("unrecognized option '%s'\n", badoption);
  lum_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat   execute string 'stat'\n"
  "  -i        enter interactive mode after executing 'script'\n"
  "  -l mod    require library 'mod' into global 'mod'\n"
  "  -l g=mod  require library 'mod' into global 'g'\n"
  "  -v        show version information\n"
  "  -E        ignore environment variables\n"
  "  -W        turn warnings on\n"
  "  --        stop handling options\n"
  "  -         stop handling options and execute stdin\n"
  ,
  progname);
}


/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void l_message (const char *pname, const char *msg) {
  if (pname) lum_writestringerror("%s: ", pname);
  lum_writestringerror("%s\n", msg);
}


/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack.
*/
static int report (lum_State *L, int status) {
  if (status != LUM_OK) {
    const char *msg = lum_tostring(L, -1);
    if (msg == NULL)
      msg = "(error message not a string)";
    l_message(progname, msg);
    lum_pop(L, 1);  /* remove message */
  }
  return status;
}


/*
** Message handler used to run all chunks
*/
static int msghandler (lum_State *L) {
  const char *msg = lum_tostring(L, 1);
  if (msg == NULL) {  /* is error object not a string? */
    if (lumL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
        lum_type(L, -1) == LUM_TSTRING)  /* that produces a string? */
      return 1;  /* that is the message */
    else
      msg = lum_pushfstring(L, "(error object is a %s value)",
                               lumL_typename(L, 1));
  }
  lumL_traceback(L, L, msg, 1);  /* append a standard traceback */
  return 1;  /* return the traceback */
}


/*
** Interface to 'lum_pcall', which sets appropriate message function
** and C-signal handler. Used to run all chunks.
*/
static int docall (lum_State *L, int narg, int nres) {
  int status;
  int base = lum_gettop(L) - narg;  /* function index */
  lum_pushcfunction(L, msghandler);  /* push message handler */
  lum_insert(L, base);  /* put it under function and args */
  globalL = L;  /* to be available to 'laction' */
  setsignal(SIGINT, laction);  /* set C-signal handler */
  status = lum_pcall(L, narg, nres, base);
  setsignal(SIGINT, SIG_DFL); /* reset C-signal handler */
  lum_remove(L, base);  /* remove message handler from the stack */
  return status;
}


static void print_version (void) {
  lum_writestring(LUM_COPYRIGHT, strlen(LUM_COPYRIGHT));
  lum_writeline();
}


/*
** Create the 'arg' table, which stores all arguments from the
** command line ('argv'). It should be aligned so that, at index 0,
** it has 'argv[script]', which is the script name. The arguments
** to the script (everything after 'script') go to positive indices;
** other arguments (before the script name) go to negative indices.
** If there is no script name, assume interpreter's name as base.
** (If there is no interpreter's name either, 'script' is -1, so
** table sizes are zero.)
*/
static void createargtable (lum_State *L, char **argv, int argc, int script) {
  int i, narg;
  narg = argc - (script + 1);  /* number of positive indices */
  lum_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    lum_pushstring(L, argv[i]);
    lum_rawseti(L, -2, i - script);
  }
  lum_setglobal(L, "arg");
}


static int dochunk (lum_State *L, int status) {
  if (status == LUM_OK) status = docall(L, 0, 0);
  return report(L, status);
}


static int dofile (lum_State *L, const char *name) {
  return dochunk(L, lumL_loadfile(L, name));
}


static int dostring (lum_State *L, const char *s, const char *name) {
  return dochunk(L, lumL_loadbuffer(L, s, strlen(s), name));
}


/*
** Receives 'globname[=modname]' and runs 'globname = require(modname)'.
** If there is no explicit modname and globname contains a '-', cut
** the suffix after '-' (the "version") to make the global name.
*/
static int dolibrary (lum_State *L, char *globname) {
  int status;
  char *suffix = NULL;
  char *modname = strchr(globname, '=');
  if (modname == NULL) {  /* no explicit name? */
    modname = globname;  /* module name is equal to global name */
    suffix = strchr(modname, *LUM_IGMARK);  /* look for a suffix mark */
  }
  else {
    *modname = '\0';  /* global name ends here */
    modname++;  /* module name starts after the '=' */
  }
  lum_getglobal(L, "require");
  lum_pushstring(L, modname);
  status = docall(L, 1, 1);  /* call 'require(modname)' */
  if (status == LUM_OK) {
    if (suffix != NULL)  /* is there a suffix mark? */
      *suffix = '\0';  /* remove suffix from global name */
    lum_setglobal(L, globname);  /* globname = require(modname) */
  }
  return report(L, status);
}


/*
** Push on the stack the contents of table 'arg' from 1 to #arg
*/
static int pushargs (lum_State *L) {
  int i, n;
  if (lum_getglobal(L, "arg") != LUM_TTABLE)
    lumL_error(L, "'arg' is not a table");
  n = (int)lumL_len(L, -1);
  lumL_checkstack(L, n + 3, "too many arguments to script");
  for (i = 1; i <= n; i++)
    lum_rawgeti(L, -i, i);
  lum_remove(L, -i);  /* remove table from the stack */
  return n;
}


static int handle_script (lum_State *L, char **argv) {
  int status;
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)
    fname = NULL;  /* stdin */
  status = lumL_loadfile(L, fname);
  if (status == LUM_OK) {
    int n = pushargs(L);  /* push arguments to script */
    status = docall(L, n, LUM_MULTRET);
  }
  return report(L, status);
}


/* bits of various argument indicators in 'args' */
#define has_error	1	/* bad option */
#define has_i		2	/* -i */
#define has_v		4	/* -v */
#define has_e		8	/* -e */
#define has_E		16	/* -E */


/*
** Traverses all arguments from 'argv', returning a mask with those
** needed before running any Lum code or an error code if it finds any
** invalid argument. In case of error, 'first' is the index of the bad
** argument.  Otherwise, 'first' is -1 if there is no program name,
** 0 if there is no script name, or the index of the script name.
*/
static int collectargs (char **argv, int *first) {
  int args = 0;
  int i;
  if (argv[0] != NULL) {  /* is there a program name? */
    if (argv[0][0])  /* not empty? */
      progname = argv[0];  /* save it */
  }
  else {  /* no program name */
    *first = -1;
    return 0;
  }
  for (i = 1; argv[i] != NULL; i++) {  /* handle arguments */
    *first = i;
    if (argv[i][0] != '-')  /* not an option? */
        return args;  /* stop handling options */
    switch (argv[i][1]) {  /* else check option */
      case '-':  /* '--' */
        if (argv[i][2] != '\0')  /* extra characters after '--'? */
          return has_error;  /* invalid option */
        *first = i + 1;
        return args;
      case '\0':  /* '-' */
        return args;  /* script "name" is '-' */
      case 'E':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        args |= has_E;
        break;
      case 'W':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        break;
      case 'i':
        args |= has_i;  /* (-i implies -v) *//* FALLTHROUGH */
      case 'v':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        args |= has_v;
        break;
      case 'e':
        args |= has_e;  /* FALLTHROUGH */
      case 'l':  /* both options need an argument */
        if (argv[i][2] == '\0') {  /* no concatenated argument? */
          i++;  /* try next 'argv' */
          if (argv[i] == NULL || argv[i][0] == '-')
            return has_error;  /* no next argument or it is another option */
        }
        break;
      default:  /* invalid option */
        return has_error;
    }
  }
  *first = 0;  /* no script name */
  return args;
}


/*
** Processes options 'e' and 'l', which involve running Lum code, and
** 'W', which also affects the state.
** Returns 0 if some code raises an error.
*/
static int runargs (lum_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    int option = argv[i][1];
    lum_assert(argv[i][0] == '-');  /* already checked */
    switch (option) {
      case 'e':  case 'l': {
        int status;
        char *extra = argv[i] + 2;  /* both options need an argument */
        if (*extra == '\0') extra = argv[++i];
        lum_assert(extra != NULL);
        status = (option == 'e')
                 ? dostring(L, extra, "=(command line)")
                 : dolibrary(L, extra);
        if (status != LUM_OK) return 0;
        break;
      }
      case 'W':
        lum_warning(L, "@on", 0);  /* warnings on */
        break;
    }
  }
  return 1;
}


static int handle_luminit (lum_State *L) {
  const char *name = "=" LUM_INITVARVERSION;
  const char *init = getenv(name + 1);
  if (init == NULL) {
    name = "=" LUM_INIT_VAR;
    init = getenv(name + 1);  /* try alternative name */
  }
  if (init == NULL) return LUM_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}


/*
** {==================================================================
** Read-Eval-Print Loop (REPL)
** ===================================================================
*/

#if !defined(LUM_PROMPT)
#define LUM_PROMPT		"> "
#define LUM_PROMPT2		">> "
#endif

#if !defined(LUM_MAXINPUT)
#define LUM_MAXINPUT		512
#endif


/*
** lum_stdin_is_tty detects whether the standard input is a 'tty' (that
** is, whether we're running lum interactively).
*/
#if !defined(lum_stdin_is_tty)	/* { */

#if defined(LUM_USE_POSIX)	/* { */

#include <unistd.h>
#define lum_stdin_is_tty()	isatty(0)

#elif defined(LUM_USE_WINDOWS)	/* }{ */

#include <io.h>
#include <windows.h>

#define lum_stdin_is_tty()	_isatty(_fileno(stdin))

#else				/* }{ */

/* ISO C definition */
#define lum_stdin_is_tty()	1  /* assume stdin is a tty */

#endif				/* } */

#endif				/* } */


/*
** lum_readline defines how to show a prompt and then read a line from
** the standard input.
** lum_saveline defines how to "save" a read line in a "history".
** lum_freeline defines how to free a line read by lum_readline.
*/

#if defined(LUM_USE_READLINE)

#include <readline/readline.h>
#include <readline/history.h>
#define lum_initreadline(L)	((void)L, rl_readline_name="lum")
#define lum_readline(b,p)	((void)b, readline(p))
#define lum_saveline(line)	add_history(line)
#define lum_freeline(b)		free(b)

#endif


#if !defined(lum_readline)	/* { */

/* pointer to dynamically loaded 'readline' function (if any) */
typedef char *(*l_readline_t) (const char *prompt);
static l_readline_t l_readline = NULL;

static char *lum_readline (char *buff, const char *prompt) {
  if (l_readline != NULL)  /* is there a dynamic 'readline'? */
    return (*l_readline)(prompt);  /* use it */
  else {  /* emulate 'readline' over 'buff' */
    fputs(prompt, stdout);
    fflush(stdout);  /* show prompt */
    return fgets(buff, LUM_MAXINPUT, stdin);  /* read line */
  }
}


/* pointer to dynamically loaded 'add_history' function (if any) */
typedef void (*l_addhist_t) (const char *string);
static l_addhist_t l_addhist = NULL;

static void lum_saveline (const char *line) {
  if (l_addhist != NULL)  /* is there a dynamic 'add_history'? */
    (*l_addhist)(line);  /* use it */
  /* else nothing to be done */
}


static void lum_freeline (char *line) {
  if (l_readline != NULL)  /* is there a dynamic 'readline'? */
    free(line);  /* free line created by it */
  /* else 'lum_readline' used an automatic buffer; nothing to free */
}


#if !defined(LUM_USE_DLOPEN) || !defined(LUM_READLINELIB)

#define lum_initreadline(L)  ((void)L)

#else /* { */

#include <dlfcn.h>


static void lum_initreadline (lum_State *L) {
  void *lib = dlopen(LUM_READLINELIB, RTLD_NOW | RTLD_LOCAL);
  if (lib == NULL)
    lum_warning(L, "library '" LUM_READLINELIB "' not found", 0);
  else {
    const char **name = cast(const char**, dlsym(lib, "rl_readline_name"));
    if (name != NULL)
      *name = "Lum";
    l_readline = cast(l_readline_t, cast_func(dlsym(lib, "readline")));
    if (l_readline == NULL)
      lum_warning(L, "unable to load 'readline'", 0);
    else
      l_addhist = cast(l_addhist_t, cast_func(dlsym(lib, "add_history")));
  }
}

#endif	/* } */

#endif				/* } */


/*
** Return the string to be used as a prompt by the interpreter. Leave
** the string (or nil, if using the default value) on the stack, to keep
** it anchored.
*/
static const char *get_prompt (lum_State *L, int firstline) {
  if (lum_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2") == LUM_TNIL)
    return (firstline ? LUM_PROMPT : LUM_PROMPT2);  /* use the default */
  else {  /* apply 'tostring' over the value */
    const char *p = lumL_tolstring(L, -1, NULL);
    lum_remove(L, -2);  /* remove original value */
    return p;
  }
}

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)


/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
static int incomplete (lum_State *L, int status) {
  if (status == LUM_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = lum_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0)
      return 1;
  }
  return 0;  /* else... */
}


/*
** Prompt the user, read a line, and push it into the Lum stack.
*/
static int pushline (lum_State *L, int firstline) {
  char buffer[LUM_MAXINPUT];
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  char *b = lum_readline(buffer, prmt);
  lum_pop(L, 1);  /* remove prompt */
  if (b == NULL)
    return 0;  /* no input */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[--l] = '\0';  /* remove it */
  lum_pushlstring(L, b, l);
  lum_freeline(b);
  return 1;
}


/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn (lum_State *L) {
  const char *line = lum_tostring(L, -1);  /* original line */
  const char *retline = lum_pushfstring(L, "return %s;", line);
  int status = lumL_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == LUM_OK)
    lum_remove(L, -2);  /* remove modified line */
  else
    lum_pop(L, 2);  /* pop result from 'lumL_loadbuffer' and modified line */
  return status;
}


static void checklocal (const char *line) {
  static const size_t szloc = sizeof("local") - 1;
  static const char space[] = " \t";
  line += strspn(line, space);  /* skip spaces */
  if (strncmp(line, "local", szloc) == 0 &&  /* "local"? */
      strchr(space, *(line + szloc)) != NULL) {  /* followed by a space? */
    lum_writestringerror("%s\n",
      "warning: locals do not survive across lines in interactive mode");
  }
}


/*
** Read multiple lines until a complete Lum statement or an error not
** for an incomplete statement. Start with first line already read in
** the stack.
*/
static int multiline (lum_State *L) {
  size_t len;
  const char *line = lum_tolstring(L, 1, &len);  /* get first line */
  checklocal(line);
  for (;;) {  /* repeat until gets a complete statement */
    int status = lumL_loadbuffer(L, line, len, "=stdin");  /* try it */
    if (!incomplete(L, status) || !pushline(L, 0))
      return status;  /* should not or cannot try to add continuation line */
    lum_remove(L, -2);  /* remove error message (from incomplete line) */
    lum_pushliteral(L, "\n");  /* add newline... */
    lum_insert(L, -2);  /* ...between the two lines */
    lum_concat(L, 3);  /* join them */
    line = lum_tolstring(L, 1, &len);  /* get what is has */
  }
}


/*
** Read a line and try to load (compile) it first as an expression (by
** adding "return " in front of it) and second as a statement. Return
** the final status of load/call with the resulting function (if any)
** in the top of the stack.
*/
static int loadline (lum_State *L) {
  const char *line;
  int status;
  lum_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  if ((status = addreturn(L)) != LUM_OK)  /* 'return ...' did not work? */
    status = multiline(L);  /* try as command, maybe with continuation lines */
  line = lum_tostring(L, 1);
  if (line[0] != '\0')  /* non empty? */
    lum_saveline(line);  /* keep history */
  lum_remove(L, 1);  /* remove line from the stack */
  lum_assert(lum_gettop(L) == 1);
  return status;
}


/*
** Prints (calling the Lum 'print' function) any values on the stack
*/
static void l_print (lum_State *L) {
  int n = lum_gettop(L);
  if (n > 0) {  /* any result to be printed? */
    lumL_checkstack(L, LUM_MINSTACK, "too many results to print");
    lum_getglobal(L, "print");
    lum_insert(L, 1);
    if (lum_pcall(L, n, 0, 0) != LUM_OK)
      l_message(progname, lum_pushfstring(L, "error calling 'print' (%s)",
                                             lum_tostring(L, -1)));
  }
}


/*
** Do the REPL: repeatedly read (load) a line, evalumte (call) it, and
** print any results.
*/
static void doREPL (lum_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;  /* no 'progname' on errors in interactive mode */
  lum_initreadline(L);
  while ((status = loadline(L)) != -1) {
    if (status == LUM_OK)
      status = docall(L, 0, LUM_MULTRET);
    if (status == LUM_OK) l_print(L);
    else report(L, status);
  }
  lum_settop(L, 0);  /* clear stack */
  lum_writeline();
  progname = oldprogname;
}

/* }================================================================== */

#if !defined(lumi_openlibs)
#define lumi_openlibs(L)	lumL_openselectedlibs(L, ~0, 0)
#endif


/*
** Main body of stand-alone interpreter (to be called in protected mode).
** Reads the options and handles them all.
*/
static int pmain (lum_State *L) {
  int argc = (int)lum_tointeger(L, 1);
  char **argv = (char **)lum_touserdata(L, 2);
  int script;
  int args = collectargs(argv, &script);
  int optlim = (script > 0) ? script : argc; /* first argv not an option */
  lumL_checkversion(L);  /* check that interpreter has correct version */
  if (args == has_error) {  /* bad arg? */
    print_usage(argv[script]);  /* 'script' has index of bad arg. */
    return 0;
  }
  if (args & has_v)  /* option '-v'? */
    print_version();
  if (args & has_E) {  /* option '-E'? */
    lum_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
    lum_setfield(L, LUM_REGISTRYINDEX, "LUM_NOENV");
  }
  lumi_openlibs(L);  /* open standard libraries */
  createargtable(L, argv, argc, script);  /* create table 'arg' */
  lum_gc(L, LUM_GCRESTART);  /* start GC... */
  lum_gc(L, LUM_GCGEN);  /* ...in generational mode */
  if (!(args & has_E)) {  /* no option '-E'? */
    if (handle_luminit(L) != LUM_OK)  /* run LUM_INIT */
      return 0;  /* error running LUM_INIT */
  }
  if (!runargs(L, argv, optlim))  /* execute arguments -e and -l */
    return 0;  /* something failed */
  if (script > 0) {  /* execute main script (if there is one) */
    if (handle_script(L, argv + script) != LUM_OK)
      return 0;  /* interrupt in case of error */
  }
  if (args & has_i)  /* -i option? */
    doREPL(L);  /* do read-eval-print loop */
  else if (script < 1 && !(args & (has_e | has_v))) { /* no active option? */
    if (lum_stdin_is_tty()) {  /* running in interactive mode? */
      print_version();
      doREPL(L);  /* do read-eval-print loop */
    }
    else dofile(L, NULL);  /* executes stdin as a file */
  }
  lum_pushboolean(L, 1);  /* signal no errors */
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  lum_State *L = lumL_newstate();  /* create state */
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  lum_gc(L, LUM_GCSTOP);  /* stop GC while building state */
  lum_pushcfunction(L, &pmain);  /* to call 'pmain' in protected mode */
  lum_pushinteger(L, argc);  /* 1st argument */
  lum_pushlightuserdata(L, argv); /* 2nd argument */
  status = lum_pcall(L, 2, 1, 0);  /* do the call */
  result = lum_toboolean(L, -1);  /* get result */
  report(L, status);
  lum_close(L);
  return (result && status == LUM_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

