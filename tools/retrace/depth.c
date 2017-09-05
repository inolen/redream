#include <math.h>
#include <stdlib.h>
#include "core/assert.h"
#include "core/sort.h"
#include "file/trace.h"
#include "guest/pvr/tr.h"

struct depth_entry {
  /* vertex index */
  int n;

  /* depth buffer value */
  union {
    float f;
    uint32_t i;
  } d;
};

typedef void (*depth_cb)(float, float, float, struct depth_entry *);

struct test {
  const char *name;
  depth_cb depth;
  sort_cmp cmp;
  int match;
  int total;
};

static struct tr_texture *find_texture(void *userdata, union tsp tsp,
                                       union tcw tcw) {
  /* return a non-zero handle so it doesn't try to create a texture with
     the render backend (which is NULL) */
  static struct tr_texture tex;
  tex.handle = 1;
  return &tex;
}

static int depth_cmp(const void *a, const void *b) {
  const struct depth_entry *ea = (const struct depth_entry *)a;
  const struct depth_entry *eb = (const struct depth_entry *)b;
  return ea->d.i <= eb->d.i;
}

static int depth_cmpf(const void *a, const void *b) {
  const struct depth_entry *ea = (const struct depth_entry *)a;
  const struct depth_entry *eb = (const struct depth_entry *)b;
  return ea->d.f <= eb->d.f;
}

static void test_context(struct trace_cmd *cmd, struct test *tests,
                         int num_tests) {
  CHECK_EQ(cmd->type, TRACE_CMD_CONTEXT);

  struct ta_context *ctx = calloc(1, sizeof(struct ta_context));
  struct tr_context *rc = calloc(1, sizeof(struct tr_context));

  /* parse the context */
  trace_copy_context(cmd, ctx);
  tr_convert_context(NULL, NULL, &find_texture, ctx, rc);

  /* sort each vertex by the original w */
  struct depth_entry *original =
      calloc(rc->num_verts, sizeof(struct depth_entry));
  float minw = FLT_MAX;
  float maxw = -FLT_MAX;

  for (int i = 0; i < rc->num_verts; i++) {
    struct ta_vertex *vert = &rc->verts[i];
    struct depth_entry *entry = &original[i];
    entry->n = i;
    entry->d.f = 1.0f / vert->xyz[2];

    minw = MIN(minw, entry->d.f);
    maxw = MAX(maxw, entry->d.f);
  }

  msort(original, rc->num_verts, sizeof(struct depth_entry), &depth_cmpf);

  for (int i = 0; i < num_tests; i++) {
    struct test *test = &tests[i];

    /* calculate the depth for each vertex using the test's depth function */
    struct depth_entry *tmp = calloc(rc->num_verts, sizeof(struct depth_entry));

    for (int j = 0; j < rc->num_verts; j++) {
      struct ta_vertex *vert = &rc->verts[j];
      struct depth_entry *entry = &tmp[j];
      entry->n = j;
      test->depth(1.0f / vert->xyz[2], minw, maxw, entry);
    }

    /* sort the vertices based on the depth value */
    msort(tmp, rc->num_verts, sizeof(struct depth_entry), test->cmp);

    /* compare sorted results with original results */
    for (int j = 0; j < rc->num_verts; j++) {
      if (original[j].n == tmp[j].n) {
        test->match++;
      }
    }

    test->total += rc->num_verts;

    free(tmp);
  }

  free(original);
  free(rc);
  free(ctx);
}

static void test_flt(float w, float minw, float maxw,
                     struct depth_entry *entry) {
  entry->d.f = (w - minw) / (maxw - minw);
}

static void test_int(float w, float minw, float maxw,
                     struct depth_entry *entry) {
  entry->d.i = ((w - minw) / (maxw - minw)) * ((1 << 24) - 1);
}

static void test_log2(float w, float minw, float maxw,
                      struct depth_entry *entry) {
  entry->d.i = (log2(1.0f + w - minw) / log2(maxw - minw)) * ((1 << 24) - 1);
}

static void test_log2_fixed(float w, float minw, float maxw,
                            struct depth_entry *entry) {
  entry->d.i = (log2(1.0f + w) / 17.0f) * ((1 << 24) - 1);
}

int cmd_depth(int argc, const char **argv) {
  if (argc < 1) {
    return 0;
  }

  const char *filename = argv[0];
  struct trace *trace = trace_parse(filename);

  struct test tests[] = {
      {"32-bit float", &test_flt, &depth_cmpf, 0, 0},
      {"24-bit int", &test_int, &depth_cmp, 0, 0},
      {"24-bit int using log2", &test_log2, &depth_cmp, 0, 0},
      {"24-bit int using log2 w/ fixed max", &test_log2_fixed, &depth_cmp, 0,
       0},
  };
  int num_tests = ARRAY_SIZE(tests);

  /* check each context in the trace */
  struct trace_cmd *next = trace->cmds;
  while (next) {
    if (next->type == TRACE_CMD_CONTEXT) {
      test_context(next, tests, num_tests);
      break;
    }
    next = next->next;
  }

  trace_destroy(trace);

  /* print results */
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("depth test results");
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("");

  int max_name_length = 0;
  for (int i = 0; i < num_tests; i++) {
    struct test *test = &tests[i];
    int l = (int)strlen(test->name);
    max_name_length = MAX(max_name_length, l);
  }

  for (int i = 0; i < num_tests; i++) {
    struct test *test = &tests[i];
    LOG_INFO("%-*s  %f%%", max_name_length, test->name,
             ((float)test->match / test->total) * 100.0f);
  }

  return 1;
}
