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

REGISTER_PRINTER(symbol, {
  PUTS("{");
  TU_MATCH(
      (symKind, in.kind),

      (sym_lvalue, /*   */ { PUTS("value"); }),
      (sym_function, /**/ { PUTS("function"); }),
      (sym_extern, /*  */ { PUTS("extern"); }),
      (sym_type, /*    */ { PUTS("type"); }),

      (sym_none, /*    */ { PUTS("none"); }),
      (default, /*     */ { PUTS("none"); }),
  );
  PUTS(",type : ");
  if (in.type) {
    USENAMEDPRINTER("item_type", in.type);
  } else {
    PUTS("none");
  }
  PUTS("}");
});

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
            iType_alllocator,
            h.issigned
                ? TU_OF(
                      (item_type, item_type_sint),
                      ((item_type_sint){
                          .bitwidth = h.bitcount,
                          .alignment = lineup(h.bitcount, 8) / 8,
                      })
                  )
                : TU_OF(
                      (item_type, item_type_uint),
                      ((item_type_uint){
                          .bitwidth = h.bitcount,
                          .alignment = lineup(h.bitcount, 8) / 8,
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
item_type *make_block(AllocatorV allocator, msList(item_type *) typething) {
  var_ res = item_type_newStub(allocator);
  *res = TU_OF(
      (item_type, item_type_block),
      ((item_type_block){
          .types = typething
      })
  );
  return res;
}
typedef typeof(*((item_type_struct *)NULL)->types) struct_inner;
item_type *make_struct(AllocatorV allocator, msList(struct_inner) typething) {
  var_ res = item_type_newStub(allocator);
  *res = TU_OF(
      (item_type, item_type_struct),
      ((item_type_struct){.types = typething})
  );
  return res;
}
bool item_type_equal(AllocatorV allocator, item_type *a, item_type *b) { // TODO actaully check the types
  var_ at = snprint(allocator, "{item_type}", a);
  defer{slice_free(allocator, at)};
  var_ bt = snprint(allocator, "{item_type}", b);
  defer{slice_free(allocator, bt)};
  return fptr_eq(*(fptr *)&at, *(fptr *)&bt);
}

item_type *get_ptr_iType(item_type *t) {
  TU_MATCH(
      (item_type, (*t)),
      (item_type_ptr, {
        return $in.type;
      }),
      (default, {
        assertMessage(
            false,
            "tried to get inner pointer to %s",
            snprint(stdAlloc, "{item_type}", &t).ptr
        );
      })
  );
}
usize get_int_bitwidth(item_type *t) {
  TU_MATCH(
      (item_type, (*t)),
      (item_type_uint, {
        return $in.bitwidth;
      }),
      (item_type_sint, {
        return $in.bitwidth;
      }),
      (default, {
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

symbol interpret(
    AllocatorV allocator,
    mList(u8) stack,
    mList(usize) stack_frames,
    mList(usize) temp_frames, // generated by begins
    astNode *node,
    mList(msHmap(symbol)) symbols
) {
  // println("stack : {}", mList_len(stack));
  defer {
    assertMessage(
        !(mList_len(stack) % 8),
        "last instruction misaligned stack : %s",
        snprint(stdAlloc, "{astNode} , {slice(c8)}", node, node->text).ptr
    );
  };

  switch (node->op) {
    case builtin_NONE: {
      assertMessage(node->text.len);
      if (fptr_is_number(node->text)) {
        usize num = fptr_to_number(node->text);
        usize res = mList_len(stack);
        mList_pushArr(stack, *VLAP((u8 *)&num, sizeof(num)));
        return (symbol){
            .type = get_itype(
                (struct intHandle){
                    .bitcount = sizeof(u64) * 8,
                    .issigned = 0,
                }
            ),
            .kind = TU_OF((symKind, sym_lvalue), res)
        };
      } else if (node->text.ptr[0] == '"') {
        assertMessage(node->text.ptr[node->text.len - 1] == '"');
        usize res = mList_len(stack);
        mList_pushArr(stack, *VLAP((node->text.ptr + 1), node->text.len - 2));
        mList_push(stack, 0);

        while (mList_len(stack) % 8)
          mList_push(stack, 0);

        usize resp = mList_len(stack);
        mList_pushArr(stack, *VLAP((u8 *)&res, sizeof(res)));

        return (symbol){
            .kind = TU_OF((symKind, sym_lvalue), resp),
            .type = make_ptr(
                msHmap_allocator(mList_last(symbols)),
                get_itype(
                    (struct intHandle){
                        .bitcount = 8,
                        .issigned = 1,
                    }
                )
            )
        };
      } else {
        symbol *s = symbolResolve(symbols, node->text);
        assertMessage(s, "unknown symbol %s", snprint(stdAlloc, "{slice(c8)}", node->text).ptr);
        return *s;
      }
    } break;
    case builtin_TYPE: {
      assertMessage(!node->args || msList_len(node->args) == 0);
      static item_type justType = TU_OF((item_type, item_type_type), (item_type_type){});
      return (symbol){
          .type = &justType,
          .kind = TU_OF((symKind, sym_type), {}),
      };
    } break;
    case builtin_INIT: {
      assertMessage(EQUAL_ANY(msList_len(node->args), 3, 2));

      if (msList_len(node->args) == 2) {
        var_ last = mList_arr(symbols)[mList_len(symbols) - 1];
        assertMessage(!msHmap_get(last, node->args[0]->text), "symbol already exists in this context");
        msHmap_set(last, node->args[0]->text, interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols));
        var_ v = *msHmap_get(last, node->args[0]->text);
        println(
            "created : {item_type} for {slice(c8)}", v.type,
            node->args[0]->text
        );
      } else {
        var_ type_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);
        assertMessage(
            TU_IS((symKind, sym_type), type_sym.kind),
            "INIT expected a type for argument 2 , %s",
            snprint(stdAlloc, "{slice(c8)}", node->text).ptr
        );

        var_ value = interpret(allocator, stack, stack_frames, temp_frames, node->args[2], symbols);

        usize t_size = type_size(type_sym.type);
        usize loc;

        if (!item_type_equal(allocator, value.type, type_sym.type)) {
          if ( // both unsigned, result is bigger
              TU_IS((item_type, item_type_uint), type_sym.type[0]) &&
              TU_IS((item_type, item_type_uint), value.type[0]) &&
              type_size(type_sym.type) <= type_size(value.type)
          ) {
            loc = value.kind._sym_lvalue;
          } else { // 0 for all types
            assertMessage(TU_IS((item_type, item_type_uint), value.type[0]));
            assertMessage(TU_IS((symKind, sym_lvalue), value.kind));
            assertMessage(fptr_isEmpty((fptr){t_size, mList_arr(stack) + value.kind._sym_lvalue}));
            loc = mList_len(stack);
            mList_pushArr(stack, *VLAP((u8 *)NULL, t_size));
          }
        } else { // value is same type
          loc = value.kind._sym_lvalue;
        }

        var_ last = mList_arr(symbols)[mList_len(symbols) - 1];
        assertMessage(!msHmap_get(last, node->args[0]->text), "%s", snprint(stdAlloc, "symbol {slice(c8)} already exists", node->args[0]->text).ptr);

        msHmap_set(
            last,
            node->args[0]->text,
            ((symbol){
                .kind = TU_OF((symKind, sym_lvalue), loc),
                .type = type_sym.type,
            })
        );
      }
      while (mList_len(stack) % 8)
        mList_push(stack, 0);
      return (symbol){};
    } break;
    case builtin_UINT:
    case builtin_SINT: {
      assertMessage(msList_len(node->args) == 1);
      assertMessage(fptr_is_number(node->args[0]->text));
      return (symbol){
          .kind = TU_OF((symKind, sym_type), {}),
          .type = get_itype(
              (struct intHandle){
                  .bitcount = fptr_to_number(node->args[0]->text),
                  .issigned = node->op == builtin_SINT,
              }
          )
      };
    } break;
    case builtin_STRUCT: {
      var_ field_types = msList_init(msHmap_allocator(mList_last(symbols)), struct_inner);
      usize current_offset = 0;

      if (node->args) {
        foreach (var_ t, vla(*msList_vla(node->args))) {
          var_ field_sym = interpret(
              allocator,
              stack, stack_frames,
              temp_frames,
              t, symbols
          );
          assertMessage(TU_IS((symKind, sym_type), field_sym.kind), "struct field must be a type");

          usize f_align = type_alignment(field_sym.type);
          if (f_align)
            current_offset = lineup(current_offset, f_align);

          msList_push(
              msHmap_allocator(mList_last(symbols)),
              field_types,
              ((struct_inner){
                  .type = field_sym.type,
                  .offset = current_offset
              })
          );

          current_offset += type_size(field_sym.type);
        }
      }

      return (symbol){
          .kind = TU_OF((symKind, sym_type), {}),
          .type = make_struct(msHmap_allocator(mList_last(symbols)), field_types)
      };

    } break;

    case builtin_PSTRUCT: {
      var_ field_types = msList_init(msHmap_allocator(mList_last(symbols)), struct_inner);
      usize current_offset = 0;

      if (node->args) {
        foreach (var_ t, vla(*msList_vla(node->args))) {
          var_ field_sym = interpret(
              allocator,
              stack, stack_frames,
              temp_frames,
              t, symbols
          );
          assertMessage(
              TU_IS((symKind, sym_type), field_sym.kind),
              "struct field must be a type"
          );

          msList_push(
              msHmap_allocator(mList_last(symbols)),
              field_types,
              ((struct_inner){
                  .type = field_sym.type,
                  .offset = current_offset
              })
          );

          current_offset += type_size(field_sym.type);
        }
      }

      return (symbol){
          .kind = TU_OF((symKind, sym_type), {}),
          .type = make_struct(msHmap_allocator(mList_last(symbols)), field_types)
      };

    } break;
    case builtin_PTR: {
      assertMessage(msList_len(node->args) == 1);
      var_ t = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      assertMessage(
          TU_IS((symKind, sym_type), t.kind),
          "pointer to non-type"
      );
      ;
      return (symbol){
          .kind = TU_OF((symKind, sym_type), {}),
          .type = make_ptr(msHmap_allocator(mList_last(symbols)), t.type),
      };
    } break;
    case builtin_BLOCK: {
      // (input types)
      // output type
      // (ops ...)

      assertMessage(msList_len(node->args) == 3);
      assertMessage(node->args[0]->op == builtin_NONE); // nothing followed by ()

      var_ result_return_type = msList_init(msHmap_allocator(mList_last(symbols)), item_type *);
      var_ rtype = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);
      assertMessage(TU_IS((symKind, sym_type), rtype.kind) || !rtype.type, "return type isnt a type or void");

      foreach (var_ t, vla(*msList_vla(node->args[0]->args))) {
        var_ argtype = interpret(
            allocator,
            stack, stack_frames,
            temp_frames,
            t, symbols
        );
        assertMessage(TU_IS((symKind, sym_type), argtype.kind));
        msList_push(msHmap_allocator(mList_last(symbols)), result_return_type, argtype.type);
      }
      msList_push(msHmap_allocator(mList_last(symbols)), result_return_type, rtype.type);

      return (symbol){
          .type = make_block(msHmap_allocator(mList_last(symbols)), result_return_type),
          .kind = TU_OF((symKind, sym_function), node->args[2]),
      };
    } break;
    case builtin_ARG: {
      assertMessage(msList_len(node->args) == 1);
      var_ n = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);

      assertMessage(TU_IS((item_type, item_type_uint), (n.type[0])));
      assertMessage(TU_IS((symKind, sym_lvalue), n.kind));

      var_ idx = *(usize *)(mList_arr(stack) + n.kind._sym_lvalue);
      var_ argCount = *(usize *)(mList_arr(stack) + mList_last(stack_frames));
      assertMessage(idx < argCount, "arg oob");
      // TODO function for this
      return ((symbol *)(mList_arr(stack) + mList_last(stack_frames) + sizeof(usize)))[idx];
    } break;
    case builtin_CALL: {
      assertMessage(msList_len(node->args) == 2);

      astNode *function_ex = node->args[0];
      astNode *function_args = node->args[1];

      assertMessage(!function_args->op, "function arguments must be in parenthesized list");

      var_ function = interpret(allocator, stack, stack_frames, temp_frames, function_ex, symbols);
      assertMessage(TU_IS((symKind, sym_function), function.kind) || TU_IS((symKind, sym_extern), function.kind), "calling non-function");

      assertMessage(
          msList_len(function.type->_item_type_block.types) - 1 == msList_len(function_args->args),
          "expected %i args, got %i", msList_len(function.type->_item_type_block.types) - 1, msList_len(function_args->args)
      );

      msList(symbol) args = msList_init(allocator, symbol, msList_len(function_args->args) ?: 1);
      foreach (var_ arg, range(0, msList_len(function_args->args))) {
        var_ s = interpret(allocator, stack, stack_frames, temp_frames, function_args->args[arg], symbols);
        var_ expected_type = function.type->_item_type_block.types[arg];

        if (TU_IS((item_type, item_type_type), expected_type[0])) {
          if (!TU_IS((symKind, sym_type), s.kind))
            assertMessage(false, "expected tyep");
        } else {
          if (!item_type_equal(allocator, s.type, expected_type)) { // type coercion
            usize expected_bytes = type_size(expected_type);
            usize s_bytes = type_size(s.type);

            if (expected_bytes <= 8 && s_bytes <= 8) {
              u64 val = 0;
              memcpy(&val, &mList_arr(stack)[s.kind._sym_lvalue], s_bytes);

              bool s_signed = s.type && TU_IS((item_type, item_type_sint), s.type[0]);
              if (s_signed && s_bytes < 8) {
                u64 shift = (8 - s_bytes) * 8;
                val = (u64)(((i64)(val << shift)) >> shift);
              }

              usize coerced_loc = mList_len(stack);
              mList_pushArr(stack, *VLAP((u8 *)&val, expected_bytes));
              while (mList_len(stack) % 8)
                mList_push(stack, 0);

              s = (symbol){
                  .type = expected_type,
                  .kind = TU_OF((symKind, sym_lvalue), coerced_loc),
              };
            } else {
              assertMessage(
                  false,
                  "type mismatch: expected %s, got %s",
                  snprint(stdAlloc, "{item_type}", expected_type).ptr,
                  snprint(stdAlloc, "{item_type}", s.type).ptr
              );
            }
          }
        }
        msList_push(allocator, args, s);
      }
      //
      // stack space
      //

      // push space for return type
      var_ return_type = function.type->_item_type_block.types[msList_len(function.type->_item_type_block.types) - 1];
      usize return_size =
          TU_IS((item_type, item_type_type), return_type[0])
              ? sizeof(usize) // TODO idk
              : lineup(type_size(return_type), sizeof(usize));
      usize return_addr = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)NULL, return_size));
      // record stack pointer
      mList_push(stack_frames, mList_len(stack));
      defer { mList_len(stack) = mList_pop(stack_frames); }; // pop stack frame

      // push length
      usize acount = msList_len(function.type->_item_type_block.types) - 1;
      mList_pushArr(stack, *VLAP((u8 *)&acount, sizeof(usize)));
      // push arguments
      foreach (var_ arg, vla(*msList_vla(args)))
        mList_pushArr(stack, *VLAP((u8 *)&arg, sizeof(arg)));

      msList_deInit(allocator, args); // get rid of temporary list

      //
      // add to symbol stack
      //
      mList_push(symbols, msHmap_init(arena_new_ext(msHmap_allocator(mList_last(symbols)), 512), symbol));
      defer {
        arena_cleanup(msHmap_allocator(mList_pop(symbols)));
      }; //  will remove both the symbols and the allocator

      //
      //  begin execution
      //

      if (TU_IS((symKind, sym_function), function.kind)) {
        var_ labels = msHmap_init(allocator, usize, 20);
        defer { msHmap_deinit(labels); };

        assertMessage(!(function.kind._sym_function->op));
        var_ ops = function.kind._sym_function->args; // list of operations
        foreach (var_ opn, range(0, msList_len(ops))) {
          if (ops[opn]->op == builtin_LABEL) {
            assertMessage(msList_len(ops[opn]->args) == 1);
            assertMessage(!ops[opn]->args[0]->op);
            msHmap_set(labels, ops[opn]->args[0]->text, opn);
          }
        }
        for (usize opn = 0; opn < msList_len(ops); opn++) {
          switch (ops[opn]->op) {
            case builtin_LABEL: {
            } break;
            case builtin_JMP:
            case builtin_JMP_IF: {
              assertMessage(msList_len(ops[opn]->args) == (ops[opn]->op == builtin_JMP ? 1 : 2));

              var_ label = msHmap_get(labels, ops[opn]->args[ops[opn]->op == builtin_JMP ? 0 : 1]->text);

              if (!label)
                assertMessage(false, "unknown label : %s", snprint(stdAlloc, "slice(c8)", ops[opn]->args[0]).ptr);
              if (ops[opn]->op == builtin_JMP)
                opn = *label;
              else {
                var_ condition = interpret(allocator, stack, stack_frames, temp_frames, ops[opn]->args[0], symbols);
                assertMessage(
                    TU_IS((symKind, sym_lvalue), condition.kind),
                    "%s", snprint(stdAlloc, "condition is a {symbol}", condition).ptr
                );

                usize len = type_size(condition.type);
                if (fptr_isEmpty((fptr){
                        len,
                        mList_arr(stack) + condition.kind._sym_lvalue,
                    }))
                  continue;
                else
                  opn = *label;
              }
            } break;
            case builtin_RETURN: {
              assertMessage(msList_len(ops[opn]->args) == 1);
              bool is_iteral_zero = ops[opn]->args[0]->op == 0 && fptr_eq(fp("0"), ops[opn]->args[0]->text);
              // literal 0 allowed , otherwise types should be equal
              var_ return_addr_ptr = mList_arr(stack) + return_addr;
              if (is_iteral_zero) {
                memset(return_addr_ptr, 0, return_size);
              } else {
                var_ retval = interpret(allocator, stack, stack_frames, temp_frames, ops[opn]->args[0], symbols);
                if (TU_IS((item_type, item_type_type), return_type[0])) {
                  assertMessage(TU_IS((symKind, sym_type), retval.kind));
                  return retval;
                } else {
                  assertMessage(item_type_equal(allocator, retval.type, return_type));
                  memcpy(return_addr_ptr, mList_arr(stack) + retval.kind._sym_lvalue, return_size);
                }
              }
              goto endloop;
            } break;
            default: {
              interpret(allocator, stack, stack_frames, temp_frames, ops[opn], symbols);
            } break;
          }
        }
        {
        endloop:
          if (TU_IS((item_type, item_type_type), return_type[0])) {
            return (symbol){
                .type = return_type,
                .kind = TU_OF((symKind, sym_type), {}),
            };
          } else {
            return (symbol){
                .type = return_type,
                .kind = TU_OF((symKind, sym_lvalue), return_addr),
            };
          }
        }
      } else if (TU_IS((symKind, sym_extern), function.kind)) {
        return function.kind._sym_extern(return_addr, stack, stack_frames);
      } else {
        assertMessage(false, "unknown function type ");
      }
    } break;
    case builtin_MOVE: {
      assertMessage(msList_len(node->args) == 2);

      var_ src = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      var_ dst = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);

      assertMessage(src.type && TU_IS((item_type, item_type_ptr), src.type[0]));
      assertMessage(dst.type && TU_IS((item_type, item_type_ptr), dst.type[0]));

      usize src_off = *(usize *)(mList_arr(stack) + src.kind._sym_lvalue);
      usize dst_off = *(usize *)(mList_arr(stack) + dst.kind._sym_lvalue);

      usize src_size = type_size(get_ptr_iType(src.type));
      usize dst_size = type_size(get_ptr_iType(dst.type));

      assertMessage(dst_size >= src_size);
      if (dst_size > src_size)
        memset(mList_arr(stack) + dst_off, 0, dst_size);
      memcpy(
          mList_arr(stack) + dst_off,
          mList_arr(stack) + src_off,
          src_size
      );

      return (symbol){};
    } break;
    case builtin_WHERE: {
      assertMessage(msList_len(node->args) == 1);

      var_ target = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);

      assertMessage(TU_IS((symKind, sym_lvalue), target.kind), "cannot take address of non-value");

      usize ptr_val = target.kind._sym_lvalue;

      // TODO add concept of an rvalue
      usize ptr_loc = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)&ptr_val, sizeof(ptr_val)));

      return (symbol){
          .type = make_ptr(msHmap_allocator(mList_last(symbols)), target.type),
          .kind = TU_OF((symKind, sym_lvalue), ptr_loc),
      };
    } break;
    case builtin_ALLOCA: {
      assertMessage(msList_len(node->args) == 2);

      var_ t = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      assertMessage(TU_IS((symKind, sym_type), t.kind));

      var_ amt = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);
      assertMessage(TU_IS((item_type, item_type_uint), amt.type[0]));

      usize count = *(usize *)(&mList_arr(stack)[amt.kind._sym_lvalue]);

      usize type_bytes = type_size(t.type);
      usize total_bytes = count * type_bytes;

      // TODO something something alignment
      usize buffer_start = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)NULL, lineup(total_bytes, 8)));

      usize ptr_loc = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)&buffer_start, sizeof(buffer_start)));

      return (symbol){
          .type = make_ptr(msHmap_allocator(mList_last(symbols)), t.type),
          .kind = TU_OF((symKind, sym_lvalue), ptr_loc),
      };
    } break;
    // TODO check these
    case builtin_EQUAL:
    case builtin_MORE:
    case builtin_LESS: {
      assertMessage(msList_len(node->args) == 2);
      var_ a_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      var_ b_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);

      assertMessage(!TU_IS((symKind, sym_type), a_sym.kind) && !TU_IS((symKind, sym_type), b_sym.kind), "cannot perform comparisons on types");

      item_type *ta = a_sym.type;
      item_type *tb = b_sym.type;

      bool a_is_ptr = TU_IS((item_type, item_type_ptr), ta[0]);
      bool b_is_ptr = TU_IS((item_type, item_type_ptr), tb[0]);

      usize a_bytes = type_size(ta);
      usize b_bytes = type_size(tb);

      u64 a_val = 0;
      memcpy(&a_val, &mList_arr(stack)[a_sym.kind._sym_lvalue], a_bytes);

      u64 b_val = 0;
      memcpy(&b_val, &mList_arr(stack)[b_sym.kind._sym_lvalue], b_bytes);

      bool a_signed = false, b_signed = false;

      if (!a_is_ptr) {
        a_signed = TU_IS((item_type, item_type_sint), ta[0]);
        if (a_signed && a_bytes < 8) {
          u64 shift = (8 - a_bytes) * 8;
          a_val = (u64)(((i64)(a_val << shift)) >> shift);
        }
      }

      if (!b_is_ptr) {
        b_signed = TU_IS((item_type, item_type_sint), tb[0]);
        if (b_signed && b_bytes < 8) {
          u64 shift = (8 - b_bytes) * 8;
          b_val = (u64)(((i64)(b_val << shift)) >> shift);
        }
      }

      u64 res_val = 0;

      if (a_is_ptr || b_is_ptr) {
        // Pointers are just unsigned stack indices, do an unsigned comparison
        switch (node->op) {
          case builtin_EQUAL:
            res_val = (a_val == b_val);
            break;
          case builtin_MORE:
            res_val = (a_val > b_val);
            break;
          case builtin_LESS:
            res_val = (a_val < b_val);
            break;
          default:
            break;
        }
      } else {
        bool compare_signed = a_signed || b_signed;

        // clang-format off
        if (compare_signed) {
          i64 sa = (i64)a_val;
          i64 sb = (i64)b_val;
          switch (node->op) {
            case builtin_EQUAL: res_val = (sa == sb); break;
            case builtin_MORE:  res_val = (sa > sb); break;
            case builtin_LESS:  res_val = (sa < sb); break;
            default: break;
          }
        } else {
          switch (node->op) {
            case builtin_EQUAL: res_val = (a_val == b_val); break;
            case builtin_MORE:  res_val = (a_val > b_val); break;
            case builtin_LESS:  res_val = (a_val < b_val); break;
            default: break;
          }
        }
        // clang-format on
      }

      // Return an 8-bit unsigned integer (u8) to represent the boolean result
      item_type *res_type = get_itype((struct intHandle){.bitcount = 8, .issigned = false});
      usize res_bytes = type_size(res_type);

      usize res_loc = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)&res_val, res_bytes));

      while (mList_len(stack) % 8)
        mList_push(stack, 0);

      return (symbol){
          .type = res_type,
          .kind = TU_OF((symKind, sym_lvalue), res_loc),
      };
    } break;
    case builtin_ADD:
    case builtin_SUB:
    case builtin_MUL:
    case builtin_DIV:
    case builtin_SHR:
    case builtin_SHL:
    case builtin_MOD: {
      assertMessage(msList_len(node->args) == 2);
      var_ a_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      var_ b_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);

      assertMessage(!TU_IS((symKind, sym_type), a_sym.kind) && !TU_IS((symKind, sym_type), b_sym.kind), "cannot perform math on types");

      item_type *ta = a_sym.type;
      item_type *tb = b_sym.type;

      bool a_is_ptr = TU_IS((item_type, item_type_ptr), ta[0]);
      bool b_is_ptr = TU_IS((item_type, item_type_ptr), tb[0]);

      assertMessage(!(a_is_ptr && b_is_ptr), "pointer to pointer math unimplemented");

      if (a_is_ptr || b_is_ptr) {
        assertMessage(node->op == builtin_ADD || node->op == builtin_SUB, "only ADD and SUB work on pointers");
        assertMessage(!(b_is_ptr && node->op == builtin_SUB), "cannot subtract a pointer from an integer");
      }

      usize a_bytes = type_size(ta);
      usize b_bytes = type_size(tb);

      u64 a_val = 0;
      memcpy(&a_val, &mList_arr(stack)[a_sym.kind._sym_lvalue], a_bytes);

      u64 b_val = 0;
      memcpy(&b_val, &mList_arr(stack)[b_sym.kind._sym_lvalue], b_bytes);

      bool a_signed = false, b_signed = false;
      usize a_bits = 0, b_bits = 0;
      item_type *ptr_inner_type = NULL;

      if (a_is_ptr) {

        ptr_inner_type = get_ptr_iType(ta);
      } else {
        a_signed = TU_IS((item_type, item_type_sint), ta[0]);
        a_bits = get_int_bitwidth(ta);
        if (a_signed && a_bytes < 8) {
          u64 shift = (8 - a_bytes) * 8;
          a_val = (u64)(((i64)(a_val << shift)) >> shift);
        }
      }
      if (b_is_ptr) {
        ptr_inner_type = get_ptr_iType(tb);
      } else {
        b_signed = TU_IS((item_type, item_type_sint), tb[0]);
        b_bits = get_int_bitwidth(tb);
        if (b_signed && b_bytes < 8) {
          u64 shift = (8 - b_bytes) * 8;
          b_val = (u64)(((i64)(b_val << shift)) >> shift);
        }
      }
      item_type *res_type = NULL;
      u64 res_val = 0;

      if (a_is_ptr || b_is_ptr) {
        usize stride = type_size(ptr_inner_type);
        assertMessage(stride);

        if (node->op == builtin_ADD) {
          if (a_is_ptr)
            res_val = a_val + (b_val * stride);
          else
            res_val = a_val * stride + b_val;
        } else if (node->op == builtin_SUB) {
          res_val = a_val - (b_val * stride);
        }
        res_type = a_is_ptr ? ta : tb;

      } else {
        usize res_bits = a_bits > b_bits ? a_bits : b_bits;
        bool res_signed = a_signed || b_signed;
        res_type = get_itype((struct intHandle){.bitcount = res_bits, .issigned = res_signed});

        // clang-format off
        if (res_signed) {
          i64 sa = (i64)a_val;
          i64 sb = (i64)b_val;
          switch (node->op) {
            case builtin_ADD: res_val = (u64)(sa + sb); break;
            case builtin_SUB: res_val = (u64)(sa - sb); break;
            case builtin_MUL: res_val = (u64)(sa * sb); break;
            case builtin_DIV: assertMessage(sb, "division by zero"); res_val = (u64)(sa / sb); break;
            case builtin_MOD: assertMessage(sb, "modulo by zero"); res_val = (u64)(sa % sb); break;
            case builtin_SHR: res_val = (u64)(sa >> sb); break;
            case builtin_SHL: res_val = (u64)(sa << sb); break;
            default: break;
          }
        } else {
          switch (node->op) {
            case builtin_ADD: res_val = a_val + b_val; break;
            case builtin_SUB: res_val = a_val - b_val; break;
            case builtin_MUL: res_val = a_val * b_val; break;
            case builtin_DIV: assertMessage(b_val, "division by zero"); res_val = a_val / b_val; break;
            case builtin_MOD: assertMessage(b_val, "modulo by zero"); res_val = a_val % b_val; break;
            case builtin_SHR: res_val = a_val >> b_val; break;
            case builtin_SHL: res_val = a_val << b_val; break;
            default: break;
          }
        }
        // clang-format on
      }

      usize res_bytes = type_size(res_type);
      usize res_loc = mList_len(stack);
      mList_pushArr(stack, *VLAP((u8 *)&res_val, res_bytes));
      while (mList_len(stack) % 8)
        mList_push(stack, 0);
      return (symbol){
          .type = res_type,
          .kind = TU_OF((symKind, sym_lvalue), res_loc),
      };
    } break;
    case builtin_ASSIGN: {
      assertMessage(msList_len(node->args) == 2);

      var_ dst_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[0], symbols);
      var_ src_sym = interpret(allocator, stack, stack_frames, temp_frames, node->args[1], symbols);

      assertMessage(TU_IS((symKind, sym_lvalue), dst_sym.kind), "cant assign non-values");
      assertMessage(TU_IS((symKind, sym_lvalue), src_sym.kind), "cant assign non-values");

      usize dst_size = type_size(dst_sym.type);
      usize src_size = type_size(src_sym.type);

      // TODO i really really need some way to coerce types
      if (dst_size <= 8 && src_size <= 8) {
        u64 val = 0;
        memcpy(&val, &mList_arr(stack)[src_sym.kind._sym_lvalue], src_size);

        // sign extend
        bool src_signed = src_sym.type && TU_IS((item_type, item_type_sint), src_sym.type[0]);
        if (src_signed && src_size < 8) {
          u64 shift = (8 - src_size) * 8;
          val = (u64)(((i64)(val << shift)) >> shift);
        }

        memcpy(&mList_arr(stack)[dst_sym.kind._sym_lvalue], &val, dst_size);
      } else {
        usize copy_size = dst_size < src_size ? dst_size : src_size;
        memcpy(
            &mList_arr(stack)[dst_sym.kind._sym_lvalue],
            &mList_arr(stack)[src_sym.kind._sym_lvalue],
            copy_size
        );
      }
      return (symbol){};
    } break;
    default: {
      assertMessage(false, "unimplemented %s", builtins[node->op]);
    } break;
  }
}
#include <stdio.h>

