#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/sList.h"
#include "wheels/tagged_unions.h"
typedef struct item_type item_type;

typedef struct item_type_type {
} item_type_type;
typedef struct item_type_module {
} item_type_module;
typedef struct item_type_sint {
  usize bitwidth;
  usize alignment;
} item_type_sint;
typedef struct item_type_uint {
  usize bitwidth;
  usize alignment;
} item_type_uint;
// typedef struct item_type_dint {
// } item_type_dint;
typedef struct item_type_ptr {
  item_type *type; // not this one
  usize alignment;
} item_type_ptr;
typedef struct item_type_struct {
  struct {
    item_type *type;
    usize offset;
  } *types;
  usize alignment;
} item_type_struct;
typedef struct item_type_union {
  item_type **types;
  usize alignment;
} item_type_union;

typedef struct item_type_block {
  item_type *types; // only one return type,
                    // last item is return type
} item_type_block;

TU_DEFINE(
    (item_type, u8),
    item_type_type,
    item_type_module,
    item_type_ptr,
    item_type_sint,
    item_type_uint,
    item_type_struct,
    item_type_union,
    item_type_block,
);
// TODO move off asserts
#pragma push_macro("max")
#pragma push_macro("min")
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))
static usize type_alignment(item_type *t) {
  item_type ts = *t;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, {
        return $in.alignment;
      }),
      (item_type_union, {
        return $in.alignment;
      }),
      (item_type_sint, {
        return $in.alignment;
      }),
      (item_type_uint, {
        return $in.alignment;
      }),
      (item_type_ptr, {
        return $in.alignment;
      }),
      (item_type_block, {
        assertMessage(false, "alignof of function");
      }),
      (item_type_module, {
        assertMessage(false, "alignof of module");
      }),
      (item_type_type, {
        assertMessage(false, "alignof of type type");
      }),
      (default, {
        assertMessage(
            false, "unhandled type : %s", snprint(stdAlloc, "{}", t->tag).ptr
        );
      })
  );
  assertMessage(false, "unreachable");
  return 0;
}
static usize type_size(item_type *t) {
  item_type ts = *t;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, {
        usize size = $in.types[msList_len($in.types) - 1].offset;
        var_ t = $in.types[msList_len($in.types) - 1].type;
        size += max(type_alignment(t), type_size(t));
        return size;
      }),
      (item_type_union, {
        usize size = 0;
        for_each_((var_ t, msList_vla($in.types)), {
          size = max(size, max(type_alignment(t), type_size(t)));
        });
        return size;
      }),
      (item_type_sint, {
        return lineup($in.bitwidth, sizeof(char) * 8) / 8;
      }),
      (item_type_uint, {
        return lineup($in.bitwidth, sizeof(char) * 8) / 8;
      }),
      (item_type_ptr, {
        return sizeof(void *);
      }),
      (item_type_block, {
        assertMessage(false, "size of function");
      }),
      (item_type_module, {
        assertMessage(false, "size of module");
      }),
      (item_type_type, {
        assertMessage(false, "size of type type");
      }),
      (default, {
        assertMessage(
            false, "unhandled type : %s", snprint(stdAlloc, "{}", t->tag).ptr
        );
      })
  );
  assertMessage(false, "unreachable");
  return 0;
}
#pragma pop_macro("max")
#pragma pop_macro("min")
