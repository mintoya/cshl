#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/shmap.h"
#include "wheels/tagged_unions.h"
#include <stddef.h>

//
// types
//

typedef struct item_type item_type;

typedef struct item_type_type {
} item_type_type;
typedef struct item_type_module {
} item_type_module;

typedef struct item_type_int {
  usize bitwidth;
  usize alignment;
} item_type_int;
typedef struct item_type_uint {
  usize bitwidth;
  usize alignment;
} item_type_uint;
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
    item_type_int,
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
usize type_alignment(item_type *t) {
  item_type ts = *t;
  TU_MATCH(
      (item_type, ts),
      (item_type_struct, {
        return $in.alignment;
      }),
      (item_type_union, {
        return $in.alignment;
      }),
      (item_type_int, {
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
usize type_size(item_type *t) {
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
      (item_type_int, {
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

//
// text processing
//

u8 fp_pop_front(fptr *f) {
  assertMessage(f->len);
  f->ptr++;
  f->len--;
  return f->ptr[-1];
}
u8 fp_pop_back(fptr *f) {
  assertMessage(f->len);
  f->len--;
  return f->ptr[f->len];
}
fptr fp_trim_space(fptr f) {
  while (f.len && f.ptr[0] <= ' ')
    fp_pop_front(&f);
  while (f.len && f.ptr[f.len - 1] <= ' ')
    fp_pop_back(&f);
  return f;
}
fptr fptr_a(u8 bounds[2], fptr f) {
  usize stack = 0;
  usize start = 0, end = f.len;
  usize curr = 0;
  while (curr < f.len) {
    if (f.ptr[curr] == bounds[0]) {
      if (!stack)
        start = curr;
      stack++;
    }
    if (f.ptr[curr] == bounds[1]) {
      if (stack == 1)
        end = curr;
      stack--;
    }
    if (!stack)
      return ((fptr){
          end - start,
          f.ptr + start,
      });
  }
  return nullFptr;
}
fptr fptr_i(u8 bounds[2], fptr f) {
  usize stack = 0;
  usize start = 0, end = f.len;
  usize curr = 0;
  while (curr < f.len) {
    if (f.ptr[curr] == bounds[0]) {
      if (!stack)
        start = curr;
      stack++;
    } else if (f.ptr[curr] == bounds[1]) {
      if (stack == 1)
        end = curr;
      stack--;
      if (!stack)
        return ((fptr){
            end - start - 1,
            f.ptr + start + 1,
        });
    }
    curr++;
  }
  return nullFptr;
}
fptr fptr_t(u8 c, fptr f) {
  usize i = 0;
  while (i < f.len && f.ptr[i] != c)
    i++;
  if (i == f.len)
    return nullFptr;
  else
    return ((fptr){i, f.ptr});
}
fptr fptr_f(u8 c, fptr f) {
  usize i = 0;
  while (i < f.len && f.ptr[i] != c)
    i++;
  if (i == f.len)
    return nullFptr;
  else
    return ((fptr){i + 1, f.ptr});
}
fptr fptr_after(fptr around, fptr inside) {
  var_ start = inside.ptr + inside.len;
  var_ end = around.ptr + around.len;
  if (start >= around.ptr && start <= end)
    return ((fptr){
        (usize)(end - start),
        start,
    });
  else
    return nullFptr;
}
fptr fptr_pop_comma(fptr p) {
  while (p.len && (p.ptr[0] <= ' ' || p.ptr[0] == ','))
    fp_pop_front(&p);
  return fp_trim_space(p);
}
struct fptr_cuti_t {
  fptr _[2];
};
struct fptr_cuti_t fptr_cut(fptr f, usize i) {
  assertMessage(i < f.len);
  typeof(fptr_cut(nullFptr, 0)) res;
  res._[0] = ((fptr){i, f.ptr});
  res._[1] = ((fptr){f.len - i, f.ptr + i});
  return res;
}
struct fptr_next_call_t { // given A(...)
  fptr fun;               // A
  fptr arg;               // ...
};
struct fptr_next_call_t fptr_next_call(fptr f) {
  f = fp_trim_space(f);
  typeof(fptr_next_call(nullFptr)) res;
  res.fun = fp_trim_space(fptr_t('(', f));
  res.arg = fp_trim_space(fptr_i((u8[]){'(', ')'}, f));
  return res;
}
msList(fptr) fp_split_comma(AllocatorV allocator, fptr in) {
  var_ res = msList_init(allocator, fptr);
  usize i = 0, stack = 0, last = 0;
  while (i < in.len) {
    if (in.ptr[i] == '(')
      stack++;
    else if (in.ptr[i] == ')')
      stack--;
    else if (in.ptr[i] == ',')
      if (!stack) {
        msList_push(allocator, res, fp_trim_space((fptr){i - last, in.ptr + last}));
        last = i + 1;
      }
    i++;
  }
  if (i > last && !stack)
    msList_push(allocator, res, fp_trim_space((fptr){i - last, in.ptr + last}));
  return res;
}

//
// 'ast'
//

// clang-format off
#define OPERATIONS_X                                   \
      X(NONE),                                             \
      X(SINT),    /*signed integer (bits)*/            \
      X(UINT),    /*unsigned integer(bits)*/           \
      X(STRUCT),  /*struct(types)*/                    \
      X(PSTRUCT), /*packed struct(types)*/             \
      X(UNION),   /*union(types)*/                     \
      X(PTR),     /*pointer to (type)*/                \
      X(MODULE),  /*module(name,(members...))*/        \
      X(MOD_GET), /*get sym from mod(mod,sym)*/        \
      X(BLOCK),   /*(inputs,output,instructions)*/     \
      X(BEGINS),  /*record custom stack pointer*/      \
      X(ENDS),    /*set last custom stack pointer */   \
      X(RETURN),  /*return (value)*/                   \
      X(LABEL),   /*create(label)*/                    \
      X(JMP),     /*jump to (label)*/                  \
      X(JMP_IF),  /*if a is true JMP (a,label)*/       \
      X(CALL),    /*call (functionid,(argslist...))*/  \
      X(INIT),    /*declare (sym,type?,value)*/        \
      X(ASSIGN),  /*assign(sym,value)*/                \
      X(ARG),     /*get nth arg (n)*/                  \
      X(MOVE),    /*copy(fromptr,toptr)*/              \
      X(WHERE),   /*pointer-to(sym)*/                  \
      X(EQUAL),   /* a == b (a,b)*/                    \
      X(MORE),    /* a > b (a,b)*/                     \
      X(LESS),    /* a < b (a,b)*/                     \
      X(ADD),     /*add b to sym(b,sym)*/              \
      X(SUB),     /*subtract b from sym(sym,b)*/       \
      X(MUL),     /*multiply a by b,then set a (a,b)*/ \
      X(DIV),     /*divide a by b , then set a (a,b)*/ \
      X(MOD),     /*mod a by b , then set a (a,b)*/    \
      X(TYPE),    /*type*/                             \
      X(SIZEOF),  /*size of t (t)*/                    \
      X(ALIGNOF), /*align of t (t)*/                   \
      X(OFSETOF), /*offset fo n'th st item (st,n)*/    \
      X(TYPEOF),  /*type of (expr)*/

#define X(n) #n
char builtins[][8] = {
    OPERATIONS_X
};
#undef X
#define X(n) builtin_##n
enum builtin_OP {
  OPERATIONS_X
};
// clang-format on

typedef struct astNode {
  fptr text;
  usize op; // index of builtin
  struct astNode **args;
} astNode;

AllocatorV astNodes_arena = NULL;

#include "wheels/arenaAllocator.h"
astNode *astNode_expand(usize op, fptr builtin, fptr text) {
  astNodes_arena = astNodes_arena ?: arena_new_ext(stdAlloc, 1024);

  static msHmap(usize) name_to_idx = NULL;
  if (!name_to_idx) {
    name_to_idx = msHmap_init(stdAlloc, usize);
    for (int i = 1; i < countof(builtins); i++) {
      msHmap_set(name_to_idx, builtins[i], i);
    }
  }

  assertMessage(op || (builtin.len));
  if (!op) {
    var_ opv = msHmap_get(name_to_idx, builtin);
    op = opv ? *opv : op;
  }

  astNode *next = aCreate(astNodes_arena, astNode);

  if (op > 0 && op < countof(builtins)) {

    next->op = op;
    next->text = text;
    next->args = msList_init(astNodes_arena, astNode *);

  } else {

    assertMessage(
        false,
        "unknown builtin  %s",
        snprint(
            stdAlloc,
            "index: {}, name: {slice(c8)}",
            op, builtin
        )
            .ptr
    );
  }
  return next;
}

astNode *astNode_recurse(fptr call, msList(fptr) args);

astNode *astnode_expand(fptr f) {
  f = fp_trim_space(f);
  struct fptr_next_call_t b = fptr_next_call(f);
  if (b.fun.len) {
    var_ split = fp_split_comma(astNodes_arena, b.arg);
    return astNode_recurse(b.fun, split);
  } else if (b.arg.len) { // just parenthesis
    astNode *node = aCreate(astNodes_arena, astNode);
    node->op = builtin_NONE;
    node->text = f;
    node->args = msList_init(astNodes_arena, astNode *);
    var_ split = fp_split_comma(astNodes_arena, b.arg);
    for (usize i = 0; i < msList_len(split); i++) {
      msList_push(astNodes_arena, node->args, astnode_expand(split[i]));
    }
    return node;
  } else { // Leaf identifier
    astNode *node = aCreate(astNodes_arena, astNode);
    node->op = builtin_NONE;
    node->text = f;
    node->args = NULL;
    return node;
  }
}

astNode *astNode_recurse(fptr call, msList(fptr) args) {
  astNode *node = astNode_expand(0, call, call);

  for_each_((var_ v, msList_vla(args)), {
    msList_push(astNodes_arena, node->args, astnode_expand(v));
  });

  return node;
}

REGISTER_SPECIAL_PRINTER("astNode", astNode *, {
  args = printer_arg_trim(args);
  bool usenumbers = false;

  if (fptr_eq(args, fp("numbers")))
    usenumbers = true;

  if (!in) {
    PUTS("NULL");
  } else if (in->op) {
    if (usenumbers)
      USETYPEPRINTER(usize, in->op);
    else
      USENAMEDPRINTER("cstr", (char *)builtins[in->op]);
    PUTS("(");
    if (in->args) {
      for (usize i = 0; i < msList_len(in->args); i++) {
        if (i > 0)
          PUTS(", ");
        USENAMEDPRINTER_WA("astNode", args, in->args[i]);
      }
    }
    PUTS(")");
  } else if (in->args) {
    PUTS("(");
    for (usize i = 0; i < msList_len(in->args); i++) {
      if (i > 0)
        PUTS(", ");
      USENAMEDPRINTER_WA("astNode", args, in->args[i]);
    }
    PUTS(")");
  } else {
    USENAMEDPRINTER("slice(c8)", in->text);
  }
});

msList(astNode *) astNode_process_file(fptr s) {
  var_ b = fptr_next_call(s);
  var_ nodes = msList_init(stdAlloc, astNode *);
  do {
    var_ split = fp_split_comma(stdAlloc, b.arg);
    defer { msList_deInit(stdAlloc, split); };

    astNode *node = astNode_recurse(b.fun, split);
    msList_push(stdAlloc, nodes, node);
    b = fptr_next_call(
        fptr_after(
            s,
            ((fptr){
                b.arg.len + 1,
                b.arg.ptr,
            })
        )
    );
  } while (b.fun.len && b.fun.ptr);
  return nodes;
}

//
// interpreter
//

typedef struct symbol symbol;
typedef struct symbol {
  item_type *type;
  union {
    void *location; // on the stack
    msHmap(symbol) module;
  };
} symbol;

item_type module_baseSymbol[1] = {
    [0] = {
        .tag = TU_MK_TAG(item_type, item_type_module)
    },
};

// clang-format off
#define OPERATIONS_X                                   \
      X(NONE),                                             \
      X(SINT),    /*signed integer (bits)*/            \
      X(UINT),    /*unsigned integer(bits)*/           \
      X(STRUCT),  /*struct(types)*/                    \
      X(PSTRUCT), /*packed struct(types)*/             \
      X(UNION),   /*union(types)*/                     \
      X(PTR),     /*pointer to (type)*/                \
      X(MODULE),  /*module(name,(members...))*/        \
      X(MOD_GET), /*get sym from mod(mod,sym)*/        \
      X(BLOCK),   /*(inputs,output,instructions)*/     \
      X(BEGINS),  /*record custom stack pointer*/      \
      X(ENDS),    /*set last custom stack pointer */   \
      X(RETURN),  /*return (value)*/                   \
      X(LABEL),   /*create(label)*/                    \
      X(JMP),     /*jump to (label)*/                  \
      X(JMP_IF),  /*if a is true JMP (a,label)*/       \
      X(CALL),    /*call (functionid,(argslist...))*/  \
      X(INIT),    /*declare (sym,type?,value)*/        \
      X(ASSIGN),  /*assign(sym,value)*/                \
      X(ARG),     /*get nth arg (n)*/                  \
      X(MOVE),    /*copy(fromptr,toptr)*/              \
      X(WHERE),   /*pointer-to(sym)*/                  \
      X(EQUAL),   /* a == b (a,b)*/                    \
      X(MORE),    /* a > b (a,b)*/                     \
      X(LESS),    /* a < b (a,b)*/                     \
      X(ADD),     /*add b to sym(b,sym)*/              \
      X(SUB),     /*subtract b from sym(sym,b)*/       \
      X(MUL),     /*multiply a by b,then set a (a,b)*/ \
      X(DIV),     /*divide a by b , then set a (a,b)*/ \
      X(MOD),     /*mod a by b , then set a (a,b)*/    \
      X(TYPE),    /*type*/                             \
      X(SIZEOF),  /*size of t (t)*/                    \
      X(ALIGNOF), /*align of t (t)*/                   \
      X(OFSETOF), /*offset fo n'th st item (st,n)*/    \
      X(TYPEOF),  /*type of (expr)*/

msHmap(symbol) mainmod = NULL;   // main module
mList(symbol) blockstack = NULL; // function symbol stack
                                 // destroy at block end
mList(AllocatorV) arenaStack = NULL;
// clang-format on
symbol interpret(msHmap(symbol) current_module, astNode *node) {
  if (!arenaStack) {
    arenaStack = mList_init(stdAlloc, AllocatorV);
    mList_push(arenaStack, arena_new_ext(stdAlloc, 1024));
  }
  switch (node->op) {
    case builtin_NONE: { // symbol or literal
      assertMessage(msList_len(node->args) == 0);
      fptr f = node->text;
    } break;
    case builtin_MODULE: {
      assertMessage(msList_len(node->args) == 2);
      msHmap(symbol) newmod = msHmap_init(stdAlloc, symbol);
      assertMessage(
          !msHmap_get(current_module, node->args[0]->text),
          "%s",
          snprint(stdAlloc, "module name {slice(c8)} collides").ptr
      );
      msHmap_set(
          current_module, node->args[0]->text,
          ((symbol){
              .type = module_baseSymbol,
              .module = newmod,
          })
      );
      for (usize i = 0; i < msList_len(node->args[1]); i++)
        interpret(newmod, node->args[1]);
      return ((symbol){
          .type = module_baseSymbol,
          .module = newmod,
      });
    } break;
  }

  return (symbol){};
}

int main(void) {
  u8 c[] =
      {
#embed "int.txt"
      };
  var_ list = astNode_process_file(fp(c));
  println("{msList : astNode}", list);
  println("{msList : astNode : numbers}", list);
}
#include "wheels/wheels.h"
