#pragma once
#include "wheels/allocator.h"
#include "wheels/fptr.h"
#include "wheels/mylist.h"
#include "wheels/mytypes.h"
#include <stddef.h>
typedef enum operation {
  OP_ASSIGNMENT, // a = b
  OP_SUBTRACT,   // a - b
  OP_ADD,        // a + b
  OP_DIVIDE,     // a / b
  OP_MULTIPLY,   // a * b
  OP_SUBSCRIPT,  // a.b
  OP_REFERANCE,  // #a
  OP_PIPE,       // |
  OP_CALL,       // a(...)
  OP_KEYOWRD,
} operation;

static fptr keywords[] = {
    fp("return"),
    fp("const"),
    fp("var"),
    fp("fn"),
    fp("type"),
};
typedef enum : u8 {
  TK_ignored = 0,
  TK_alpha,
  TK_single,
} token_kind;
constexpr token_kind tk_convert[] = {
    ['a' ... 'z'] = TK_alpha,
    ['A' ... 'Z'] = TK_alpha,
    ['0' ... '9'] = TK_alpha,
    ['@'] = TK_alpha,
    ['_'] = TK_alpha,
    // operators
    ['='] = TK_single,
    ['-'] = TK_single,
    ['+'] = TK_single,
    ['*'] = TK_single,
    ['.'] = TK_single,
    ['#'] = TK_single,
    ['&'] = TK_single,
    ['|'] = TK_single,
    ['/'] = TK_single,
    ['>'] = TK_single,
    ['<'] = TK_single,
    // punctuation
    ['('] = TK_single,
    [')'] = TK_single,
    ['{'] = TK_single,
    ['}'] = TK_single,
    ['['] = TK_single,
    [']'] = TK_single,
    [';'] = TK_single,
    ['"'] = TK_single,
};

typedef struct tlocation {
  usize len;
  ptrdiff_t ptr;
} tlocation;
static mList(tlocation) breakup(AllocatorV allocator, fptr str) {
  var_ res = mList_init(allocator, tlocation);
  ptrdiff_t start = 0, i = 0;
  usize len = str.len;

  while (i < len) {
    token_kind cur = tk_convert[(u8)str.ptr[i]];

    if (cur == TK_ignored) {
      if (i > start)
        mList_push(res, ((tlocation){.ptr = start, .len = i - start}));
      start = ++i;
      continue;
    } else if (cur == TK_single) {
      if (i > start)
        mList_push(res, ((tlocation){.ptr = start, .len = i - start}));

      if (str.ptr[i] == '"') {
        ptrdiff_t sstart = i++;
        while (i < (ptrdiff_t)len && str.ptr[i] != '"')
          i++;
        i++; // consume closing "
        mList_push(res, ((tlocation){.ptr = sstart, .len = i - sstart}));
        start = i;
        continue;
      }
      if (str.ptr[i] == '/') {
        if (i + 1 < (ptrdiff_t)len && str.ptr[i + 1] == '/') {
          ptrdiff_t sstart = i++;
          while (i < (ptrdiff_t)len && str.ptr[i] != '\n')
            i++;
          i++;
          mList_push(res, ((tlocation){.ptr = sstart, .len = i - sstart}));
          start = i;
          continue;
        } else if (i + 1 < (ptrdiff_t)len && str.ptr[i + 1] == '*') {
          ptrdiff_t sstart = i++;
          while (i + 1 < (ptrdiff_t)len &&
                 !(str.ptr[i] == '*' && str.ptr[i + 1] == '/')) {
            i++;
          }
          i += 2;
          mList_push(res, ((tlocation){.ptr = sstart, .len = i - sstart}));
          start = i;
          continue;
        }
      }

      if (str.ptr[i] == '.' && i + 2 < (ptrdiff_t)len && str.ptr[i + 1] == '.' && str.ptr[i + 2] == '.') {
        mList_push(res, ((tlocation){.ptr = i, .len = 3}));
        start = i = i + 3;
        continue;
      }

      mList_push(res, ((tlocation){.ptr = i, .len = 1}));
      start = ++i;
      continue;
    }
    i++;
  }
  if (i > start)
    mList_push(res, ((tlocation){.ptr = start, .len = i - start}));

  return res;
}
