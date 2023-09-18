/**
 * lock_manager_test.cpp
 */

#include <random>
#include <thread>  // NOLINT

#include "common/config.h"
#include "common_checker.h"  // NOLINT
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"

#include "gtest/gtest.h"

namespace bustub {

#define TEST_TIMEOUT_BEGIN                           \
  std::promise<bool> promisedFinished;               \
  auto futureResult = promisedFinished.get_future(); \
                             std::thread([](std::promise<bool>& finished) {
#define TEST_TIMEOUT_FAIL_END(X)                                                                  \
  finished.set_value(true);                                                                       \
  }, std::ref(promisedFinished)).detach();                                                        \
  EXPECT_TRUE(futureResult.wait_for(std::chrono::milliseconds(X)) != std::future_status::timeout) \
      << "Test Failed Due to Time Out";

/*
 * This test is only a sanity check. Please do not rely on this test
 * to check the correctness.
 */

// --- Helper functions ---
void CheckGrowing(Transaction *txn) { EXPECT_EQ(txn->GetState(), TransactionState::GROWING); }

void CheckShrinking(Transaction *txn) { EXPECT_EQ(txn->GetState(), TransactionState::SHRINKING); }

void CheckAborted(Transaction *txn) { EXPECT_EQ(txn->GetState(), TransactionState::ABORTED); }

void CheckCommitted(Transaction *txn) { EXPECT_EQ(txn->GetState(), TransactionState::COMMITTED); }

void CheckTxnRowLockSize(Transaction *txn, table_oid_t oid, size_t shared_size, size_t exclusive_size) {
  bool correct = true;
  correct = correct && (*txn->GetSharedRowLockSet())[oid].size() == shared_size;
  correct = correct && (*txn->GetExclusiveRowLockSet())[oid].size() == exclusive_size;
  if (!correct) {
    fmt::print("row lock size incorrect for txn={} oid={}: expected (S={} X={}), actual (S={} X={})\n",
               txn->GetTransactionId(), oid, shared_size, exclusive_size, (*txn->GetSharedRowLockSet())[oid].size(),
               (*txn->GetExclusiveRowLockSet())[oid].size());
  }
  EXPECT_TRUE(correct);
}

int GetTxnTableLockSize(Transaction *txn, LockManager::LockMode lock_mode) {
  switch (lock_mode) {
    case LockManager::LockMode::SHARED:
      return txn->GetSharedTableLockSet()->size();
    case LockManager::LockMode::EXCLUSIVE:
      return txn->GetExclusiveTableLockSet()->size();
    case LockManager::LockMode::INTENTION_SHARED:
      return txn->GetIntentionSharedTableLockSet()->size();
    case LockManager::LockMode::INTENTION_EXCLUSIVE:
      return txn->GetIntentionExclusiveTableLockSet()->size();
    case LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE:
      return txn->GetSharedIntentionExclusiveTableLockSet()->size();
  }

  return -1;
}

void CheckTableLockSizes(Transaction *txn, size_t s_size, size_t x_size, size_t is_size, size_t ix_size,
                         size_t six_size) {
  bool correct = true;
  correct = correct && s_size == txn->GetSharedTableLockSet()->size();
  correct = correct && x_size == txn->GetExclusiveTableLockSet()->size();
  correct = correct && is_size == txn->GetIntentionSharedTableLockSet()->size();
  correct = correct && ix_size == txn->GetIntentionExclusiveTableLockSet()->size();
  correct = correct && six_size == txn->GetSharedIntentionExclusiveTableLockSet()->size();
  if (!correct) {
    fmt::print(
        "table lock size incorrect for txn={}: expected (S={} X={}, IS={}, IX={}, SIX={}), actual (S={} X={}, IS={}, "
        "IX={}, "
        "SIX={})\n",
        txn->GetTransactionId(), s_size, x_size, is_size, ix_size, six_size, txn->GetSharedTableLockSet()->size(),
        txn->GetExclusiveTableLockSet()->size(), txn->GetIntentionSharedTableLockSet()->size(),
        txn->GetIntentionExclusiveTableLockSet()->size(), txn->GetSharedIntentionExclusiveTableLockSet()->size());
  }
  EXPECT_TRUE(correct);
}

void TableLockTest1() {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};

