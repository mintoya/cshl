#if !defined(MY_POOLS_H)
  #define MY_POOLS_H (1)
  #include "ast.h"
  #include "wheels/allocator.h"
  #include "wheels/macros.h"
  #include "wheels/mytypes.h"
typedef struct {
  usize capacity, length;
  u8 buf[];
} tPool_bock;
typedef struct tPool {
  AllocatorV allocator;
  usize itemSize, blockSize;
  msList(tPool_bock *) blocks;
} tPool;
static tPool *tPool_new(AllocatorV allocator, usize tsize, usize blockCount) {
  var_ res = aCreate(allocator, tPool);
  *res = (typeof(*res)){
      .allocator = allocator,
      .blocks = msList_init(allocator, tPool_bock *),
      .itemSize = tsize,
      .blockSize = blockCount,
  };
  return res;
}
static void *tPool_push(tPool *pool) {
  tPool_bock *block = NULL;
  for_each_((block, msList_vla(pool->blocks)), {
    if (block->length < block->capacity)
      return (block->buf + (block->length++ * pool->itemSize));
  });
  block = (typeof(block))aAlloc(pool->allocator, sizeof(block) + pool->itemSize * pool->blockSize);
  block->length = 0;
  block->capacity = 0;
  msList_push(pool->allocator, pool->blocks, block);
  return (block->buf + (block->length++ * pool->itemSize));
}
static void tPool_free(tPool *pool) {
  var_ allocator = pool->allocator;
  for_each_((var_ b, msList_vla(pool->blocks)), {
    aFree(allocator, b);
  });
  msList_deInit(allocator, pool->blocks);
  aFree(allocator, pool);
}
#endif
