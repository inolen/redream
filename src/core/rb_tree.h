#ifndef RB_TREE_H
#define RB_TREE_H

#ifdef __cplusplus
extern "C" {
#endif

#define RB_NODE(n) ((rb_node_t *)n)

typedef enum {
  RB_RED,
  RB_BLACK,
} rb_color_t;

typedef struct rbnode_s {
  struct rbnode_s *parent;
  struct rbnode_s *left;
  struct rbnode_s *right;
  rb_color_t color;
} rb_node_t;

typedef struct { rb_node_t *root; } rb_tree_t;

typedef int (*rb_cmp_cb)(const rb_node_t *, const rb_node_t *);
typedef void (*rb_augment_propagate_cb)(rb_tree_t *, rb_node_t *);
typedef void (*rb_augment_rotate_cb)(rb_tree_t *, rb_node_t *, rb_node_t *);

typedef struct {
  rb_cmp_cb cmp;
  rb_augment_propagate_cb propagate;
  rb_augment_rotate_cb rotate;
} rb_callback_t;

void rb_link(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);
void rb_unlink(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);
void rb_insert(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);

rb_node_t *rb_find(rb_tree_t *t, const rb_node_t *search, rb_callback_t *cb);
rb_node_t *rb_upper_bound(rb_tree_t *t, const rb_node_t *search,
                          rb_callback_t *cb);

rb_node_t *rb_first(rb_tree_t *t);
rb_node_t *rb_last(rb_tree_t *t);
rb_node_t *rb_prev(rb_node_t *n);
rb_node_t *rb_next(rb_node_t *n);

#define rb_entry(n, type, member) container_of(n, type, member)

#ifdef __cplusplus

#define rb_find_entry(t, search, member, cb)                            \
  ({                                                                    \
    rb_node_t *it = rb_find(t, &(search)->member, cb);                  \
    it ? rb_entry(it, std::remove_reference<decltype(*(search))>::type, \
                  member)                                               \
       : NULL;                                                          \
  })

#else

#define rb_find_entry(t, search, member, cb)               \
  ({                                                       \
    rb_node_t *it = rb_find(t, &(search)->member, cb);     \
    it ? rb_entry(it, __typeof__(*search), member) : NULL; \
  })

#endif

// #define rb_for_each_entry(t, member, it)                                             \
//   for (rb_node_t *it = rb_first(t), *it##_next = rb_next(it); it; \
//        it = it##_next, it##_next = rb_next(it))

// rb_node_t *it = rb_first(&ta->live_entries);

//   while (it) {
//     rb_node_t *next = rb_next(it);

//     texture_entry_t *entry = rb_entry(it, texture_entry_t, live_it);

#ifdef __cplusplus
}
#endif

#endif