  std::vector<table_oid_t> oids;
  std::vector<Transaction *> txns;

  /** 10 tables */
  int num_oids = 10;
  for (int i = 0; i < num_oids; i++) {
    table_oid_t oid{static_cast<uint32_t>(i)};
    oids.push_back(oid);
    txns.push_back(txn_mgr.Begin());
    EXPECT_EQ(i, txns[i]->GetTransactionId());
  }

  /** Each transaction takes an X lock on every table and then unlocks */
  auto task = [&](int txn_id) {
    bool res;
    for (const table_oid_t &oid : oids) {
      res = lock_mgr.LockTable(txns[txn_id], LockManager::LockMode::EXCLUSIVE, oid);
      // LOG_DEBUG("[TableLockTest1] finish take lock of table %d, txn id %d", oid, txn_id);
      EXPECT_TRUE(res);
      CheckGrowing(txns[txn_id]);
    }
    for (const table_oid_t &oid : oids) {
      // LOG_DEBUG("[TableLockTest1] begin to unlock the table %d, txn id %d", oid, txn_id);
      res = lock_mgr.UnlockTable(txns[txn_id], oid);
      // LOG_DEBUG("[TableLockTest1] finish unlock the table %d, txn id %d", oid, txn_id);
      EXPECT_TRUE(res);
      CheckShrinking(txns[txn_id]);
    }
    txn_mgr.Commit(txns[txn_id]);
    CheckCommitted(txns[txn_id]);

    /** All locks should be dropped */
    CheckTableLockSizes(txns[txn_id], 0, 0, 0, 0, 0);
  };

  std::vector<std::thread> threads;
  threads.reserve(num_oids);

  for (int i = 0; i < num_oids; i++) {
    threads.emplace_back(std::thread{task, i});
  }

  for (int i = 0; i < num_oids; i++) {
    threads[i].join();
  }

  for (int i = 0; i < num_oids; i++) {
    delete txns[i];
  }
}
TEST(LockManagerTest, TableLockTest1) { TableLockTest1(); }  // NOLINT

/** Upgrading single transaction from S -> X */
void TableLockUpgradeTest1() {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};

  table_oid_t oid = 0;
  auto txn1 = txn_mgr.Begin();

  /** Take S lock */
  EXPECT_EQ(true, lock_mgr.LockTable(txn1, LockManager::LockMode::SHARED, oid));
  CheckTableLockSizes(txn1, 1, 0, 0, 0, 0);

  /** Upgrade S to X */
  EXPECT_EQ(true, lock_mgr.LockTable(txn1, LockManager::LockMode::EXCLUSIVE, oid));
  CheckTableLockSizes(txn1, 0, 1, 0, 0, 0);

  /** Clean up */
  txn_mgr.Commit(txn1);
  CheckCommitted(txn1);
  CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);

  delete txn1;
}
TEST(LockManagerTest, TableLockUpgradeTest1) { TableLockUpgradeTest1(); }  // NOLINT

