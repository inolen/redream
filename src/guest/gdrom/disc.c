#include "guest/gdrom/disc.h"
#include "core/core.h"
#include "guest/gdrom/cdi.h"
#include "guest/gdrom/chd.h"
#include "guest/gdrom/gdi.h"
#include "guest/gdrom/iso.h"

/* ip.bin layout */
#define IP_OFFSET_META 0x0000    /* meta information */
#define IP_OFFSET_TOC 0x0100     /* table of contents */
#define IP_OFFSET_LICENSE 0x0300 /* license screen code */
#define IP_OFFSET_AREAS 0x3700   /* area protection symbols */
#define IP_OFFSET_BOOT1 0x3800   /* bootstrap 1 */
#define IP_OFFSET_BOOT2 0x6000   /* bootstrap 2 */

/* meta information found in the ip.bin */
struct disc_meta {
  char hwareid[DISC_HWAREID_SIZE];
  char makerid[DISC_MAKERID_SIZE];
  char devinfo[DISC_DEVINFO_SIZE];
  char areasym[DISC_AREASYM_SIZE];
  char periphs[DISC_PERIPHS_SIZE];
  char prodnum[DISC_PRODNUM_SIZE];
  char prodver[DISC_PRODVER_SIZE];
  char reldate[DISC_RELDATE_SIZE];
  char bootnme[DISC_BOOTNME_SIZE];
  char company[DISC_COMPANY_SIZE];
  char prodnme[DISC_PRODNME_SIZE];
};

static void disc_get_meta(struct disc *disc, struct disc_meta *meta) {
  struct session *session = disc_get_session(disc, 1);

  uint8_t tmp[DISC_MAX_SECTOR_SIZE];
  disc_read_sectors(disc, session->leadin_fad, 1, GD_SECTOR_ANY, GD_MASK_DATA,
                    tmp, sizeof(tmp));

  memcpy(meta, tmp, sizeof(*meta));
}

static void disc_patch_sector(struct disc *disc, int fad, uint8_t *data) {
  /* patch discs to boot in all regions by patching data read from the disk. for
     a disc to be boot for a region, the region must be enabled in two places:

     1.) in the meta information section of the ip.bin
     2.) in the area protection symbols section of the ip.bin */
  if (fad == disc->meta_fad) {
    /* the area symbols in the meta information contains 8 characters, each of
       which is either a space, or the first letter of the area if supported */
    struct disc_meta *meta = (struct disc_meta *)data;
    strncpy_pad_spaces(meta->areasym, "JUE", sizeof(meta->areasym));
  } else if (fad == disc->area_fad) {
    /* the area protection symbols section contains 8 slots, each of which is
       either spaces, or the name of the area if supported. note, each slot
       has a 4-byte code prefix which jumps past it as part of the bootstrap
       control flow */
    char *slot0 = (char *)(data + disc->area_off);
    char *slot1 = (char *)(data + disc->area_off + 32);
    char *slot2 = (char *)(data + disc->area_off + 64);
    strncpy_pad_spaces(slot0 + 4, "For JAPAN,TAIWAN,PHILIPINES.", 28);
    strncpy_pad_spaces(slot1 + 4, "For USA and CANADA.", 28);
    strncpy_pad_spaces(slot2 + 4, "For EUROPE.", 28);
  }
}

int track_set_layout(struct track *track, int sector_mode, int sector_size) {
  track->sector_size = sector_size;

  if (sector_mode == 0 && sector_size == 2352) {
    track->sector_fmt = GD_SECTOR_CDDA;
    track->header_size = 0;
    track->error_size = 0;
    track->data_size = 2352;
  } else if (sector_mode == 1 && sector_size == 2048) {
    track->sector_fmt = GD_SECTOR_M1;
    track->header_size = 0;
    track->error_size = 0;
    track->data_size = 2048;
  } else if (sector_mode == 1 && sector_size == 2352) {
    track->sector_fmt = GD_SECTOR_M1;
    /* skip sync, header */
    track->header_size = 16;
    track->error_size = 288;
    track->data_size = 2048;
  } else if (sector_mode == 1 && sector_size == 2336) {
    track->sector_fmt = GD_SECTOR_M1;
    track->header_size = 0;
    track->error_size = 288;
    track->data_size = 2048;
  } else if (sector_mode == 2 && sector_size == 2048) {
    /* assume form1 */
    track->sector_fmt = GD_SECTOR_M2F1;
    track->header_size = 0;
    track->error_size = 0;
    track->data_size = 2048;
  } else if (sector_mode == 2 && sector_size == 2352) {
    /* assume form1 */
    track->sector_fmt = GD_SECTOR_M2F1;
    /* skip sync, header and subheader */
    track->header_size = 24;
    track->error_size = 280;
    track->data_size = 2048;
  } else if (sector_mode == 2 && sector_size == 2336) {
    /* assume form1 */
    track->sector_fmt = GD_SECTOR_M2F1;
    /* skip subheader */
    track->header_size = 8;
    track->error_size = 280;
    track->data_size = 2048;
  } else {
    return 0;
  }

  /* sanity check */
  CHECK_EQ(track->header_size + track->error_size + track->data_size,
           track->sector_size);

  return 1;
}

