#pragma once
#include "lexer.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include "wheels/shmap.h"
#include "wheels/tagged_unions.h"
#include <stddef.h>

typedef struct astNode astNode;

typedef struct astRoot { // file
  tlocation *text;
  msList(astNode) nodes;
} astRoot;
typedef struct astText {
  tlocation *text;
} astText;
typedef struct astComment { // ... | /* ... */
  tlocation *text;
} astComment;
typedef struct astBlock { // {...}
  tlocation *text;
  msList(astNode) nodes;
} astBlock;
typedef struct astArg { // (...)
  tlocation *text;
  msList(astNode) nodes;
} astArg;
typedef struct astExpr { // ?left ?op right
  tlocation *text;
  astNode *left, *right;
  operation op;
} astExpr;
typedef struct astDecl { // qualifiers  name type =  expr
  tlocation *text;
  struct {
    astNode *qualifiers;
    astText *name;
    astNode *type;
  } *left;
  astExpr *expr;
} astDecl;
typedef struct astIf {
  tlocation *text;
  astNode *condition, *ifTrue, *ifFalse;
} astIf;
typedef struct astWhile {
  tlocation *text;
  astNode *condition, *block;
} astWhile;
typedef struct astFunction { // fn (...) ?? {}
  tlocation *text;
  astArg *arguments;
  astBlock *body;
  astNode *returnType;
  msList(astNode) attributes;
} astFunction;
typedef struct astKeyword {
  tlocation *text;
  astText *word;
  astNode *expr;
} astKeyword;

TU_DEFINE(
    (astNode, u8),
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
    astKeyword,
);

