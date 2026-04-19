#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mylist.h"
#include "wheels/shmap.h"
#include "wheels/tagged_unions.h"

typedef struct item_type item_type;

typedef struct {
  usize bitwidth;
} item_type_int;
typedef struct {
  usize bitwidth;
} item_type_uint;

typedef struct {
  item_type(*type[1]);
} item_type_ptr;

typedef struct {
  item_type(*types[]);
} item_type_struct;
typedef struct {
  item_type(*types[]);
} item_type_union;

typedef struct {
  item_type(*types[]); // only one return type,
                       // last item is return type
} item_type_block;

TU_DEFINE(
    (item_type, u8),
    item_type_ptr,
    item_type_int,
    item_type_uint,
    item_type_struct,
    item_type_union,
    item_type_block,
);

typedef struct {
  void *value;
  item_type *type;
} symbol;

msHmap(msHmap(
    msList(symbol)
)) modules;
