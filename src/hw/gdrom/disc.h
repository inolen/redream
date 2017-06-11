#ifndef DISC_H
#define DISC_H

#include "core/filesystem.h"
#include "hw/gdrom/gdrom_types.h"

#define DISC_MAX_SECTOR_SIZE 2352
#define DISC_MAX_SESSIONS 2
#define DISC_MAX_TRACKS 64

struct track {
  int num;
  int fad;
  int adr;
  int ctrl;
  int sector_size;
  char filename[PATH_MAX];
  int file_offset;
};

struct session {
  int leadin_fad;
  int leadout_fad;
  int first_track;
  int last_track;
};

struct disc {
  void (*destroy)(struct disc *);
  int (*get_format)(struct disc *);
  int (*get_num_sessions)(struct disc *);
  struct session *(*get_session)(struct disc *, int);
  int (*get_num_tracks)(struct disc *);
  struct track *(*get_track)(struct disc *, int);
  int (*read_sector)(struct disc *, int, enum gd_secfmt, enum gd_secmask,
                     void *);
};

struct disc *disc_create(const char *filename);
void disc_destroy(struct disc *disc);

int disc_get_format(struct disc *disc);
int disc_get_num_sessions(struct disc *disc);
struct session *disc_get_session(struct disc *disc, int n);
int disc_get_num_tracks(struct disc *disc);
struct track *disc_get_track(struct disc *disc, int n);
int disc_read_sector(struct disc *disc, int fad, enum gd_secfmt fmt,
                     enum gd_secmask mask, void *dst);
struct track *disc_lookup_track(struct disc *disc, int fad);

#endif
