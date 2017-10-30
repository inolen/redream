#ifndef DISC_H
#define DISC_H

#include "core/filesystem.h"
#include "guest/gdrom/gdrom_types.h"

#define DISC_MAX_SECTOR_SIZE 2352
#define DISC_MAX_SESSIONS 2
#define DISC_MAX_TRACKS 128
#define DISC_UID_SIZE 256

enum {
  DISC_REGION_JAPAN = 0x1,
  DISC_REGION_USA = 0x2,
  DISC_REGION_EUROPE = 0x4,
  DISC_REGION_ALL = 0x7,
};

struct track {
  int num;
  /* frame adddress, equal to lba + 150 */
  int fad;
  /* type of information encoded in the sub q channel */
  int adr;
  /* type of track */
  int ctrl;
  /* sector info */
  int sector_fmt;
  int sector_size;
  int header_size;
  int error_size;
  int data_size;
  /* backing file */
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
  /* information about the IP.BIN location on disc, cached to quickly patch
     region information */
  int meta_fad;
  int area_fad;
  int area_off;

  /* meta information extracted from IP.BIN */
  char uid[DISC_UID_SIZE];
  char product_name[129];
  char product_number[11];
  char product_version[7];
  char media_config[12];
  char bootname[17];

  /* media-specific interface */
  void (*destroy)(struct disc *);

  int (*get_format)(struct disc *);

  int (*get_num_sessions)(struct disc *);
  struct session *(*get_session)(struct disc *, int);

  int (*get_num_tracks)(struct disc *);
  struct track *(*get_track)(struct disc *, int);

  void (*get_toc)(struct disc *, int, struct track **, struct track **, int *,
                  int *);
  void (*read_sector)(struct disc *, struct track *, int, void *);
};

struct disc *disc_create(const char *filename);
void disc_destroy(struct disc *disc);

int disc_get_format(struct disc *disc);
int disc_get_num_sessions(struct disc *disc);
struct session *disc_get_session(struct disc *disc, int n);
int disc_get_num_tracks(struct disc *disc);
struct track *disc_get_track(struct disc *disc, int n);
struct track *disc_lookup_track(struct disc *disc, int fad);
void disc_get_toc(struct disc *disc, int area, struct track **first_track,
                  struct track **last_track, int *leadin_fad, int *leadout_fad);

int disc_find_file(struct disc *disc, const char *filename, int *fad, int *len);
int disc_read_sectors(struct disc *disc, int fad, int num_sectors,
                      int sector_fmt, int sector_mask, uint8_t *dst,
                      int dst_size);
int disc_read_bytes(struct disc *disc, int fad, int len, uint8_t *dst,
                    int dst_size);

#endif
