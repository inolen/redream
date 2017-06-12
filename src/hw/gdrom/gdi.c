#include "hw/gdrom/gdi.h"
#include "core/assert.h"
#include "hw/gdrom/disc.h"

struct gdi {
  struct disc;
  FILE *files[DISC_MAX_TRACKS];
  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;
};

static int gdi_read_sector(struct disc *disc, int fad, enum gd_secfmt fmt,
                           enum gd_secmask mask, void *dst) {
  struct gdi *gdi = (struct gdi *)disc;

  struct track *track = disc_lookup_track(disc, fad);
  CHECK_NOTNULL(track);
  CHECK(fmt == SECTOR_ANY || fmt == track->sector_fmt);
  CHECK(mask == MASK_DATA);

  /* open the file backing the track */
  int n = (int)(track - gdi->tracks);
  FILE *fp = gdi->files[n];

  if (!fp) {
    fp = gdi->files[n] = fopen(track->filename, "rb");
    CHECK_NOTNULL(fp);
  }

  /* read the user data portion of the sector */
  int offset = track->file_offset + fad * track->sector_size + 16;
  int size = track->sector_size - 304;
  CHECK_EQ(size, 2048);

  int res = fseek(fp, offset, SEEK_SET);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, 1, size, fp);
  CHECK_EQ(res, size);

  return res;
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

static void gdi_get_uid(struct disc *disc, char* dst) {
  char ip[2048];
  gdi_read_sector(disc, 45150, SECTOR_ANY, MASK_DATA, ip);
  strncpy(dst, &ip[0x40], 10);
}

static int gdi_get_format(struct disc *disc) {
  return DISC_GDROM;
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

static int gdi_parse(struct disc *disc, const char *filename) {
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

    n = fscanf(fp, "%d %d %d %d %s %d", &num, &lba, &ctrl, &sector_size,
               filename, &file_offset);

    if (n != 6) {
      fclose(fp);
      return 0;
    }

    /* add track */
    CHECK_LT(gdi->num_tracks, array_size(gdi->tracks));
    struct track *track = &gdi->tracks[gdi->num_tracks++];
    track->num = gdi->num_tracks;
    track->fad = lba + GDROM_PREGAP;
    track->ctrl = ctrl;
    track->sector_size = sector_size;
    track->sector_fmt = SECTOR_M1;
    track->file_offset = file_offset - track->fad * track->sector_size;
    snprintf(track->filename, sizeof(track->filename), "%s" PATH_SEPARATOR "%s",
             dirname, filename);

    /* sanity check */
    CHECK_EQ(num, track->num);

    LOG_INFO("gdi_parse_track track=%d fad=%d secsz=%d", track->num, track->fad,
             track->sector_size);
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

struct disc *gdi_create(const char *filename) {
  struct gdi *gdi = calloc(1, sizeof(struct gdi));

  gdi->destroy = &gdi_destroy;
  gdi->get_format = &gdi_get_format;
  gdi->get_num_sessions = &gdi_get_num_sessions;
  gdi->get_uid = &gdi_get_uid;
  gdi->get_session = &gdi_get_session;
  gdi->get_num_tracks = &gdi_get_num_tracks;
  gdi->get_track = &gdi_get_track;
  gdi->read_sector = &gdi_read_sector;

  struct disc *disc = (struct disc *)gdi;

  if (!gdi_parse(disc, filename)) {
    gdi_destroy(disc);
    return NULL;
  }

  return disc;
}
