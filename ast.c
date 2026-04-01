#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include "wheels/tagged_unions.h"
#include <stddef.h>

typedef struct astNode_inner astNode_inner;
typedef enum operation {
  OP_ASSIGNMENT, // a = b
  OP_SUBTRACT,   // a - b
  OP_ADD,        // a + b
  OP_DIVIDE,     // a / b
  OP_MULTIPLY,   // a * b
  OP_SUBSCRIPT,  // a.b
  OP_REFERANCE,  // #a
  // dereferance
} operation;

typedef struct astRoot { // file
  msList(astNode_inner) nodes;
} astRoot;
typedef struct astText {
} astText;
typedef struct astComment { // ... | /* ... */
} astComment;
typedef struct astBlock { // {...}
  msList(astNode_inner) nodes;
} astBlock;
typedef struct astArg { // (...)
  msList(astNode_inner) nodes;
} astArg;
typedef struct astExpr { // ?left ?op right
  astNode_inner *left, *right;
  operation op;
} astExpr;
typedef struct astDecl { // qualifiers  name type =  expr
  struct {
    astText *qualifiers;
    astText *name;
    astText *type;
  } *left;
  astExpr *expr;
} astDecl;
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
  msList(astNode_inner) attributes;
} astFunction;

TU_DEFINE(
    (astNode_inner, u8),
    astRoot,
    astText,
    astComment,
    astBlock,
    astArg,
    astExpr,
    astDecl,
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