void RowLockTest1() {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};

  table_oid_t oid = 0;
  RID rid{0, 0};

  int num_txns = 3;
  std::vector<Transaction *> txns;
  for (int i = 0; i < num_txns; i++) {
    txns.push_back(txn_mgr.Begin());
    EXPECT_EQ(i, txns[i]->GetTransactionId());
  }

  /** Each transaction takes an S lock on the same table and row and then unlocks */
  auto task = [&](int txn_id) {
    bool res;
    LOG_DEBUG("[RowLockTest1] lock the table, table id %d, txn id %d", oid, txn_id);
    res = lock_mgr.LockTable(txns[txn_id], LockManager::LockMode::SHARED, oid);
    LOG_DEBUG("[RowLockTest1] finish lock the table , table id %d, txn id %d", oid, txn_id);
    EXPECT_TRUE(res);
    CheckGrowing(txns[txn_id]);

    LOG_DEBUG("[RowLockTest1] lock table row, table id %d, txn id %d", oid, txn_id);
    res = lock_mgr.LockRow(txns[txn_id], LockManager::LockMode::SHARED, oid, rid);
    LOG_DEBUG("[RowLockTest1] finish lock table row, table id %d, txn id %d", oid, txn_id);
    EXPECT_TRUE(res);
    CheckGrowing(txns[txn_id]);
    /** Lock set should be updated */
    ASSERT_EQ(true, txns[txn_id]->IsRowSharedLocked(oid, rid));

    res = lock_mgr.UnlockRow(txns[txn_id], oid, rid);
    EXPECT_TRUE(res);
    CheckShrinking(txns[txn_id]);
    /** Lock set should be updated */
    ASSERT_EQ(false, txns[txn_id]->IsRowSharedLocked(oid, rid));

    res = lock_mgr.UnlockTable(txns[txn_id], oid);
    EXPECT_TRUE(res);
    CheckShrinking(txns[txn_id]);

    txn_mgr.Commit(txns[txn_id]);
    CheckCommitted(txns[txn_id]);
  };

  std::vector<std::thread> threads;
  threads.reserve(num_txns);

  for (int i = 0; i < num_txns; i++) {
    threads.emplace_back(std::thread{task, i});
  }

  for (int i = 0; i < num_txns; i++) {
    threads[i].join();
    delete txns[i];
  }
}
TEST(LockManagerTest, RowLockTest1) { RowLockTest1(); }  // NOLINT

void TwoPLTest1() {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  RID rid0{0, 0};
  RID rid1{0, 1};

  auto *txn = txn_mgr.Begin();
  EXPECT_EQ(0, txn->GetTransactionId());

  bool res;
  LOG_DEBUG("[TwoPLTest1] begin lock table, table id: %d", oid);
  res = lock_mgr.LockTable(txn, LockManager::LockMode::INTENTION_EXCLUSIVE, oid);
  LOG_DEBUG("[TwoPLTest1] finish lock table, tabel id: %d", oid);
  EXPECT_TRUE(res);

  LOG_DEBUG("[TwoPLTest1] begin lock row, table id: %d , rid: %s", oid, rid0.ToString().c_str());
  res = lock_mgr.LockRow(txn, LockManager::LockMode::SHARED, oid, rid0);
  LOG_DEBUG("[TwoPLTest1] finish lock row, table id %d, rid: %s", oid, rid0.ToString().c_str());
  EXPECT_TRUE(res);

  CheckGrowing(txn);
  CheckTxnRowLockSize(txn, oid, 1, 0);

  LOG_DEBUG("[TwoPLTest1] begin lock row, table id: %d , rid: %s", oid, rid1.ToString().c_str());
  res = lock_mgr.LockRow(txn, LockManager::LockMode::EXCLUSIVE, oid, rid1);
  LOG_DEBUG("[TwoPLTest1] finish lock row, table id %d, rid: %s", oid, rid1.ToString().c_str());
  EXPECT_TRUE(res);
  CheckGrowing(txn);
  CheckTxnRowLockSize(txn, oid, 1, 1);

  LOG_DEBUG("[TwoPLTest1] begin unlock row, table id: %d , rid: %s", oid, rid0.ToString().c_str());
  res = lock_mgr.UnlockRow(txn, oid, rid0);
  LOG_DEBUG("[TwoPLTest1] finish unlock row, table id: %d , rid: %s", oid, rid0.ToString().c_str());
  EXPECT_TRUE(res);
  CheckShrinking(txn);
  CheckTxnRowLockSize(txn, oid, 0, 1);

  try {
    LOG_DEBUG("[TwoPLTest1] begin re lock row, table id: %d , rid: %s", oid, rid1.ToString().c_str());
    lock_mgr.LockRow(txn, LockManager::LockMode::SHARED, oid, rid0);
    LOG_DEBUG("[TwoPLTest1] finish re lock row, table id %d, rid: %s", oid, rid0.ToString().c_str());
  } catch (TransactionAbortException &e) {
    CheckAborted(txn);
    CheckTxnRowLockSize(txn, oid, 0, 1);
  }

  // Need to call txn_mgr's abort
  LOG_DEBUG("[TwoPLTest1] begin abort");
  txn_mgr.Abort(txn);
  LOG_DEBUG("[TwoPLTest1] finish abort");
  CheckAborted(txn);
  CheckTxnRowLockSize(txn, oid, 0, 0);
  CheckTableLockSizes(txn, 0, 0, 0, 0, 0);

  delete txn;
}

