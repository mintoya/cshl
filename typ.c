#include "cshl.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/sList.h"
#include "wheels/tu_macros.h"
// TODO move off asserts
#pragma push_macro("max")
#pragma push_macro("min")
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

static usize item_type_size(item_type *t);
static usize item_type_alignment(item_type *t);

static usize item_type_size(item_type *t) {
  item_type ts = *t;
  tu_match(
      ts,
      case (item_type_struct, $in, {
        usize len = msList_len($in.types);
        if (!len)
          return 0;
        var_ lastitem = $in.types[len - 1];
        usize size = lastitem.offset + item_type_size(lastitem.type);
        usize align = item_type_alignment(t);
        return lineup(size, align);
      }),
      case (item_type_union, $in, {
        usize max_size = 0;
        foreach (var_ t_ptr, vla(*msList_vla($in.types)))
          max_size = max(max_size, item_type_size(t_ptr));
        usize align = item_type_alignment(t);
        return lineup(max_size, align);
      }),
      case (item_type_sint, $in, {
        return lineup($in.bitwidth, 8) / 8;
      }),
      case (item_type_uint, $in, {
        return lineup($in.bitwidth, 8) / 8;
      }),
      case (item_type_ptr, _, {
        return sizeof(void *);
      }),
      case (item_type_array, $in, {
        return item_type_size($in.type) * $in.count;
      }),
      case (item_type_block, _, {
        assertMessage(false, "size of function");
      }),
      case (item_type_type, _, {
        assertMessage(false, "size of type type");
      }),
      default(assertMessage(false, "unhandled type : %s", snprint(stdAlloc, "{}", (usize)t->tag).ptr);)
  );
  assertMessage(false, "unreachable");
  return 0;
}

static usize item_type_alignment(item_type *t) {
  item_type ts = *t;
  tu_match(
      ts,
      case (item_type_struct, $in, {
        return $in.alignment;
      }),
      case (item_type_union, $in, {
        usize max_align = 1;
        foreach (var_ t_ptr, vla(*msList_vla($in.types)))
          max_align = max(max_align, item_type_alignment(t_ptr));
        return max_align;
      }),
      case (item_type_sint, $in, {
        return lineup($in.bitwidth, 8) / 8;
      }),
      case (item_type_uint, $in, {
        return lineup($in.bitwidth, 8) / 8;
      }),
      case (item_type_ptr, _, {
        return sizeof(void *);
      }),
      case (item_type_array, $in, {
        return item_type_alignment($in.type);
      }),
      case (item_type_block, _, {
        assertMessage(false, "alignment of function");
      }),
      case (item_type_type, _, {
        assertMessage(false, "alignment of type type");
      }),
      default(assertMessage(false, "unhandled type : %s", snprint(stdAlloc, "{}", (usize)t->tag).ptr);)
  );
  assertMessage(false, "unreachable");
  return 0;
}

bool item_type_equal(item_type *a, item_type *b) {
  if (a->tag != b->tag)
    return false;

  switch (a->tag) {
    case TU_TAG(item_type_type): {
      return true;
    } break;
    case TU_TAG(item_type_array): {
      return item_type_equal(
                 a->item_type_array.type,
                 b->item_type_array.type
             ) &&
             a->item_type_array.count == b->item_type_array.count;
    } break;
    case TU_TAG(item_type_ptr): {
      return item_type_equal(
          a->item_type_ptr.type,
          b->item_type_ptr.type
      );
    } break;
    case TU_TAG(item_type_sint): {
      return ( // TODO idk about alignment
          a->item_type_sint.bitwidth == b->item_type_sint.bitwidth
      );
    } break;
    case TU_TAG(item_type_uint): {
      return ( // TODO idk about alignment
          a->item_type_uint.bitwidth == b->item_type_uint.bitwidth
      );
    } break;
    case TU_TAG(item_type_struct): {
      var_ alist = a->item_type_struct.types;
      var_ blist = b->item_type_struct.types;
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
    case TU_TAG(item_type_union): {
      var_ alist = a->item_type_union.types;
      var_ blist = b->item_type_union.types;
      usize la = msList_len(alist);
      usize lb = msList_len(blist);
      if (la != lb)
        return false;
      foreach (var_ i, range(0, la))
        if (!item_type_equal(alist[i], blist[i]))
          return false;
      return true;
    } break;
    case TU_TAG(item_type_block): {
      var_ alist = a->item_type_block.types;
      var_ blist = b->item_type_block.types;
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
REGISTER_SPECIAL_PRINTER("item_type", item_type, {
  item_type ts = in;
  tu_match(
      ts,
      case (item_type_type, $in, {
        PUTS("type");
      }),
      case (item_type_sint, $in, {
        PUTS("i{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      case (item_type_uint, $in, {
        PUTS("u{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      case (item_type_ptr, $in, {
        PUTS("*{");
        if ($in.type)
          USENAMEDPRINTER("item_type", $in.type[0]);
        PUTS("}");
      }),
      case (item_type_struct, $in, {
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
              USENAMEDPRINTER("item_type", $in.types[i].type[0]);
          }
        }
        PUTS(" }");
      }),
      case (item_type_union, $in, {
        PUTS("union{ ");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len; i++) {
            if (i > 0)
              PUTS(", ");
            if ($in.types[i])
              USENAMEDPRINTER("item_type", $in.types[i][0]);
          }
        }
        PUTS(" }");
      }),
      case (item_type_block, $in, {
        PUTS("block{");
        PUTS("(");
        foreach (usize i, range(0, msList_len($in.types) - 1)) {
          if (i)
            PUTS(",");
          if ($in.types[i])
            USENAMEDPRINTER("item_type", ($in.types[i][0]));
        }
        PUTS(")");
        USENAMEDPRINTER("item_type", $in.types[msList_len($in.types) - 1][0]);
        PUTS("}");
      }),
      default(PUTS("unknown");)
  );
});
