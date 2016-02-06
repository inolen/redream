#include <gtest/gtest.h>
#include "core/arena.h"
#include "core/intrusive_list.h"

using namespace re;

struct Person : public IntrusiveListNode<Person> {
  Person(const char *name) : name(name) {}

  const char *name;
};

struct PersonComparator {
  bool operator()(const Person *a, const Person *b) const {
    // sort is descending order
    return strcmp(b->name, a->name) < 0;
  }
};

class IntrusiveTestEmptySet : public ::testing::Test {
 public:
  IntrusiveTestEmptySet() : arena(1024) {}

  Arena arena;
  IntrusiveList<Person> people;
};

class IntrusiveTestABCSet : public ::testing::Test {
 public:
  IntrusiveTestABCSet() : arena(1024) {
    aaa = new (arena.Alloc<Person>()) Person("aaa");
    people.Append(aaa);

    bbb = new (arena.Alloc<Person>()) Person("bbb");
    people.Append(bbb);

    ccc = new (arena.Alloc<Person>()) Person("ccc");
    people.Append(ccc);
  }

  Arena arena;
  IntrusiveList<Person> people;
  Person *aaa, *bbb, *ccc;
};

// add tests
TEST_F(IntrusiveTestEmptySet, AddSingle) {
  Person *aaa = new (arena.Alloc<Person>()) Person("aaa");
  people.Append(aaa);

  ASSERT_EQ(aaa, people.head());
  ASSERT_EQ(aaa, *people.begin());

  ASSERT_EQ(aaa, people.tail());
  ASSERT_EQ(aaa, *(--people.end()));
}

TEST_F(IntrusiveTestEmptySet, Prepend) {
  Person *aaa = new (arena.Alloc<Person>()) Person("aaa");
  people.Prepend(aaa);

  ASSERT_EQ(aaa, people.head());
  ASSERT_EQ(NULL, people.head()->prev());
  ASSERT_EQ(NULL, people.head()->next());
  ASSERT_EQ(aaa, *people.begin());

  Person *bbb = new (arena.Alloc<Person>()) Person("bbb");
  people.Prepend(bbb);

  ASSERT_EQ(aaa, people.head()->next());

  ASSERT_EQ(aaa, people.tail());
  ASSERT_EQ(bbb, people.tail()->prev());
  ASSERT_EQ(NULL, people.tail()->next());
  ASSERT_EQ(aaa, *(--people.end()));
}

TEST_F(IntrusiveTestEmptySet, Append) {
  Person *aaa = new (arena.Alloc<Person>()) Person("aaa");
  people.Append(aaa);

  ASSERT_EQ(aaa, people.head());
  ASSERT_EQ(NULL, people.head()->prev());
  ASSERT_EQ(NULL, people.head()->next());
  ASSERT_EQ(aaa, *people.begin());

  Person *bbb = new (arena.Alloc<Person>()) Person("bbb");
  people.Append(bbb);

  ASSERT_EQ(bbb, people.head()->next());

  ASSERT_EQ(bbb, people.tail());
  ASSERT_EQ(aaa, people.tail()->prev());
  ASSERT_EQ(NULL, people.tail()->next());
  ASSERT_EQ(bbb, *(--people.end()));
}

// remove tests
TEST_F(IntrusiveTestABCSet, RemoveHead) {
  people.Remove(aaa);

  ASSERT_EQ(bbb, people.head());
  ASSERT_EQ(NULL, people.head()->prev());
  ASSERT_EQ(ccc, people.head()->next());
  ASSERT_EQ(bbb, *people.begin());

  ASSERT_EQ(ccc, people.tail());
  ASSERT_EQ(bbb, people.tail()->prev());
  ASSERT_EQ(NULL, people.tail()->next());
  ASSERT_EQ(ccc, *(--people.end()));
}

TEST_F(IntrusiveTestABCSet, RemoveMiddle) {
  people.Remove(bbb);

  ASSERT_EQ(aaa, people.head());
  ASSERT_EQ(NULL, people.head()->prev());
  ASSERT_EQ(ccc, people.head()->next());
  ASSERT_EQ(aaa, *people.begin());

  ASSERT_EQ(ccc, people.tail());
  ASSERT_EQ(aaa, people.tail()->prev());
  ASSERT_EQ(NULL, people.tail()->next());
  ASSERT_EQ(ccc, *(--people.end()));
}

TEST_F(IntrusiveTestABCSet, RemoveTail) {
  people.Remove(ccc);

  ASSERT_EQ(aaa, people.head());
  ASSERT_EQ(NULL, people.head()->prev());
  ASSERT_EQ(bbb, people.head()->next());
  ASSERT_EQ(aaa, *people.begin());

  ASSERT_EQ(bbb, people.tail());
  ASSERT_EQ(aaa, people.tail()->prev());
  ASSERT_EQ(NULL, people.tail()->next());
  ASSERT_EQ(bbb, *(--people.end()));
}

TEST_F(IntrusiveTestABCSet, Clear) {
  people.Clear();

  ASSERT_EQ(NULL, people.head());
  ASSERT_EQ(NULL, people.tail());
}

// iterator tests
TEST_F(IntrusiveTestEmptySet, EmptyIterate) {
  ASSERT_EQ(people.begin(), people.end());
}

TEST_F(IntrusiveTestABCSet, ForwardIterator) {
  auto it = people.begin();
  ASSERT_EQ(aaa, *it);
  ASSERT_EQ(bbb, *(++it));
  ASSERT_EQ(ccc, *(++it));
  ASSERT_EQ(people.end(), ++it);
}

TEST_F(IntrusiveTestABCSet, ForwardIteratorReverse) {
  auto it = people.end();
  ASSERT_EQ(ccc, *(--it));
  ASSERT_EQ(bbb, *(--it));
  ASSERT_EQ(aaa, *(--it));
  ASSERT_EQ(people.begin(), it);
}

TEST_F(IntrusiveTestABCSet, ReverseIterator) {
  auto it = people.rbegin();
  ASSERT_EQ(ccc, *it);
  ASSERT_EQ(bbb, *(++it));
  ASSERT_EQ(aaa, *(++it));
  ASSERT_EQ(people.rend(), ++it);
}

TEST_F(IntrusiveTestABCSet, ReverseIteratorReverse) {
  auto it = people.rend();
  ASSERT_EQ(aaa, *(--it));
  ASSERT_EQ(bbb, *(--it));
  ASSERT_EQ(ccc, *(--it));
  ASSERT_EQ(people.rbegin(), it);
}

TEST_F(IntrusiveTestABCSet, ValidOnInsert) {
  auto it = people.begin();
  ASSERT_EQ(aaa, *it);

  Person *zzz = new (arena.Alloc<Person>()) Person("zzz");
  people.Prepend(zzz);
  ASSERT_EQ(aaa, *it);
}

TEST_F(IntrusiveTestABCSet, ValidOnRemove) {
  auto it = ++people.begin();
  ASSERT_EQ(bbb, *it);

  people.Remove(aaa);
  ASSERT_EQ(bbb, *it);
}

// sort tests
TEST_F(IntrusiveTestEmptySet, EmptySort) {
  people.Sort(PersonComparator());
  ASSERT_EQ(NULL, people.head());
  ASSERT_EQ(NULL, people.tail());
}

TEST_F(IntrusiveTestABCSet, Sort) {
  people.Sort(PersonComparator());
  auto it = people.begin();
  ASSERT_STREQ("ccc", (it++)->name);
  ASSERT_STREQ("bbb", (it++)->name);
  ASSERT_STREQ("aaa", (it++)->name);
}
