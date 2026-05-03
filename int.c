#include "cshl.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include <assert.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>

#include "sexp_parser.c"
#include "typ.c"
#include "wheels/tu_macros.h"

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))
static_assert(!(sizeof(symbol) % sizeof(usize)));

AllocatorV type_allocator = NULL;

mHmap(item_type, item_type *) tmap = NULL;
item_type *make_type(item_type t) {
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
) {

  // println("interpreting {astNode}", node);
  // println("stack : {}", mList_len(stack));

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
      if (fptr_is_number(node->text)) {
        usize val = fptr_to_number(node->text);

        while (mList_len(stack) % sizeof(usize))
          mList_push(stack, 0);

        usize addr = mList_len(stack);

        u8 *val_ptr = (u8 *)&val;
        for (usize i = 0; i < sizeof(usize); i++) {
          mList_push(stack, val_ptr[i]);
        }

        return (symbol){
            .type = make_type(ITYPE_OF(((item_type_uint){0, 64}))),
            .kind = SYM_OF((sym_value){addr})
        };
      }

      // 2. If it's not a number, proceed with normal symbol lookup
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

      var_ members = msList_init(
          type_allocator,
          typeof(*(((item_type_struct *)NULL)->types)),
          member_count
      );

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
        msList_push(
            type_allocator,
            members,
            ((typeof(*members)){.type = member_type, .offset = offset})
        );

        current_offset = offset + item_type_size(member_type);
      }

      usize struct_size = lineup(current_offset, max_align);

      item_type type_val = ITYPE_OF(((item_type_struct){
          .alignment = (u32)max_align,
          .types = members
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
       *  arg
       */
      var_ return_type = msList_last(function_typ.types);
      usize return_type_size;

      tu_match(
          return_type[0],
          case (item_type_block, _, return_type_size = 0;),
          case (item_type_type, _, return_type_size = 0;),
          default(return_type_size = item_type_size(return_type);)
      );
      usize retrun_addr = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)NULL, return_type_size));

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
        var_ it =
            function_ops[i];
        switch (it->op) {
          case builtin_LABEL: {
            // Already processed
          } break;
          case builtin_JMP_IF: {
            assertMessage(it->args && msList_len(it->args) == 2);
            usize curr = mList_len(stack);
            var_ cond = interpret(stack, stack_frames, it->args[0], symbols);
            assertprint(SYM_IS(value, cond.kind), "JMP_IF condition must be a value");

            var_ name = checkLiteral(it->args[1], "jmp target");
            usize *target = msHmap_get(label_to_line, name);
            assertprint(target, "unknown label : {slice(c8)}", name);

            usize size = item_type_size(cond.type);
            u8 *ptr = mList_arr(stack) + cond.kind.sym_value;
            bool is_true = false;
            for (usize b = 0; b < size; b++) {
              if (ptr[b]) {
                is_true = true;
                break;
              }
            }

            mList_len(stack) = curr;
            if (is_true)
              i = *target;
          } break;
          case builtin_JMP: {
            assertMessage(it->args && msList_len(it->args) == 1);
            var_ name = checkLiteral(it->args[0], "jmp target");
            usize *target = msHmap_get(label_to_line, name);
            assertprint(target, "unknown label : {slice(c8)}", name);
            i = *target;
          } break;
          case builtin_RETURN: {
            assertMessage(it->args && msList_len(it->args) == 1);
            var_ res = interpret(stack, stack_frames, it->args[0], symbols);
            tu_match(
                return_type[0],
                case (item_type_type, _, {
                  assertprint(SYM_IS(type, res.kind), "");

                  return (symbol){
                      .type = res.type,
                      .kind = SYM_OF((sym_type){})
                  };
                }),
                default({
                  assertprint(SYM_IS(value, res.kind), "");
                  item_type_equal(res.type, return_type);
                  memcpy(
                      retrun_addr + mList_arr(stack),
                      res.kind.sym_value + mList_arr(stack),
                      return_type_size
                  );
                  return (symbol){
                      .type = return_type,
                      .kind = SYM_OF((sym_value){retrun_addr})
                  };
                });
            );
          } break;
          default:
            interpret(stack, stack_frames, it, symbols);
        }
      }
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

      var_ name = checkLiteral(node->args[0], "creating variable");
      var_ item = node->args[1];

      msHmap_set(
          mList_last(symbols),
          name,
          interpret(stack, stack_frames, item, symbols)
      );
      return *msHmap_get(mList_last(symbols), name);
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
      //
      // Pointers & Mutation
      //

    case builtin_REF: {
      assertMessage(node->args && msList_len(node->args) == 1);
      var_ target = interpret(stack, stack_frames, node->args[0], symbols);

      // We can only take the address of something that physically exists on the stack
      assertprint(SYM_IS(value, target.kind), "REF target must be an l-value");

      // Create the pointer type
      item_type *ptr_type = make_type(ITYPE_OF(((item_type_ptr){
          .alignment = sizeof(usize),
          .type = target.type
      })));

      // Allocate space on the stack for the pointer itself (a usize holding an offset)
      while (mList_len(stack) % sizeof(usize))
        mList_push(stack, 0);

      usize ptr_addr = mList_len(stack);

      for (usize i = 0; i < sizeof(usize); i++)
        mList_push(stack, 0);

      // Write the target's stack offset into our new pointer
      memcpy(mList_arr(stack) + ptr_addr, &target.kind.sym_value, sizeof(usize));

      return (symbol){
          .type = ptr_type,
          .kind = SYM_OF((sym_value){ptr_addr})
      };
    } break;

    case builtin_CP: {
      assertMessage(node->args && msList_len(node->args) == 2);
      var_ dst_ptr = interpret(stack, stack_frames, node->args[0], symbols);
      var_ src_ptr = interpret(stack, stack_frames, node->args[1], symbols);

      assertprint(SYM_IS(value, dst_ptr.kind), "CP dst must be a value");
      assertprint(SYM_IS(value, src_ptr.kind), "CP src must be a value");

      // Extract inner types to know how many bytes to copy
      item_type *dst_inner = get_ptr_iType(dst_ptr.type);
      item_type *src_inner = get_ptr_iType(src_ptr.type);

      // Optional: assert item_type_equal(dst_inner, src_inner);

      usize dst_addr;
      usize src_addr;

      // Read the actual stack offsets stored inside the pointers
      memcpy(&dst_addr, mList_arr(stack) + dst_ptr.kind.sym_value, sizeof(usize));
      memcpy(&src_addr, mList_arr(stack) + src_ptr.kind.sym_value, sizeof(usize));

      usize copy_size = item_type_size(dst_inner);

      // Execute *dst = *src
      memcpy(mList_arr(stack) + dst_addr, mList_arr(stack) + src_addr, copy_size);

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

//
//
//

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
  println("stack size : {} , stack capacity : {}", mList_len(stack), mList_cap(stack));
  println("{}", ((fptr){sizeof(*mList_vla(stack)), mList_arr(stack)}));
  println("types : ");
  foreach (var_ v, iter(mHmap_iterator(tmap, item_type))) {
    println("{ item_type }", *v->val);
  }
}
#include "wheels/wheels.h"
