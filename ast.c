#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include "wheels/tagged_unions.h"
#include <stddef.h>

typedef struct astNode_inner astNode_inner;
typedef struct astRoot { // file
  msList(astNode_inner) nodes;
} astRoot;
typedef struct astBlock { // {...}
  msList(astNode_inner) nodes;
} astBlock;
typedef struct astArg { // (...)
  msList(astNode_inner) nodes;
} astArg;
typedef struct astExpr { // ?left ?op right
  astNode_inner *left, *right;
  // operation op; //TODO
} astExpr;
typedef struct astIf { // if (x) x else x
  astExpr *condition;
  astExpr *ifTrue;
  astExpr *ifFalse;
} astIf;
typedef struct astWhile { // while (x) x
  astExpr *condition;
  astExpr *block;
} astWhile;
typedef struct astFunction { // fn (...) ?? {}
  astArg *arguments;
  astBlock *body;
  astNode_inner *attributes;
} astFunction;

TU_DEFINE(
    (astNode_inner, u8),
    astRoot,
    astBlock,
    astArg,
    astExpr,
    astIf,
    astWhile,
    astFunction,
);
typedef struct tlocation {
  usize len;
  ptrdiff_t ptr;
} tlocation;

typedef struct astNode {
  tlocation text;
  astNode_inner node;
} astNode;
