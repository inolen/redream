#include "core/list.h"
#include "retest.h"

struct person {
  const char *name;
  struct list_node it;
};

static struct person aaa = {"aaa", {0}};
static struct person bbb = {"bbb", {0}};
static struct person ccc = {"ccc", {0}};

static void init_people(struct list *people) {
  list_add(people, &aaa.it);
  list_add(people, &bbb.it);
  list_add(people, &ccc.it);
}

static void validate_people(struct list *people, struct person **expected,
                            int num_expected) {
  /* validate iterating forward */
  {
    int n = 0;

    list_for_each_entry(person, people, struct person, it) {
      struct person *expected_person = expected[n];
      CHECK_STREQ(person->name, expected_person->name);
      n++;
    }

    CHECK_EQ(n, num_expected);
  }

  /* validate iterating in reverse */
  {
    int n = 0;

    list_for_each_entry_reverse(person, people, struct person, it) {
      struct person *expected_person = expected[num_expected - n - 1];
      CHECK_STREQ(person->name, expected_person->name);
      n++;
    }

    CHECK_EQ(n, num_expected);
  }
}

/* add tests */
TEST(intrusive_list_append) {
  struct list people = {0};
  list_add(&people, &aaa.it);
  list_add(&people, &bbb.it);
  list_add(&people, &ccc.it);

  struct person *expected[] = {&aaa, &bbb, &ccc};
  int num_expected = ARRAY_SIZE(expected);
  validate_people(&people, expected, num_expected);
}

TEST(intrusive_list_prepend) {
  struct list people = {0};
  list_add_after(&people, NULL, &aaa.it);
  list_add_after(&people, NULL, &bbb.it);
  list_add_after(&people, NULL, &ccc.it);

  struct person *expected[] = {&ccc, &bbb, &aaa};
  int num_expected = ARRAY_SIZE(expected);
  validate_people(&people, expected, num_expected);
}

/* remove tests */
TEST(intrusive_list_remove_head) {
  struct list people = {0};
  init_people(&people);

  list_remove(&people, &aaa.it);

  struct person *expected[] = {&bbb, &ccc};
  int num_expected = ARRAY_SIZE(expected);
  validate_people(&people, expected, num_expected);
}

TEST(intrusive_list_remove_middle) {
  struct list people = {0};
  init_people(&people);

  list_remove(&people, &bbb.it);

  struct person *expected[] = {&aaa, &ccc};
  int num_expected = ARRAY_SIZE(expected);
  validate_people(&people, expected, num_expected);
}

TEST(intrusive_list_remove_tail) {
  struct list people = {0};
  init_people(&people);

  list_remove(&people, &ccc.it);

  struct person *expected[] = {&aaa, &bbb};
  int num_expected = ARRAY_SIZE(expected);
  validate_people(&people, expected, num_expected);
}

TEST(intrusive_list_remove_clear) {
  struct list people = {0};
  init_people(&people);

  CHECK(!list_empty(&people));

  list_for_each(&people, it) {
    list_remove(&people, it);
  }

  CHECK(list_empty(&people));
}
