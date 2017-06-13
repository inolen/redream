#include "core/assert.h"
#include "guest/gdrom/disc.h"
#include "guest/gdrom/gdrom_types.h"

#define CDI_V2 0x80000004
#define CDI_V3 0x80000005
#define CDI_V35 0x80000006

static const uint32_t cdi_versions[] = {
    CDI_V2, CDI_V3, CDI_V35,
};
static const char *cdi_version_names[] = {
    "2", "3", "3.5",
};

static const int cdi_sector_sizes[] = {2048, 2336, 2352};

static const enum gd_secfmt cdi_sector_formats[] = {
    SECTOR_CDDA, 0, SECTOR_M2F1,
};

static const char *cdi_modes[] = {
    "CDDA", "???", "M2F1",
};

static const uint8_t cdi_start_mark[] = {0, 0, 1, 0, 0, 0, 255, 255, 255, 255};

struct cdi {
  struct disc;
  FILE *fp;
  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;
};

static int cdi_read_sector(struct disc *disc, int fad, enum gd_secfmt fmt,
                           enum gd_secmask mask, void *dst) {
  struct cdi *cdi = (struct cdi *)disc;

  struct track *track = disc_lookup_track(disc, fad);
  CHECK_NOTNULL(track);
  CHECK(fmt == SECTOR_ANY || fmt == track->sector_fmt);
  CHECK(mask == MASK_DATA);

  /* read the user data portion of the sector */
  int offset = track->file_offset + fad * track->sector_size + 8;
  int size = track->sector_size - 288;
  CHECK_EQ(size, 2048);

  int res = fseek(cdi->fp, offset, SEEK_SET);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, 1, size, cdi->fp);
  CHECK_EQ(res, size);

  return res;
}

static struct track *cdi_get_track(struct disc *disc, int n) {
  struct cdi *cdi = (struct cdi *)disc;
  CHECK_LT(n, cdi->num_tracks);
  return &cdi->tracks[n];
}

static int cdi_get_num_tracks(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;
  return cdi->num_tracks;
}

static struct session *cdi_get_session(struct disc *disc, int n) {
  struct cdi *cdi = (struct cdi *)disc;
  CHECK_LT(n, cdi->num_sessions);
  return &cdi->sessions[n];
}

static int cdi_get_num_sessions(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;
  return cdi->num_sessions;
}

static int cdi_get_format(struct disc *disc) {
  return DISC_CDROM_XA;
}

static void cdi_destroy(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;

  if (cdi->fp) {
    fclose(cdi->fp);
  }
}

static int cdi_parse_track(struct disc *disc, uint32_t version,
                           int *track_offset, int *leadout_fad) {
  struct cdi *cdi = (struct cdi *)disc;
  FILE *fp = cdi->fp;

  struct track *track = &cdi->tracks[cdi->num_tracks++];

  /* track numbers are 1 indexed */
  track->num = cdi->num_tracks;

  /* extra data (DJ 3.00.780 and up) */
  uint32_t tmp;
  size_t r = fread(&tmp, 4, 1, fp);
  CHECK_EQ(r, 1);
  if (tmp != 0) {
    fseek(fp, 8, SEEK_CUR);
  }

  char start_mark[10];
  r = fread(&start_mark, 10, 1, fp);
  CHECK_EQ(r, 1);
  if (memcmp(start_mark, cdi_start_mark, 10)) {
    LOG_WARNING("cdi_parse start mark does not match");
    return 0;
  }

  r = fread(&start_mark, 10, 1, fp);
  CHECK_EQ(r, 1);
  if (memcmp(start_mark, cdi_start_mark, 10)) {
    LOG_WARNING("cdi_parse start mark does not match");
    return 0;
  }

  /* skip filename and other fields */
  fseek(fp, 4, SEEK_CUR);
  uint8_t filename_len;
  r = fread(&filename_len, 1, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, filename_len + 11 + 4 + 4, SEEK_CUR);

  /* DJ4 */
  r = fread(&tmp, 4, 1, fp);
  CHECK_EQ(r, 1);
  if (tmp == 0x80000000) {
    fseek(fp, 8, SEEK_CUR);
  }

  /* parse track info */
  uint32_t pregap_length, track_length, mode, lba, total_length, sector_type;
  fseek(fp, 2, SEEK_CUR);
  r = fread(&pregap_length, 4, 1, fp);
  CHECK_EQ(r, 1);
  r = fread(&track_length, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 6, SEEK_CUR);
  r = fread(&mode, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 12, SEEK_CUR);
  r = fread(&lba, 4, 1, fp);
  CHECK_EQ(r, 1);
  r = fread(&total_length, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 16, SEEK_CUR);
  r = fread(&sector_type, 4, 1, fp);
  CHECK_EQ(r, 1);

  if (pregap_length != GDROM_PREGAP) {
    LOG_WARNING("cdi_parse non-standard pregap size %u", pregap_length);
    return 0;
  }

  if (total_length != (pregap_length + track_length)) {
    LOG_WARNING("cdi_parse track length is invalid");
    return 0;
  }

  if (mode != 0 && mode != 2) {
    LOG_WARNING("cdi_parse unsupported track mode %d", mode);
    return 0;
  }

  if (sector_type >= array_size(cdi_sector_sizes)) {
    LOG_WARNING("cdi_parse unsupported sector type 0x%x", sector_type);
    return 0;
  }

  int sector_size = cdi_sector_sizes[sector_type];
  track->fad = pregap_length + lba;
  track->adr = 0;  /* no subq channel mode info */
  track->ctrl = 4; /* data track */
  track->sector_size = sector_size;
  track->sector_fmt = cdi_sector_formats[mode];
  track->file_offset = *track_offset + pregap_length * sector_size -
                       track->fad * track->sector_size;

  LOG_INFO("cdi_parse_track track=%d fad=%d mode=%s/%d", track->num, track->fad,
           cdi_modes[mode], track->sector_size);

  *track_offset += total_length * sector_size;
  *leadout_fad = track->fad + total_length;

  return 1;
}

