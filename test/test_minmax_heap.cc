#include <gtest/gtest.h>
#include "core/minmax_heap.h"

TEST(MinMaxHeap, PopEmpty) {
  std::vector<int> elements;

  // shouldn't do anything, just sanity checking that it doesn't crash
  dvm::mmheap_pop_min(elements.begin(), elements.end());
  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));

  dvm::mmheap_pop_max(elements.begin(), elements.end());
  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
}

TEST(MinMaxHeap, PopMinRoot) {
  std::vector<int> elements = {1};

  dvm::mmheap_pop_min(elements.begin(), elements.end());
  ASSERT_EQ(elements.back(), 1);
  elements.pop_back();

  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
}

TEST(MinMaxHeap, PopMaxRoot) {
  std::vector<int> elements = {1};

  dvm::mmheap_pop_min(elements.begin(), elements.end());
  ASSERT_EQ(elements.back(), 1);
  elements.pop_back();

  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
}

TEST(MinMaxHeap, PopMax1) {
  std::vector<int> elements = {1};

  dvm::mmheap_pop_max(elements.begin(), elements.end());
  ASSERT_EQ(elements.back(), 1);
  elements.pop_back();

  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
}

TEST(MinMaxHeap, PopMax2) {
  std::vector<int> elements = {1, 2};

  dvm::mmheap_pop_max(elements.begin(), elements.end());
  ASSERT_EQ(elements.back(), 2);
  elements.pop_back();

  ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
}

TEST(MinMaxHeap, PopMax3) {
  {
    std::vector<int> elements = {1, 2, 3};

    dvm::mmheap_pop_max(elements.begin(), elements.end());
    ASSERT_EQ(elements.back(), 3);
    elements.pop_back();

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }

  {
    std::vector<int> elements = {1, 3, 2};

    dvm::mmheap_pop_max(elements.begin(), elements.end());
    ASSERT_EQ(elements.back(), 3);
    elements.pop_back();

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }
}

TEST(MinMaxHeap, PushPopMinN) {
  static const int N = 1337;

  std::vector<int> elements = {};

  for (int i = 0; i < N; i++) {
    elements.push_back(i);
    dvm::mmheap_push(elements.begin(), elements.end());

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }

  for (int i = 0; i < N; i++) {
    dvm::mmheap_pop_min(elements.begin(), elements.end());
    ASSERT_EQ(elements.back(), i);
    elements.pop_back();

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }
}

TEST(MinMaxHeap, PushPopMaxN) {
  static const int N = 1337;

  std::vector<int> elements = {};

  for (int i = 0; i < N; i++) {
    elements.push_back(i);
    dvm::mmheap_push(elements.begin(), elements.end());

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }

  for (int i = N - 1; i >= 0; i--) {
    dvm::mmheap_pop_max(elements.begin(), elements.end());
    ASSERT_EQ(elements.back(), i);
    elements.pop_back();

    ASSERT_TRUE(dvm::mmheap_validate(elements.begin(), elements.end()));
  }
}
