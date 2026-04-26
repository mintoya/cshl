#include "wheels/bigint.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/shmap.h"

//
// types
//

//
// text processing
//

static u8 fp_pop_front(fptr *f) {
  assertMessage(f->len);
  f->ptr++;
  f->len--;
  return f->ptr[-1];
}
static u8 fp_pop_back(fptr *f) {
  assertMessage(f->len);
  f->len--;
  return f->ptr[f->len];
}
static fptr fp_trim_space(fptr f) {
  while (f.len && f.ptr[0] <= ' ')
    fp_pop_front(&f);
  while (f.len && f.ptr[f.len - 1] <= ' ')
    fp_pop_back(&f);
  return f;
}
static fptr fptr_a(u8 bounds[2], fptr f) {
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
static fptr fptr_i(u8 bounds[2], fptr f) {
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
static fptr fptr_t(u8 c, fptr f) {
  usize i = 0;
  while (i < f.len && f.ptr[i] != c)
    i++;
  if (i == f.len)
    return nullFptr;
  else
    return ((fptr){i, f.ptr});
}
static fptr fptr_f(u8 c, fptr f) {
  usize i = 0;
  while (i < f.len && f.ptr[i] != c)
    i++;
  if (i == f.len)
    return nullFptr;
  else
    return ((fptr){i + 1, f.ptr});
}
static fptr fptr_after(fptr around, fptr inside) {
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
static fptr fptr_pop_comma(fptr p) {
  while (p.len && (p.ptr[0] <= ' ' || p.ptr[0] == ','))
    fp_pop_front(&p);
  return fp_trim_space(p);
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
// TODO simply ignores unclosed parenthesis
static msList(fptr) fp_split_comma(AllocatorV allocator, fptr in) {
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
  // if (stack)
  //   msList_clear(res);
  return res;
}

//
// 'ast'
//

#define OPERATIONS_X                                   \
  X(NONE),                                             \
      X(SINT),    /*signed integer (bits)*/            \
      X(UINT),    /*unsigned integer(bits)*/           \
      X(STRUCT),  /*struct(types)*/                    \
      X(PSTRUCT), /*packed struct(types)*/             \
      X(UNION),   /*union(types)*/                     \
      X(PTR),     /*pointer to (type)*/                \
      X(BLOCK),   /*(inputs,output,instructions)*/     \
      X(BEGINS),  /*record custom stack pointer*/      \
      X(ENDS),    /*set last custom stack pointer*/    \
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
      X(OFSOF),   /*offset fo n'th st item (st,n)*/
// begins and ends must be within the current function, equivalent of {} in c, jumping across ends must cause ends to be triggered  if after begins

#define X(n) #n
char builtins[][8] = {
    OPERATIONS_X
};
#undef X
#define X(n) builtin_##n
enum builtin_OP {
  OPERATIONS_X
};
#undef X

typedef struct astNode {
  fptr text;
  usize op; // index of builtin
  struct astNode **args;
} astNode;

static AllocatorV astNodes_arena = NULL;

static astNode *astNode_expand(usize op, fptr builtin, fptr text) {
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

static astNode *astnode_expand2(fptr f) {
  f = fp_trim_space(f);
  struct fptr_next_call_t b = fptr_next_call(f);
  if (b.fun.len) {
    var_ split = fp_split_comma(astNodes_arena, b.arg);

    astNode *node = astNode_expand(0, b.fun, b.fun);
    foreach (var_ v, vla(*msList_vla(split)))
      msList_push(astNodes_arena, node->args, astnode_expand2(v));
    return node;
  } else if (b.arg.len) { // just parenthesis
    astNode *node = aCreate(astNodes_arena, astNode);
    node->op = builtin_NONE;
    node->text = f;
    node->args = msList_init(astNodes_arena, astNode *);
    var_ split = fp_split_comma(astNodes_arena, b.arg);
    for (usize i = 0; i < msList_len(split); i++)
      msList_push(astNodes_arena, node->args, astnode_expand2(split[i]));
    return node;
  } else { // id
    astNode *node = aCreate(astNodes_arena, astNode);
    node->op = builtin_NONE;
    node->text = f;
    node->args = NULL;
    return node;
  }
}

static astNode *astNode_recurse(fptr call, msList(fptr) args) {
  astNode *node = astNode_expand(0, call, call);

  foreach (var_ v, vla(*msList_vla(args)))
    msList_push(astNodes_arena, node->args, astnode_expand2(v));

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

static msList(astNode *) astNode_process_file(AllocatorV allocator, fptr s) {
  var_ fps = fp_split_comma(allocator, s);
  msList(astNode *) res = msList_init(allocator, astNode *, msList_len(fps));
  foreach (fptr s, vla(*msList_vla(fps))) {
    var_ b = fptr_next_call(s);
    do {
      var_ split = fp_split_comma(stdAlloc, b.arg);
      defer { msList_deInit(stdAlloc, split); };

      astNode *node = astNode_recurse(b.fun, split);
      msList_push(stdAlloc, res, node);
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
  }
  return res;
}
