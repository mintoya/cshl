#if !defined(CSHL_H)
  #define CSHL_H (1)
  #include "wheels/fptr.h"
  #include "wheels/mylist.h"
  #include "wheels/print.h"
  #include "wheels/sList.h"
  #include "wheels/tu_macros.h"

//
// 'ast'
//

  #define OPERATIONS_X                                        \
    X(NONE),                                                  \
        X(SINT),    /*signed integer (bits)*/                 \
        X(UINT),    /*unsigned integer(bits)*/                \
        X(STRUCT),  /*struct(types)*/                         \
        X(PSTRUCT), /*packed struct(types)*/                  \
        X(UNION),   /*union(types)*/                          \
        X(PTR),     /*pointer to (type)*/                     \
        X(BLOCK),   /*(inputs,output,instructions)*/          \
        X(RETURN),  /*return (value)*/                        \
        X(LABEL),   /*create(label)*/                         \
        X(JMP),     /*jump to (label)*/                       \
        X(JMP_IF),  /*if a is true JMP (a,label)*/            \
        X(CALL),    /*call (functionid,(argslist...))*/       \
        X(INIT),    /*declare (sym,type?,value)*/             \
        X(ASSIGN),  /*assign b to a(a,b)*/                    \
        X(MOVE),    /*copy(fromptr,toptr)*/                   \
        X(ARG),     /*get nth arg (n)*/                       \
        X(WHERE),   /*pointer-to(sym)*/                       \
        X(NOT),     /*  !(a)*/                                \
        X(OR),      /*a | b  (a,b)*/                          \
        X(AND),     /*a && b (a,b)*/                          \
        X(XOR),     /*a ^ b  (a,b)*/                          \
        X(BNOT),    /*  ~(a)*/                                \
        X(BOR),     /*a | b  (a,b)*/                          \
        X(BAND),    /*a & b (a,b)*/                           \
        X(BXOR),    /*a ^ b  (a,b)*/                          \
        X(EQUAL),   /* a == b (a,b)*/                         \
        X(MORE),    /* a > b (a,b)*/                          \
        X(LESS),    /* a < b (a,b)*/                          \
        X(ADD),     /*add b to sym(b,sym)*/                   \
        X(SUB),     /*subtract b from sym(sym,b)*/            \
        X(MUL),     /*multiply a by b,then set a (a,b)*/      \
        X(DIV),     /*divide a by b , then set a (a,b)*/      \
        X(SHR),     /*shift a by b (a , b)*/                  \
        X(SHL),     /*shift a by b (a , b)*/                  \
        X(MOD),     /*mod a by b , then set a (a,b)*/         \
        X(TYPE),    /*type*/                                  \
        X(SIZEOF),  /*size of t (t)*/                         \
        X(ALIGNOF), /*align of t (t)*/                        \
        X(OFSOF),   /*offset fo n'th st item (st,n)*/         \
        X(ALLOCA),  /*push n of type onto the stack(type,n)*/ \
        X(ELEMPTR), /**/

  #define OP_NAME(n) #n
  #define OP_ENUM(n) builtin_##n

  #define X(n) OP_ENUM(n)
typedef enum builtin_OP {
  OPERATIONS_X
} builtin_OP;
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

