#ifndef DISC_H
#define DISC_H

#include "sys/filesystem.h"

#define SECTOR_SIZE 2352

struct disc {
  void (*destroy)(struct disc *);
  int (*get_num_tracks)(struct disc *);
  struct track *(*get_track)(struct disc *, int);
  int (*read_sector)(struct disc *, int, void *);
};

struct track {
  int num;
  int fad;
  int adr;
  int ctrl;
};

struct disc;

struct disc *disc_create_gdi(const char *filename);
void disc_destroy(struct disc *disc);

int disc_get_num_tracks(struct disc *disc);
struct track *disc_get_track(struct disc *disc, int n);
int disc_read_sector(struct disc *disc, int fad, void *dst);

void disc_destroy(struct disc *disc);

#endif
