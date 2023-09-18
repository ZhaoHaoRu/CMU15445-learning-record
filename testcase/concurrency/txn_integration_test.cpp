#include <fmt/format.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>  // NOLINT
#include <cstdio>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>  //NOLINT
#include <utility>
#include <vector>

#include "common_checker.h"  // NOLINT

namespace bustub {

void CommitTest1() {
  // should scan changes of committed txn
  auto db = GetDbForCommitAbortTest("CommitTest1");
  auto txn1 = Begin(*db, IsolationLevel::READ_UNCOMMITTED);
  Insert(txn1, *db, 1);
  Commit(*db, txn1);
  auto txn2 = Begin(*db, IsolationLevel::READ_UNCOMMITTED);
  Scan(txn2, *db, {1, 233, 234});
  Commit(*db, txn2);
}

// NOLINTNEXTLINE
TEST(CommitAbortTest, CommitTestA) { CommitTest1(); }

void Test1(IsolationLevel lvl) {
  // should scan changes of committed txn
  auto db = GetDbForVisibilityTest("Test1");
  auto txn1 = Begin(*db, lvl);
  Delete(txn1, *db, 233);
  Commit(*db, txn1);
  auto txn2 = Begin(*db, lvl);
  Scan(txn2, *db, {234});
  Commit(*db, txn2);
}

// NOLINTNEXTLINE
TEST(VisibilityTest, TestA) {
  // only this one will be public :)
  Test1(IsolationLevel::READ_COMMITTED);
}

TEST(VisibilityTest, TestB) {
  auto db = GetDbForVisibilityTest("Test1");
  auto txn1 = Begin(*db, IsolationLevel::READ_COMMITTED);
  Delete(txn1, *db, 233);
  Commit(*db, txn1);
  auto txn2 = Begin(*db, IsolationLevel::READ_UNCOMMITTED);
  Scan(txn2, *db, {234});
  Commit(*db, txn2);
}

TEST(VisibilityTest, TestC) {
  auto db = GetDbForVisibilityTest("Test1");
  auto txn1 = Begin(*db, IsolationLevel::READ_COMMITTED);
  Delete(txn1, *db, 233);
  Commit(*db, txn1);
  auto txn2 = Begin(*db, IsolationLevel::READ_UNCOMMITTED);
  Scan(txn2, *db, {234});
  Commit(*db, txn2);
}

// NOLINTNEXTLINE
TEST(IsolationLevelTest, InsertTestA) {
  ExpectTwoTxn("InsertTestA.1", IsolationLevel::READ_UNCOMMITTED, IsolationLevel::READ_UNCOMMITTED, false, IS_INSERT,
               ExpectedOutcome::DirtyRead);
}

TEST(IsolationLevelTest, DeleteTestA) {
  ExpectTwoTxn("DeleteTestA.2", IsolationLevel::READ_UNCOMMITTED, IsolationLevel::READ_COMMITTED, false, IS_DELETE,
               ExpectedOutcome::DirtyRead);
  ExpectTwoTxn("DeleteTestA.3", IsolationLevel::READ_COMMITTED, IsolationLevel::READ_UNCOMMITTED, false, IS_DELETE,
               ExpectedOutcome::BlockOnRead);
  ExpectTwoTxn("DeleteTestA.4", IsolationLevel::READ_COMMITTED, IsolationLevel::READ_COMMITTED, false, IS_DELETE,
               ExpectedOutcome::BlockOnRead);
}

}  // namespace bustub
