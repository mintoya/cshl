#include "cshl.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include <assert.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "sexp_parser.c"
#include "typ.c"
#include "wheels/omap.h"
#include "wheels/sList.h"
#include "wheels/tu_macros.h"

#define max(a, b) ({             \
  var_ _a = a;                   \
  var_ _b = b;                   \
  (((_a) > (_b)) ? (_a) : (_b)); \
})
#define min(a, b) ({             \
  var_ _a = a;                   \
  var_ _b = b;                   \
  (((_a) < (_b)) ? (_a) : (_b)); \
})

static_assert(!(sizeof(symbol) % sizeof(usize)));

AllocatorV type_allocator = NULL;

mHmap(item_type, item_type *) tmap = NULL;

int c_tl(const void *a, const void *b) {
  assertMessage(a && b);
  var_ al = *(msList(uptr) *)a;
  var_ bl = *(msList(uptr) *)b;
  var_ ca = ((fptr){sizeof(*msList_vla(al)), (u8 *)al});
  var_ cb = ((fptr){sizeof(*msList_vla(bl)), (u8 *)bl});
  return fptr_cmp(ca, cb);
}

bool memzero(void *ptr, usize len) {
  foreach (var_ i, span((u8 *)ptr, len))
    if (*i)
      return false;
  return true;
}
msList(item_type *) cache_typeList(msList(item_type *) types) {
  static msList(msList(item_type *)) listList = nullptr;
  listList = listList ?: msList_init(type_allocator, typeof(*listList), 100);
  var_ f = bbsearch(&types, listList, msList_len(listList), sizeof(*listList), c_tl);
  if (!f.f) {
    usize place = (msList(item_type *) *)f.p - (msList(item_type *) *)listList;
    msList_ins(type_allocator, listList, place, msList_clone(type_allocator, types));
    f = bbsearch(&types, listList, msList_len(listList), sizeof(*listList), c_tl);
    assertMessage(f.f);
  }
  return *(msList(item_type *) *)f.p;
}
msList(uptr) cache_offsetList(msList(uptr) types) {
  static msList(msList(uptr)) listList = nullptr;
  listList = listList ?: msList_init(type_allocator, typeof(*listList), 100);
  var_ f = bbsearch(&types, listList, msList_len(listList), sizeof(*listList), c_tl);
  if (!f.f) {
    uptr place = (msList(uptr) *)f.p - (msList(uptr) *)listList;
    msList_ins(type_allocator, listList, place, msList_clone(type_allocator, types));
    f = bbsearch(&types, listList, msList_len(listList), sizeof(*listList), c_tl);
    assertMessage(f.f);
  }
  return *(msList(uptr) *)f.p;
}
item_type *make_type(item_type t0) {
  item_type t = {};
  t.tag = t0.tag;
  TU_MATCH(t0) {
    TU_OF(item_type_type, k) { t.item_type_type = k; }
    TU_OF(item_type_ptr, k) { t.item_type_ptr = k; }
    TU_OF(item_type_array, k) { t.item_type_array = k; }
    TU_OF(item_type_sint, k) { t.item_type_sint = k; }
    TU_OF(item_type_uint, k) { t.item_type_uint = k; }
    TU_OF(item_type_struct, k) { t.item_type_struct = k; }
    TU_OF(item_type_union, k) { t.item_type_union = k; }
    TU_OF(item_type_block, k) { t.item_type_block = k; }
  }
  TU_MATCH(t) {
    TU_OF(item_type_struct, s) {
      t.item_type_struct.types = cache_typeList(s.types);
      t.item_type_struct.offsets = cache_offsetList(s.offsets);
    }
    TU_OF(item_type_union, s) {
      t.item_type_union.types = cache_typeList(s.types);
    }
    TU_OF(item_type_block, s) {
      t.item_type_block.types = cache_typeList(s.types);
    }
  }
  tmap = tmap ?: mHmap_init(type_allocator, item_type, item_type *);
  return *mHmap_GetOrSet(
      tmap, t, ({
        var_ rp = aCreate(type_allocator, item_type);
        *rp = t;
        rp;
      })
  );
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
            snprint(stdAlloc, " {item_type}", t[0]).ptr
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
            snprint(stdAlloc, "{item_type}", t[0]).ptr
        );
      })
  );
}
bool is_digit(u8 u) { return !(u > '9' || u < '0'); }
usize fptr_to_number(fptr f) {
  usize res = 0;
  foreach (u8 *c, span(f.ptr, f.len)) {
    if (!is_digit(*c))
      break;
    res *= 10;
    res += *c - '0';
  }
  return res;
}
bool fptr_is_number(fptr f) {
  if (!f.len)
    return false;
  foreach (u8 *c, span(f.ptr, f.len))
    if (!is_digit(*c))
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

fptr coerce(
    AllocatorV allocator,
    mList(u8) stack,
    symbol sym,
    item_type *to
) {
  // println("coercing {item_type} to {item_type}", sym.type[0], to[0]);
  var_ srct = sym.type[0];
  var_ dstt = to[0];
  u8 *src_ptr = 0;
  tu_match(
      sym.kind, case (sym_value, val, {
        src_ptr = (u8 *)mList_arr(stack) + val;
      }),
      case (sym_type, _, {
        tu_match(
            dstt,
            case (item_type_type, _, return slice_alloc(allocator, u8, 1);),
            default(return nullFptr;)
        );
      }),
      default(assertMessage(false, "casting non-value");)
  );

  usize ds = item_type_size(&dstt);

  usize ss = item_type_size(&srct);

  tu_match(
      dstt,
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

              return (fptr){ds, dp};
            }),
            case (item_type_sint, src_sint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              for (usize i = 0; i < ds; i++)
                dp[i] = 0;
              for (usize i = 0; i < cp; i++)
                dp[i] = src_ptr[i];

              return (fptr){ds, dp};
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

              return (fptr){ds, dp};
            }),
            case (item_type_sint, src_sint, {
              u8 *dp = slice_alloc(allocator, u8, ds).ptr;
              usize cp = ss < ds ? ss : ds;

              u8 sign = (src_ptr[ss - 1] & 0x80) ? 0xFF : 0x00;
              for (usize i = 0; i < ds; i++) {
                dp[i] = (i < ss) ? src_ptr[i] : sign;
              }

              return (fptr){ds, dp};
            }),
            default(return nullFptr;)
        );
      }),
      default(return nullFptr;)
  );

  return nullFptr;
}

