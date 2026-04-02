#include "ast.h"
#include "wheels/macros.h"
#include "wheels/print.h"
#include "wheels/wheels.h"
#include <stdio.h>
fptr read_file(AllocatorV allocator, char *path) {
  FILE *f = fopen(path, "rb");
  assertMessage(f);
  defer { fclose(f); };

  fseek(f, 0, SEEK_END);
  var_ res = (fptr){ftell(f)};
  fseek(f, 0, SEEK_SET);

  res.ptr = aCreate(allocator, u8, res.len);
  fread(res.ptr, 1, res.len, f);
  return res;
}
int main(int nargs, char *args[nargs]) {
  assertMessage(nargs > 1);
  fptr file = read_file(stdAlloc, args[1]);
  var_ tlist = breakup(stdAlloc, file);
  for_each_((var_ item, mList_vla(tlist)), {
    fptr thisS;
    thisS.len = item.len;
    thisS.ptr = file.ptr + item.ptr;
    println("{slice(c8)}", thisS);
    println("---");
  });
  return 0;
}
