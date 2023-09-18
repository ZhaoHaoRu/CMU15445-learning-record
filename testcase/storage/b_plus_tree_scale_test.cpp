//
// Created by Haoru Zhao on 2023/7/24.
//

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/index/b_plus_tree.h"
#include "test_util.h"  // NOLINT

namespace bustub {
TEST(BPlusTreeTests, ScaleTest) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager.get());
  // create and fetch header_page
  page_id_t page_id;
  auto header_page = bpm->NewPage(&page_id);

  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", header_page->GetPageId(), bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;
  // create transaction
  auto *transaction = new Transaction(0);

  // (wxx)  修改这里测试
  int size = 500;

  // output to file
  std::ofstream output("/Users/zhaohaoru/cmu445/cmu445/test/storage/input.in");

  std::vector<int64_t> keys(size);

  std::iota(keys.begin(), keys.end(), 1);

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(keys.begin(), keys.end(), g);

  for (auto key : keys) {
    std::cout << key << std::endl;
    output << key << std::endl;
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid, transaction);
  }

  std::vector<RID> rids;

  std::shuffle(keys.begin(), keys.end(), g);

  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    tree.GetValue(index_key, &rids);
    EXPECT_EQ(rids.size(), 1);

    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }

  //  std::shuffle(keys.begin(), keys.end(), g);

  for (auto key : keys) {
    index_key.SetFromInteger(key);
    tree.Remove(index_key, transaction);
    rids.clear();
    tree.GetValue(index_key, &rids);
    EXPECT_EQ(rids.size(), 0);
  }

  EXPECT_EQ(true, tree.IsEmpty());

  bpm->UnpinPage(HEADER_PAGE_ID, true);

  delete transaction;
  delete bpm;
  remove("test.db");
  remove("test.log");
}
}  // namespace bustub
