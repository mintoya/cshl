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
typedef usize sym_value; // location on the stack
TU_DEFINE(
    (symKind, u8),
    sym_none,
    sym_function,
    sym_extern,
    sym_type,
    sym_value,
);
static_assert(!TU_MK_TAG(symKind, sym_none)); // just (symbol){} needs to be none

typedef struct symbol {
  item_type *type;
  symKind kind;
} symbol;

#endif
