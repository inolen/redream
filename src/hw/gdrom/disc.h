#ifndef DISC_H
#define DISC_H

#include "sys/filesystem.h"

#ifdef __cplusplus
extern "C" {
#endif

static const int SECTOR_SIZE = 2352;

typedef struct track_s {
  int num;
  int fad;
  int adr;
  int ctrl;
  char filename[PATH_MAX];
  int file_offset;
  FILE *file;
} track_t;

struct disc_s;

struct disc_s *disc_create_gdi(const char *filename);
void disc_destroy(struct disc_s *disc);

int disc_num_tracks(struct disc_s *disc);
track_t *disc_get_track(struct disc_s *disc, int n);
int disc_read_sector(struct disc_s *disc, int fad, void *dst);

#ifdef __cplusplus
}
#endif

#endif
