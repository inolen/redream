#include "hw/gdrom/gdi.h"
#include "core/assert.h"

static const int GDI_PREGAP_SIZE = 150;

struct gdi_track {
  struct track;

  // GDI specific
  FILE *file;
  int file_offset;
  char filename[PATH_MAX];
};


struct gdi {
  struct disc;
  struct gdi_track tracks[64];
  int num_tracks;
};

static int gdi_get_num_tracks(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;
  return gdi->num_tracks;
}

static struct track *gdi_get_track(struct disc *disc, int n) {
  struct gdi *gdi = (struct gdi *)disc;
  return (struct track*)&gdi->tracks[n];
}

static int gdi_read_sector(struct disc *disc, int fad, void *dst) {
  struct gdi *gdi = (struct gdi *)disc;

  // find the track to read from
  struct gdi_track *track = NULL;
  for (int i = 0; i < gdi->num_tracks; i++) {
    struct gdi_track *curr_track = &gdi->tracks[i];
    struct gdi_track *next_track =
        i < gdi->num_tracks - 1 ? &gdi->tracks[i + 1] : NULL;

    if (fad >= curr_track->fad && (!next_track || fad < next_track->fad)) {
      track = curr_track;
      break;
    }
  }
  CHECK_NOTNULL(track);

  // open the file backing the track
  if (!track->file) {
    track->file = fopen(track->filename, "rb");
    CHECK(track->file);
  }

  // read from it
  int res =
      fseek(track->file, track->file_offset + fad * SECTOR_SIZE, SEEK_SET);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, SECTOR_SIZE, 1, track->file);
  CHECK_EQ(res, 1);

  return 1;
}

static void gdi_destroy(struct disc *disc) {
  struct gdi *gdi = (struct gdi *)disc;

  // cleanup file handles
  for (int i = 0; i < gdi->num_tracks; i++) {
    struct gdi_track *track = &gdi->tracks[i];

    if (track->file) {
      fclose(track->file);
    }
  }
}

static struct gdi *gdi_create(const char *filename) {
  struct gdi *gdi = calloc(1, sizeof(struct gdi));

  gdi->destroy = &gdi_destroy;
  gdi->get_num_tracks = &gdi_get_num_tracks;
  gdi->get_track = &gdi_get_track;
  gdi->read_sector = &gdi_read_sector;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    free(gdi);
    return NULL;
  }

  // <number of tracks>
  // <track num> <lba> <ctrl> <sector size> <file name> <file offset>
  // <track num> <lba> <ctrl> <sector size> <file name> <file offset>
  int num_tracks;
  int n = fscanf(fp, "%d", &num_tracks);
  if (n != 1) {
    free(gdi);
    fclose(fp);
    return NULL;
  }

  // get gdi dirname to help resolve track paths
  char dirname[PATH_MAX];
  fs_dirname(filename, dirname, sizeof(dirname));

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

    // add track
    CHECK_LT(gdi->num_tracks, array_size(gdi->tracks));
    struct gdi_track *track = &gdi->tracks[gdi->num_tracks++];
    track->num = num;
    track->fad = lba + GDI_PREGAP_SIZE;
    track->ctrl = ctrl;
    track->file_offset = file_offset - track->fad * SECTOR_SIZE;
    snprintf(track->filename, sizeof(track->filename), "%s" PATH_SEPARATOR "%s",
             dirname, filename);
  }

  fclose(fp);

  return gdi;
}

struct disc *disc_create_gdi(const char *filename) {
  return (struct disc *)gdi_create(filename);
}

