#include "ast.h"
#include "lexer.h"
#include "pool.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include "wheels/shmap.h"
#include "wheels/tagged_unions.h"
typedef struct cType cType;

typedef struct cType_ptr {
  cType *type;
} cType_ptr;
typedef struct cType_arr {
  cType *type;
} cType_arr;
typedef struct cType_ref {
  cType *type;
} cType_ref;
typedef struct cType_int {
  u8 bitCount;
} cType_int;
typedef struct cType_uint {
  u8 bitCount;
} cType_uint;
typedef struct cType_struct {
  msList(tlocation) names;
  msList(cType) types;
} cType_struct;
typedef struct cType_union { // probably wont have untagged unions
  msList(tlocation) names;
  msList(cType) types;
} cType_union;
typedef struct cType_function {
  msList(cType) args;
  cType *returnType;
} cType_function;
typedef struct cType_unknown {
} cType_unknown;
TU_DEFINE(
    (cType, u8),
    cType_ptr,
    cType_ref,
    cType_arr,
    cType_int,
    cType_uint,
    cType_struct,
    cType_union,
    cType_function,
    cType_unknown,
);

static tPool *typePool;
[[gnu::constructor]] void initTypesPool(void) {
  typePool = tPool_new(stdAlloc, sizeof(cType), 1 << 8);
}
cType getType(astNode n) {
  msHmap(cType *) types = {};
  types = types ?: msHmap_init(stdAlloc, cType *);
}
