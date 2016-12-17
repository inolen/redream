#include <math.h>
#include "retest.h"
#define VERIFY_INTRUSIVE_TREE
#include "core/interval_tree.h"

#define LOW 0x0
#define HIGH 0x10000
#define INTERVAL 0x2000
#define MAX_NODES 0x1000
static struct interval_node nodes[MAX_NODES];

static void init_interval_tree(struct rb_tree *t) {
  for (int i = 0; i < MAX_NODES; i++) {
    uint32_t low = 0;
    uint32_t high = 0;

    while (high <= low) {
      low = LOW + (rand() % (HIGH - LOW));
      high = low + INTERVAL;
    }

    struct interval_node *n = &nodes[i];
    n->low = low;
    n->high = high;
    interval_tree_insert(t, n);
  }
}

TEST(interval_tree_size) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  int size = interval_tree_size(&tree);
  CHECK_EQ(size, MAX_NODES);
}

TEST(interval_tree_height) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  int height = interval_tree_height(&tree);
  int size = interval_tree_size(&tree);
  CHECK(height <= 2 * log2(size + 1));
}

TEST(interval_tree_remove) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  /* remove all nodes and ensure size is updated in the process */
  int expected_size = interval_tree_size(&tree);
  int current_size = 0;

  struct interval_tree_it it;
  struct interval_node *n = interval_tree_iter_first(&tree, LOW, HIGH, &it);

  while (n) {
    struct interval_node *next = interval_tree_iter_next(&it);

    interval_tree_remove(&tree, n);

    current_size = interval_tree_size(&tree);
    CHECK_EQ(current_size, --expected_size);

    n = next;
  }
}

TEST(interval_tree_clear) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  interval_tree_clear(&tree);

  int size = interval_tree_size(&tree);
  CHECK_EQ(size, 0);
}

TEST(interval_tree_find) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  for (uint32_t i = 0; i < HIGH; i += 0x1000) {
    /* manually generate a list of expected nodes */
    static struct interval_node *expected[MAX_NODES];
    int num_expected = 0;

    rb_for_each(rb, &tree) {
      struct interval_node *n = INTERVAL_NODE(rb);

      if (i < n->low || i > n->high) {
        continue;
      }

      expected[num_expected++] = n;
    }

    /* query the tree for nodes */
    struct interval_tree_it it;
    struct interval_node *n = interval_tree_iter_first(&tree, i, i, &it);

    int found = 0;

    while (n) {
      /* validate that it's in the expected set */
      for (int j = 0; j < num_expected; j++) {
        if (expected[j] == n) {
          found++;
          break;
        }
      }

      n = interval_tree_iter_next(&it);
    }

    CHECK_EQ(found, num_expected);
  }
}

TEST(interval_tree_find_destructive) {
  struct rb_tree tree = {0};
  init_interval_tree(&tree);

  for (uint32_t i = 0; i < HIGH; i += 0x1000) {
    /* manually generate a list of results */
    static struct interval_node *expected[MAX_NODES];
    int num_expected = 0;

    rb_for_each(rb, &tree) {
      struct interval_node *n = INTERVAL_NODE(rb);

      if (i < n->low || i > n->high) {
        continue;
      }

      expected[num_expected++] = n;
    }

    /* query the tree for nodes and compare with the expected results */
    int found = 0;

    while (1) {
      struct interval_node *root = INTERVAL_NODE(tree.root);
      struct interval_node *n = interval_tree_min_interval(root, i, i);

      if (!n) {
        break;
      }

      /* validate that it's in the expected set */
      for (int j = 0; j < num_expected; j++) {
        if (expected[j] == n) {
          found++;
          break;
        }
      }

      /* delete the current interval to move onto the next */
      interval_tree_remove(&tree, n);
    }

    /* validate that the same number of nodes were matched */
    CHECK_EQ(found, num_expected);
  }
}