int disc_read_bytes(struct disc *disc, int fad, int len, uint8_t *dst,
                    int dst_size) {
  CHECK_LE(len, dst_size);

  uint8_t tmp[DISC_MAX_SECTOR_SIZE];
  int rem = len;

  while (rem) {
    int n = disc_read_sectors(disc, fad, 1, GD_SECTOR_ANY, GD_MASK_DATA, tmp,
                              sizeof(tmp));
    CHECK(n);

    /* don't overrun */
    n = MIN(n, rem);
    memcpy(dst, tmp, n);

    rem -= n;
    dst += n;
    fad++;
  }

  return len;
}

int disc_read_sectors(struct disc *disc, int fad, int num_sectors,
                      int sector_fmt, int sector_mask, uint8_t *dst,
                      int dst_size) {
  struct track *track = disc_lookup_track(disc, fad);
  CHECK_NOTNULL(track);
  CHECK(sector_fmt == GD_SECTOR_ANY || sector_fmt == track->sector_fmt);
  CHECK(sector_mask == GD_MASK_DATA);

  int read = 0;
  int endfad = fad + num_sectors;

  for (int i = fad; i < endfad; i++) {
    CHECK_LE(read + track->data_size, dst_size);
    disc->read_sector(disc, track, i, dst + read);

    disc_patch_sector(disc, i, dst + read);

    read += track->data_size;
  }

  return read;
}

int disc_find_file(struct disc *disc, const char *filename, int *fad,
                   int *len) {
  uint8_t tmp[0x10000];

  /* get the session for the main data track */
  struct session *session = disc_get_session(disc, 1);
  struct track *track = disc_get_track(disc, session->first_track);

  /* read primary volume descriptor */
  int read = disc_read_sectors(disc, track->fad + ISO_PVD_SECTOR, 1,
                               GD_SECTOR_ANY, GD_MASK_DATA, tmp, sizeof(tmp));
  if (!read) {
    return 0;
  }

  struct iso_pvd *pvd = (struct iso_pvd *)tmp;
  CHECK(pvd->type == 1);
  CHECK(memcmp(pvd->id, "CD001", 5) == 0);
  CHECK(pvd->version == 1);

  /* check root directory for the file
     FIXME recurse subdirectories */
  struct iso_dir *root = &pvd->root_directory_record;
  int root_len = root->size.le;
  int root_fad = GDROM_PREGAP + root->extent.le;
  read = disc_read_bytes(disc, root_fad, root_len, tmp, sizeof(tmp));
  if (!read) {
    return 0;
  }

  uint8_t *ptr = tmp;
  uint8_t *end = tmp + root_len;
  struct iso_dir *found = NULL;

  while (ptr < end) {
    struct iso_dir *dir = (struct iso_dir *)ptr;

    if (!dir->length) {
      /* no more entries */
      break;
    }

    const char *name = (const char *)(ptr + sizeof(*dir));
    if (memcmp(name, filename, strlen(filename)) == 0) {
      found = dir;
      break;
    }

    ptr += dir->length;
  }

  if (!found) {
    return 0;
  }

  *fad = GDROM_PREGAP + found->extent.le;
  *len = found->size.le;

  return 1;
}

void disc_get_toc(struct disc *disc, int area, struct track **first_track,
                  struct track **last_track, int *leadin_fad,
                  int *leadout_fad) {
  disc->get_toc(disc, area, first_track, last_track, leadin_fad, leadout_fad);
}

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

int disc_get_format(struct disc *disc) {
  return disc->get_format(disc);
}

void disc_destroy(struct disc *disc) {
  disc->destroy(disc);
}

struct disc *disc_create(const char *filename) {
  struct disc *disc = NULL;

  if (strstr(filename, ".cdi")) {
    disc = cdi_create(filename);
  } else if (strstr(filename, ".chd")) {
    disc = chd_create(filename);
  } else if (strstr(filename, ".gdi")) {
    disc = gdi_create(filename);
  }

  if (!disc) {
    return NULL;
  }

  /* cache off information about the IP.BIN file location for region patching */
  struct session *session = disc_get_session(disc, 1);
  struct track *first_track = disc_get_track(disc, session->first_track);
  disc->meta_fad = first_track->fad;
  disc->area_fad = first_track->fad + IP_OFFSET_AREAS / first_track->data_size;
  disc->area_off = IP_OFFSET_AREAS % first_track->data_size;

  /* extract meta information from the IP.BIN */
  struct disc_meta meta;
  disc_get_meta(disc, &meta);

  strncpy_trim_space(disc->prodnme, meta.prodnme, sizeof(meta.prodnme));
  strncpy_trim_space(disc->prodnum, meta.prodnum, sizeof(meta.prodnum));
  strncpy_trim_space(disc->prodver, meta.prodver, sizeof(meta.prodver));
  strncpy_trim_space(disc->discnum, meta.devinfo + 5, sizeof(meta.devinfo) - 5);
  strncpy_trim_space(disc->bootnme, meta.bootnme, sizeof(meta.bootnme));

  /* generate unique id for the disc */
  snprintf(disc->uid, sizeof(disc->uid), "%s %s %s %s", disc->prodnme,
           disc->prodnum, disc->prodver, disc->discnum);

  LOG_INFO("disc_create id=%s", disc->uid);

  return disc;
}