TEST(LockManagerTest, TwoPLTest1) { TwoPLTest1(); }  // NOLINT

void AbortTest1() {
  fmt::print(stderr, "AbortTest1: multiple X should block\n");

  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};

  table_oid_t oid = 0;
  RID rid{0, 0};

  auto txn1 = txn_mgr.Begin();
  auto txn2 = txn_mgr.Begin();
  auto txn3 = txn_mgr.Begin();

  /** All takes IX lock on table */
  LOG_DEBUG("[AbortTest1] begin to lock the table %d, txn id %d", oid, txn1->GetTransactionId());
  EXPECT_EQ(true, lock_mgr.LockTable(txn1, LockManager::LockMode::INTENTION_EXCLUSIVE, oid));
  LOG_DEBUG("[AbortTest1] finish lock the table %d, txn id %d", oid, txn1->GetTransactionId());
  CheckTableLockSizes(txn1, 0, 0, 0, 1, 0);
  LOG_DEBUG("[AbortTest1] begin to lock the table %d, txn id %d", oid, txn2->GetTransactionId());
  EXPECT_EQ(true, lock_mgr.LockTable(txn2, LockManager::LockMode::INTENTION_EXCLUSIVE, oid));
  LOG_DEBUG("[AbortTest1] finish lock the table %d, txn id %d", oid, txn2->GetTransactionId());
  CheckTableLockSizes(txn2, 0, 0, 0, 1, 0);
  LOG_DEBUG("[AbortTest1] begin to lock the table %d, txn id %d", oid, txn3->GetTransactionId());
  EXPECT_EQ(true, lock_mgr.LockTable(txn3, LockManager::LockMode::INTENTION_EXCLUSIVE, oid));
  LOG_DEBUG("[AbortTest1] finish lock the table %d, txn id %d", oid, txn3->GetTransactionId());
  CheckTableLockSizes(txn3, 0, 0, 0, 1, 0);

  /** txn1 takes X lock on row */
  LOG_DEBUG("[AbortTest1] begin lock row, table id: %d , rid: %s", oid, rid.ToString().c_str());
  EXPECT_EQ(true, lock_mgr.LockRow(txn1, LockManager::LockMode::EXCLUSIVE, oid, rid));
  LOG_DEBUG("[AbortTest1] finish lock row, table id %d, rid: %s", oid, rid.ToString().c_str());
  CheckTxnRowLockSize(txn1, oid, 0, 1);

  /** txn2 attempts X lock on table but should be blocked */
  LOG_DEBUG("[AbortTest1] begin lock row, table id: %d , rid: %s", oid, rid.ToString().c_str());
  auto txn2_task = std::thread{[&]() { lock_mgr.LockRow(txn2, LockManager::LockMode::EXCLUSIVE, oid, rid); }};

  /** Sleep for a bit */
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  /** txn2 shouldn't have been granted the lock */
  CheckTxnRowLockSize(txn2, oid, 0, 0);

  /** txn3 attempts X lock on row but should be blocked */
  LOG_DEBUG("[AbortTest1] begin lock row, table id: %d , rid: %s", oid, rid.ToString().c_str());
  auto txn3_task = std::thread{[&]() { lock_mgr.LockRow(txn3, LockManager::LockMode::EXCLUSIVE, oid, rid); }};
  /** Sleep for a bit */
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  /** txn3 shouldn't have been granted the lock */
  CheckTxnRowLockSize(txn3, oid, 0, 0);

  /** Abort txn2 */
  LOG_DEBUG("[AbortTest1] begin to abort");
  txn_mgr.Abort(txn2);

  /** txn1 releases lock */
  EXPECT_EQ(true, lock_mgr.UnlockRow(txn1, oid, rid));
  CheckTxnRowLockSize(txn1, oid, 0, 0);

  txn2_task.join();
  LOG_DEBUG("[AbortTest1] join txn2 successfully");
  txn3_task.join();
  LOG_DEBUG("[AbortTest1] join txn3 successfully");
  /** txn2 shouldn't have any row locks */
  CheckTxnRowLockSize(txn2, oid, 0, 0);
  CheckTableLockSizes(txn2, 0, 0, 0, 0, 0);
  /** txn3 should have the row lock */
  CheckTxnRowLockSize(txn3, oid, 0, 1);

  delete txn1;
  delete txn2;
  delete txn3;
}