// responsible for writing to the return adress
// TODO ffi
symbol extern_putc(usize return_addr, mList(u8) stack, mList(usize) stack_frames) {
  usize frame_start = mList_last(stack_frames);

  usize acount = *(usize *)(mList_arr(stack) + frame_start);
  assertMessage(acount == 1);

  symbol *args = (symbol *)(mList_arr(stack) + frame_start + sizeof(usize));
  char c = *(char *)(mList_arr(stack) + args->kind._sym_lvalue);
  // println("input : {item_type}", args[0].type);
  // println("extern putc : {i8} , {c8}", c, c);
  // i32 result = 1;
  i32 result = putchar(c);
  *(i32 *)(mList_arr(stack) + return_addr) = result;
  return (symbol){
      .kind = TU_OF((symKind, sym_lvalue), return_addr),
      .type = get_itype((struct intHandle){
          .bitcount = 32,
          .issigned = 1,
      }),
  };
}
void generate_externs(mList(msHmap(symbol)) symbols) {
  AllocatorV ms_allocator = ((List *)symbols)->allocator;
  var_ puts_it = msList_init(ms_allocator, item_type *);
  msList_push(ms_allocator, puts_it, get_itype((struct intHandle){.bitcount = 8, .issigned = 1}));
  msList_push(ms_allocator, puts_it, get_itype((struct intHandle){.bitcount = 32, .issigned = 1}));
  msHmap_set(
      mList_last(symbols),
      "putc",
      ((symbol){
          .type = make_block(ms_allocator, puts_it),
          .kind = TU_OF((symKind, sym_extern), extern_putc),
      })
  );
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

  generate_externs(symbols);

  foreach (var_ node, vla(*msList_vla(list)))
    interpret(
        stdAlloc,
        stack,
        stack_frames,
        NULL, // never uses this
        node,
        symbols
    );
}
#include "wheels/wheels.h"
