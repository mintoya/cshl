#include "cshl.h"
#include "wheels/macros.h"

static const char *sexp_op_names[] = {APPLY_N(OP_NAME, OPERATIONS)};
// countof(sexp_op_names);

static msHmap(builtin_OP) sexp_build_op_table(AllocatorV allocator) {
  msHmap(builtin_OP) table = msHmap_init(allocator, builtin_OP);
  foreach (var_ v, ipairs(i, sexp_op_names)) {
    builtin_OP op = (builtin_OP)i;
    msHmap_set(table, v, op);
  }
  return table;
}

static slice(c8) sexp_current_line(fptr src, usize i) {
  usize start = i;
  while (start > 0 && src.ptr[start - 1] != '\n')
    start--;
  usize end = i;
  while (end < src.len && src.ptr[end] != '\n')
    end++;
  return (slice(c8)){.ptr = (c8 *)src.ptr + start, .len = end - start};
}

#define sexp_assert(allocator, expr, src, i, line, fmt, ...) \
  assertMessage(                                             \
      expr, "%s",                                            \
      snprint(                                               \
          allocator,                                         \
          "sexp:{usize}: " fmt "\n    {slice(c8)}",          \
          (line), ##__VA_ARGS__, sexp_current_line(src, i)   \
      )                                                      \
          .ptr                                               \
  )
static bool is_space(char c) { return EQUAL_ANY(c, ' ', '\r', '\n', '\f', '\v', '\t'); }
static usize sexp_skip(fptr src, usize i, usize *line) {
  while (i < src.len) {
    if (src.ptr[i] == ';')
      while (i < src.len && src.ptr[i] != '\n')
        i++;
    else if (src.ptr[i] == '\n') {
      (*line)++;
      i++;
    } else if (is_space(src.ptr[i]))
      i++;
    else
      break;
  }
  return i;
}

static astNode *sexp_parse_atom(AllocatorV allocator, fptr src, usize i, usize *end, usize line) {
  usize start = i;
  while (i < src.len &&
         !is_space((unsigned char)src.ptr[i]) &&
         src.ptr[i] != '(' && src.ptr[i] != ')' && src.ptr[i] != ';')
    i++;
  sexp_assert(allocator, i > start, src, start, line, "empty atom");
  astNode *node = aCreate(allocator, astNode, 1);
  node->text = (fptr){.ptr = src.ptr + start, .len = i - start};
  node->op = builtin_NONE;
  node->args = NULL;
  *end = i;
  return node;
}

static astNode *sexp_parse_list(AllocatorV allocator, msHmap(builtin_OP) ops, fptr src, usize i, usize *end, usize *line) {
  i++; // skip '('

  astNode *node = aCreate(allocator, astNode, 1);
  node->op = builtin_NONE;
  node->text = (fptr){0, src.ptr};
  node->args = msList_init(allocator, astNode *);

  bool first = true;

  while (1) {
    i = sexp_skip(src, i, line);
    sexp_assert(allocator, i < src.len, src, i, *line, "unclosed list, expected )");

    if (src.ptr[i] == ')') {
      i++;
      break;
    }

    usize next;
    astNode *child;

    if (src.ptr[i] == '\'') {
      i++;
      i = sexp_skip(src, i, line);
      sexp_assert(allocator, i < src.len, src, i, *line, "unexpected EOF after quote");

      astNode *quoted_child;
      if (src.ptr[i] == '(') {
        quoted_child = sexp_parse_list(allocator, ops, src, i, &next, line);
      } else {
        quoted_child = sexp_parse_atom(allocator, src, i, &next, *line);
      }
      child = quoted_child;
    } else if (src.ptr[i] == '(') {
      child = sexp_parse_list(allocator, ops, src, i, &next, line);
    } else {
      child = sexp_parse_atom(allocator, src, i, &next, *line);

      if (first) {
        builtin_OP *found = msHmap_get(ops, child->text);
        if (found) {
          node->op = *found;
          i = next;
          first = false;
          continue; // don't push the op name as a child
        }
      }
    }

    first = false;
    msList_push(allocator, node->args, child);
    i = next;
  }

  *end = i;
  return node;
}

msList(astNode *) sexp_parse_file(AllocatorV allocator, fptr src) {
  msHmap(builtin_OP) ops = sexp_build_op_table(allocator);

  var_ res = msList_init(allocator, astNode *);
  usize i = 0;
  usize line = 1;

  while ((i = sexp_skip(src, i, &line)) < src.len) {
    sexp_assert(allocator, src.ptr[i] != ')', src, i, line, "unexpected )");
    usize next;
    astNode *child;

    if (src.ptr[i] == '(') {
      child = sexp_parse_list(allocator, ops, src, i, &next, &line);
    } else {
      child = sexp_parse_atom(allocator, src, i, &next, line);
    }

    msList_push(allocator, res, child);
    i = next;
  }

  return res;
}
