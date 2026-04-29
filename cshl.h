#if !defined(CSHL_H)
  #define CSHL_H (1)
  #include "wheels/fptr.h"
  #include "wheels/mylist.h"
  #include "wheels/sList.h"
  #include "wheels/tagged_unions.h"

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
        X(ASSIGN),  /*assign b to a(a,b)*/               \
        X(MOVE),    /*copy(fromptr,toptr)*/              \
        X(ARG),     /*get nth arg (n)*/                  \
        X(WHERE),   /*pointer-to(sym)*/                  \
        X(NOT),     /*  !(a)*/                           \
        X(OR),      /*a | b  (a,b)*/                     \
        X(AND),     /*a && b (a,b)*/                     \
        X(XOR),     /*a ^ b  (a,b)*/                     \
        X(BNOT),    /*  !(a)*/                           \
        X(BOR),     /*a | b  (a,b)*/                     \
        X(BAND),    /*a && b (a,b)*/                     \
        X(BXOR),    /*a ^ b  (a,b)*/                     \
        X(EQUAL),   /* a == b (a,b)*/                    \
        X(MORE),    /* a > b (a,b)*/                     \
        X(LESS),    /* a < b (a,b)*/                     \
        X(ADD),     /*add b to sym(b,sym)*/              \
        X(SUB),     /*subtract b from sym(sym,b)*/       \
        X(MUL),     /*multiply a by b,then set a (a,b)*/ \
        X(DIV),     /*divide a by b , then set a (a,b)*/ \
        X(SHR),     /*shift a by b (a , b)*/             \
        X(SHL),     /*shift a by b (a , b)*/             \
        X(MOD),     /*mod a by b , then set a (a,b)*/    \
        X(TYPE),    /*type*/                             \
        X(SIZEOF),  /*size of t (t)*/                    \
        X(ALIGNOF), /*align of t (t)*/                   \
        X(OFSOF),   /*offset fo n'th st item (st,n)*/    \
        X(ALLOCA),  /*push n of type onto the stack(type,n)*/
                    // begins and ends must be within the current function, equivalent of {} in c, jumping across ends must cause ends to be triggered  if after begins

  #define OP_NAME(n) #n
  #define OP_ENUM(n) builtin_##n

  #define X(n) OP_ENUM(n)
enum builtin_OP {
  OPERATIONS_X
};
  #undef X

/**
 * text : the text that represents this ast node
 * op   : what operatoin is represented
 *          0 for parenthesized lists
 * args : msList corresponding to arguments of this operation
 */
typedef struct astNode {
  fptr text;
  enum builtin_OP op;
  msList(struct astNode *) args;
} astNode;

//
// types
//

typedef struct item_type item_type;

typedef struct item_type_type {
} item_type_type;
typedef struct item_type_sint {
  usize bitwidth;
  usize alignment;
} item_type_sint;
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
  } *types; // msList
  usize alignment;
} item_type_struct;
typedef struct item_type_union {
  item_type **types;
  usize alignment;
} item_type_union;

typedef struct item_type_block {
  item_type **types; // only one return type,
                     // last item is return type
} item_type_block;

TU_DEFINE(
    (item_type, u8),
    item_type_type,
    item_type_ptr,
    item_type_sint,
    item_type_uint,
    item_type_struct,
    item_type_union,
    item_type_block,
);
  #define ITYPE_OF(item)                                                                                                         \
    _Generic(                                                                                                                    \
        (REF(typeof(item), item)),                                                                                               \
        item_type_type * /*  */: TU_OF((item_type, item_type_type), /*  */ (*(item_type_type *)/*  */ REF(typeof(item), item))), \
        item_type_ptr * /*   */: TU_OF((item_type, item_type_ptr), /*   */ (*(item_type_ptr *)/*   */ REF(typeof(item), item))), \
        item_type_sint * /*  */: TU_OF((item_type, item_type_sint), /*  */ (*(item_type_sint *)/*  */ REF(typeof(item), item))), \
        item_type_uint * /*  */: TU_OF((item_type, item_type_uint), /*  */ (*(item_type_uint *)/*  */ REF(typeof(item), item))), \
        item_type_struct * /**/: TU_OF((item_type, item_type_struct), /**/ (*(item_type_struct *)/**/ REF(typeof(item), item))), \
        item_type_union * /* */: TU_OF((item_type, item_type_union), /* */ (*(item_type_union *)/* */ REF(typeof(item), item))), \
        item_type_block * /* */: TU_OF((item_type, item_type_block), /* */ (*(item_type_block *)/* */ REF(typeof(item), item)))  \
    )

  #define ITYPE_IS(type, s) TU_IS((item_type, item_type_##type), s)

//
// interpreter
//

typedef struct symbol symbol;

typedef symbol (*sym_extern)(usize return_addr, mList(u8) stack, mList(usize) stack_frame);
typedef astNode *sym_function;
// clang-format off
typedef struct {} sym_type;
typedef struct {} sym_none;
// clang-format on
typedef usize sym_lvalue; // location on the stack
typedef fptr sym_rvalue;  // entire value

TU_DEFINE(
    (symKind, u8),
    sym_none,
    sym_function,
    sym_extern,
    sym_type,
    sym_lvalue,
    sym_rvalue,
);
  #define SYM_OF(item)                                                                                             \
    _Generic(                                                                                                      \
        (REF(typeof(item), item)),                                                                                 \
        sym_none * /*    */: TU_OF((symKind, sym_none), /*    */ (*(sym_none *)/*    */ REF(typeof(item), item))), \
        sym_function * /**/: TU_OF((symKind, sym_function), /**/ (*(sym_function *)/**/ REF(typeof(item), item))), \
        sym_extern * /*  */: TU_OF((symKind, sym_extern), /*  */ (*(sym_extern *)/*  */ REF(typeof(item), item))), \
        sym_type * /*    */: TU_OF((symKind, sym_type), /*    */ (*(sym_type *)/*    */ REF(typeof(item), item))), \
        sym_lvalue * /*  */: TU_OF((symKind, sym_lvalue), /*  */ (*(sym_lvalue *)/*  */ REF(typeof(item), item))), \
        sym_rvalue * /*  */: TU_OF((symKind, sym_rvalue), /*  */ (*(sym_rvalue *)/*  */ REF(typeof(item), item)))  \
    )
  #define SYM_IS(type, s) TU_IS((symKind, sym_##type), s)
  #include "wheels/print.h"

static_assert(!TU_MK_TAG(symKind, sym_none)); // just (symbol){} needs to be none

typedef struct symbol {
  item_type *type;
  symKind kind;
} symbol;

REGISTER_PRINTER(symbol, {
  PUTS("{");
  TU_MATCH(
      (symKind, in.kind),

      (sym_lvalue, /*  */ { PUTS("value"); }),
      (sym_rvalue, /*  */ { PUTS("value"); }),
      (sym_function, /**/ { PUTS("function"); }),
      (sym_extern, /*  */ { PUTS("extern"); }),
      (sym_type, /*    */ { PUTS("type"); }),

      (sym_none, /*    */ { PUTS("none"); }),
  );
  PUTS(",type : ");
  if (in.type) {
    USENAMEDPRINTER("item_type", in.type);
  } else {
    PUTS("none");
  }
  PUTS("}");
});
  #define X(...) #__VA_ARGS__
REGISTER_SPECIAL_PRINTER("astNode", astNode *, {
  args = printer_arg_trim(args);
  bool usenumbers = false;

  char builtins[][8] = {
      OPERATIONS_X
  };
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
  #undef X
#endif
