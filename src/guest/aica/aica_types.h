#ifndef AICA_TYPES_H
#define AICA_TYPES_H

#include <stdint.h>

/* interrupts */
enum {
  AICA_INT_INTON,
  AICA_INT_RES1,
  AICA_INT_RES2,
  AICA_INT_MIDI_IN,
  AICA_INT_DMA_END,
  AICA_INT_DATA,
  AICA_INT_TIMER_A,
  AICA_INT_TIMER_B,
  AICA_INT_TIMER_C,
  AICA_INT_MIDI_OUT,
  AICA_INT_SAMPLE,
  NUM_AICA_INT,
};

enum {
  AICA_FMT_PCMS16,
  AICA_FMT_PCMS8,
  AICA_FMT_ADPCM,
  AICA_FMT_ADPCM_STREAM,
};

enum {
  AICA_LOOP_NONE,
  AICA_LOOP_FORWARD,
};

enum {
  AICA_EG_ATTACK,
  AICA_EG_DECAY1,
  AICA_EG_DECAY2,
  AICA_EG_RELEASE,
};

struct channel_data {
  /* 0x0 */
  uint32_t SA_hi : 7;
  uint32_t PCMS : 2;
  uint32_t LPCTL : 1;
  uint32_t SSCTL : 1;
  uint32_t : 3;
  uint32_t KYONB : 1;
  uint32_t KYONEX : 1;
  uint32_t : 16;

  /* 0x4 */
  uint32_t SA_lo : 16;
  uint32_t : 16;

  /* 0x8 */
  uint32_t LSA : 16;
  uint32_t : 16;

  /* 0xc */
  uint32_t LEA : 16;
  uint32_t : 16;

  /* 0x10 */
  uint32_t AR : 5;
  uint32_t : 1;
  uint32_t D1R : 5;
  uint32_t D2R : 5;
  uint32_t : 16;

  /* 0x14 */
  uint32_t RR : 5;
  uint32_t DL : 5;
  uint32_t KRS : 4;
  uint32_t LPSLNK : 1;
  uint32_t : 1;
  uint32_t : 16;

  /* 0x18 */
  uint32_t FNS : 10;
  uint32_t : 1;
  uint32_t OCT : 4;
  uint32_t : 1;
  uint32_t : 16;

  /* 0x1c */
  uint32_t ALFOS : 3;
  uint32_t ALFOWS : 2;
  uint32_t PLFOS : 3;
  uint32_t PLFOWS : 2;
  uint32_t LFOF : 5;
  uint32_t LFORE : 1;
  uint32_t : 16;

  /* 0x20 */
  uint32_t ISEL : 4;
  uint32_t IMXL : 4;
  uint32_t : 8;
  uint32_t : 16;

  /* 0x24 */
  uint32_t DIPAN : 5;
  uint32_t : 3;
  uint32_t DISDL : 4;
  uint32_t : 4;
  uint32_t : 16;

  /* 0x28 */
  uint32_t Q : 5;
  uint32_t : 3;
  uint32_t TL : 8;
  uint32_t : 16;

  /* 0x2c */
  uint32_t FLV0 : 13;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x30 */
  uint32_t FLV1 : 13;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x34 */
  uint32_t FLV2 : 13;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x38 */
  uint32_t FLV3 : 13;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x3c */
  uint32_t FLV4 : 13;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x40 */
  uint32_t FD1R : 5;
  uint32_t : 3;
  uint32_t FAR : 5;
  uint32_t : 3;
  uint32_t : 16;

  /* 0x44 */
  uint32_t FRR : 5;
  uint32_t : 3;
  uint32_t FD2R : 5;
  uint32_t : 3;
  uint32_t : 16;
};

struct common_data {
  /* 0x0 */
  uint32_t MVOL : 4;
  uint32_t VER : 4;
  uint32_t DAC18B : 1;
  uint32_t MEM8MB : 1;
  uint32_t : 5;
  uint32_t MONO : 1;
  uint32_t : 16;

  /* 0x4 */
  uint32_t RBP : 12;
  uint32_t : 1;
  uint32_t RBL : 2;
  uint32_t TESTB0 : 1;
  uint32_t : 16;

  /* 0x8 */
  uint32_t MIBUF : 8;
  uint32_t MIEMP : 1;
  uint32_t MIFUL : 1;
  uint32_t MIOVF : 1;
  uint32_t MOEMP : 1;
  uint32_t MOFUL : 1;
  uint32_t : 3;
  uint32_t : 16;

  /* 0xc */
  uint32_t MOBUF : 8;
  uint32_t MSLC : 6;
  uint32_t AFSEL : 1;
  uint32_t : 1;
  uint32_t : 16;

  /* 0x10 */
  uint32_t EG : 13;
  uint32_t SGC : 2;
  uint32_t LP : 1;
  uint32_t : 16;

  /* 0x14 */
  uint32_t CA : 16;
  uint32_t : 16;

  /* 0x18 */
  uint8_t padding0[0x68];

  /* 0x80 */
  uint32_t MRWINH : 4;
  uint32_t T : 1;
  uint32_t TSCD : 3;
  uint32_t : 1;
  uint32_t DMEA_hi : 7;
  uint32_t : 16;

  /* 0x84 */
  uint32_t : 2;
  uint32_t DMEA_lo : 14;
  uint32_t : 16;

  /* 0x88 */
  uint32_t : 2;
  uint32_t DRGA : 13;
  uint32_t DGATE : 1;
  uint32_t : 16;

  /* 0x8c */
  uint32_t DEXE : 1;
  uint32_t : 1;
  uint32_t DLG : 13;
  uint32_t DDIR : 1;
  uint32_t : 16;

  /* 0x90 */
  uint32_t TIMA : 8;
  uint32_t TACTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  /* 0x94 */
  uint32_t TIMB : 8;
  uint32_t TBCTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  /* 0x98 */
  uint32_t TIMC : 8;
  uint32_t TCCTL : 3;
  uint32_t : 5;
  uint32_t : 16;

  /* 0x9c */
  uint32_t SCIEB : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xa0 */
  uint32_t SCIPD : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xa4 */
  uint32_t SCIRE : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xa8 */
  uint32_t SCILV0 : 8;
  uint32_t : 8;
  uint32_t : 16;

  /* 0xac */
  uint32_t SCILV1 : 8;
  uint32_t : 8;
  uint32_t : 16;

  /* 0xb0 */
  uint32_t SCILV2 : 8;
  uint32_t : 8;
  uint32_t : 16;

  /* 0xb4 */
  uint32_t MCIEB : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xb8 */
  uint32_t MCIPD : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xbc */
  uint32_t MCIRE : 11;
  uint32_t : 5;
  uint32_t : 16;

  /* 0xc0 */
  uint8_t padding1[0x340];

  /* 0x400 */
  uint32_t ARMRST : 1;
  uint32_t : 7;
  uint32_t VREG : 2;
  uint32_t : 6;
  uint32_t : 16;

  /* 0x404 */
  uint8_t padding2[0xfc];

  /* 0x500 */
  uint32_t L : 8;
  uint32_t : 8;
  uint32_t : 16;

  /* 0x504 */
  uint32_t M : 8;
  uint32_t RP : 1;
  uint32_t : 7;
  uint32_t : 16;
};

#endif
