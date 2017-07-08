#include "guest/gdrom/disc.h"
#include "core/assert.h"
#include "core/string.h"
#include "guest/gdrom/cdi.h"
#include "guest/gdrom/gdi.h"
#include "guest/gdrom/iso.h"
#include "guest/gdrom/chd.h"

/* meta information found in the ip.bin */
struct disc_meta {
  char hardware_id[16];
  char marker_id[16];
  char device_info[16];
  char area_symbols[8];
  char peripherals[8];
  char product_number[10];
  char product_version[6];
  char release_date[16];
  char bootname[16];
  char producer[16];
  char name[128];
};

static void disc_get_meta(struct disc *disc, struct disc_meta *meta) {
  struct session *session = disc_get_session(disc, 1);

  uint8_t tmp[DISC_MAX_SECTOR_SIZE];
  disc_read_sectors(disc, session->leadin_fad, 1, GD_SECTOR_ANY, GD_MASK_DATA,
                    tmp, sizeof(tmp));

  memcpy(meta, tmp, sizeof(*meta));
}

int disc_read_bytes(struct disc *disc, int fad, int len, void *dst,
                    int dst_size) {
  CHECK_LE(len, dst_size);

  uint8_t tmp[DISC_MAX_SECTOR_SIZE];
  int rem = len;

  while (rem) {
    int n = disc->read_sectors(disc, fad, 1, GD_SECTOR_ANY, GD_MASK_DATA, tmp,
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
                      int sector_fmt, int sector_mask, void *dst,
                      int dst_size) {
  return disc->read_sectors(disc, fad, num_sectors, sector_fmt, sector_mask,
                            dst, dst_size);
}

int disc_find_file(struct disc *disc, const char *filename, int *fad,
                   int *len) {
  uint8_t tmp[0x10000];

  /* get the session for the main data track */
  struct session *session = disc_get_session(disc, 1);
  struct track *data_track = disc_get_track(disc, session->first_track);

  /* read primary volume descriptor */
  int read = disc_read_sectors(disc, data_track->fad + ISO_PVD_SECTOR, 1,
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

  while (ptr < end) {
    struct iso_dir *dir = (struct iso_dir *)ptr;
    const char *name = (const char *)(ptr + sizeof(*dir));

    if (memcmp(name, filename, strlen(filename)) == 0) {
      break;
    }

    /* dir entries always begin on an even byte */
    ptr = (uint8_t *)name + dir->name_len;
    ptr = (uint8_t *)align_up((intptr_t)ptr, (intptr_t)2);
  }

  if (ptr == end) {
    return 0;
  }

  struct iso_dir *dir = (struct iso_dir *)ptr;
  *fad = GDROM_PREGAP + dir->extent.le;
  *len = dir->size.le;

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

void disc_destroy(struct disc *disc) {
  disc->destroy(disc);
}

int disc_get_format(struct disc *disc) {
  return disc->get_format(disc);
}

void disc_get_id(struct disc *disc, char *id, int size) {
  struct disc_meta meta;
  disc_get_meta(disc, &meta);

  char device_info[17];
  char product_number[11];
  char product_version[7];
  char name[129];
  strncpy_trim_spaces(device_info, meta.device_info, sizeof(meta.device_info));
  strncpy_trim_spaces(product_number, meta.product_number,
                      sizeof(meta.product_number));
  strncpy_trim_spaces(product_version, meta.product_version,
                      sizeof(meta.product_version));
  strncpy_trim_spaces(name, meta.name, sizeof(meta.name));

  snprintf(id, size, "%s %s %s %s", name, product_number, product_version,
           device_info);
}

struct disc *disc_create(const char *filename) {
  struct disc *disc = NULL;

  if (strstr(filename, ".cdi")) {
    disc = cdi_create(filename);
  } else if (strstr(filename, ".gdi")) {
    disc = gdi_create(filename);
  } else if (strstr(filename, ".chd")) {
    disc = chd_create(filename);
  }

  if (disc) {
    char id[DISC_MAX_ID_SIZE];
    disc_get_id(disc, id, sizeof(id));
    LOG_INFO("disc_create %s", id);
  }

  return disc;
}
