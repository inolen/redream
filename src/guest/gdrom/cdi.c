#include "core/core.h"
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

static const char *cdi_sector_modes[] = {"CDDA", "MODE1", "MODE2"};

static const int cdi_sector_sizes[] = {2048, 2336, 2352};

static const uint8_t cdi_start_mark[] = {0, 0, 1, 0, 0, 0, 255, 255, 255, 255};

struct cdi {
  struct disc;
  FILE *fp;
  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;
};

static void cdi_read_sector(struct disc *disc, struct track *track, int fad,
                            void *dst) {
  struct cdi *cdi = (struct cdi *)disc;

  /* seek the to the starting fad */
  int offset = track->file_offset + fad * track->sector_size;
  int res = fseek(cdi->fp, offset, SEEK_SET);
  CHECK_EQ(res, 0);

  /* only read the data portion of the track */
  res = fseek(cdi->fp, track->header_size, SEEK_CUR);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, 1, track->data_size, cdi->fp);
  CHECK_EQ(res, track->data_size);
}

static void cdi_get_toc(struct disc *disc, int area, struct track **first_track,
                        struct track **last_track, int *leadin_fad,
                        int *leadout_fad) {
  struct cdi *cdi = (struct cdi *)disc;

  /* cdi's don't have a high-density area */
  CHECK_NE(area, GD_AREA_HIGH);

  /* the toc on cdi's represents all tracks / sessions */
  struct session *first_session = &cdi->sessions[0];
  struct session *last_session = &cdi->sessions[cdi->num_sessions - 1];

  *first_track = &cdi->tracks[0];
  *last_track = &cdi->tracks[cdi->num_tracks - 1];
  *leadin_fad = first_session->leadin_fad;
  *leadout_fad = last_session->leadout_fad;
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
  return GD_DISC_CDROM_XA;
}

static void cdi_destroy(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;

  if (cdi->fp) {
    fclose(cdi->fp);
  }
}

static int cdi_parse_track(struct disc *disc, uint32_t version,
                           int *track_offset, int *leadout_fad, int verbose) {
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
  uint32_t pregap_len, track_len, sector_mode, lba, total_len, sector_type;
  fseek(fp, 2, SEEK_CUR);
  r = fread(&pregap_len, 4, 1, fp);
  CHECK_EQ(r, 1);
  r = fread(&track_len, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 6, SEEK_CUR);
  r = fread(&sector_mode, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 12, SEEK_CUR);
  r = fread(&lba, 4, 1, fp);
  CHECK_EQ(r, 1);
  r = fread(&total_len, 4, 1, fp);
  CHECK_EQ(r, 1);
  fseek(fp, 16, SEEK_CUR);
  r = fread(&sector_type, 4, 1, fp);
  CHECK_EQ(r, 1);

  if (total_len != (pregap_len + track_len)) {
    LOG_WARNING("cdi_parse track length is invalid");
    return 0;
  }

  if (sector_type >= ARRAY_SIZE(cdi_sector_sizes)) {
    LOG_WARNING("cdi_parse unsupported sector type 0x%x", sector_type);
    return 0;
  }

  int sector_size = cdi_sector_sizes[sector_type];
  int data_offset = *track_offset + pregap_len * sector_size;

  if (!track_set_layout(track, sector_mode, sector_size)) {
    LOG_WARNING("cdi_parse unsupported track layout mode=%d size=%d",
                sector_mode, sector_size);
    return 0;
  }

  track->fad = pregap_len + lba;
  track->adr = 0;
  track->ctrl = sector_mode == 0 ? 0 : 4;
  track->file_offset = data_offset - track->fad * track->sector_size;

  if (verbose) {
    LOG_INFO("cdi_parse_track track=%d fad=%d off=%d mode=%s/%d", track->num,
             track->fad, data_offset, cdi_sector_modes[sector_mode],
             track->sector_size);
  }

  *track_offset += total_len * sector_size;
  *leadout_fad = track->fad + track_len;

  return 1;
}

static int cdi_parse_session(struct disc *disc, uint32_t version,
                             int *track_offset, int verbose) {
  struct cdi *cdi = (struct cdi *)disc;
  FILE *fp = cdi->fp;

  /* parse tracks for the session */
  uint16_t num_tracks;
  size_t r = fread(&num_tracks, 2, 1, fp);
  CHECK_EQ(r, 1);

  if (!num_tracks) {
    LOG_WARNING("cdi_parse_session session contains no tracks");
    return 0;
  }

  int first_track_num = cdi->num_tracks;
  int last_track_num = 0;
  int leadout_fad = 0;

  while (num_tracks--) {
    if (!cdi_parse_track(disc, version, track_offset, &leadout_fad, verbose)) {
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

  last_track_num = cdi->num_tracks - 1;

  /* add session */
  struct track *first_track = &cdi->tracks[first_track_num];
  struct session *session = &cdi->sessions[cdi->num_sessions++];
  session->leadin_fad = first_track->fad;
  session->leadout_fad = leadout_fad;
  session->first_track = first_track_num;
  session->last_track = last_track_num;

  return 1;
}

static int cdi_parse(struct disc *disc, const char *filename, int verbose) {
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
  for (int i = 0; i < ARRAY_SIZE(cdi_versions); i++) {
    if (version == cdi_versions[i]) {
      if (verbose) {
        LOG_INFO("cdi_parse version %s detected", cdi_version_names[i]);
      }
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

  if (verbose) {
    LOG_INFO("cdi_parse found %d sessions", num_sessions);
  }

  /* tracks the current track's data offset from the file start */
  int track_offset = 0;

  while (num_sessions--) {
    if (!cdi_parse_session(disc, version, &track_offset, verbose)) {
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

struct disc *cdi_create(const char *filename, int verbose) {
  struct cdi *cdi = calloc(1, sizeof(struct cdi));

  cdi->destroy = &cdi_destroy;
  cdi->get_format = &cdi_get_format;
  cdi->get_num_sessions = &cdi_get_num_sessions;
  cdi->get_session = &cdi_get_session;
  cdi->get_num_tracks = &cdi_get_num_tracks;
  cdi->get_track = &cdi_get_track;
  cdi->get_toc = &cdi_get_toc;
  cdi->read_sector = &cdi_read_sector;

  struct disc *disc = (struct disc *)cdi;

  if (!cdi_parse(disc, filename, verbose)) {
    cdi_destroy(disc);
    return NULL;
  }

  return disc;
}