// FIXME：not pass
TEST(LockManagerTest, RowAbortTest1) { AbortTest1(); }  // NOLINT

TEST(LockManagerTest, UpgradeTest) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  auto *txn0 = txn_mgr.Begin();
  auto *txn1 = txn_mgr.Begin();
  auto *txn2 = txn_mgr.Begin();

  lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED, oid);
  lock_mgr.LockTable(txn0, LockManager::LockMode::EXCLUSIVE, oid);
  lock_mgr.UnlockTable(txn0, oid);
  txn_mgr.Commit(txn0);
  txn_mgr.Begin(txn0);
  CheckTableLockSizes(txn0, 0, 0, 0, 0, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // relock on a committed tx?
  //  std::thread t0([&]() {
  //    bool res;
  //    res = lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED, oid);
  //    EXPECT_TRUE(res);
  //    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  //    res = lock_mgr.LockTable(txn0, LockManager::LockMode::EXCLUSIVE, oid);
  //    EXPECT_TRUE(res);
  //    lock_mgr.UnlockTable(txn0, oid);
  //    txn_mgr.Commit(txn1);
  //  });

  std::thread t1([&]() {
    bool res;
    res = lock_mgr.LockTable(txn1, LockManager::LockMode::SHARED, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    res = lock_mgr.UnlockTable(txn1, oid);
    EXPECT_TRUE(res);
    CheckTableLockSizes(txn0, 0, 0, 0, 0, 0);
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);
    txn_mgr.Commit(txn1);
  });

  std::thread t2([&]() {
    bool res;
    res = lock_mgr.LockTable(txn2, LockManager::LockMode::SHARED, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    res = lock_mgr.UnlockTable(txn2, oid);
    EXPECT_TRUE(res);
    txn_mgr.Commit(txn2);
  });

  // t0.join();
  t1.join();
  t2.join();
  delete txn0;
  delete txn1;
  delete txn2;
}

TEST(LockManagerTest, UpgradeTest1) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  auto *txn0 = txn_mgr.Begin();
  auto *txn1 = txn_mgr.Begin();
  auto *txn2 = txn_mgr.Begin();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::thread t0([&]() {
    bool res;

    LOG_DEBUG("[UpgradeTest1] begin to lock the table %d, txn id %d", oid, txn0->GetTransactionId());
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED, oid);
    LOG_DEBUG("[UpgradeTest1] finish to lock the table %d, txn id %d", oid, txn0->GetTransactionId());
    EXPECT_TRUE(res);
    LOG_DEBUG("[UpgradeTest1] begin to upgrade the table %d, txn id %d", oid, txn0->GetTransactionId());
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE, oid);
    LOG_DEBUG("[UpgradeTest1] finish upgrade the table %d, txn id %d", oid, txn0->GetTransactionId());
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    LOG_DEBUG("[UpgradeTest1] begin to unlock the table %d, txn id %d", oid, txn0->GetTransactionId());
    lock_mgr.UnlockTable(txn0, oid);
    LOG_DEBUG("[UpgradeTest1] finish unlock the table %d, txn id %d", oid, txn0->GetTransactionId());
    txn_mgr.Commit(txn0);
  });

  std::thread t1([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    LOG_DEBUG("[UpgradeTest1] begin to lock the table %d, txn id %d, txn state: %d", oid, txn1->GetTransactionId(),
              txn1->GetState());
    res = lock_mgr.LockTable(txn1, LockManager::LockMode::SHARED, oid);
    LOG_DEBUG("[UpgradeTest1] finish to lock the table %d, txn id %d, txn state: %d", oid, txn1->GetTransactionId(),
              txn1->GetState());
    EXPECT_TRUE(res);
    LOG_DEBUG("[UpgradeTest1] begin to unlock the table %d, txn id %d", oid, txn1->GetTransactionId());
    res = lock_mgr.UnlockTable(txn1, oid);
    LOG_DEBUG("[UpgradeTest1] finish unlock the table %d, txn id %d", oid, txn1->GetTransactionId());
    EXPECT_TRUE(res);
    CheckTableLockSizes(txn0, 0, 0, 0, 0, 0);
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);
    txn_mgr.Commit(txn1);
  });

  std::thread t2([&]() {
    bool res;
    LOG_DEBUG("[UpgradeTest1] begin to lock the table %d, txn id %d", oid, txn2->GetTransactionId());
    res = lock_mgr.LockTable(txn2, LockManager::LockMode::SHARED, oid);
    LOG_DEBUG("[UpgradeTest1] finish lock the table %d, txn id %d", oid, txn2->GetTransactionId());
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    res = lock_mgr.UnlockTable(txn2, oid);
    EXPECT_TRUE(res);
    txn_mgr.Commit(txn2);
  });

  t0.join();
  t1.join();
  t2.join();
  delete txn0;
  delete txn1;
  delete txn2;
}

