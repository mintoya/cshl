#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/shmap.h"
#include "wheels/tagged_unions.h"

//
// types
//

typedef struct item_type item_type;

typedef struct item_type_int {
  usize bitwidth;
} item_type_int;
typedef struct item_type_type {
} item_type_type;
typedef struct item_type_uint {
  usize bitwidth;
} item_type_uint;
typedef struct item_type_ptr {
  item_type(*type[1]); // not this one
} item_type_ptr;
typedef struct item_type_struct {
  item_type *types;
} item_type_struct;
typedef struct item_type_packed_struct {
  item_type *types;
} item_type_packed_struct;
typedef struct item_type_union {
  item_type *types;
} item_type_union;

typedef struct item_type_block {
  item_type *types; // only one return type,
                    // last item is return type
} item_type_block;

TU_DEFINE(
    (item_type, u8),
    item_type_type,
    item_type_ptr,
    item_type_int,
    item_type_uint,
    item_type_struct,
    item_type_union,
    item_type_block,
);

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
#define OPERATIONS_X                                 \
  NONE,                                              \
      INT,      /*signed integer (bits)*/            \
      UINT,     /*unsigned integer(bits)*/           \
      STRUCT,   /*struct(types)*/                    \
      PSTRUCT,  /*packed struct(types)*/             \
      UNION,    /*union(types)*/                     \
      TYPE,     /*type*/                             \
      PTR,      /*pointer to (type)*/                \
      MODULE,   /*module(name,(members...))*/        \
      MODU_GET, /*get sym from mod(mod,sym)*/        \
      BLOCK,    /*(inputs,output,instructions)*/     \
      BEGINS,   /*record custom stack pointer*/      \
      ENDS,     /*set last custom stack pointer */   \
      RETURN,   /*return (value)*/                   \
      LABEL,    /*create(label)*/                    \
      JMP,      /*jump to (label)*/                  \
      JMP_IF,   /**/                                 \
      CALL,     /*call (functionid,(argslist...))*/  \
      INIT,     /*declare (sym,type?,value)*/        \
      ASSIGN,   /*assign(sym,value)*/                \
      ARG,      /*get nth arg (n)*/                  \
      MOVE,     /*copy(fromptr,toptr)*/              \
      WHERE,    /*pointer-to(sym)*/                  \
      ADD,      /*add b to sym(b,sym)*/              \
      SUB,      /*subtract b from sym(sym,b)*/       \
      MUL,      /*multiply a by b,then set a (a,b)*/ \
      DIV,      /*divide a by b , then set a (a,b)*/ \
      MOD,      /*mod a by b , then set a (a,b)*/

#define builtin_string(n) #n,
char builtins[][8] = {
    APPLY_N(builtin_string, OPERATIONS_X)
};

#define builtin_enum(n) builtin_##n,
enum builtin_OP {
  APPLY_N(builtin_enum, OPERATIONS_X)
};
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

typedef struct {
  item_type *type;
  void *place;
} symbol;

symbol interpret(astNode *astnode) {}

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
