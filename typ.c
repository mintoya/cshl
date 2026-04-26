#include "wheels/bigint.h"
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
// typedef struct item_type_bint { // will parse integers with this
//   struct {
//     usize count;
//     u8 bits[/*count*/];
//   } *b; // bits pointer is soemwhat convertable to bigint
// } item_type_bint;
typedef struct item_type_ptr {
  item_type *type; // not this one
  usize alignment;
} item_type_ptr;
typedef struct item_type_struct {
  struct {
    item_type *type;
    usize offset;
  } *types; // msList
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
static usize type_size(item_type *t);
static usize type_alignment(item_type *t) {
  item_type ts = *t;
  usize res = 0;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, { res = $in.alignment; }),
      (item_type_union, { res = $in.alignment; }),
      (item_type_sint, { res = $in.alignment; }),
      (item_type_uint, { res = $in.alignment; }),
      (item_type_ptr, { res = $in.alignment; }),
      (item_type_block, { assertMessage(false, "alignof of function"); }),
      (item_type_module, { assertMessage(false, "alignof of module"); }),
      (item_type_type, { assertMessage(false, "alignof of type type"); }),
      (default, {
        assertMessage(
            false, "unhandled type : %s", snprint(stdAlloc, "{}", t->tag).ptr
        );
      })
  );

  if (res)
    return res;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, {
        usize max_align = 1;
        foreach (var_ t, vla(*msList_vla($in.types)))
          max_align = max(max_align, type_alignment(t.type));
        return max_align;
      }),
      (item_type_union, {
        usize max_align = 1;
        foreach (var_ t, vla(*msList_vla($in.types)))
          max_align = max(max_align, type_alignment(t));
        return max_align;
      }),
      (default, { return min(type_size(t), 8); }),
  );
}
static usize type_size(item_type *t) {
  item_type ts = *t;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, {
        usize len = msList_len($in.types);
        if (!len)
          return 0;

        var_ last_item = $in.types[len - 1];
        usize size = last_item.offset + type_size(last_item.type);

        usize align = type_alignment(t);
        return lineup(size, align);
      }),
      (item_type_union, {
        usize max_size = 0;
        foreach (var_ t_ptr, vla(*msList_vla($in.types)))
          max_size = max(max_size, type_size(t_ptr));

        usize align = type_alignment(t);
        return lineup(max_size, align);
      }),
      (item_type_sint, {
        return lineup($in.bitwidth, 8) / 8;
      }),
      (item_type_uint, {
        return lineup($in.bitwidth, 8) / 8;
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
REGISTER_SPECIAL_PRINTER("item_type", item_type *, {
  if (!in) {
    PUTS("NULL");
    return;
  }

  item_type ts = *in;
  TU_MATCH(
      (item_type, ts),
      (item_type_type, {
        PUTS("type");
      }),
      (item_type_module, {
        PUTS("module");
      }),
      (item_type_sint, {
        PUTS("i{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      (item_type_uint, {
        PUTS("u{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      (item_type_ptr, {
        PUTS("ptr(");
        if ($in.type)
          USENAMEDPRINTER("item_type", $in.type);
        PUTS(")");
      }),
      (item_type_struct, {
        PUTS("struct { ");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len; i++) {
            if (i > 0)
              PUTS(", ");
            PUTS("[");
            USETYPEPRINTER(usize, $in.types[i].offset);
            PUTS("]:");
            if ($in.types[i].type)
              USENAMEDPRINTER("item_type", $in.types[i].type);
          }
        }
        PUTS(" }");
      }),
      (item_type_union, {
        PUTS("union { ");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len; i++) {
            if (i > 0)
              PUTS(", ");
            if ($in.types[i])
              USENAMEDPRINTER("item_type", $in.types[i]);
          }
        }
        PUTS(" }");
      }),
      (item_type_block, {
        PUTS("block(");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len - 1; i++) {
            if (i > 0)
              PUTS(", ");
            USENAMEDPRINTER("item_type", &$in.types[i]);
          }
          PUTS(") -> ");
          USENAMEDPRINTER("item_type", &$in.types[len - 1]);
        } else {
          PUTS(")");
        }
      }),
      (default, {
        PUTS("unknown");
      })
  );
});