TEST(LockManagerTest, CompatibilityTest1) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;
  auto *txn0 = txn_mgr.Begin();
  auto *txn1 = txn_mgr.Begin();
  auto *txn2 = txn_mgr.Begin();
  // [S] SIX IS
  // [SIX IS]
  std::thread t0([&]() {
    bool res;
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);
    // CheckTableLockSizes(txn2, 0, 0, 0, 0, 0);
    lock_mgr.UnlockTable(txn0, oid);
    txn_mgr.Commit(txn0);
  });

  std::thread t1([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    res = lock_mgr.LockTable(txn1, LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CheckTableLockSizes(txn2, 0, 0, 1, 0, 0);
    res = lock_mgr.UnlockTable(txn1, oid);
    txn_mgr.Commit(txn1);
  });

  std::thread t2([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    res = lock_mgr.LockTable(txn2, LockManager::LockMode::INTENTION_SHARED, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    res = lock_mgr.UnlockTable(txn2, oid);
    txn_mgr.Commit(txn2);
  });

  t0.join();
  t1.join();
  t2.join();
  delete txn0;
  delete txn1;
  delete txn2;
}

TEST(LockManagerTest, CompatibilityTest2) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;
  auto *txn0 = txn_mgr.Begin();
  auto *txn1 = txn_mgr.Begin();
  auto *txn2 = txn_mgr.Begin();
  // [IS IX] SIX
  // [IS SIX]
  std::thread t0([&]() {
    bool res;
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::INTENTION_SHARED, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CheckTableLockSizes(txn0, 0, 0, 1, 0, 0);
    CheckTableLockSizes(txn1, 0, 0, 0, 1, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CheckTableLockSizes(txn0, 0, 0, 1, 0, 0);
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);
    CheckTableLockSizes(txn2, 0, 0, 0, 0, 1);
    lock_mgr.UnlockTable(txn0, oid);
    txn_mgr.Commit(txn0);
  });

  std::thread t1([&]() {
    bool res;
    res = lock_mgr.LockTable(txn1, LockManager::LockMode::INTENTION_EXCLUSIVE, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    res = lock_mgr.UnlockTable(txn1, oid);
    txn_mgr.Commit(txn1);
  });

  std::thread t2([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    res = lock_mgr.LockTable(txn2, LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE, oid);
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    res = lock_mgr.UnlockTable(txn2, oid);
    txn_mgr.Commit(txn2);
  });

  t0.join();
  t1.join();
  t2.join();

  delete txn0;
  delete txn1;
  delete txn2;
}

TEST(LockManagerTest, ReadUncommittedTest) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  auto *txn0 = txn_mgr.Begin(nullptr, IsolationLevel::READ_UNCOMMITTED);
  LOG_DEBUG("AttemptTableLockWhileGrowing: isolation_level=READ_UNCOMMITTED, lock_mode=SHARED");
  bool res = lock_mgr.LockTable(txn0, LockManager::LockMode::EXCLUSIVE, oid);
  EXPECT_TRUE(res);
  res = lock_mgr.UnlockTable(txn0, oid);
  EXPECT_TRUE(res);
  try {
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::EXCLUSIVE, oid);
  } catch (...) {
    LOG_INFO("pass AttemptTableLockWhileShrinking: isolation_level=READ_UNCOMMITTED, lock_mode=EXCLUSIVE");
  }
}

