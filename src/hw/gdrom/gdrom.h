#ifndef GDROM_H
#define GDROM_H

#include <memory>
#include "core/array.h"
#include "hw/gdrom/disc.h"
#include "hw/gdrom/gdrom_types.h"
#include "hw/machine.h"

namespace re {
namespace hw {
namespace holly {
class Holly;
}

class Dreamcast;
struct Register;
class Memory;

namespace gdrom {

static const int SPI_CMD_SIZE = 12;
static const int SUBCODE_SIZE = 100;

// internal gdrom state machine
enum GDEvent {
  EV_ATA_CMD_DONE,
  // each incomming SPI command will either:
  // a.) have no additional data and immediately fire EV_SPI_CMD_DONE
  // b.) read additional data over PIO with EV_SPI_READ_START
  // c.) write additional data over PIO with EV_SPI_WRITE_START
  // d.) write additional data over DMA / PIO with EV_SPI_WRITE_SECTORS
  EV_SPI_WAIT_CMD,
  EV_SPI_READ_START,
  EV_SPI_READ_END,
  EV_SPI_WRITE_START,
  EV_SPI_WRITE_SECTORS,
  EV_SPI_WRITE_END,
  EV_SPI_CMD_DONE,
};

enum GDState {
  STATE_STANDBY,
  STATE_SPI_READ_CMD,
  STATE_SPI_READ_DATA,
  STATE_SPI_WRITE_DATA,
  STATE_SPI_WRITE_SECTORS,
};

// ata / spi commands
enum ATACommand {
  ATA_NOP = 0x00,
  ATA_SOFT_RESET = 0x08,
  ATA_EXEC_DIAG = 0x90,
  ATA_PACKET = 0xa0,
  ATA_IDENTIFY_DEV = 0xa1,
  ATA_SET_FEATURES = 0xef,
};

enum SPICommand {
  SPI_TEST_UNIT = 0x00,  // Verify access readiness
  SPI_REQ_STAT = 0x10,   // Get CD status
  SPI_REQ_MODE = 0x11,   // Get various settings
  SPI_SET_MODE = 0x12,   // Make various settings
  SPI_REQ_ERROR = 0x13,  // Get error details
  SPI_GET_TOC = 0x14,    // Get all TOC data
  SPI_REQ_SES = 0x15,    // Get specified session data
  SPI_CD_OPEN = 0x16,    // Open tray
  SPI_CD_PLAY = 0x20,    // Play CD
  SPI_CD_SEEK = 0x21,    // Seek for playback position
  SPI_CD_SCAN = 0x22,    // Perform scan
  SPI_CD_READ = 0x30,    // Read CD
  SPI_CD_READ2 = 0x31,   // CD read (pre-read position)
  SPI_GET_SCD = 0x40,    // Get subcode
  SPI_UNKNOWN_70 = 0x70,
  SPI_UNKNOWN_71 = 0x71,
};

enum AreaType {
  AREA_SINGLE_DENSITY,
  AREA_DOUBLE_DENSITY,
};

enum AudioStatus {
  AUDIO_INVALID = 0x00,
  AUDIO_INPROGRESS = 0x11,
  AUDIO_PAUSED = 0x12,
  AUDIO_ENDED = 0x13,
  AUDIO_ERROR = 0x14,
  AUDIO_NOSTATUS = 0x15,
};

enum SectorMask {
  MASK_HEADER = 0x8,
  MASK_SUBHEADER = 0x4,
  MASK_DATA = 0x2,
  MASK_OTHER = 0x1
};

enum SectorFormat {
  SECTOR_ANY,
  SECTOR_CDDA,
  SECTOR_M1,
  SECTOR_M2,
  SECTOR_M2F1,
  SECTOR_M2F2,
  SECTOR_M2_NOXA
};

union TOCEntry {
  uint32_t full;
  struct {
    uint32_t adr : 4;
    uint32_t ctrl : 4;
    uint32_t fad : 24;
  };
};

struct CDReadRequest {
  bool dma;
  SectorFormat sector_format;
  SectorMask sector_mask;
  int first_sector;
  int num_sectors;
};

struct TOC {
  TOCEntry entries[99];
  TOCEntry start;
  TOCEntry end;
  TOCEntry leadout;
};

struct Session {
  uint8_t status : 8;
  uint8_t reserved : 8;
  uint8_t first_track : 8;
  uint32_t start_fad : 24;
};

#define GDROM_DECLARE_R32_DELEGATE(name) uint32_t name##_read(Register &)
#define GDROM_DECLARE_W32_DELEGATE(name) void name##_write(Register &, uint32_t)

#define GDROM_REGISTER_R32_DELEGATE(name) \
  holly_->reg(name##_OFFSET).read = make_delegate(&GDROM::name##_read, this)
#define GDROM_REGISTER_W32_DELEGATE(name) \
  holly_->reg(name##_OFFSET).write = make_delegate(&GDROM::name##_write, this)

#define GDROM_R32_DELEGATE(name) uint32_t GDROM::name##_read(Register &reg)
#define GDROM_W32_DELEGATE(name) \
  void GDROM::name##_write(Register &reg, uint32_t old_value)

class GDROM : public Device {
 public:
  GDROM(Dreamcast &dc);

  bool Init() final;

  void SetDisc(std::unique_ptr<Disc> disc);

  void BeginDMA();
  int ReadDMA(uint8_t *data, int data_size);
  void EndDMA();

 private:
  void TriggerEvent(GDEvent ev);
  void TriggerEvent(GDEvent ev, intptr_t arg0, intptr_t arg1);
  void ProcessATACommand(ATACommand cmd);
  void ProcessSPICommand(uint8_t *data);
  void ProcessSetMode(int offset, uint8_t *data, int data_size);

  void GetTOC(AreaType area_type, TOC *toc);
  void GetSession(int session, Session *ses);
  void GetSubcode(int format, uint8_t *data);
  int GetFAD(uint8_t a, uint8_t b, uint8_t c, bool msf);
  int ReadSectors(int fad, SectorFormat format, SectorMask mask,
                  int num_sectors, uint8_t *dst, int dst_size);

  GDROM_DECLARE_R32_DELEGATE(GD_ALTSTAT_DEVCTRL);
  GDROM_DECLARE_W32_DELEGATE(GD_ALTSTAT_DEVCTRL);
  GDROM_DECLARE_R32_DELEGATE(GD_DATA);
  GDROM_DECLARE_W32_DELEGATE(GD_DATA);
  GDROM_DECLARE_R32_DELEGATE(GD_ERROR_FEATURES);
  GDROM_DECLARE_W32_DELEGATE(GD_ERROR_FEATURES);
  GDROM_DECLARE_R32_DELEGATE(GD_INTREASON_SECTCNT);
  GDROM_DECLARE_W32_DELEGATE(GD_INTREASON_SECTCNT);
  GDROM_DECLARE_R32_DELEGATE(GD_SECTNUM);
  GDROM_DECLARE_W32_DELEGATE(GD_SECTNUM);
  GDROM_DECLARE_R32_DELEGATE(GD_BYCTLLO);
  GDROM_DECLARE_W32_DELEGATE(GD_BYCTLLO);
  GDROM_DECLARE_R32_DELEGATE(GD_BYCTLHI);
  GDROM_DECLARE_W32_DELEGATE(GD_BYCTLHI);
  GDROM_DECLARE_R32_DELEGATE(GD_DRVSEL);
  GDROM_DECLARE_W32_DELEGATE(GD_DRVSEL);
  GDROM_DECLARE_R32_DELEGATE(GD_STATUS_COMMAND);
  GDROM_DECLARE_W32_DELEGATE(GD_STATUS_COMMAND);

  Dreamcast &dc_;
  Memory *memory_;
  holly::Holly *holly_;

  GD_FEATURES_T features_;
  GD_INTREASON_T intreason_;
  GD_SECTNUM_T sectnum_;
  GD_BYTECT_T byte_count_;
  GD_STATUS_T status_;

  uint8_t pio_buffer_[0x10000];
  int pio_head_;
  int pio_size_;
  int pio_read_offset_;

  re::array<uint8_t> dma_buffer_;
  int dma_head_;
  int dma_size_;

  GDState state_;
  std::unique_ptr<Disc> current_disc_;
  CDReadRequest cdreq_;
};
}
}
}

#endif
