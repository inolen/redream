#ifndef AICA_TYPES_H
#define AICA_TYPES_H

#include <stdint.h>

namespace re {
namespace hw {
namespace aica {

struct ChannelData {
  uint32_t SA_hi : 7;
  uint32_t PCMS : 2;
  uint32_t LPCTL : 1;
  uint32_t SSCTL : 1;
  uint32_t : 3;
  uint32_t KYONB : 1;
  uint32_t KYONEX : 1;
  uint32_t : 16;

  uint32_t SA_lo : 16;
  uint32_t : 16;

  uint32_t LSA : 16;
  uint32_t : 16;

  uint32_t LEA : 16;
  uint32_t : 16;

  uint32_t AR : 5;
  uint32_t : 1;
  uint32_t D1R : 5;
  uint32_t D2R : 5;
  uint32_t : 16;

  uint32_t RR : 5;
  uint32_t DL : 5;
  uint32_t KRS : 4;
  uint32_t LPSLNK : 1;
  uint32_t : 1;
  uint32_t : 16;

  uint32_t FNS : 10;
  uint32_t : 1;
  uint32_t OCT : 4;
  uint32_t : 1;
  uint32_t : 16;

  uint32_t ALFOS : 3;
  uint32_t ALFOWS : 2;
  uint32_t PLFOS : 3;
  uint32_t PLFOWS : 2;
  uint32_t LFOF : 5;
  uint32_t LFORE : 1;
  uint32_t : 16;

  uint32_t ISEL : 4;
  uint32_t IMXL : 4;
  uint32_t : 8;
  uint32_t : 16;

  uint32_t DIPAN : 5;
  uint32_t : 3;
  uint32_t DISDL : 4;
  uint32_t : 4;
  uint32_t : 16;

  uint32_t Q : 5;
  uint32_t : 3;
  uint32_t TL : 8;
  uint32_t : 16;

  uint32_t FLV0 : 13;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FLV1 : 13;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FLV2 : 13;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FLV3 : 13;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FLV4 : 13;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FD1R : 5;
  uint32_t : 3;
  uint32_t FAR : 5;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t FRR : 5;
  uint32_t : 3;
  uint32_t FD2R : 5;
  uint32_t : 3;
  uint32_t : 16;
};

struct CommonData {
  uint32_t MVOL : 4;
  uint32_t VER : 4;
  uint32_t DAC18B : 1;
  uint32_t MEM8MB : 1;
  uint32_t : 5;
  uint32_t MONO : 1;
  uint32_t : 16;

  uint32_t RBP : 12;
  uint32_t : 1;
  uint32_t RBL : 2;
  uint32_t TESTB0 : 1;
  uint32_t : 16;

  uint32_t MIBUF : 8;
  uint32_t MIEMP : 1;
  uint32_t MIFUL : 1;
  uint32_t MIOVF : 1;
  uint32_t MOEMP : 1;
  uint32_t MOFUL : 1;
  uint32_t : 3;
  uint32_t : 16;

  uint32_t MOBUF : 8;
  uint32_t MSLC : 6;
  uint32_t AFSEL : 1;
  uint32_t : 1;
  uint32_t : 16;

  uint32_t EG : 13;
  uint32_t SGC : 2;
  uint32_t LP : 1;
  uint32_t : 16;

  uint32_t CA : 16;
  uint32_t : 16;

  uint8_t padding0[0x68];

  uint32_t MRWINH : 4;
  uint32_t T : 1;
  uint32_t TSCD : 3;
  uint32_t : 1;
  uint32_t DMEA_hi : 7;
  uint32_t : 16;

  uint32_t : 2;
  uint32_t DMEA_lo : 14;
  uint32_t : 16;

  uint32_t : 2;
  uint32_t DRGA : 13;
  uint32_t DGATE : 1;
  uint32_t : 16;

  uint32_t DEXE : 1;
  uint32_t : 1;
  uint32_t DLG : 13;
  uint32_t DDIR : 1;
  uint32_t : 16;

  uint32_t TIMA : 8;
  uint32_t TACTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t TIMB : 8;
  uint32_t TBCTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t TIMC : 8;
  uint32_t TCCTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t SCIEB : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t SCIPD : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t SCIRE : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t SCILV0 : 8;
  uint32_t : 8;
  uint32_t : 16;

  uint32_t SCILV1 : 8;
  uint32_t : 8;
  uint32_t : 16;

  uint32_t SCILV2 : 8;
  uint32_t : 8;
  uint32_t : 16;

  uint32_t MCIEB : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t MCIPD : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint32_t MCIRE : 11;
  uint32_t : 5;
  uint32_t : 16;

  uint8_t padding1[0x340];

  uint32_t ARMRST : 1;
  uint32_t : 7;
  uint32_t VREG : 2;
  uint32_t : 6;
  uint32_t : 16;

  uint8_t padding2[0xfc];

  uint32_t L0 : 1;
  uint32_t L1 : 1;
  uint32_t L2 : 1;
  uint32_t L3 : 1;
  uint32_t L4 : 1;
  uint32_t L5 : 1;
  uint32_t L6 : 1;
  uint32_t L7 : 1;
  uint32_t : 8;
  uint32_t : 16;

  uint32_t M0 : 1;
  uint32_t M1 : 1;
  uint32_t M2 : 1;
  uint32_t M3 : 1;
  uint32_t M4 : 1;
  uint32_t M5 : 1;
  uint32_t M6 : 1;
  uint32_t M7 : 1;
  uint32_t RP : 1;
  uint32_t : 7;
  uint32_t : 16;
};
}
}
}

#endif
