#include "ast.c"
#include "typ.c"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/tagged_unions.h"
#include <stdio.h>
#include <string.h>

typedef struct symbol symbol;
typedef struct symbol {
  item_type *type;
  union {
    usize location;
    msHmap(symbol) module;
  };
} symbol;

constexpr item_type module_baseSymbol[1] = {{.tag = TU_MK_TAG(item_type, item_type_module)}};

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
item_type *get_itype(struct intHandle h) {
  static mHmap(typeof(h), item_type *) imap = NULL;
  if (!imap) {
    imap = mHmap_init(stdAlloc, typeof(h), item_type *);
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
              stdAlloc,
              TU_OF(
                  (item_type, item_type_uint),
                  ((item_type_uint){
                      .bitwidth = j,
                      .alignment = j
                  })
              )
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
            stdAlloc,
            TU_OF(
                (item_type, item_type_uint),
                ((item_type_uint){
                    .bitwidth = h.bitcount,
                    .alignment = lineup(h.bitcount, 8) * 8,
                })
            )
        )
    );
    res = mHmap_get(imap, h);
    assertMessage(res);
    return *res;
  }
}
item_type *make_ptr(AllocatorV allocator, item_type *type) {
  var_ res = item_type_newStub(allocator);
  *res = TU_OF(
      (item_type, item_type_ptr),
      ((item_type_ptr){
          .type = type
      })
  );
  return res;
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
symbol symbolResolve(mList(msHmap(symbol)) symstack, fptr sym) {
  foreach (usize i, range(mList_len(symstack), 0)) {
    var_ v = msHmap_get(mList_arr(symstack)[i - 1], sym);
    if (v)
      return *v;
  }
  return (symbol){};
}

symbol interpret(AllocatorV allocator, mList(u8) stack, astNode *node, mList(msHmap(symbol)) symbols) {
  assertMessage(false, "unimplemented");
}

int main(void) {
  u8 c[] =
      {
#embed "int.txt"
      };
  var_ list = astNode_process_file(stdAlloc, fp(c));
  println("{msList : astNode}", list);
  println("{msList : astNode : numbers}", list);
  var_ stack = mList_init(stdAlloc, u8);

  var_ symbols = mList_init(stdAlloc, msHmap(symbol));
  mList_push(symbols, msHmap_init(stdAlloc, symbol));
  foreach (var_ node, vla(*msList_vla(list)))
    interpret(stdAlloc, stack, node, symbols);
}
#include "wheels/wheels.h"
