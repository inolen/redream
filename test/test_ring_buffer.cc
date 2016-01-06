#include "gtest/gtest.h"
#include "core/core.h"
#include "core/ring_buffer.h"

using namespace dvm;

class RingTest : public ::testing::Test {
 public:
  RingTest() : items(2) {}

  RingBuffer<int> items;
};

// empty / full
TEST_F(RingTest, Size) {
  ASSERT_TRUE(items.Empty());
  items.PushBack(7);
  ASSERT_TRUE(!items.Empty() && !items.Full());
  items.PushBack(9);
  ASSERT_TRUE(items.Full());
  items.PopBack();
  ASSERT_TRUE(!items.Full());
  items.PopFront();
  ASSERT_TRUE(items.Empty());
}

// add tests
TEST_F(RingTest, PushBack) {
  // push the first two items and fill up the buffer
  items.PushBack(7);
  items.PushBack(9);

  ASSERT_EQ(7, items.front());
  ASSERT_EQ(7, *items.begin());
  ASSERT_EQ(9, items.back());
  ASSERT_EQ(9, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());

  // push two more to overwrite
  items.PushBack(10);
  items.PushBack(11);

  ASSERT_EQ(10, items.front());
  ASSERT_EQ(10, *items.begin());
  ASSERT_EQ(11, items.back());
  ASSERT_EQ(11, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());
}

TEST_F(RingTest, Insert) {
  items.PushBack(7);
  items.PushBack(9);

  // insert at beginning
  items.Insert(items.begin(), 3);
  ASSERT_EQ(3, items.front());
  ASSERT_EQ(3, *items.begin());
  ASSERT_EQ(7, items.back());
  ASSERT_EQ(7, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());

  // insert at end
  items.Insert(++items.begin(), 5);
  ASSERT_EQ(3, items.front());
  ASSERT_EQ(3, *items.begin());
  ASSERT_EQ(5, items.back());
  ASSERT_EQ(5, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());
}

// remove tests
TEST_F(RingTest, PopBack) {
  items.PushBack(7);
  items.PushBack(9);
  ASSERT_EQ(9, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());

  items.PopBack();
  ASSERT_EQ(7, items.front());
  ASSERT_EQ(7, *items.begin());
  ASSERT_EQ(7, items.back());
  ASSERT_EQ(7, *(--items.end()));
  ASSERT_EQ(1, (int)items.Size());
}

TEST_F(RingTest, PopFront) {
  items.PushBack(7);
  items.PushBack(9);
  ASSERT_EQ(9, *(--items.end()));
  ASSERT_EQ(2, (int)items.Size());

  items.PopFront();
  ASSERT_EQ(9, items.front());
  ASSERT_EQ(9, *items.begin());
  ASSERT_EQ(9, items.back());
  ASSERT_EQ(9, *(--items.end()));
  ASSERT_EQ(1, (int)items.Size());
}

TEST_F(RingTest, Clear) {
  items.PushBack(7);
  items.PushBack(9);
  ASSERT_EQ(2, (int)items.Size());

  items.Clear();
  ASSERT_EQ(0, (int)items.Size());
}

// iterator tests
TEST_F(RingTest, EmptyIterate) { ASSERT_EQ(items.begin(), items.end()); }

TEST_F(RingTest, ForwardIterator) {
  items.PushBack(7);
  items.PushBack(9);

  auto it = items.begin();
  ASSERT_EQ(7, *it);
  ASSERT_EQ(9, *(++it));
  ASSERT_EQ(items.end(), ++it);
}
