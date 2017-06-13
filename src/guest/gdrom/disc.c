#include "guest/gdrom/disc.h"
#include "core/string.h"
#include "guest/gdrom/cdi.h"
#include "guest/gdrom/gdi.h"

struct track *disc_lookup_track(struct disc *disc, int fad) {
  int num_tracks = disc_get_num_tracks(disc);

  for (int i = 0; i < num_tracks; i++) {
    struct track *track = disc_get_track(disc, i);

    if (fad < track->fad) {
      continue;
    }

    if (i < num_tracks - 1) {
      struct track *next = disc_get_track(disc, i + 1);

      if (fad >= next->fad) {
        continue;
      }
    }

    return track;
  }

  return NULL;
}

int disc_read_sector(struct disc *disc, int fad, enum gd_secfmt fmt,
                     enum gd_secmask mask, void *dst) {
  return disc->read_sector(disc, fad, fmt, mask, dst);
}

struct track *disc_get_track(struct disc *disc, int n) {
  return disc->get_track(disc, n);
}

int disc_get_num_tracks(struct disc *disc) {
  return disc->get_num_tracks(disc);
}

struct session *disc_get_session(struct disc *disc, int n) {
  return disc->get_session(disc, n);
}

int disc_get_num_sessions(struct disc *disc) {
  return disc->get_num_sessions(disc);
}

void disc_destroy(struct disc *disc) {
  disc->destroy(disc);
}

int disc_get_format(struct disc *disc) {
  return disc->get_format(disc);
}

struct disc *disc_create(const char *filename) {
  if (strstr(filename, ".cdi")) {
    return cdi_create(filename);
  }

  if (strstr(filename, ".gdi")) {
    return gdi_create(filename);
  }

  return NULL;
}
