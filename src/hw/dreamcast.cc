#include "hw/sh4/sh4.h"
#include "hw/arm7/arm7.h"
#include "hw/aica/aica.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::aica;
using namespace re::hw::arm7;
using namespace re::hw::gdrom;
using namespace re::hw::holly;
using namespace re::hw::maple;
using namespace re::hw::sh4;
using namespace re::renderer;

Dreamcast::Dreamcast(renderer::Backend *rb) {
  sh4 = new SH4(*this);
  arm7 = new ARM7(*this);
  aica = new AICA(*this);
  holly = new Holly(*this);
  gdrom = new GDROM(*this);
  maple = new Maple(*this);
  pvr = new PVR2(*this);
  ta = new TileAccelerator(*this, rb);
}

Dreamcast::~Dreamcast() {
  delete sh4;
  delete arm7;
  delete aica;
  delete holly;
  delete gdrom;
  delete maple;
  delete pvr;
  delete ta;
}
