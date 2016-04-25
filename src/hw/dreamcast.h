#ifndef DREAMCAST_H
#define DREAMCAST_H

#include "hw/machine.h"

namespace re {

namespace renderer {
class Backend;
}

namespace hw {

class AddressMap;
class AddressSpace;
class Memory;

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
  Dreamcast(renderer::Backend *rb);
  ~Dreamcast();

  hw::sh4::SH4 *sh4() { return sh4_; }
  hw::arm7::ARM7 *arm7() { return arm7_; }
  hw::aica::AICA *aica() { return aica_; }
  hw::holly::Holly *holly() { return holly_; }
  hw::gdrom::GDROM *gdrom() { return gdrom_; }
  hw::maple::Maple *maple() { return maple_; }
  hw::holly::PVR2 *pvr() { return pvr_; }
  hw::holly::TileAccelerator *ta() { return ta_; }

 private:
  hw::sh4::SH4 *sh4_;
  hw::arm7::ARM7 *arm7_;
  hw::aica::AICA *aica_;
  hw::holly::Holly *holly_;
  hw::gdrom::GDROM *gdrom_;
  hw::maple::Maple *maple_;
  hw::holly::PVR2 *pvr_;
  hw::holly::TileAccelerator *ta_;
};
}
}

#endif
