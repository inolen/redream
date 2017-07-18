#ifndef DISC_H
#define DISC_H

#include "core/filesystem.h"
#include "guest/gdrom/gdrom_types.h"

#define DISC_MAX_SECTOR_SIZE 2352
#define DISC_MAX_SESSIONS 2
#define DISC_MAX_TRACKS 64

struct track {
  int num;
  int fad;
  int adr;
  int ctrl;
  int sector_fmt;
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

/* meta information found in the ip.bin */
struct disc_meta {
  char hardware_id[16];
  char marker_id[16];
  char device_info[16];
  char area_symbols[8];
  char peripherals[8];
  char id[10];
  char version[6];
  char release_date[16];
  char bootname[16];
  char producer[16];
  char name[128];
};

struct disc {
  void (*destroy)(struct disc *);

  int (*get_format)(struct disc *);

  int (*get_num_sessions)(struct disc *);
  struct session *(*get_session)(struct disc *, int);

  int (*get_num_tracks)(struct disc *);
  struct track *(*get_track)(struct disc *, int);

  void (*get_toc)(struct disc *, int, struct track **, struct track **, int *,
                  int *);
  int (*read_sectors)(struct disc *, int, int, int, int, void *, int);
};

struct disc *disc_create(const char *filename);
void disc_destroy(struct disc *disc);

void disc_get_meta(struct disc *disc, struct disc_meta *meta);
int disc_get_format(struct disc *disc);
int disc_get_num_sessions(struct disc *disc);
struct session *disc_get_session(struct disc *disc, int n);
int disc_get_num_tracks(struct disc *disc);
struct track *disc_get_track(struct disc *disc, int n);
void disc_get_toc(struct disc *disc, int area, struct track **first_track,
                  struct track **last_track, int *leadin_fad, int *leadout_fad);
int disc_read_sectors(struct disc *disc, int fad, int num_sectors,
                      int sector_fmt, int sector_mask, void *dst, int dst_size);
struct track *disc_lookup_track(struct disc *disc, int fad);

#endif