#define assertprint(bool, fmt, ...) assertMessage(bool, "%s", snprint(stdAlloc, fmt, __VA_ARGS__).ptr)
fptr checkLiteral(astNode *node, char *message) {
  assertprint(node->op == builtin_NONE, "not a literal : {slice(c8)} : {cstr}", node->text, message);
  assertprint(!(node->args), "literal has args: {astNode*} : {cstr}", node, message);
  var_ res = node->text;
  assertprint(res.len && res.ptr, "not a literal : {slice(c8)} : {cstr}", node->text, message);
  return res;
}
msList(astNode *) checkList(astNode *node, char *message) {
  assertprint(node->op == builtin_NONE, "not a literal : {astNode*} {cstr}", node, message);
  assertprint(node->args, "not a literal : {astNode*} {cstr}", node, message);
  return node->args;
}

sliceDef(symbol);

void push_args(
    slice(symbol) args,
    mList(u8) stack,
    mList(usize) stack_frames,
    msList(astNode *) node,
    mList(msHmap(symbol)) symbols
) {
  if (!args.len)
    return;

  usize frame_start = mList_last(stack_frames);
  usize num_args = args.len;

  mList_pushArr(stack, *VLAP((u8 *)NULL, num_args * sizeof(symbol)));

  for (usize i = 0; i < num_args; i++) {
    symbol sym = args.ptr[i];

    if (SYM_IS(value, sym.kind)) {
      usize size = item_type_size(sym.type);
      usize align = item_type_alignment(sym.type);

      while (mList_len(stack) % align)
        mList_push(stack, 0);

      usize new_offset = mList_len(stack);
      usize src_offset = sym.kind.sym_value;

      for (usize b = 0; b < size; b++)
        mList_push(stack, 0);

      memcpy(mList_arr(stack) + new_offset, mList_arr(stack) + src_offset, size);
      sym.kind = SYM_OF((sym_value){new_offset});
    }

    symbol *frame_symbols = (symbol *)(mList_arr(stack) + frame_start);
    frame_symbols[i] = sym;
  }

  while (mList_len(stack) % 8) {
    mList_push(stack, 0);
  }
}
symbol pull_arg(
    usize idx,
    mList(u8) stack,
    mList(usize) stack_frames,
    mList(msHmap(symbol)) symbols
) {
  usize frame_start = mList_last(stack_frames);
  symbol *frame_symbols = (symbol *)(mList_arr(stack) + frame_start);
  return frame_symbols[idx];
}
symbol ffi_call(
    mList(u8) stack,
    mList(usize) stack_frames,
    item_type_block callType,
    mList(msHmap(symbol)) symbols
) {
}
symbol interpret(
    mList(u8) stack,
    mList(usize) stack_frames,
    astNode *node,
    mList(msHmap(symbol)) symbols
);
static symbol interpret_call(
    symbol dst,
    symbol function,
    msList(astNode *) argslist,
    mList(u8) stack,
    mList(usize) stack_frames,
    mList(msHmap(symbol)) symbols
) {
  var_ function_ops = tu_assert(sym_function, function.kind);
  var_ function_typ = tu_assert(item_type_block, function.type[0]);
  var_ return_type = msList_last(function_typ.types);

  usize return_type_size;
  tu_match(
      return_type[0],
      case (item_type_block, _, return_type_size = 0;),
      case (item_type_type, _, return_type_size = 0;),
      default(return_type_size = item_type_size(return_type);)
  );

  var_ old_stack_allocator = msHmap_allocator(mList_last(symbols));
  var_ new_stack_allocator = arena_new_ext(old_stack_allocator, 1024);
  mList_push(symbols, msHmap_init(new_stack_allocator, symbol));
  defer { arena_cleanup(msHmap_allocator(mList_pop(symbols))); };

  usize next_frame = mList_len(stack);
  usize call_len = msList_len(function_typ.types) - 1;

  slice(symbol) s = {};
  if (call_len) {
    s = slice_alloc(new_stack_allocator, symbol, call_len);
    foreach (var_ i, span(0, s.len))
      s.ptr[i] = interpret(stack, stack_frames, argslist[i], symbols);
  }

  mList_push(stack_frames, next_frame);
  defer { mList_len(stack) = mList_pop(stack_frames); };

  push_args(s, stack, stack_frames, argslist, symbols);

  msHmap(usize) label_to_line = msHmap_init(new_stack_allocator, usize);
  for (usize i = 0; i < msList_len(function_ops); i++) {
    if (function_ops[i]->op == builtin_LABEL) {
      assertMessage(function_ops[i]->args && msList_len(function_ops[i]->args) == 1);
      var_ name = checkLiteral(function_ops[i]->args[0], "label name");
      msHmap_set(label_to_line, name, i);
    }
  }

  for (usize i = 0; i < msList_len(function_ops); i++) {
    var_ it = function_ops[i];
    switch (it->op) {
      case builtin_LABEL:
        break;
      case builtin_JMP_IF:
      case builtin_JMP: {
        if (it->op == builtin_JMP)
          assertMessage(it->args && msList_len(it->args) == 1);
        else
          assertMessage(it->args && msList_len(it->args) == 2);

        var_ name = checkLiteral(it->args[0], "jmp target");

        usize *target = msHmap_get(label_to_line, name);
        assertprint(target, "unknown label : {slice(c8)}", name);

        if (it->op == builtin_JMP)
          i = *target;
        else {
          usize curr = mList_len(stack);
          var_ cond = interpret(stack, stack_frames, it->args[1], symbols);
          var_ v = tu_assert(sym_value, cond.kind) + mList_arr(stack);
          var_ size = item_type_size(cond.type);
          if (!memzero(v, size))
            i = *target;
          mList_len(stack) = curr;
        }

      } break;
      case builtin_RETURN: {
        assertMessage(it->args && msList_len(it->args) == 1);
        var_ res = interpret(stack, stack_frames, it->args[0], symbols);
        tu_match(
            return_type[0],
            case (item_type_type, _, {
              assertprint(SYM_IS(type, res.kind), "");
              return (symbol){.type = res.type, .kind = SYM_OF((sym_type){})};
            }),
            default({
              assertprint(SYM_IS(value, res.kind), "");
              assertprint(item_type_equal(res.type, return_type), "return type mismatch");
              var_ resf = coerce(new_stack_allocator, stack, res, dst.type);
              memcpy(
                  mList_arr(stack) + dst.kind.sym_value,
                  resf.ptr,
                  resf.len
              );
              return dst;
            });
        );
      } break;
      default:
        interpret(stack, stack_frames, it, symbols);
    }
  }
  return dst;
}
symbol interpret(
    mList(u8) stack,
    mList(usize) stack_frames,
    astNode *node,
    mList(msHmap(symbol)) symbols
) {

  // literals can stay, declared values can stay
  struct {
    usize current_length;
    bool keep;
  } keep_len = {
      mList_len(stack),
      true,
  };
  switch (node->op) {
    case builtin_INIT:
    case builtin_DECL:
    case builtin_NONE:
      keep_len.keep = false;
      break;
    default:
      keep_len.keep = true;
      break;
  }
  defer {
    if (keep_len.keep) {
      mList_len(stack) = keep_len.current_length;
    } else
      // ;
      while (mList_len(stack) % 8)
        mList_push(stack, 0);

    println("{}", ((fptr){sizeof(*mList_vla(stack)), mList_arr(stack)}));
  };
  switch (node->op) {

      //
      // misc
      //

    case builtin_NONE: {
      fptr f = checkLiteral(node, "leaf literal");
      if (fptr_is_number(f)) {

        usize val = fptr_to_number(f);
        while (mList_len(stack) % sizeof(usize))
          mList_push(stack, 0);

        usize addr = mList_len(stack);

        mList_pushArr(stack, *VLAP((u8 *)&val, sizeof(val)));

        return (symbol){
            .type = make_type(ITYPE_OF(((item_type_uint){0, sizeof(usize) * 8}))),
            .kind = SYM_OF((sym_value){addr})
        };
      }

      var_ s = symbolResolve(symbols, f);
      assertprint(s, "unknown symbol : {slice(c8)}", f);
      return *s;
    } break;

      //
      // types
      //

    case builtin_SINT:
    case builtin_UINT: {
      assertMessage(node->args && msList_len(node->args) == 1);
      fptr number = checkLiteral(node->args[0], "integer bitcount");
      assertMessage(fptr_is_number(number));
      u32 bitwidth = (u32)fptr_to_number(number);
      item_type t =
          node->op == builtin_SINT
              ? ITYPE_OF(((item_type_sint){0, bitwidth}))
              : ITYPE_OF(((item_type_uint){0, bitwidth}));
      return (symbol){
          .type = make_type(t),
          .kind = SYM_OF((sym_type){})
      };
    } break;
    case builtin_TYPE: {
      static var_ typetype = ITYPE_OF((item_type_type){});
      return (symbol){
          .type = &typetype,
          .kind = SYM_OF((sym_type){})
      };
    } break;
    case builtin_PTR: {
      assertMessage(node->args && msList_len(node->args) == 1);
      AllocatorV tscope = msHmap_allocator(mList_last(symbols));
      var_ typesym = interpret(stack, stack_frames, node->args[0], symbols);
      assertMessage(SYM_IS(type, typesym.kind));
      return (symbol){
          .type = make_type(ITYPE_OF(((item_type_ptr){0, typesym.type}))),
          .kind = SYM_OF((sym_type){})
      };
    } break;
    case builtin_STRUCT: {
      assertMessage(node->args && msList_len(node->args) >= 0); // allow empty struct
      usize member_count = msList_len(node->args);

      struct {
        msList(usize) offsets;
        msList(item_type *) types;
      } members = {
          .types = msList_init(
              stdAlloc,
              item_type *,
              member_count
          ),
          .offsets = msList_init(
              stdAlloc,
              usize,
              member_count
          )
      };
      defer { msList_deInit(stdAlloc, members.types); };
      defer { msList_deInit(stdAlloc, members.offsets); };

      usize current_offset = 0;
      usize max_align = 1;

      for (usize i = 0; i < member_count; i++) {
        var_ member_node = node->args[i];
        var_ sym = interpret(stack, stack_frames, member_node, symbols);
        assertMessage(SYM_IS(type, sym.kind), "STRUCT member must be a type");
        item_type *member_type = sym.type;

        usize align = item_type_alignment(member_type);
        max_align = max(max_align, align);

        usize offset = lineup(current_offset, align);

        // println("struct : {item_type}", member_type[0]);
        msList_push(type_allocator, members.types, member_type);
        msList_push(type_allocator, members.offsets, offset);

        current_offset = offset + item_type_size(member_type);
      }

      usize struct_size = lineup(current_offset, max_align);

      item_type type_val = ITYPE_OF(((item_type_struct){
          .alignment = (u32)max_align,
          .types = members.types,
          .offsets = members.offsets
      }));

      return (symbol){
          .type = make_type(type_val),
          .kind = SYM_OF((sym_type){})
      };
    } break;

      //
      // functions
      //

    case builtin_BLOCK: {
      assertMessage(node->args && msList_len(node->args) == 3);
      var_ arglist_node = checkList(node->args[0], "parsing arglist");
      var_ rtype_node = node->args[1];
      var_ opslist_node = checkList(node->args[2], "parsing opslist");
      var_ typeslist = msList_init(
          stdAlloc,
          item_type *,
          msList_len(arglist_node)
      );
      defer { msList_deInit(stdAlloc, typeslist); };
      foreach (var_ type, vla(*msList_vla(arglist_node))) {
        var_ sym = interpret(stack, stack_frames, type, symbols);
        assertMessage(SYM_IS(type, sym.kind));
        var_ ttype = sym.type;
        msList_push(type_allocator, typeslist, ttype);
      }
      {
        var_ sym = interpret(stack, stack_frames, rtype_node, symbols);
        assertMessage(SYM_IS(type, sym.kind));
        var_ type = sym.type;
        msList_push(type_allocator, typeslist, type);
      }

      return (symbol){
          .kind = SYM_OF((sym_function){opslist_node}),
          .type = make_type(ITYPE_OF(((item_type_block){typeslist}))),
      };
    } break;

    case builtin_CALL: {
      assertMessage(node->args && msList_len(node->args) == 3, "%i", msList_len(node->args));
      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);
      var_ function = interpret(stack, stack_frames, node->args[1], symbols);
      var_ argslist = checkList(node->args[2], "called argslist");
      return interpret_call(dst, function, argslist, stack, stack_frames, symbols);
    } break;

    case builtin_LABEL:
    case builtin_JMP:
    case builtin_JMP_IF: {
      assertprint(false, "jmp,label dont mean anything outside function");
    } break;
    case builtin_ARG: {
      assertprint(msList_len(node->args) == 1, "");
      var_ v = checkLiteral(node->args[0], "ARG arg");
      assertprint(fptr_is_number(v), "arg expects a number");
      usize u = fptr_to_number(v);
      return pull_arg(u, stack, stack_frames, symbols);
    } break;

      //
      // creations
      //

    case builtin_INIT: {
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ name = checkLiteral(node->args[0], "INIT name");
      var_ rhs = node->args[1];

      if (rhs->op == builtin_CALL) {
        assertMessage(rhs->args && msList_len(rhs->args) == 2);
        var_ function = interpret(stack, stack_frames, rhs->args[0], symbols);
        var_ function_typ = tu_assert(item_type_block, function.type[0]);
        var_ return_type = msList_last(function_typ.types);
        var_ argslist = checkList(rhs->args[1], "INIT call argslist");

        symbol dst;
        tu_match(
            return_type[0],
            case (item_type_type, _, {
              // type-valued return: no stack slot needed
              dst = (symbol){.type = return_type, .kind = SYM_OF((sym_type){})};
            }),
            default({
              usize align = item_type_alignment(return_type);
              usize size = item_type_size(return_type);
              while (mList_len(stack) % align)
                mList_push(stack, 0);
              usize addr = mList_len(stack);
              mList_pushArr(stack, *VLAP((u8 *)NULL, size));
              dst = (symbol){.type = return_type, .kind = SYM_OF((sym_value){addr})};
            }),
        );

        symbol result = interpret_call(dst, function, argslist, stack, stack_frames, symbols);
        msHmap_set(mList_last(symbols), name, result);
        return result;
      } else {
        var_ result = interpret(stack, stack_frames, rhs, symbols);
        msHmap_set(mList_last(symbols), name, result);
        return *msHmap_get(mList_last(symbols), name);
      }

    } break;
    case builtin_DECL: {
      assertMessage(node->args && msList_len(node->args) == 2);

      var_ name = checkLiteral(node->args[0], "DECL name");
      var_ type_sym = interpret(stack, stack_frames, node->args[1], symbols);
      assertprint(SYM_IS(type, type_sym.kind), "DECL requires a type as its second argument");
      tu_match(
          type_sym.type[0],
          case (item_type_type, _, {
            assertprint(false, "type type not supported for decl");
          }),
          default({
            usize align = item_type_alignment(type_sym.type);
            usize size = item_type_size(type_sym.type);

            usize addr = mList_len(stack);

            mList_pushArr(stack, *VLAP((u8 *)NULL, size));

            symbol local_sym = (symbol){
                .type = type_sym.type,
                .kind = SYM_OF((sym_value){addr})
            };

            msHmap_set(mList_last(symbols), name, local_sym);
            return local_sym;
          }),
      );

    } break;

      //
      // logic
      //

    case builtin_REF: {
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ dst = interpret(stack, stack_frames, node->args[0], symbols); // a
      var_ src = interpret(stack, stack_frames, node->args[1], symbols); // b
      assertprint(SYM_IS(value, dst.kind), "REF dst must be an l-value");
      assertprint(SYM_IS(value, src.kind), "REF src must be an l-value");
      memcpy(mList_arr(stack) + dst.kind.sym_value, &src.kind.sym_value, sizeof(usize));
      return dst;
    } break;
    case builtin_CP: {
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ dst_ptr = interpret(stack, stack_frames, node->args[0], symbols);
      var_ src_ptr = interpret(stack, stack_frames, node->args[1], symbols);

      assertprint(SYM_IS(value, dst_ptr.kind), "CP dst must be a value");
      assertprint(SYM_IS(value, src_ptr.kind), "CP src must be a value");

      item_type *dst_inner = get_ptr_iType(dst_ptr.type);
      item_type *src_inner = get_ptr_iType(src_ptr.type);

      assertprint(item_type_size(dst_inner) <= item_type_size(src_inner), "copy would overflow size");

      usize dst_addr, src_addr;
      memcpy(&dst_addr, mList_arr(stack) + dst_ptr.kind.sym_value, sizeof(usize));
      memcpy(&src_addr, mList_arr(stack) + src_ptr.kind.sym_value, sizeof(usize));

      memcpy(mList_arr(stack) + dst_addr, mList_arr(stack) + src_addr, item_type_size(dst_inner));
      return dst_ptr;
    } break;

    case builtin_SET: {
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ dst_sym = interpret(stack, stack_frames, node->args[0], symbols);
      var_ src_sym = interpret(stack, stack_frames, node->args[1], symbols);

      assertprint(SYM_IS(value, dst_sym.kind), "SET dst must be an l-value");
      assertprint(SYM_IS(value, src_sym.kind), "SET src must be a value");

      usize copy_size = item_type_size(dst_sym.type);

      // Standard local assignment: a = b
      memcpy(mList_arr(stack) + dst_sym.kind.sym_value, mList_arr(stack) + src_sym.kind.sym_value, copy_size);

      return dst_sym;
    } break;
      //
      //    binary
      //
    case builtin_NOT:
    case builtin_BNOT: {
      assertMessage(node->args && msList_len(node->args) == 2);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);
      var_ src = interpret(stack, stack_frames, node->args[1], symbols);

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ src_v = tu_assert(sym_value, src.kind);

      if (node->op == builtin_BNOT) {
        assertprint(item_type_equal(dst.type, src.type), "types must be the same");
        var_ s = (u8 *)(src_v + mList_arr(stack));
        var_ d = (u8 *)(dst_v + mList_arr(stack));
        foreach (var_ i, range(0, item_type_size(dst.type)))
          d[i] = ~s[i];
      } else {
        tu_match(
            dst.type[0],
            case (item_type_uint, _, {}),
            case (item_type_sint, _, {}),
            default({ assertprint(false, "not result can only be an integer"); }),
        );
        var_ d = (u8 *)(dst_v + mList_arr(stack));
        var_ dst_size = item_type_size(dst.type);
        var_ src_size = item_type_size(src.type);
        memset(d, 0, dst_size);
        d[0] = memzero(mList_arr(stack) + src_v, src_size);
      }
      return dst;
    } break;
    case builtin_BOR:
    case builtin_BAND:
    case builtin_BXOR: {
      assertMessage(node->args && msList_len(node->args) == 3);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);

      var_ srca = interpret(stack, stack_frames, node->args[1], symbols);
      var_ srcb = interpret(stack, stack_frames, node->args[2], symbols);

      assertMessage(item_type_equal(srca.type, srcb.type));
      assertMessage(item_type_equal(dst.type, srcb.type));

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ srca_v = tu_assert(sym_value, srca.kind);
      var_ srcb_v = tu_assert(sym_value, srcb.kind);

      var_ dst_size = item_type_size(dst.type);
      var_ c = (u8 *)(dst_v + mList_arr(stack));
      var_ a = (u8 *)(srca_v + mList_arr(stack));
      var_ b = (u8 *)(srcb_v + mList_arr(stack));
      foreach (var_ i, range(0, dst_size))
        c[i] =
            node->op == builtin_BOR
                ? a[i] | b[i]
            : node->op == builtin_BAND
                ? a[i] & b[i]
                : a[i] ^ b[i];

      return dst;
    } break;
    case builtin_OR:
    case builtin_AND:
    case builtin_XOR: {
      assertMessage(node->args && msList_len(node->args) == 3);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);

      var_ srca = interpret(stack, stack_frames, node->args[1], symbols);
      var_ srcb = interpret(stack, stack_frames, node->args[2], symbols);
      assertMessage(item_type_equal(srca.type, srcb.type));

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ srca_v = tu_assert(sym_value, srca.kind);
      var_ srcb_v = tu_assert(sym_value, srcb.kind);

      tu_match(
          dst.type[0],
          case (item_type_uint, _, {}),
          case (item_type_sint, _, {}),
          default({ assertprint(false, "not result can only be an integer"); }),
      );
      var_ d = (u8 *)(dst_v + mList_arr(stack));
      var_ dst_size = item_type_size(dst.type);

      var_ src_size = item_type_size(srca.type);

      memset(d, 0, dst_size);
      bool ab = !memzero(mList_arr(stack) + srca_v, src_size);
      bool bb = !memzero(mList_arr(stack) + srcb_v, src_size);
      d[0] =
          node->op == builtin_OR
              ? ab || bb
          : node->op == builtin_AND
              ? ab && bb
              : ab ^ bb;
      return dst;
    } break;

      //
      //    math
      //

    case builtin_EQUAL: {
      assertMessage(node->args && msList_len(node->args) == 3);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);
      var_ srca = interpret(stack, stack_frames, node->args[1], symbols);
      var_ srcb = interpret(stack, stack_frames, node->args[2], symbols);

      AllocatorV alloc = msHmap_allocator(mList_last(symbols));
      var_ coerced_b = coerce(alloc, stack, srcb, srca.type);
      assertprint(coerced_b.ptr, "failed to coerce srcb to srca type");

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ srca_v = tu_assert(sym_value, srca.kind);

      var_ dst_size = item_type_size(dst.type);
      var_ src_size = item_type_size(srca.type);

      var_ d = (u8 *)(dst_v + mList_arr(stack));
      memset(d, 0, dst_size);
      d[0] = memcmp(
                 mList_arr(stack) + srca_v,
                 coerced_b.ptr,
                 src_size
             ) == 0;
      return dst;
    } break;

    case builtin_MORE:
    case builtin_LESS: {
      assertMessage(node->args && msList_len(node->args) == 3);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);
      var_ srca = interpret(stack, stack_frames, node->args[1], symbols);
      var_ srcb = interpret(stack, stack_frames, node->args[2], symbols);

      AllocatorV alloc = msHmap_allocator(mList_last(symbols));
      var_ coerced_b = coerce(alloc, stack, srcb, srca.type);
      assertprint(coerced_b.ptr, "failed to coerce srcb to srca type");

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ srca_v = tu_assert(sym_value, srca.kind);

      var_ dst_size = item_type_size(dst.type);
      var_ src_size = item_type_size(srca.type);

      bool is_signed = false;
      tu_match(
          srca.type[0],
          case (item_type_uint, _, { is_signed = false; }),
          case (item_type_sint, _, { is_signed = true; }),
          case (item_type_ptr, _, { is_signed = false; }),
          default({ assertprint(false, "comparison requires integers or pointers"); }),
      );

      var_ d = (u8 *)(dst_v + mList_arr(stack));
      memset(d, 0, dst_size);

