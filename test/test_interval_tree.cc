#include <math.h>
#include <set>
#include "gtest/gtest.h"
#include "core/core.h"
#define VERIFY_INTRUSIVE_TREE
#include "core/interval_tree.h"

using namespace dreavm;

typedef IntervalTree<void *> TestTree;
typedef TestTree::node_type TestNode;

class IntervalTreeTest : public ::testing::Test {
 public:
  IntervalTreeTest() {}

  static const int LOW = 0x0;
  static const int HIGH = 0x10000;
  static const int INTERVAL = 0x2000;

  void SetUp() {
    // insert dummy intervals
    for (int i = 0; i < 0x1000; i++) {
      uint32_t low = 0;
      uint32_t high = 0;

      while (high <= low) {
        low = LOW + (rand() % (HIGH - LOW));
        high = low + INTERVAL;
      }

      TestNode *n = intervals.Insert(low, high, nullptr);
      nodes.insert(n);
    }
  }

  TestTree intervals;
  std::set<TestNode *> nodes;
};

TEST_F(IntervalTreeTest, ValidateRelations) {
  // make sure all children parent pointers match
  for (auto n : nodes) {
    if (n->left) {
      ASSERT_EQ(n->left->parent, n);
    }
    if (n->right) {
      ASSERT_EQ(n->right->parent, n);
    }
  }
}

TEST_F(IntervalTreeTest, Size) {
  ASSERT_EQ(intervals.Size(), (int)nodes.size());
}

TEST_F(IntervalTreeTest, Height) {
  int height = intervals.Height();
  int size = intervals.Size();
  ASSERT_TRUE(height <= 2 * log2(size + 1));
}

TEST_F(IntervalTreeTest, Remove) {
  // remove all results and ensure size is updated in the process
  int size = nodes.size();

  for (auto n : nodes) {
    intervals.Remove(n);

    ASSERT_EQ(intervals.Size(), --size);
  }
}

TEST_F(IntervalTreeTest, Clear) {
  int original_size = intervals.Size();

  intervals.Clear();

  ASSERT_NE(original_size, intervals.Size());
  ASSERT_EQ(0, intervals.Size());
}

TEST_F(IntervalTreeTest, Find) {
  for (uint32_t i = 0; i < HIGH; i += 0x1000) {
    // manually generate a list of results
    std::set<TestNode *> expected;

    for (auto n : nodes) {
      if (i < n->low || i > n->high) {
        continue;
      }

      expected.insert(n);
    }

    // query the tree for nodes and compare with the expected results
    int found = 0;
    TestNode *n = intervals.Find(i, i);

    while (n) {
      // validate that it's in the expected set
      auto it = expected.find(n);
      ASSERT_NE(it, expected.end());
      found++;

      // remove from nodes so the node isn't expected by the next loop
      auto it2 = nodes.find(n);
      ASSERT_NE(it2, nodes.end());
      nodes.erase(it2);

      // remove from intervals so Find() can locate the next node
      intervals.Remove(n);

      // locate the next node
      n = intervals.Find(i, i);
    }

    // validate that the same number of nodes were matched
    ASSERT_EQ(found, (int)expected.size());
  }
}

TEST_F(IntervalTreeTest, Iterate) {
  for (uint32_t i = 0; i < HIGH; i += 0x1000) {
    // manually generate a list of expected nodes
    std::set<TestNode *> expected;

    for (auto n : nodes) {
      if (i < n->low || i > n->high) {
        continue;
      }

      expected.insert(n);
    }

    // query the tree for nodes
    std::set<TestNode *> results;

    intervals.Iterate(i, i, [&](const TestTree &tree, TestNode *node) {
      results.insert(node);
    });

    // compare the results
    ASSERT_EQ(expected.size(), results.size());

    for (auto n : results) {
      auto it = expected.find(n);
      ASSERT_NE(it, expected.end());
    }
  }
}
