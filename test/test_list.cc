#include <gtest/gtest.h>

extern "C" {
#include "core/list.h"
}

typedef struct {
  list_node_t it;
  const char *name;
} person_t;

// struct PersonComparator {
//   bool operator()(const Person *a, const Person *b) const {
//     // sort is descending order
//     return strcmp(b->name, a->name) < 0;
//   }
// };

class IntrusiveTestEmptySet : public ::testing::Test {
 public:
  IntrusiveTestEmptySet() {}

  list_t people_list = {};
};

class IntrusiveTestABCSet : public ::testing::Test {
 public:
  IntrusiveTestABCSet() {
    list_add(&people_list, &aaa.it);
    list_add(&people_list, &bbb.it);
    list_add(&people_list, &ccc.it);
  }

  list_t people_list = {};
  person_t aaa = {{}, "aaa"};
  person_t bbb = {{}, "bbb"};
  person_t ccc = {{}, "ccc"};
};

static void validate_people(list_t *people_list, person_t **expected_people,
                            int num_expected_people) {
  int n = 0;

  list_for_each_entry(person, people_list, person_t, it) {
    person_t *expected_person = expected_people[n];
    ASSERT_STREQ(person->name, expected_person->name);
    n++;
  }

  ASSERT_EQ(n, num_expected_people);
}

static void validate_people_reverse(list_t *people_list,
                                    person_t **expected_people,
                                    int num_expected_people) {
  int n = 0;

  list_for_each_entry_reverse(person, people_list, person_t, it) {
    person_t *expected_person = expected_people[num_expected_people - n - 1];
    ASSERT_STREQ(person->name, expected_person->name);
    n++;
  }

  ASSERT_EQ(n, num_expected_people);
}

// add tests
TEST_F(IntrusiveTestEmptySet, Append) {
  person_t aaa = {{}, "aaa"};
  person_t bbb = {{}, "bbb"};
  person_t ccc = {{}, "ccc"};

  person_t *people[] = {&aaa, &bbb, &ccc};
  int num_people = array_size(people);

  for (int i = 0; i < num_people; i++) {
    person_t *expected_person = people[i];
    list_add(&people_list, &expected_person->it);
  }

  person_t *expected_people[] = {&aaa, &bbb, &ccc};
  int num_expected_people = array_size(expected_people);

  validate_people(&people_list, expected_people, num_expected_people);
  validate_people_reverse(&people_list, expected_people, num_expected_people);
}

TEST_F(IntrusiveTestEmptySet, Prepend) {
  person_t aaa = {{}, "aaa"};
  person_t bbb = {{}, "bbb"};
  person_t ccc = {{}, "ccc"};

  person_t *people[] = {&aaa, &bbb, &ccc};
  int num_people = array_size(people);

  for (int i = 0; i < num_people; i++) {
    person_t *expected_person = people[i];
    list_add_after(&people_list, NULL, &expected_person->it);
  }

  person_t *expected_people[] = {&ccc, &bbb, &aaa};
  int num_expected_people = array_size(expected_people);

  validate_people(&people_list, expected_people, num_expected_people);
  validate_people_reverse(&people_list, expected_people, num_expected_people);
}

// remove tests
TEST_F(IntrusiveTestABCSet, RemoveHead) {
  list_remove(&people_list, &aaa.it);

  person_t *expected_people[] = {&bbb, &ccc};
  int num_expected_people = array_size(expected_people);

  validate_people(&people_list, expected_people, num_expected_people);
  validate_people_reverse(&people_list, expected_people, num_expected_people);
}

TEST_F(IntrusiveTestABCSet, RemoveMiddle) {
  list_remove(&people_list, &bbb.it);

  person_t *expected_people[] = {&aaa, &ccc};
  int num_expected_people = array_size(expected_people);

  validate_people(&people_list, expected_people, num_expected_people);
  validate_people_reverse(&people_list, expected_people, num_expected_people);
}

TEST_F(IntrusiveTestABCSet, RemoveTail) {
  list_remove(&people_list, &ccc.it);

  person_t *expected_people[] = {&aaa, &bbb};
  int num_expected_people = array_size(expected_people);

  validate_people(&people_list, expected_people, num_expected_people);
  validate_people_reverse(&people_list, expected_people, num_expected_people);
}

TEST_F(IntrusiveTestABCSet, Clear) {
  ASSERT_FALSE(list_empty(&people_list));

  list_for_each(&people_list, it) {
    list_remove(&people_list, it);
  }

  ASSERT_TRUE(list_empty(&people_list));
}

// // sort tests
// TEST_F(IntrusiveTestEmptySet, EmptySort) {
//   people.Sort(PersonComparator());
//   ASSERT_EQ(NULL, people.head());
//   ASSERT_EQ(NULL, people.tail());
// }

// TEST_F(IntrusiveTestABCSet, Sort) {
//   people.Sort(PersonComparator());
//   auto it = people.begin();
//   ASSERT_STREQ("ccc", (it++)->name);
//   ASSERT_STREQ("bbb", (it++)->name);
//   ASSERT_STREQ("aaa", (it++)->name);
// }
