#include "cshl.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/sList.h"
#include "wheels/tagged_unions.h"
// TODO move off asserts
#pragma push_macro("max")
#pragma push_macro("min")
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

static usize type_size(item_type *t);
static usize type_alignment(item_type *t);

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
bool item_type_equal(item_type *a, item_type *b) {
  if (a->tag != b->tag)
    return false;

  switch (a->tag) {
    case TU_MK_TAG(item_type, item_type_type): {
      return true;
    } break;
    case TU_MK_TAG(item_type, item_type_ptr): {
      return item_type_equal(
          a->_item_type_ptr.type,
          b->_item_type_ptr.type
      );
    } break;
    case TU_MK_TAG(item_type, item_type_sint): {
      return ( // TODO idk about alignment
          a->_item_type_sint.bitwidth == b->_item_type_sint.bitwidth
      );
    } break;
    case TU_MK_TAG(item_type, item_type_uint): {
      return ( // TODO idk about alignment
          a->_item_type_sint.bitwidth == b->_item_type_sint.bitwidth
      );
    } break;
    case TU_MK_TAG(item_type, item_type_struct): {
      var_ alist = a->_item_type_struct.types;
      var_ blist = b->_item_type_struct.types;
      usize la = msList_len(alist);
      usize lb = msList_len(blist);
      if (la != lb)
        return false;
      foreach (var_ i, range(0, la)) {
        if (alist[i].offset != blist[i].offset)
          return false;
        if (!item_type_equal(alist[i].type, blist[i].type))
          return false;
      }
      return true;
    } break;
    case TU_MK_TAG(item_type, item_type_union): {
      var_ alist = a->_item_type_union.types;
      var_ blist = b->_item_type_union.types;
      usize la = msList_len(alist);
      usize lb = msList_len(blist);
      if (la != lb)
        return false;
      foreach (var_ i, range(0, la))
        if (!item_type_equal(alist[i], blist[i]))
          return false;
      return true;
    } break;
    case TU_MK_TAG(item_type, item_type_block): {
      var_ alist = a->_item_type_block.types;
      var_ blist = b->_item_type_block.types;
      usize la = msList_len(alist);
      usize lb = msList_len(blist);
      if (la != lb)
        return false;
      foreach (var_ i, range(0, la))
        if (!item_type_equal(alist[i], blist[i]))
          return false;
      return true;
    } break;
  }
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
        PUTS("*{");
        if ($in.type)
          USENAMEDPRINTER("item_type", $in.type);
        PUTS("}");
      }),
      (item_type_struct, {
        PUTS("struct{ ");
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
        PUTS("union{ ");
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
        PUTS("block{");
        PUTS("(");
        foreach (usize i, range(0, msList_len($in.types) - 1)) {
          if (i)
            PUTS(",");
          if ($in.types[i])
            USENAMEDPRINTER("item_type", ($in.types[i]));
        }
        PUTS(")");
        USENAMEDPRINTER("item_type", $in.types[msList_len($in.types) - 1]);
        PUTS("}");
      }),
      (default, {
        PUTS("unknown");
      })
  );
});
