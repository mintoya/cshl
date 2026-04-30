#include "cshl.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/tagged_unions.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "ast.c"
#include "typ.c"
#include "wheels/tu_macros.h"

static_assert(!(sizeof(symbol) % sizeof(usize)));

item_type *item_type_newStub(AllocatorV typesArena) { return aCreate(typesArena, item_type); }
item_type *item_type_allocate_from(AllocatorV allocator, item_type i) {
  var_ res = item_type_newStub(allocator);
  *res = i;
  return res;
}
struct intHandle {
  bool issigned;
  usize bitcount;
};
AllocatorV iType_alllocator = NULL;
item_type *get_itype(struct intHandle h) {
  assertMessage(iType_alllocator);
  static mHmap(typeof(h), item_type *) imap = NULL;
  if (!imap) {
    imap = mHmap_init(iType_alllocator, typeof(h), item_type *);
    // set up signed and unsigned
    // 8, 16, 32, 64
    usize widths[] = {8, 16, 32, 64};
    foreach (usize j, vla(widths))
      mHmap_set(
          imap,
          ((typeof(h)){
              .issigned = false,
              .bitcount = j,
          }),
          item_type_allocate_from(
              iType_alllocator,
              ITYPE_OF(((item_type_uint){j, j}))
          )
      );
  }
  item_type **res = mHmap_get(imap, h);
  if (res)
    return *res;
  else {
    mHmap_set(
        imap,
        h,
        item_type_allocate_from(
            iType_alllocator,
            h.issigned
                ? ITYPE_OF(((item_type_sint){
                      .bitwidth = h.bitcount,
                      .alignment = lineup(h.bitcount, 8) / 8,
                  }))
                : ITYPE_OF(((item_type_uint){
                      .bitwidth = h.bitcount,
                      .alignment = lineup(h.bitcount, 8) / 8,
                  }))
        )
    );
    res = mHmap_get(imap, h);
    assertMessage(res);
    return *res;
  }
}
item_type *make_ptr(AllocatorV allocator, item_type *type) {
  var_ res = item_type_newStub(allocator);
  *res = ITYPE_OF(((item_type_ptr){.type = type}));
  return res;
}
item_type *make_block(AllocatorV allocator, msList(item_type *) typething) {
  var_ res = item_type_newStub(allocator);
  *res = ITYPE_OF((item_type_block){.types = typething});
  return res;
}
typedef typeof(*((item_type_struct *)NULL)->types) struct_inner;
item_type *make_struct(AllocatorV allocator, msList(struct_inner) typething) {
  var_ res = item_type_newStub(allocator);
  *res = ITYPE_OF((item_type_struct){.types = typething});
  return res;
}

item_type *get_ptr_iType(item_type *t) {
  tu_match(
      (*t),
      case (item_type_ptr, $in, {
        return $in.type;
      }),
      default({
        assertMessage(
            false,
            "tried to get inner pointer to %s",
            snprint(stdAlloc, "{item_type}", &t).ptr
        );
      })
  );
}
usize get_int_bitwidth(item_type *t) {
  tu_match(
      (*t),
      case (item_type_uint, $in, return $in.bitwidth;),
      case (item_type_sint, $in, return $in.bitwidth;),
      default({
        assertMessage(
            false,
            "tried to bitwidth to %s",
            snprint(stdAlloc, "{item_type}", &t).ptr
        );
      })
  );
}
bool isDigit(u8 u) { return !(u > '9' || u < '0'); }
usize fptr_to_number(fptr f) {
  usize res = 0;
  foreach (u8 *c, span(f.ptr, f.len)) {
    if (!isDigit(*c))
      break;
    res *= 10;
    res += *c - '0';
  }
  return res;
}
bool fptr_is_number(fptr f) {
  foreach (u8 *c, span(f.ptr, f.len))
    if (!isDigit(*c))
      return false;
  return true;
}
symbol *symbolResolve(mList(msHmap(symbol)) symstack, fptr sym) {
  foreach (usize i, range(mList_len(symstack), 0)) {
    var_ v = msHmap_get(mList_arr(symstack)[i - 1], sym);
    if (v)
      return v;
  }
  return NULL;
}

