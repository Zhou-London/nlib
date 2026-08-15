#include <nlib/memory_pool.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <type_traits>
#include <vector>

namespace {

static_assert(!std::is_copy_constructible_v<nlib::memory_pool>);
static_assert(!std::is_copy_assignable_v<nlib::memory_pool>);
static_assert(!std::is_move_constructible_v<nlib::memory_pool>);
static_assert(!std::is_move_assignable_v<nlib::memory_pool>);

TEST(MemoryPool, StartsEmptyWithRequestedCapacity) {
  nlib::memory_pool p(32, 10);
  EXPECT_TRUE(p.empty());
  EXPECT_EQ(p.size(), 0u);
  EXPECT_EQ(p.capacity(), 10u);
}

TEST(MemoryPool, RoundsBlockSizeUpToAlignmentAndPointerSize) {
  nlib::memory_pool tiny(1, 4);
  EXPECT_GE(tiny.block_size(), sizeof(void*));
  EXPECT_EQ(tiny.block_size() % tiny.alignment(), 0u);

  nlib::memory_pool cache_line(24, 4, 64);
  EXPECT_EQ(cache_line.block_size(), 64u);
  EXPECT_EQ(cache_line.alignment(), 64u);
}

TEST(MemoryPool, AllocateReturnsAlignedDistinctBlocks) {
  nlib::memory_pool p(48, 8, 64);
  std::set<void*> seen;
  for (int i = 0; i < 8; ++i) {
    void* b = p.allocate();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(b) % 64, 0u);
    EXPECT_TRUE(seen.insert(b).second);
  }
  EXPECT_EQ(p.size(), 8u);
  EXPECT_FALSE(p.empty());
}

TEST(MemoryPool, DeallocateRecyclesMostRecentBlockFirst) {
  nlib::memory_pool p(16, 4);
  void* a = p.allocate();
  void* b = p.allocate();
  p.deallocate(a);
  p.deallocate(b);
  EXPECT_EQ(p.size(), 0u);

  EXPECT_EQ(p.allocate(), b);
  EXPECT_EQ(p.allocate(), a);
}

TEST(MemoryPool, ExhaustedPoolRejectsUntilDeallocate) {
  nlib::memory_pool p(16, 3);
  void* a = p.allocate();
  void* b = p.allocate();
  void* c = p.allocate();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(p.allocate(), nullptr);
  EXPECT_EQ(p.size(), 3u);

  p.deallocate(b);
  EXPECT_EQ(p.allocate(), b);
  EXPECT_EQ(p.allocate(), nullptr);
  p.deallocate(a);
  p.deallocate(c);
  EXPECT_EQ(p.size(), 1u);
}

TEST(MemoryPool, BlocksDoNotOverlap) {
  nlib::memory_pool p(24, 40);
  std::vector<unsigned char*> blocks;
  for (int i = 0; i < 40; ++i) {
    auto* b = static_cast<unsigned char*>(p.allocate());
    std::memset(b, i, p.block_size());
    blocks.push_back(b);
  }
  for (int i = 0; i < 40; ++i)
    for (std::size_t j = 0; j < p.block_size(); ++j)
      ASSERT_EQ(blocks[static_cast<std::size_t>(i)][j], static_cast<unsigned char>(i));
}

TEST(MemoryPool, InterleavedAllocateAndDeallocate) {
  nlib::memory_pool p(64, 32);
  std::vector<void*> live;
  for (int i = 0; i < 32; ++i) live.push_back(p.allocate());
  for (int i = 0; i < 32; i += 2) {  // free every other block
    p.deallocate(live[static_cast<std::size_t>(i)]);
    live[static_cast<std::size_t>(i)] = nullptr;
  }
  EXPECT_EQ(p.size(), 16u);

  std::set<void*> seen(live.begin(), live.end());
  for (int i = 0; i < 16; ++i) EXPECT_TRUE(seen.insert(p.allocate()).second);
  EXPECT_EQ(p.size(), 32u);
}

TEST(MemoryPool, ZeroCapacityRejectsEverything) {
  nlib::memory_pool p(8, 0);
  EXPECT_EQ(p.capacity(), 0u);
  EXPECT_EQ(p.allocate(), nullptr);
}

}  // namespace
