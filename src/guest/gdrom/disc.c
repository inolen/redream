#include "guest/gdrom/disc.h"
#include "core/assert.h"
#include "core/string.h"
#include "guest/gdrom/cdi.h"
#include "guest/gdrom/chd.h"
#include "guest/gdrom/gdi.h"
#include "guest/gdrom/iso.h"
#include "guest/gdrom/patch.h"

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
  int bootstart = disc->bootfad;
  int bootend = disc->bootfad + (disc->bootlen / track->data_size);

  for (int i = fad; i < endfad; i++) {
    CHECK_LE(read + track->data_size, dst_size);
    disc->read_sector(disc, track, i, dst + read);
    read += track->data_size;
  }

  /* apply bootfile patches */
  if (bootstart <= endfad && fad <= bootend) {
    int offset = (fad - bootstart) * track->data_size;
    patch_bootfile(disc->id, dst, offset, read);
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

  /* generate a unique id for the disc, and cache off info about the bootfile */
  struct disc_meta meta;
  disc_get_meta(disc, &meta);

  char device_info[17];
  char product_number[11];
  char product_version[7];
  char bootname[17];
  char name[129];

  strncpy_trim_space(device_info, meta.device_info, sizeof(meta.device_info));
  strncpy_trim_space(product_number, meta.product_number,
                     sizeof(meta.product_number));
  strncpy_trim_space(product_version, meta.product_version,
                     sizeof(meta.product_version));
  strncpy_trim_space(bootname, meta.bootname, sizeof(meta.bootname));
  strncpy_trim_space(name, meta.name, sizeof(meta.name));

  snprintf(disc->id, sizeof(disc->id), "%s %s %s %s", name, product_number,
           product_version, device_info);

  int found = disc_find_file(disc, bootname, &disc->bootfad, &disc->bootlen);
  CHECK(found);

  LOG_INFO("disc_create id=%s bootfile=%s fad=%d len=%d", disc->id, bootname,
           disc->bootfad, disc->bootlen, disc->id);

  return disc;
}
