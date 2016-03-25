#ifndef DISC_H
#define DISC_H

#include <vector>
#include "sys/filesystem.h"

namespace re {
namespace hw {
namespace gdrom {

static const int SECTOR_SIZE = 2352;

struct Track {
  Track()
      : num(0),
        fad(0),
        adr(0),
        ctrl(0),
        filename(),
        file_offset(0),
        priv(nullptr) {}

  int num;
  int fad;
  int adr;
  int ctrl;
  char filename[PATH_MAX];
  int file_offset;
  void *priv;
};

class Disc {
 public:
  virtual ~Disc() {}

  virtual int num_tracks() const = 0;
  virtual const Track &track(int i) const = 0;

  virtual int ReadSector(int fad, void *dst) = 0;
};

class GDI : public Disc {
 public:
  ~GDI();

  int num_tracks() const { return static_cast<int>(tracks_.size()); }
  const Track &track(int i) const { return tracks_[i]; }

  bool Load(const char *filename);
  int ReadSector(int fad, void *dst);

 private:
  std::vector<Track> tracks_;
};
}
}
}

#endif
