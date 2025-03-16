/*
** $Id: lcode.h $
** Code generator for Lum
** See Copyright Notice in lum.h
*/

#ifndef lcode_h
#define lcode_h

#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)


/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
typedef enum BinOpr {
  /* arithmetic operators */
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,
  /* bitwise operators */
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  /* string operator */
  OPR_CONCAT,
  /* comparison operators */
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  /* logical operators */
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;


/* true if operation is foldable (that is, it is arithmetic or bitwise) */
#define foldbinop(op)	((op) <= OPR_SHR)


#define lumK_codeABC(fs,o,a,b,c)	lumK_codeABCk(fs,o,a,b,c,0)


typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


/* get (pointer to) instruction of given 'expdesc' */
#define getinstruction(fs,e)	((fs)->f->code[(e)->u.info])


#define lumK_setmultret(fs,e)	lumK_setreturns(fs, e, LUM_MULTRET)

#define lumK_jumpto(fs,t)	lumK_patchlist(fs, lumK_jump(fs), t)

LUMI_FUNC int lumK_code (FuncState *fs, Instruction i);
LUMI_FUNC int lumK_codeABx (FuncState *fs, OpCode o, int A, int Bx);
LUMI_FUNC int lumK_codeABCk (FuncState *fs, OpCode o, int A, int B, int C,
                                            int k);
LUMI_FUNC int lumK_codevABCk (FuncState *fs, OpCode o, int A, int B, int C,
                                             int k);
LUMI_FUNC int lumK_exp2const (FuncState *fs, const expdesc *e, TValue *v);
LUMI_FUNC void lumK_fixline (FuncState *fs, int line);
LUMI_FUNC void lumK_nil (FuncState *fs, int from, int n);
LUMI_FUNC void lumK_reserveregs (FuncState *fs, int n);
LUMI_FUNC void lumK_checkstack (FuncState *fs, int n);
LUMI_FUNC void lumK_int (FuncState *fs, int reg, lum_Integer n);
LUMI_FUNC void lumK_dischargevars (FuncState *fs, expdesc *e);
LUMI_FUNC int lumK_exp2anyreg (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_exp2anyregup (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_exp2nextreg (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_exp2val (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_self (FuncState *fs, expdesc *e, expdesc *key);
LUMI_FUNC void lumK_indexed (FuncState *fs, expdesc *t, expdesc *k);
LUMI_FUNC void lumK_goiftrue (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_goiffalse (FuncState *fs, expdesc *e);
LUMI_FUNC void lumK_storevar (FuncState *fs, expdesc *var, expdesc *e);
LUMI_FUNC void lumK_setreturns (FuncState *fs, expdesc *e, int nresults);
LUMI_FUNC void lumK_setoneret (FuncState *fs, expdesc *e);
LUMI_FUNC int lumK_jump (FuncState *fs);
LUMI_FUNC void lumK_ret (FuncState *fs, int first, int nret);
LUMI_FUNC void lumK_patchlist (FuncState *fs, int list, int target);
LUMI_FUNC void lumK_patchtohere (FuncState *fs, int list);
LUMI_FUNC void lumK_concat (FuncState *fs, int *l1, int l2);
LUMI_FUNC int lumK_getlabel (FuncState *fs);
LUMI_FUNC void lumK_prefix (FuncState *fs, UnOpr op, expdesc *v, int line);
LUMI_FUNC void lumK_infix (FuncState *fs, BinOpr op, expdesc *v);
LUMI_FUNC void lumK_posfix (FuncState *fs, BinOpr op, expdesc *v1,
                            expdesc *v2, int line);
LUMI_FUNC void lumK_settablesize (FuncState *fs, int pc,
                                  int ra, int asize, int hsize);
LUMI_FUNC void lumK_setlist (FuncState *fs, int base, int nelems, int tostore);
LUMI_FUNC void lumK_finish (FuncState *fs);
LUMI_FUNC l_noret lumK_semerror (LexState *ls, const char *msg);


#endif
