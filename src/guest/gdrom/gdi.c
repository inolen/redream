#include "guest/gdrom/gdi.h"
#include "core/core.h"
#include "guest/gdrom/disc.h"

struct gdi {
  struct disc;
  FILE *files[DISC_MAX_TRACKS];
  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;
};

static void gdi_read_sector(struct disc *disc, struct track *track, int fad,
                            void *dst) {
  struct gdi *gdi = (struct gdi *)disc;

  int n = (int)(track - gdi->tracks);
  FILE *fp = gdi->files[n];

  /* lazily open the file backing the track */
  if (!fp) {
    fp = fopen(track->filename, "rb");
    CHECK_NOTNULL(fp, "gdi_read_sector failed to open %s", track->filename);
    gdi->files[n] = fp;
  }

  /* seek the to the starting fad */
  int offset = track->file_offset + fad * track->sector_size;
  int res = fseek(fp, offset, SEEK_SET);
  CHECK_EQ(res, 0);

  /* only read the data portion of the track */
  res = fseek(fp, track->header_size, SEEK_CUR);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, 1, track->data_size, fp);
  CHECK_EQ(res, track->data_size);

  res = fseek(fp, track->error_size, SEEK_CUR);
  CHECK_EQ(res, 0);
}

static void gdi_get_toc(struct disc *disc, int area, struct track **first_track,
                        struct track **last_track, int *leadin_fad,
                        int *leadout_fad) {
  struct gdi *gdi = (struct gdi *)disc;

  /* gdi's have one toc per area, and there is one session per area */
  struct session *session = &gdi->sessions[area];

  *first_track = &gdi->tracks[session->first_track];
  *last_track = &gdi->tracks[session->last_track];
  *leadin_fad = session->leadin_fad;
  *leadout_fad = session->leadout_fad;
}

static struct track *gdi_get_track(struct disc *disc, int n) {
  struct gdi *gdi = (struct gdi *)disc;
  CHECK_LT(n, gdi->num_tracks);
  return &gdi->tracks[n];
}

static int gdi_get_num_tracks(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;
  return gdi->num_tracks;
}

static struct session *gdi_get_session(struct disc *disc, int n) {
  struct gdi *gdi = (struct gdi *)disc;
  CHECK_LT(n, gdi->num_sessions);
  return &gdi->sessions[n];
}

static int gdi_get_num_sessions(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;
  return gdi->num_sessions;
}

static int gdi_get_format(struct disc *disc) {
  return GD_DISC_GDROM;
}

static void gdi_destroy(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;

  /* cleanup file handles */
  for (int i = 0; i < gdi->num_tracks; i++) {
    FILE *fp = gdi->files[i];

    if (fp) {
      fclose(fp);
    }
  }
}

static int gdi_parse(struct disc *disc, const char *filename, int verbose) {
  struct gdi *gdi = (struct gdi *)disc;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return 0;
  }

  /* get gdi dirname to help resolve track paths */
  char dirname[PATH_MAX];
  fs_dirname(filename, dirname, sizeof(dirname));

  /* parse tracks */
  int num_tracks;
  int n = fscanf(fp, "%d", &num_tracks);
  if (n != 1) {
    fclose(fp);
    return 0;
  }

  for (int i = 0; i < num_tracks; i++) {
    int num, lba, ctrl, sector_size, file_offset;
    char filename[PATH_MAX];

    /* parse track information, including filenames which may include single or
       double quotes */
    int parse_err = 0;

    n = fscanf(fp, "%d %d %d %d", &num, &lba, &ctrl, &sector_size);
    parse_err |= (n != 4);

    n = fscanf(fp, " \"%[^\"]\"", filename);
    if (n != 1) {
      n = fscanf(fp, " '%[^']'", filename);
      if (n != 1) {
        n = fscanf(fp, " %s", filename);
        parse_err |= (n != 1);
      }
    }

    n = fscanf(fp, " %d", &file_offset);
    parse_err |= (n != 1);

    if (parse_err) {
      LOG_WARNING("gdi_parse failed to parse track information");
      fclose(fp);
      return 0;
    }

    /* add track */
    CHECK_LT(gdi->num_tracks, ARRAY_SIZE(gdi->tracks));
    struct track *track = &gdi->tracks[gdi->num_tracks++];

    /* sanity check */
    CHECK_EQ(num, gdi->num_tracks);

    if (!track_set_layout(track, 1, sector_size)) {
      LOG_WARNING("gdi_parse unsupported track layout sector_size=%d",
                  sector_size);
      return 0;
    }

    track->num = gdi->num_tracks;
    track->fad = lba + GDROM_PREGAP;
    track->ctrl = ctrl;
    track->file_offset = file_offset - track->fad * track->sector_size;
    snprintf(track->filename, sizeof(track->filename), "%s" PATH_SEPARATOR "%s",
             dirname, filename);

    if (verbose) {
      LOG_INFO("gdi_parse track=%d filename='%s' fad=%d secsz=%d", track->num,
               track->filename, track->fad, track->sector_size);
    }
  }

  /* gdroms contains two sessions, one for the single density area (tracks 0-1)
     and one for the high density area (tracks 3+) */
  gdi->num_sessions = 2;

  /* single density area starts at 00:00:00 (fad 0x0) and can hold up to 4
     minutes of data (18,000 sectors at 75 sectors per second) */
  struct session *single = &gdi->sessions[0];
  single->leadin_fad = 0x0;
  single->leadout_fad = 0x4650;
  single->first_track = 0;
  single->last_track = 0;

  /* high density area starts at 10:00:00 (fad 0xb05e) and can hold up to
     504,300 sectors (112 minutes, 4 seconds at 75 sectors per second) */
  struct session *high = &gdi->sessions[1];
  high->leadin_fad = 0xb05e;
  high->leadout_fad = 0x861b4;
  high->first_track = 2;
  high->last_track = num_tracks - 1;

  fclose(fp);

  return 1;
}

struct disc *gdi_create(const char *filename, int verbose) {
  struct gdi *gdi = calloc(1, sizeof(struct gdi));

  gdi->destroy = &gdi_destroy;
  gdi->get_format = &gdi_get_format;
  gdi->get_num_sessions = &gdi_get_num_sessions;
  gdi->get_session = &gdi_get_session;
  gdi->get_num_tracks = &gdi_get_num_tracks;
  gdi->get_track = &gdi_get_track;
  gdi->get_toc = &gdi_get_toc;
  gdi->read_sector = &gdi_read_sector;

  struct disc *disc = (struct disc *)gdi;

  if (!gdi_parse(disc, filename, verbose)) {
    gdi_destroy(disc);
    return NULL;
  }

  return disc;
}