tu_def(
    (item_type, u8),
    (item_type_type, struct {}),
    (item_type_ptr, struct { usize alignment ; item_type* type ; }),
    (item_type_sint, struct { usize alignment; usize bitwidth; }),
    (item_type_uint, struct { usize alignment; usize bitwidth; }),
    (item_type_struct, struct { usize alignment; struct { item_type *type; usize offset; } *types; }),
    (item_type_union, struct { usize alignment; item_type **types; }),
    (item_type_block, struct { item_type **types; }),
);
  #define ITYPE_OF(item)                                                                                                         \
    _Generic(                                                                                                                    \
        (REF(typeof(item), item)),                                                                                               \
        item_type_type * /*  */: (item_type)tu_of(item_type_type, /*  */ (*(item_type_type *)/*  */ REF(typeof(item), (item)))), \
        item_type_ptr * /*   */: (item_type)tu_of(item_type_ptr, /*   */ (*(item_type_ptr *)/*   */ REF(typeof(item), (item)))), \
        item_type_sint * /*  */: (item_type)tu_of(item_type_sint, /*  */ (*(item_type_sint *)/*  */ REF(typeof(item), (item)))), \
        item_type_uint * /*  */: (item_type)tu_of(item_type_uint, /*  */ (*(item_type_uint *)/*  */ REF(typeof(item), (item)))), \
        item_type_struct * /**/: (item_type)tu_of(item_type_struct, /**/ (*(item_type_struct *)/**/ REF(typeof(item), (item)))), \
        item_type_union * /* */: (item_type)tu_of(item_type_union, /* */ (*(item_type_union *)/* */ REF(typeof(item), (item)))), \
        item_type_block * /* */: (item_type)tu_of(item_type_block, /* */ (*(item_type_block *)/* */ REF(typeof(item), (item))))  \
    )

  #define ITYPE_IS(type, s) tu_is(item_type_##type, s)

//
// interpreter
//

typedef struct symbol symbol;
typedef symbol (*sym_extern)(usize return_addr, mList(u8) stack, mList(usize) stack_frame);
tu_def(
    (symKind, u8),
    (sym_none, struct {}),
    (sym_type, struct {}),
    (sym_lvalue, usize),
    (sym_rvalue, fptr),
    (sym_function, astNode *),
    (sym_extern, sym_extern),
);

  #define SYM_OF(item)                                                                                           \
    _Generic(                                                                                                    \
        (REF(typeof(item), item)),                                                                               \
        sym_none * /*    */: (symKind)tu_of(sym_none, /*    */ (*(sym_none *)/*    */ REF(typeof(item), item))), \
        sym_function * /**/: (symKind)tu_of(sym_function, /**/ (*(sym_function *)/**/ REF(typeof(item), item))), \
        sym_extern * /*  */: (symKind)tu_of(sym_extern, /*  */ (*(sym_extern *)/*  */ REF(typeof(item), item))), \
        sym_type * /*    */: (symKind)tu_of(sym_type, /*    */ (*(sym_type *)/*    */ REF(typeof(item), item))), \
        sym_lvalue * /*  */: (symKind)tu_of(sym_lvalue, /*  */ (*(sym_lvalue *)/*  */ REF(typeof(item), item))), \
        sym_rvalue * /*  */: (symKind)tu_of(sym_rvalue, /*  */ (*(sym_rvalue *)/*  */ REF(typeof(item), item)))  \
    )
  #define SYM_IS(type, s) tu_is(sym_##type, s)

static_assert(!TU_TAG(sym_none)); // just (symbol){} needs to be none

typedef struct symbol {
  item_type *type;
  symKind kind;
} symbol;

  #define X(...) #__VA_ARGS__

REGISTER_PRINTER(symbol, {
  PUTS("{");
  tu_match(
      in.kind,
      case (sym_lvalue, x, /*  */ { PUTS("lvalue"); }),
      case (sym_rvalue, x, /*  */ { PUTS("rvalue"); }),
      case (sym_function, x, /**/ { PUTS("function"); }),
      case (sym_extern, x, /*  */ { PUTS("extern"); }),
      case (sym_type, x, /*    */ { PUTS("type"); }),
      case (sym_none, x, /*    */ { PUTS("none"); }),
  );
  PUTS(",type : ");
  if (in.type) {
    USENAMEDPRINTER("item_type", in.type);
  } else {
    PUTS("none");
  }
  PUTS("}");
});
REGISTER_PRINTER(builtin_OP, {
  char builtins[][8] = {
      OPERATIONS_X
  };
  var_ v = builtins[in];
  USENAMEDPRINTER("cstr", v);
})
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