sym_rvalue coerce(
    AllocatorV allocator,
    mList(u8) stack,
    symbol sym,
    item_type *to
) {
  var_ srct = sym.type[0];
  var_ dstt = to[0];
  u8 *src_ptr = 0;

  tu_match(
      sym.kind, case (sym_lvalue, val, {
        src_ptr = (u8 *)mList_arr(stack) + val;
      }),
      case (sym_rvalue, val, {
        src_ptr = val.ptr;
      }),
      case (sym_type, _, {
        src_ptr = (u8 *)sym.type;
      }),
      default(assertMessage(false, "casting non-value");)
  );

  usize ds = type_size(to);
  usize ss = type_size(sym.type);

  tu_match(
      dstt,
      case (item_type_type, _, {
        tu_match(
            srct,
            case (item_type_type, _, return slice_alloc(allocator, u8, 1);),
            default(return nullFptr;)
        );
      }),
      case (item_type_uint, dst_uint, {
        tu_match(
            srct,
            case (item_type_uint, src_uint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              for (usize i = 0; i < ds; i++)
                dp[i] = 0;
              for (usize i = 0; i < cp; i++)
                dp[i] = src_ptr[i];

              return (sym_rvalue){ds, dp};
            }),
            case (item_type_sint, src_sint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              for (usize i = 0; i < ds; i++)
                dp[i] = 0;
              for (usize i = 0; i < cp; i++)
                dp[i] = src_ptr[i];

              return (sym_rvalue){ds, dp};
            }),
            default(return nullFptr;)
        );
      }),
      case (item_type_sint, dst_sint, {
        tu_match(
            srct,
            case (item_type_uint, src_uint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              for (usize i = 0; i < ds; i++)
                dp[i] = 0;
              for (usize i = 0; i < cp; i++)
                dp[i] = src_ptr[i];

              return (sym_rvalue){ds, dp};
            }),
            case (item_type_sint, src_sint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              u8 sign = (src_ptr[ss - 1] & 0x80) ? 0xFF : 0x00;
              for (usize i = 0; i < ds; i++) {
                dp[i] = (i < ss) ? src_ptr[i] : sign;
              }

              return (sym_rvalue){ds, dp};
            }),
            default(return nullFptr;)
        );
      }),
      default(return nullFptr;)
  );

  return nullFptr;
}

symbol interpret(
    AllocatorV allocator,
    mList(u8) stack,
    mList(usize) stack_frames,
    astNode *node,
    mList(msHmap(symbol)) symbols
) {
  defer {
    assertMessage(
        !(mList_len(stack) % 8),
        "last instruction misaligned stack : %s",
        snprint(stdAlloc, "{astNode} , {slice(c8)}", node, node->text).ptr
    );
  };

  switch (node->op) {
    default:
      assertMessage(false, "%s", snprint(stdAlloc, "{builtin_OP}", node->op).ptr);
      break;
  }
}
#include <stdio.h>

// responsible for writing to the return adress
// TODO ffi

fptr read_stdin(AllocatorV allocator) {
  usize size = 0;
  c8 *data = NULL;
  usize capacity = 4096;
  size = 0;
  data = (c8 *)aAlloc(allocator, capacity);
  usize bytes;
  while ((bytes = fread(data + size, 1, capacity - size, stdin)) > 0) {
    size += bytes;
    if (size == capacity) {
      data = (c8 *)aResize(allocator, data, capacity, (capacity * 2));
      capacity *= 2;
    }
  }
  return (fptr){size, (u8 *)data};
}
int main(void) {

  AllocatorV astArena = arena_new_ext(stdAlloc, 512);
  defer { arena_cleanup(astArena); };

  var_ stin = read_stdin(stdAlloc);
  defer { slice_free(stdAlloc, stin); };
  var_ list = astNode_process_file(astArena, stin);

  iType_alllocator = arena_new_ext(stdAlloc, 512);
  defer { arena_cleanup(iType_alllocator); };

  //   c8 lit[] =
  //       {
  // #embed "int.txt"
  //       };
  //   var_ list = astNode_process_file(stdAlloc, fp(lit));
  // println("{msList : astNode}", list);
  // println("{msList : astNode : numbers}", list);

  var_ stack = mList_init(stdAlloc, u8);
  defer { mList_deInit(stack); };

  AllocatorV symArena = arena_new_ext(stdAlloc, 512);
  defer { arena_cleanup(symArena); };

  var_ stack_frames = mList_init(symArena, usize);
  var_ symbols = mList_init(symArena, msHmap(symbol));
  mList_push(symbols, (msHmap_init(symArena, symbol)));

  foreach (var_ node, vla(*msList_vla(list)))
    interpret(
        stdAlloc,
        stack,
        stack_frames,
        node,
        symbols
    );
  println("stack capacity : {}", mList_cap(stack));
}
#include "wheels/wheels.h"