TEST(LockManagerTest, CompatibilityTest3) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  auto *txn0 = txn_mgr.Begin();
  auto *txn1 = txn_mgr.Begin();
  auto *txn2 = txn_mgr.Begin();
  // [SIX] SIX IS
  // [SIX] [IS]
  std::thread t0([&]() {
    bool res;
    LOG_DEBUG("[CompatibilityTest3] begin to lock the table %d, txn id %d", oid, txn0->GetTransactionId());
    res = lock_mgr.LockTable(txn0, LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE, oid);
    LOG_DEBUG("[CompatibilityTest3] finish lock the table %d, txn id %d", oid, txn0->GetTransactionId());
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 0);
    CheckTableLockSizes(txn2, 0, 0, 0, 0, 0);
    lock_mgr.UnlockTable(txn0, oid);
    txn_mgr.Commit(txn0);
  });

  std::thread t1([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    LOG_DEBUG("[CompatibilityTest3] begin to lock the table %d, txn id %d", oid, txn1->GetTransactionId());
    res = lock_mgr.LockTable(txn1, LockManager::LockMode::SHARED_INTENTION_EXCLUSIVE, oid);
    LOG_DEBUG("[CompatibilityTest3] finish lock the table %d, txn id %d", oid, txn1->GetTransactionId());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(res);
    CheckTableLockSizes(txn1, 0, 0, 0, 0, 1);
    CheckTableLockSizes(txn2, 0, 0, 1, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    res = lock_mgr.UnlockTable(txn1, oid);
    txn_mgr.Commit(txn1);
  });

  std::thread t2([&]() {
    bool res;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    LOG_DEBUG("[CompatibilityTest3] begin to lock the table %d, txn id %d", oid, txn2->GetTransactionId());
    res = lock_mgr.LockTable(txn2, LockManager::LockMode::INTENTION_SHARED, oid);
    LOG_DEBUG("[CompatibilityTest3] finish lock the table %d, txn id %d", oid, txn2->GetTransactionId());
    EXPECT_TRUE(res);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    res = lock_mgr.UnlockTable(txn2, oid);
    txn_mgr.Commit(txn2);
  });

  t0.join();
  t1.join();
  t2.join();
  delete txn0;
  delete txn1;
  delete txn2;
}

