#if !defined(CSHL_H)
  #define CSHL_H (1)
  #include "wheels/fptr.h"
  #include "wheels/macros.h"
  #include "wheels/mytypes.h"
  #include "wheels/print.h"
  #include "wheels/sList.h"
  #include "wheels/tu_macros.h"

//
// 'ast'
//

// clang-format off

  #define TYPE_OPERATIONS\
    SINT,/* _BitInt(n)               (n)      */\
    UINT,/* unsigned _Bitint(n)      (n)      */\
    FLOAT,/* double enum             (n)      */\
    STRUCT,/*struct of ... types     (type...)*/\
    PSTRUCT,/*packed version                  */\
    UNION,/*union of ... types       (type...)*/\
    PTR,/*pointer to type            (type)   */\
    ARRAY,/*arr of type              (type)   */\
    TYPE,/*type of type              ()       */\

  #define STRUCT_OPERATIONS\
    ELEMPTR,

  #define PTR_OPERATIONS\
    REF,/* a = &b   (a,b)*/\
    CP, /* *a = *b  (a,b)*/

  #define MATH_OPERATIONS  /*all syms*/\
    NOT,  /*a = !(b)          (a,b)  */\
    BNOT, /*a = ~(b)          (a,b)  */\
    OR,   /*a = (b||c)        (a,b,c)*/\
    BOR,  /*a = (b|c)         (a,b,c)*/\
    AND,  /*a = (b&&c)        (a,b,c)*/\
    BAND, /*a = (b&c)         (a,b,c)*/\
    XOR,  /*a = ((!!b)^(!!c)) (a,b,c)*/\
    BXOR, /*a = (b^c)         (a,b,c)*/\
    EQUAL,/*a = (b==c)        (a,b,c)*/\
    MORE, /*a = (b>c)         (a,b,c)*/\
    LESS, /*a = (b<c)         (a,b,c)*/\
    ADD,  /*a = (b+c)         (a,b,c)*/\
    SUB,  /*a = (b-c)         (a,b,c)*/\
    MUL,  /*a = (b*c)         (a,b,c)*/\
    DIV,  /*a = (b/c)         (a,b,c)*/\
    SHR,  /*a = (b>>c)        (a,b,c)*/\
    SHL,  /*a = (b<<c)        (a,b,c)*/\
    MOD,  /*a = (b%c)         (a,b,c)*/\

  #define FUNCTION_OPERATIONS\
    RETURN,\
    ARG,/*nth arg of current fn (n)*/\
    BLOCK, /*       ((...),t,(...))*/\
    EXTERN,/*ffi*/\
    CALL,  /*  a = b(c) CALL(a,b,c)*/\
    JMP,   /* goto(a)         (a)  */\
    JMP_IF,/* if(a) goto(b)   (a,b)*/\
    LABEL, /* create label    (b)  */\

  #define MISC_OPERATIONS\
    NONE,  /*symbol is none , none with args is list*/\
    DECL,  /* t s             (s,t)*/\
    SET,   /* s = v           (s,v)*/\
    INIT,  /* typeof(v) s = v (s,v)*/

// clang-format on

  #define OPERATIONS  \
    MISC_OPERATIONS   \
    MATH_OPERATIONS   \
    PTR_OPERATIONS    \
    STRUCT_OPERATIONS \
    TYPE_OPERATIONS   \
    FUNCTION_OPERATIONS

  #define OP_NAME(n) #n,
  #define OP_ENUM(n) builtin_##n,

