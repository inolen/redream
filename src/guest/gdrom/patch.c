#include "guest/gdrom/patch.h"
#include "core/assert.h"
#include "core/string.h"
#include "guest/memory.h"
#include "render/imgui.h"

DEFINE_PERSISTENT_OPTION_INT(patch_widescreen, 1, "Apply widescreen patches");

#if 0
#define LOG_PATCH LOG_INFO
#else
#define LOG_PATCH(...)
#endif

#define DATA(...) \
  (uint8_t[]) {   \
    __VA_ARGS__   \
  }

#define HUNKS(...)        \
  (struct patch_hunk[]) { \
    __VA_ARGS__           \
  }

#define NUM_HUNKS(...) (sizeof(HUNKS(__VA_ARGS__)) / sizeof(struct patch_hunk))

#define HUNK(offset, ...) \
  { offset, DATA(__VA_ARGS__), sizeof(DATA(__VA_ARGS__)) }

#define PATCH(game, desc, flags, ...) \
  {game, desc, flags, HUNKS(__VA_ARGS__), NUM_HUNKS(__VA_ARGS__)},

static struct patch patches[] = {
#include "guest/gdrom/patch.inc"
};
static int num_patches = sizeof(patches) / sizeof(patches[0]);

void patch_bootfile(const char *game, uint8_t *buffer, int offset, int size) {
  for (int i = 0; i < num_patches; i++) {
    struct patch *patch = &patches[i];

    /* only apply patches for the current game */
    if (strcmp(patch->game, game)) {
      continue;
    }

    if (!(patch->flags & PATCH_BOOTFILE)) {
      continue;
    }

    if ((patch->flags & PATCH_WIDESCREEN) && !OPTION_patch_widescreen) {
      continue;
    }

    LOG_PATCH("patches_apply %s at 0x%x", patch->desc, offset);

    for (int j = 0; j < patch->num_hunks; j++) {
      struct patch_hunk *hunk = &patch->hunks[j];

      for (int k = 0; k < hunk->len; k++) {
        int index = hunk->offset + k;

        if (index < offset || index >= offset + size) {
          continue;
        }

        buffer[index - offset] = hunk->data[k];
      }
    }
  }
}

#ifdef HAVE_IMGUI
void patch_debug_menu() {
  if (igBeginMenu("patches", 1)) {
    if (igMenuItem("widescreen", NULL, OPTION_patch_widescreen, 1)) {
      OPTION_patch_widescreen = !OPTION_patch_widescreen;
    }

    igEndMenu();
  }
}
#endif
