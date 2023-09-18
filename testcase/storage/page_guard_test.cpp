//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard_test.cpp
//
// Identification: test/storage/page_guard_test.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <random>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/page/page_guard.h"

#include "gtest/gtest.h"

namespace bustub {

// NOLINTNEXTLINE
TEST(PageGuardTest, SampleTest) {
  const std::string db_name = "test.db";
  const size_t buffer_pool_size = 5;
  const size_t k = 2;

  auto disk_manager = std::make_shared<DiskManagerUnlimitedMemory>();
  auto bpm = std::make_shared<BufferPoolManager>(buffer_pool_size, disk_manager.get(), k);

  page_id_t page_id_temp;
  auto *page0 = bpm->NewPage(&page_id_temp);

  auto guarded_page = BasicPageGuard(bpm.get(), page0);

  EXPECT_EQ(page0->GetData(), guarded_page.GetData());
  EXPECT_EQ(page0->GetPageId(), guarded_page.PageId());
  EXPECT_EQ(1, page0->GetPinCount());

  guarded_page.Drop();

  EXPECT_EQ(0, page0->GetPinCount());

  // Shutdown the disk manager and remove the temporary file we created.
  disk_manager->ShutDown();
}

TEST(PageGuardTest, ReadTset) {
  const std::string db_name = "test.db";
  const size_t buffer_pool_size = 5;
  const size_t k = 2;

  auto disk_manager = std::make_shared<DiskManagerUnlimitedMemory>();
  auto bpm = std::make_shared<BufferPoolManager>(buffer_pool_size, disk_manager.get(), k);

  page_id_t page_id_temp;
  auto *page0 = bpm->NewPage(&page_id_temp);

  // test ~ReadPageGuard()
  {
    auto reader_guard = bpm->FetchPageRead(page_id_temp);
    EXPECT_EQ(2, page0->GetPinCount());
  }
  EXPECT_EQ(1, page0->GetPinCount());

  // test ReadPageGuard(ReadPageGuard &&that)
  {
    auto reader_guard = bpm->FetchPageRead(page_id_temp);
    auto reader_guard_2 = ReadPageGuard(std::move(reader_guard));
    EXPECT_EQ(2, page0->GetPinCount());
  }
  EXPECT_EQ(1, page0->GetPinCount());

  // test ReadPageGuard::operator=(ReadPageGuard &&that)
  {
    auto reader_guard_1 = bpm->FetchPageRead(page_id_temp);
    auto reader_guard_2 = bpm->FetchPageRead(page_id_temp);
    EXPECT_EQ(3, page0->GetPinCount());
    reader_guard_1 = std::move(reader_guard_2);
    EXPECT_EQ(2, page0->GetPinCount());
  }
  EXPECT_EQ(1, page0->GetPinCount());

  // Shutdown the disk manager and remove the temporary file we created.
  disk_manager->ShutDown();
}

TEST(PageGuardTest, BPMTest) {
  const std::string db_name = "test.db";
  const size_t buffer_pool_size = 10;
  const size_t k = 2;

  auto disk_manager = std::make_shared<DiskManagerUnlimitedMemory>();
  auto bpm = std::make_shared<BufferPoolManager>(buffer_pool_size, disk_manager.get(), k);

  page_id_t page_id_temp;
  auto basic_guard = bpm->NewPageGuarded(&page_id_temp);

  auto page0 = bpm->FetchPage(0);

  basic_guard.Drop();

  auto basic_guard2 = bpm->FetchPageBasic(0);
  basic_guard2.Drop();

  bpm->UnpinPage(page0->GetPageId(), page0->IsDirty());

  for (int i = 0; i < 10; ++i) {
    bpm->NewPage(&page_id_temp);
  }

  for (size_t i = 1; i <= buffer_pool_size; ++i) {
    bpm->UnpinPage(i, 0);
  }

  page0 = bpm->FetchPage(0);
  ASSERT_NE(nullptr, page0);

  { auto read_guard = bpm->FetchPageRead(0); }

  auto write_guard = bpm->FetchPageWrite(0);
  // auto write_guard2 = bpm->FetchPageWrite(0);
  auto basic_guard3 = bpm->FetchPageBasic(0);
  ASSERT_EQ(page0->GetPinCount(), 3);
}

}  // namespace bustub