typedef enum builtin_OP {
  APPLY_N(OP_ENUM, OPERATIONS)
} builtin_OP;

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
    (item_type_type /*  */, struct {}),
    (item_type_ptr /*   */, struct {u32 alignment ;item_type*type; }),
    (item_type_array /* */, struct {u32 alignment ;u32(count);item_type*type; }),
    (item_type_sint /*  */, struct {u32 alignment ;u32(bitwidth); }),
    (item_type_uint /*  */, struct {u32 alignment ;u32(bitwidth); }),
    // list types
    (item_type_struct /**/, struct {u32 alignment ;msList(item_type*)types;msList(usize) offsets; }),
    (item_type_union /* */, struct {u32 alignment ;msList(item_type*)types; }),
    (item_type_block /* */, struct { msList(item_type *) types; }),
);
typedef struct symbol symbol;
tu_def(
    (symKind, u8),
    (sym_none /*    */, struct {}),
    (sym_type /*    */, struct {}),
    (sym_value /*   */, usize),
    (sym_function /**/, msList(astNode *)),
    (sym_extern /*  */, item_type_block *),
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

  #define SYM_OF(item)                                                                                            \
    _Generic(                                                                                                     \
        (REF(typeof(item), item)),                                                                                \
        sym_none * /*    */: (symKind)tu_of(sym_none, /*    */ (*(sym_none *)/*    */ REF(typeof(item), item))),  \
        sym_type * /*    */: (symKind)tu_of(sym_type, /*    */ (*(sym_type *)/*    */ REF(typeof(item), item))),  \
        sym_value * /*   */: (symKind)tu_of(sym_value, /*    */ (*(sym_value *)/*   */ REF(typeof(item), item))), \
        sym_function * /**/: (symKind)tu_of(sym_function, /**/ (*(sym_function *)/**/ REF(typeof(item), item))),  \
        sym_extern * /*  */: (symKind)tu_of(sym_extern, /*  */ (*(sym_extern *)/*  */ REF(typeof(item), item)))   \
    )
  #define SYM_IS(type, s) tu_is(sym_##type, s)

static_assert(!TU_TAG(sym_none)); // just (symbol){} needs to be none

typedef struct symbol {
  item_type *type;
  symKind kind;
} symbol;

REGISTER_PRINTER(symbol, {
  PUTS("{");
  tu_match(
      in.kind,
      case (sym_value, /*   */ _, { PUTS("value"); }),
      case (sym_function, /**/ _, { PUTS("function"); }),
      case (sym_extern, /*  */ _, { PUTS("extern"); }),
      case (sym_type, /*    */ _, { PUTS("type"); }),
      case (sym_none, /*    */ _, { PUTS("none"); }),
  );
  PUTS(",type : ");
  if (in.type) {
    USENAMEDPRINTER("item_type", in.type);
  } else
    PUTS("none");

  PUTS("}");
});
REGISTER_PRINTER(builtin_OP, {
  char *builtins[] = {APPLY_N(OP_NAME, OPERATIONS)};
  var_ v = in < countof(builtins) ? builtins[in] : "unknown";
  USENAMEDPRINTER("cstr", v);
})
REGISTER_PRINTER(item_type, {
  item_type ts = in;
  tu_match(
      ts,
      case (item_type_type, $in, {
        PUTS("type");
      }),
      case (item_type_sint, $in, {
        PUTS("i{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      case (item_type_uint, $in, {
        PUTS("u{");
        USETYPEPRINTER(usize, $in.bitwidth);
        PUTS("}");
      }),
      case (item_type_ptr, $in, {
        PUTS("*{");
        if ($in.type)
          USENAMEDPRINTER("item_type", $in.type[0]);
        PUTS("}");
      }),
      case (item_type_struct, $in, {
        PUTS("s{");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len; i++) {
            if (i > 0)
              PUTS(",");
            PUTS("[");
            USETYPEPRINTER(usize, $in.offsets[i]);
            PUTS("]:");
            if ($in.types[i])
              USENAMEDPRINTER("item_type", $in.types[i][0]);
          }
        }
        PUTS("}");
      }),
      case (item_type_union, $in, {
        PUTS("u{");
        usize len = msList_len($in.types);
        if (len) {
          for (usize i = 0; i < len; i++) {
            if (i > 0)
              PUTS(",");
            if ($in.types[i])
              USENAMEDPRINTER("item_type", $in.types[i][0]);
          }
        }
        PUTS("}");
      }),
      case (item_type_block, $in, {
        PUTS("fn{");
        PUTS("(");
        foreach (usize i, range(0, msList_len($in.types) - 1)) {
          if (i)
            PUTS(",");
          if ($in.types[i])
            USENAMEDPRINTER("item_type", ($in.types[i][0]));
        }
        PUTS(")");
        USENAMEDPRINTER("item_type", $in.types[msList_len($in.types) - 1][0]);
        PUTS("}");
      }),
      default(PUTS("unknown");)
  );
});
REGISTER_SPECIAL_PRINTER("astNode*", astNode *, {
  args = printer_arg_trim(args);
  bool usenumbers = false;

  char *builtins[] = {
      APPLY_N(OP_NAME, OPERATIONS)
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
        USENAMEDPRINTER_WA("astNode*", args, in->args[i]);
      }
    }
    PUTS(")");
  } else if (in->args) {
    PUTS("(");
    for (usize i = 0; i < msList_len(in->args); i++) {
      if (i > 0)
        PUTS(", ");
      USENAMEDPRINTER_WA("astNode*", args, in->args[i]);
    }
    PUTS(")");
  } else {
    USENAMEDPRINTER("slice(c8)", in->text);
  }
});

REGISTER_SPECIAL_PRINTER("item_type*", item_type *, {
  USETYPEPRINTER(item_type, in[0]);
});

#endif