#define DO_CMP(type_cast, op) \
  d[0] = (*((type_cast *)(mList_arr(stack) + srca_v)) op * ((type_cast *)(coerced_b.ptr)))

      if (node->op == builtin_MORE) {
        if (is_signed) {
          if (src_size == 1)
            DO_CMP(i8, >);
          else if (src_size == 2)
            DO_CMP(i16, >);
          else if (src_size == 4)
            DO_CMP(i32, >);
          else
            DO_CMP(i64, >);
        } else {
          if (src_size == 1)
            DO_CMP(u8, >);
          else if (src_size == 2)
            DO_CMP(u16, >);
          else if (src_size == 4)
            DO_CMP(u32, >);
          else
            DO_CMP(u64, >);
        }
      } else {
        if (is_signed) {
          if (src_size == 1)
            DO_CMP(i8, <);
          else if (src_size == 2)
            DO_CMP(i16, <);
          else if (src_size == 4)
            DO_CMP(i32, <);
          else
            DO_CMP(i64, <);
        } else {
          if (src_size == 1)
            DO_CMP(u8, <);
          else if (src_size == 2)
            DO_CMP(u16, <);
          else if (src_size == 4)
            DO_CMP(u32, <);
          else
            DO_CMP(u64, <);
        }
      }
#undef DO_CMP

      return dst;
    } break;

    case builtin_ADD:
    case builtin_SUB:
    case builtin_MUL:
    case builtin_DIV:
    case builtin_MOD:
    case builtin_SHR:
    case builtin_SHL: {
      assertMessage(node->args && msList_len(node->args) == 3);

      var_ dst = interpret(stack, stack_frames, node->args[0], symbols);
      var_ srca = interpret(stack, stack_frames, node->args[1], symbols);
      var_ srcb = interpret(stack, stack_frames, node->args[2], symbols);

      AllocatorV alloc = msHmap_allocator(mList_last(symbols));

      // Coerce both srca and srcb directly to dst.type so everything aligns
      var_ coerced_a = coerce(alloc, stack, srca, dst.type);
      var_ coerced_b = coerce(alloc, stack, srcb, dst.type);
      assertprint(coerced_a.ptr && coerced_b.ptr, "math type coercion failed");

      var_ dst_v = tu_assert(sym_value, dst.kind);
      var_ size = item_type_size(dst.type);

      bool is_signed = false;
      tu_match(
          dst.type[0],
          case (item_type_uint, _, { is_signed = false; }),
          case (item_type_sint, _, { is_signed = true; }),
          case (item_type_ptr, _, { is_signed = false; }),
          default({ assertprint(false, "math requires integers or pointers"); }),
      );

#define DO_MATH(type_cast, op) \
  *((type_cast *)(mList_arr(stack) + dst_v)) = (*((type_cast *)(coerced_a.ptr))op * ((type_cast *)(coerced_b.ptr)))

#define MATH_SWITCH(op) \
  if (is_signed) {      \
    if (size == 1)      \
      DO_MATH(i8, op);  \
    else if (size == 2) \
      DO_MATH(i16, op); \
    else if (size == 4) \
      DO_MATH(i32, op); \
    else                \
      DO_MATH(i64, op); \
  } else {              \
    if (size == 1)      \
      DO_MATH(u8, op);  \
    else if (size == 2) \
      DO_MATH(u16, op); \
    else if (size == 4) \
      DO_MATH(u32, op); \
    else                \
      DO_MATH(u64, op); \
  }

      switch (node->op) {
        case builtin_ADD:
          MATH_SWITCH(+);
          break;
        case builtin_SUB:
          MATH_SWITCH(-);
          break;
        case builtin_MUL:
          MATH_SWITCH(*);
          break;
        case builtin_DIV:
          MATH_SWITCH(/);
          break;
        case builtin_MOD:
          MATH_SWITCH(%);
          break;
        case builtin_SHR:
          MATH_SWITCH(>>);
          break;
        case builtin_SHL:
          MATH_SWITCH(<<);
          break;
        default:
          break;
      }

#undef MATH_SWITCH
#undef DO_MATH

      return dst;
    } break;
    default:
      assertMessage(
          false,
          "%s",
          snprint(
              stdAlloc,
              "unimplemented op : {builtin_OP} , text : \"{slice(c8)}\"",
              node->op, node->text
          )
              .ptr
      );
      return (symbol){};
      break;
  }
  assertprint(false, "reached end of interpret without return \nnode :  {astNode}", node);
}

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
  var_ stin = read_stdin(stdAlloc);
  defer { slice_free(stdAlloc, stin); };
  //   const char sti[] = {
  // #embed "example.sexp"
  //   };
  // var_ stin = fp(sti);

  AllocatorV astArena = arena_new_ext(stdAlloc, 1024);
  defer { arena_cleanup(astArena); };

  var_ list = sexp_parse_file(astArena, stin);

  type_allocator = arena_new_ext(stdAlloc, 1024);
  defer { arena_cleanup(type_allocator); };

  println("{msList : astNode* : numbers}", list);
  // println("{msList : astNode*}", list);

  var_ stack = mList_init(stdAlloc, u8);
  defer { mList_deInit(stack); };

  AllocatorV symArena = arena_new_ext(stdAlloc, 1024);
  defer { arena_cleanup(symArena); };

  var_ stack_frames = mList_init(symArena, usize);
  var_ symbols = mList_init(symArena, msHmap(symbol));
  mList_push(symbols, (msHmap_init(symArena, symbol)));

  foreach (var_ node, vla(*msList_vla(list)))
    interpret(
        stack,
        stack_frames,
        node,
        symbols
    );

  println("arena capacity : {}", arena_totalMem(symArena));
  println("stack size : {} , stack capacity : {}", mList_len(stack), mList_cap(stack));
  println("types : ");
  foreach (var_ v, iter(mHmap_iterator(tmap, item_type)))
    println("{ item_type }", *v->val);
}
#include "wheels/wheels.h"