TEST(LockManagerRowTest, TableTest) {
  LockManager lock_mgr{};
  TransactionManager txn_mgr{&lock_mgr};
  table_oid_t oid = 0;

  int num_txns = 1;
  int num_rows = 10;
  std::vector<Transaction *> txns;
  for (int i = 0; i < num_txns; ++i) {
    txns.push_back(txn_mgr.Begin());
    EXPECT_EQ(i, txns[i]->GetTransactionId());
  }

  auto task = [&](txn_id_t txn_id) {
    bool res;
    res = lock_mgr.LockTable(txns[txn_id], LockManager::LockMode::INTENTION_SHARED, oid);
    for (int i = 0; i < num_rows; ++i) {
      RID rid{i, static_cast<uint32_t>(i)};
      res = lock_mgr.LockRow(txns[txn_id], LockManager::LockMode::SHARED, oid, rid);
      EXPECT_TRUE(res);
    }

    try {
      res = lock_mgr.UnlockTable(txns[txn_id], oid);
    } catch (TransactionAbortException &e) {
      CheckAborted(txns[txn_id]);
      CheckTxnRowLockSize(txns[txn_id], oid, 10, 0);
    }

    txn_mgr.Abort(txns[txn_id]);
    CheckAborted(txns[txn_id]);
    CheckTxnRowLockSize(txns[txn_id], oid, 0, 0);
    CheckTableLockSizes(txns[txn_id], 0, 0, 0, 0, 0);
  };

  std::vector<std::thread> threads;
  threads.reserve(num_txns);
  for (int i = 0; i < num_txns; ++i) {
    threads.emplace_back(std::thread{task, i});
  }

  for (int i = 0; i < num_txns; ++i) {
    threads[i].join();
    delete txns[i];
  }
}

// TEST(LockManagerTest, MixedTest) {
//   TEST_TIMEOUT_BEGIN
//   const int num = 10;
//   LockManager lock_mgr{};
//   TransactionManager txn_mgr{&lock_mgr};
//   std::stringstream result;
//   auto bustub = std::make_unique<bustub::BustubInstance>();
//   auto writer = bustub::SimpleStreamWriter(result, true, " ");
//
//   bustub->ExecuteSql("\\dt", writer);
//   auto schema = "CREATE TABLE test_1 (x int, y int);";
//   bustub->ExecuteSql(schema, writer);
//   std::string query = "INSERT INTO test_1 VALUES ";
//   for (size_t i = 0; i < num; i++) {
//     query += fmt::format("({}, {})", i, 0);
//     if (i != num - 1) {
//       query += ", ";
//     } else {
//       query += ";";
//     }
//   }
//   bustub->ExecuteSql(query, writer);
//   schema = "CREATE TABLE test_2 (x int, y int);";
//   bustub->ExecuteSql(schema, writer);
//   bustub->ExecuteSql(query, writer);
//
//   auto txn1 = bustub->txn_manager_->Begin();
//   auto txn2 = bustub->txn_manager_->Begin();
//
//   fmt::print("------\n");
//
//   query = "delete from test_1 where x = 100;";
//   bustub->ExecuteSqlTxn(query, writer, txn2);
//
//   query = "select * from test_1;";
//   bustub->ExecuteSqlTxn(query, writer, txn2);
//
//   query = "select * from test_1;";
//   bustub->ExecuteSqlTxn(query, writer, txn1);
//
//   bustub->txn_manager_->Commit(txn1);
//   fmt::print("txn1 commit\n");
//
//   bustub->txn_manager_->Commit(txn2);
//   fmt::print("txn2 commit\n");
//
//   delete txn1;
//   delete txn2;
//   TEST_TIMEOUT_FAIL_END(10000)
// }

}  // namespace bustub
