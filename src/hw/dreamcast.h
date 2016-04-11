#ifndef DREAMCAST_H
#define DREAMCAST_H

#include "hw/machine.h"
#include "hw/regions.h"

namespace re {
namespace hw {

namespace aica {
class AICA;
}

namespace arm7 {
class ARM7;
}

namespace gdrom {
class GDROM;
}

namespace holly {
class Holly;
class PVR2;
class TileAccelerator;
}

namespace maple {
class Maple;
}

namespace sh4 {
class SH4;
}

class Dreamcast : public Machine {
 public:
  Dreamcast()
      : sh4(nullptr),
        arm7(nullptr),
        aica(nullptr),
        holly(nullptr),
        gdrom(nullptr),
        maple(nullptr),
        pvr(nullptr),
        ta(nullptr) {}

  hw::sh4::SH4 *sh4;
  hw::arm7::ARM7 *arm7;
  hw::aica::AICA *aica;
  hw::holly::Holly *holly;
  hw::gdrom::GDROM *gdrom;
  hw::maple::Maple *maple;
  hw::holly::PVR2 *pvr;
  hw::holly::TileAccelerator *ta;
};
}
}

#endif
