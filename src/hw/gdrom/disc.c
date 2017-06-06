#include "hw/gdrom/disc.h"
#include "core/assert.h"

static const int GDI_PREGAP_SIZE = 150;

struct disc {
  void (*destroy)(struct disc *);

  int (*get_num_sessions)(struct disc *);
  struct session *(*get_session)(struct disc *, int);

  int (*get_num_tracks)(struct disc *);
  struct track *(*get_track)(struct disc *, int);

  int (*read_sector)(struct disc *, int, void *);
};

struct gdi {
  struct disc;
  struct session sessions[2];
  int num_sessions;
  struct track tracks[64];
  int num_tracks;
};

static int gdi_read_sector(struct disc *disc, int fad, void *dst) {
  struct gdi *gdi = (struct gdi *)disc;

  /* find the track to read from */
  struct track *track = NULL;
  for (int i = 0; i < gdi->num_tracks; i++) {
    struct track *curr_track = &gdi->tracks[i];
    struct track *next_track =
        i < gdi->num_tracks - 1 ? &gdi->tracks[i + 1] : NULL;

    if (fad >= curr_track->fad && (!next_track || fad < next_track->fad)) {
      track = curr_track;
      break;
    }
  }
  CHECK_NOTNULL(track);

  /* open the file backing the track */
  if (!track->file) {
    track->file = fopen(track->filename, "rb");
    CHECK(track->file);
  }

  /* read from it */
  int res =
      fseek(track->file, track->file_offset + fad * SECTOR_SIZE, SEEK_SET);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, SECTOR_SIZE, 1, track->file);
  CHECK_EQ(res, 1);

  return 1;
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

static void gdi_destroy(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;

  /* cleanup file handles */
  for (int i = 0; i < gdi->num_tracks; i++) {
    struct track *track = &gdi->tracks[i];

    if (track->file) {
      fclose(track->file);
    }
  }
}

static struct gdi *gdi_create(const char *filename) {
  struct gdi *gdi = calloc(1, sizeof(struct gdi));

  gdi->destroy = &gdi_destroy;
  gdi->get_num_sessions = &gdi_get_num_sessions;
  gdi->get_session = &gdi_get_session;
  gdi->get_num_tracks = &gdi_get_num_tracks;
  gdi->get_track = &gdi_get_track;
  gdi->read_sector = &gdi_read_sector;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    free(gdi);
    return NULL;
  }

  /* get gdi dirname to help resolve track paths */
  char dirname[PATH_MAX];
  fs_dirname(filename, dirname, sizeof(dirname));

  /* parse tracks */
  int num_tracks;
  int n = fscanf(fp, "%d", &num_tracks);
  if (n != 1) {
    free(gdi);
    fclose(fp);
    return NULL;
  }

  for (int i = 0; i < num_tracks; i++) {
    int num, lba, ctrl, sector_size, file_offset;
    char filename[PATH_MAX];

    n = fscanf(fp, "%d %d %d %d %s %d", &num, &lba, &ctrl, &sector_size,
               filename, &file_offset);

    if (n != 6) {
      free(gdi);
      fclose(fp);
      return NULL;
    }

    /* add track */
    CHECK_LT(gdi->num_tracks, array_size(gdi->tracks));
    struct track *track = &gdi->tracks[gdi->num_tracks++];
    track->num = num;
    track->fad = lba + GDI_PREGAP_SIZE;
    track->ctrl = ctrl;
    track->file_offset = file_offset - track->fad * SECTOR_SIZE;
    snprintf(track->filename, sizeof(track->filename), "%s" PATH_SEPARATOR "%s",
             dirname, filename);
  }

  /* gdroms contains two sessions, one for the single density area (tracks 0-1)
     and one for the high density area (tracks 3+) */
  gdi->num_sessions = 2;

  /* single density area starts at 00:00:00 (fad 0x0) and can hold up to 4
     minutes of data (18,000 sectors at 75 sectors per second) */
  struct session *single = &gdi->sessions[0];
  single->leadin_fad = 0x0;
  single->first_track = &gdi->tracks[0];
  single->last_track = &gdi->tracks[1];
  single->leadout_fad = 0x4650;

  /* high density area starts at 10:00:00 (fad 0xb05e) and can hold up to
     504,300 sectors (112 minutes, 4 seconds at 75 sectors per second) */
  struct session *high = &gdi->sessions[1];
  high->leadin_fad = 0xb05e;
  high->first_track = &gdi->tracks[2];
  high->last_track = &gdi->tracks[num_tracks - 1];
  high->leadout_fad = 0x861b4;

  fclose(fp);

  return gdi;
}

int disc_read_sector(struct disc *disc, int fad, void *dst) {
  return disc->read_sector(disc, fad, dst);
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

struct disc *disc_create_gdi(const char *filename) {
  return (struct disc *)gdi_create(filename);
}
