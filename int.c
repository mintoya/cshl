#include "cshl.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/tagged_unions.h"
#include <assert.h>
#include <locale.h>
#include <stdbool.h>

#include "sexp_parser.c"
#include "typ.c"
#include "wheels/tu_macros.h"

static_assert(!(sizeof(symbol) % sizeof(usize)));

AllocatorV type_allocator = NULL;

item_type *make_type(item_type t) {
  mHmap(item_type, item_type *) tmap = NULL;
  tmap = tmap ?: mHmap_init(type_allocator, item_type, item_type *);

  return *mHmapGetOrSet(
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
            snprint(stdAlloc, "{item_type}", t[0]).ptr
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

  usize ds = type_size(&dstt);

  usize ss = type_size(&srct);

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
  assertprint(!(node->args), "literal has args: {astNode} : {cstr}", node, message);
  var_ res = node->text;
  assertprint(res.len && res.ptr, "not a literal : {slice(c8)} : {cstr}", node->text, message);
  return res;
}
msList(astNode *) checkList(astNode *node, char *message) {
  assertprint(node->op == builtin_NONE, "not a literal : {astNode} {cstr}", node, message);
  assertprint(node->args, "not a literal : {astNode} {cstr}", node, message);
  return node->args;
}
symbol interpret(
    mList(u8) stack,
    mList(usize) stack_frames,
    astNode *node,
    mList(msHmap(symbol)) symbols
) {
  println("interpreting {astNode}", node);
  defer {
    // assertMessage(
    //     !(mList_len(stack) % 8),
    //     "last instruction misaligned stack : %s",
    //     snprint(stdAlloc, "{astNode} , {slice(c8)}", node, node->text).ptr
    // );

    while (mList_len(stack) % 8)
      mList_push(stack, 0);
  };

  switch (node->op) {

      //
      // misc
      //

    case builtin_NONE: {
      var_ s = symbolResolve(symbols, checkLiteral(node, "resolving symbol"));
      assertMessage(
          s,
          "%s",
          snprint(stdAlloc, "unknown symbol : {slice(c8)}", node->text).ptr
      );
      return *s;
    } break;
      //
      // types
      //

      // TODO
      // hashmap of stringified types
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

      //
      // functions
      //
    case builtin_BLOCK: {
      assertMessage(node->args && msList_len(node->args) == 3);
      var_ arglist_node = checkList(node->args[0], "parsing arglist");
      var_ rtype_node = node->args[1];
      var_ opslist_node = checkList(node->args[2], "parsing opslist");
      var_ typeslist =
          msList_init(
              type_allocator,
              item_type *,
              msList_len(arglist_node)
          );
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
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ function = interpret(stack, stack_frames, node->args[0], symbols);
      var_ argslist = checkList(node->args[1], "called argslist");

      var_ function_ops = tu_assert(sym_function, function.kind);
      var_ function_typ = tu_assert(item_type_block, function.type[0]);

      /*
       * returned item
       * -- new stack frame --
       *  usize args_len
       */
      var_ return_type = msList_last(function_typ.types);
      usize return_type_size;

      tu_match(
          return_type[0],
          case (item_type_block, _, return_type_size = 0;),
          case (item_type_type, _, return_type_size = 0;),
          default(return_type_size = type_size(return_type);)
      );
      mList_pushArr(stack, *VLAP((u8 *)NULL, return_type_size));
      while (mList_len(stack) % 8)
        mList_push(stack, 0);

      var_ old_stack_allocator = msHmap_allocator(mList_last(symbols));
      var_ new_stack_allocator = arena_new_ext(old_stack_allocator, 1024);
      mList_push(symbols, msHmap_init(new_stack_allocator, symbol));
      defer { arena_cleanup(msHmap_allocator(mList_pop(symbols))); };
      usize next_frame = mList_len(stack);

      usize call_len = msList_len(function_typ.types) - 1;
      var_ s = aCreate(new_stack_allocator, symbol, call_len);
      for (usize i = 0; i < call_len; i++) {
        s[i] = interpret(stack, stack_frames, argslist[i], symbols);
        if (!item_type_equal(s[i].type, function_typ.types[i])) {
          var_ to = coerce(new_stack_allocator, stack, s[i], function_typ.types[i]);
          assertprint(to.ptr && to.ptr, "failed to coerce type {item_type} to {item_type}", s[i].type[0], function_typ.types[i][0]);
          s[i] = (symbol){
              .type = function_typ.types[i],
              .kind = SYM_OF(mList_len(stack)),
          };
          mList_pushArr(stack, *VLAP(to.ptr, to.len));
        };
      }

      mList_push(stack_frames, next_frame);
      defer { mList_len(stack) = mList_pop(stack_frames); };

      mList_pushArr(stack, *VLAP((u8 *)&call_len, sizeof(usize)));
      for (usize i = 0; i < call_len; i++) {
        usize ts = 0;
        tu_match(
            s[i].type[0],
            case (item_type_type, _, ts = 0;),
            default(ts = type_size(s[i].type);)
        );
        mList_pushArr(stack, *VLAP((u8 *)s[i].type, sizeof(s[i].type[0])));
        mList_pushArr(stack, *VLAP((u8 *)s[i].kind.sym_value, ts));
      }

      msHmap(usize) label_to_line = msHmap_init(new_stack_allocator, usize);
      for (usize i = 0; i < msList_len(function_ops); i++) {
        var_ it =
            function_ops[i];
        switch (it->op) {
          case builtin_LABEL: {
            assertprint(false, "unimplemented");
          } break;
          case builtin_JMP_IF:
          case builtin_JMP: {
            assertprint(false, "unimplemented");
          } break;
          case builtin_RETURN: {
            // var_ res =
          } break;
          default:
            interpret(stack, stack_frames, it, symbols);
        }
      }

    } break;
    case builtin_ARG: {
      usize len = *(usize *)(mList_arr(stack) + mList_last(stack_frames));
      var_ litn = checkLiteral(node->args[0], "args item");
      assertprint(fptr_to_number(litn), "arg requires a number");
      var_ n = fptr_to_number(litn);
      assertprint(n < len, "arg index out of bounds");

      usize i = 0;
      usize ts = 0;
      u8 *place = (mList_arr(stack) + mList_last(stack_frames) + sizeof(usize));

      while (i < n) {
        item_type t = *(typeof(t) *)place;
        tu_match(
            t,
            // case (item_type_block, _, ts = 0;), // ?
            case (item_type_type, _, ts = 0;),
            default(ts = type_size(&t);)
        );
        place += ts;
      }
      var_ it = (item_type *)place;
      usize idx = (usize)(it - (item_type *)mList_arr(stack));
      tu_match(
          it[0],
          case (item_type_type, _,
                return (symbol){
                    .type = it,
                    .kind = SYM_OF((sym_type){})
                };),
          default(
              return (symbol){
                  .type = it,
                  .kind = SYM_OF((sym_value){idx})
              };
          ),

      );

    } break;

      //
      // creations
      //

    case builtin_INIT: {
      assertMessage(node->args && msList_len(node->args) == 2);

      var_ name = checkLiteral(node->args[0], "creating variable");
      var_ item = node->args[1];

      msHmap_set(
          mList_last(symbols),
          name,
          interpret(stack, stack_frames, item, symbols)
      );
      return *msHmap_get(mList_last(symbols), name);
    } break;
      //
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

  var_ stin = read_stdin(stdAlloc);
  defer { slice_free(stdAlloc, stin); };

  AllocatorV astArena = arena_new_ext(stdAlloc, 1024);
  defer { arena_cleanup(astArena); };

  var_ list = sexp_parse_file(astArena, stin);

  type_allocator = arena_new_ext(stdAlloc, 1024);
  defer { arena_cleanup(type_allocator); };

  println("{msList : astNode : numbers}", list);
  println("{msList : astNode}", list);

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
  println("stack capacity : {}", mList_cap(stack));
}
#include "wheels/wheels.h"
