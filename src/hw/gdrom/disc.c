#include "hw/gdrom/disc.h"
#include "core/assert.h"

static const int GDI_PREGAP_SIZE = 150;

int disc_get_num_tracks(struct disc *disc) {
  return disc->get_num_tracks(disc);
}

struct track *disc_get_track(struct disc *disc, int n) {
  return disc->get_track(disc, n);
}

int disc_read_sector(struct disc *disc, int fad, void *dst) {
  return disc->read_sector(disc, fad, dst);
}

void disc_destroy(struct disc *disc) {
  disc->destroy(disc);
}

