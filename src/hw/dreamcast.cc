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
  sh4_ = new SH4(*this);
  arm7_ = new ARM7(*this);
  aica_ = new AICA(*this);
  holly_ = new Holly(*this);
  gdrom_ = new GDROM(*this);
  maple_ = new Maple(*this);
  pvr_ = new PVR2(*this);
  ta_ = new TileAccelerator(*this, rb);
}

Dreamcast::~Dreamcast() {
  delete sh4_;
  delete arm7_;
  delete aica_;
  delete holly_;
  delete gdrom_;
  delete maple_;
  delete pvr_;
  delete ta_;
}
