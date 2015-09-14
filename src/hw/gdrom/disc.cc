#include "core/core.h"
#include "hw/gdrom/disc.h"

#define PREGAP_SIZE 150

using namespace dreavm;
using namespace dreavm::hw::gdrom;

//
// GDI format
//
// <number of tracks>
// <track num> <lba> <ctrl> <sector size> <file name> <file offset>
// <track num> <lba> <ctrl> <sector size> <file name> <file offset>
//

GDI::~GDI() {
  // cleanup file handles
  for (Track &track : tracks_) {
    FILE *fp = reinterpret_cast<FILE *>(track.priv);
    if (!fp) {
      continue;
    }
    fclose(fp);
    track.priv = nullptr;
  }
}

bool GDI::Load(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return false;
  }

  int num_tracks;
  int n = fscanf(fp, "%d", &num_tracks);
  if (n != 1) {
    fclose(fp);
    return false;
  }

  // get gdi dirname to help resolve track paths
  char dirname[PATH_MAX];
  DirName(filename, dirname, sizeof(dirname));

  for (int i = 0; i < num_tracks; i++) {
    int num, lba, ctrl, sector_size, file_offset;
    char filename[PATH_MAX];
    n = fscanf(fp, "%d %d %d %d %s %d", &num, &lba, &ctrl, &sector_size,
               filename, &file_offset);
    if (n != 6) {
      fclose(fp);
      return false;
    }

    // add track
    Track track;
    track.num = num;
    track.fad = lba + PREGAP_SIZE;
    track.ctrl = ctrl;
    track.file_offset = file_offset - track.fad * SECTOR_SIZE;
    snprintf(track.filename, sizeof(track.filename), "%s" PATH_SEPARATOR "%s",
             dirname, filename);
    tracks_.push_back(track);
  }

  fclose(fp);

  return true;
}

int GDI::ReadSector(int fad, void *dst) {
  // find the track to read from
  Track *trk = nullptr;
  for (int i = 0, l = static_cast<int>(tracks_.size()); i < l; i++) {
    Track *curr_track = &tracks_[i];
    Track *next_track = i < l - 1 ? &tracks_[i + 1] : nullptr;

    if (fad >= curr_track->fad && (!next_track || fad < next_track->fad)) {
      trk = curr_track;
      break;
    }
  }
  CHECK_NOTNULL(trk);

  // open the file backing the track
  FILE *fp = reinterpret_cast<FILE *>(trk->priv);
  if (!fp) {
    fp = fopen(trk->filename, "rb");
    CHECK(fp);
    trk->priv = fp;
  }

  // read from it
  int res = fseek(fp, trk->file_offset + fad * SECTOR_SIZE, SEEK_SET);
  CHECK_EQ(res, 0);

  res = static_cast<int>(fread(dst, SECTOR_SIZE, 1, fp));
  CHECK_EQ(res, 1);

  return 1;
}