static int cdi_parse_session(struct disc *disc, uint32_t version,
                             int *track_offset) {
  struct cdi *cdi = (struct cdi *)disc;
  FILE *fp = cdi->fp;

  int first_track = cdi->num_tracks;
  int leadout_fad = 0;

  /* parse tracks for the session */
  uint16_t num_tracks;
  size_t r = fread(&num_tracks, 2, 1, fp);
  CHECK_EQ(r, 1);

  if (!num_tracks) {
    LOG_WARNING("cdi_parse_session session contains no tracks");
    return 0;
  }

  while (num_tracks--) {
    if (!cdi_parse_track(disc, version, track_offset, &leadout_fad)) {
      return 0;
    }

    /* seek to the next track */
    fseek(fp, 29, SEEK_CUR);

    /* extra data (DJ 3.00.780 and up) */
    if (version != CDI_V2) {
      uint32_t tmp;

      fseek(fp, 5, SEEK_CUR);

      r = fread(&tmp, 4, 1, fp);
      CHECK_EQ(r, 1);

      if (tmp == 0xffffffff) {
        fseek(fp, 78, SEEK_CUR);
      }
    }
  }

  /* add session */
  struct session *session = &cdi->sessions[cdi->num_sessions++];
  session->leadin_fad = 0x0;
  session->leadout_fad = leadout_fad;
  session->first_track = first_track;
  session->last_track = cdi->num_tracks - 1;

  return 1;
}

static int cdi_parse(struct disc *disc, const char *filename) {
  struct cdi *cdi = (struct cdi *)disc;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return 0;
  }
  cdi->fp = fp;

  /* validate the cdi headers */
  uint32_t version;
  uint32_t header_offset;

  fseek(fp, -8, SEEK_END);

  size_t r = fread(&version, 4, 1, fp);
  CHECK_EQ(r, 1);

  r = fread(&header_offset, 4, 1, fp);
  CHECK_EQ(r, 1);

  if (!header_offset) {
    LOG_WARNING("cdi_parse failed, corrupt image");
    return 0;
  }

  int found = 0;
  for (int i = 0; i < array_size(cdi_versions); i++) {
    if (version == cdi_versions[i]) {
      LOG_INFO("cdi_parse version %s detected", cdi_version_names[i]);
      found = 1;
      break;
    }
  }

  if (!found) {
    LOG_WARNING("cdi_parse unknown version 0x%x", version);
    return 0;
  }

  /* parse sessions, for 3.5 offset counts from file EOF */
  if (version == CDI_V35) {
    fseek(fp, -(long int)header_offset, SEEK_END);
  } else {
    fseek(fp, header_offset, SEEK_SET);
  }

  uint16_t num_sessions;
  r = fread(&num_sessions, 2, 1, fp);
  CHECK_EQ(r, 1);

  if (num_sessions != 2) {
    LOG_WARNING("cdi_parse unexpected number of sessions %d", num_sessions);
    return 0;
  }

  LOG_INFO("cdi_parse found %d sessions", num_sessions);

  /* tracks the current track's data offset from the file start */
  int track_offset = 0;

  while (num_sessions--) {
    if (!cdi_parse_session(disc, version, &track_offset)) {
      return 0;
    }

    /* seek to the next session */
    int offset = 4 + 8;
    if (version != CDI_V2) {
      offset += 1;
    }
    fseek(fp, offset, SEEK_CUR);
  }

  return 1;
}

struct disc *cdi_create(const char *filename) {
  struct cdi *cdi = calloc(1, sizeof(struct cdi));

  cdi->destroy = &cdi_destroy;
  cdi->get_format = &cdi_get_format;
  cdi->get_num_sessions = &cdi_get_num_sessions;
  cdi->get_session = &cdi_get_session;
  cdi->get_num_tracks = &cdi_get_num_tracks;
  cdi->get_track = &cdi_get_track;
  cdi->read_sector = &cdi_read_sector;

  struct disc *disc = (struct disc *)cdi;

  if (!cdi_parse(disc, filename)) {
    cdi_destroy(disc);
    return NULL;
  }

  return disc;
}
