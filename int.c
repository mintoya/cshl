#include "ast.c"
#include "typ.c"
typedef struct symbol symbol;
typedef struct symbol {
  item_type *type;
  union {
    void *location; // on the stack
    msHmap(symbol) module;
  };
} symbol;

constexpr item_type module_baseSymbol[1] = {
    [0] = {
        .tag = TU_MK_TAG(item_type, item_type_module)
    },
};
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
