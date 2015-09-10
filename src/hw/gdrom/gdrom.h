#ifndef GDROM_H
#define GDROM_H

#include <memory>
#include "hw/gdrom/disc.h"

namespace dreavm {
namespace emu {
class Dreamcast;
struct Register;
}

namespace hw {
namespace holly {
class Holly;
}

class Memory;

namespace gdrom {

enum GDState {  //
  STATE_STANDBY,
  STATE_SPI_READ_CMD,
  STATE_SPI_READ_DATA,
  STATE_SPI_WRITE_DATA
};

enum GDEvent {
  EV_ATA_CMD_DONE,
  // each incomming SPI command will either:
  // a.) have no additional data and immediately fire EV_SPI_CMD_DONE
  // b.) read additional data over PIO with EV_SPI_READ_START
  // c.) write additional data over PIO with EV_SPI_WRITE_START
  EV_SPI_WAIT_CMD,
  EV_SPI_READ_START,
  EV_SPI_READ_END,
  EV_SPI_WRITE_START,
  EV_SPI_WRITE_END,
  EV_SPI_CMD_DONE
};

// ata / spi commands
enum ATACommand {
  ATA_NOP = 0x00,
  ATA_SOFT_RESET = 0x08,
  ATA_EXEC_DIAG = 0x90,
  ATA_PACKET = 0xa0,
  ATA_IDENTIFY_DEV = 0xa1,
  ATA_SET_FEATURES = 0xef
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
  SPI_UNKNOWN_71 = 0x71
};

enum AreaType {  //
  AREA_SINGLE_DENSITY,
  AREA_DOUBLE_DENSITY
};

enum AudioStatus {
  AUDIO_INVALID = 0x00,
  AUDIO_INPROGRESS = 0x11,
  AUDIO_PAUSED = 0x12,
  AUDIO_ENDED = 0x13,
  AUDIO_ERROR = 0x14,
  AUDIO_NOSTATUS = 0x15
};

union TOCEntry {
  uint32_t full;
  struct {
    uint32_t adr : 4;
    uint32_t ctrl : 4;
    uint32_t fad : 24;
  };
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

enum { SUBCODE_SIZE = 100 };

// register types
enum DriveStatus {
  GD_STATUS_BUSY,     // State transition
  GD_STATUS_PAUSE,    // Pause
  GD_STATUS_STANDBY,  // Standby (drive stop)
  GD_STATUS_PLAY,     // CD playback
  GD_STATUS_SEEK,     // Seeking
  GD_STATUS_SCAN,     // Scanning
  GD_STATUS_OPEN,     // Tray is open
  GD_STATUS_NODISC,   // No disc
  GD_STATUS_RETRY,    // Read retry in progress (option)
  GD_STATUS_ERROR,  // Reading of disc TOC failed (state does not allow access)
};

enum DiscType {
  GD_CDDA = 0x00,
  GD_CDROM = 0x01,
  GD_CDROM_XA = 0x02,
  GD_CDROM_EX = 0x03,
  GD_CDROM_CDI = 0x04,
  GD_GDROM = 0x08
};

union GD_FEATURES_T {
  uint32_t full;
  struct {
    uint32_t dma : 1;
    uint32_t reserved : 31;
  };
};

union GD_INTREASON_T {
  uint32_t full;
  struct {
    uint32_t CoD : 1;  // "0" indicates data and "1" indicates a command.
    uint32_t IO : 1;   // "1" indicates transfer from device to host, and "0"
                       // from host to device.
    uint32_t reserved : 30;
  };
};

union GD_SECTNUM_T {
  uint32_t full;
  struct {
    uint32_t status : 4;
    uint32_t format : 4;
    uint32_t reserved : 24;
  };
};

union GD_STATUS_T {
  uint32_t full;
  struct {
    uint32_t CHECK : 1;  // Becomes "1" when an error has occurred during
                         // execution of the command the previous time.
    uint32_t res : 1;    // Reserved
    uint32_t CORR : 1;   // Indicates that a correctable error has occurred.
    uint32_t DRQ : 1;    // Becomes "1" when preparations for data transfer
    // between drive and host are completed. Information held
    // in the Interrupt Reason Register becomes valid in the
    // packet command when DRQ is set.
    uint32_t DSC : 1;   // Becomes "1" when seek processing is completed.
    uint32_t DF : 1;    // Returns drive fault information.
    uint32_t DRDY : 1;  // Set to "1" when the drive is able to respond to an
                        // ATA command.
    uint32_t BSY : 1;   // BSY is always set to "1" when the drive accesses the
                        // command block.
    uint32_t reserved : 24;
  };
};

union GD_BYTECT_T {
  uint32_t full;
  struct {
    uint32_t lo : 8;
    uint32_t hi : 8;
    uint32_t reserved : 16;
  };
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

enum DataMask {
  MASK_HEADER = 0x8,
  MASK_SUBHEADER = 0x4,
  MASK_DATA = 0x2,
  MASK_OTHER = 0x1
};

class GDROM {
 public:
  GDROM(emu::Dreamcast *dc);
  ~GDROM();

  void Init();

  void SetDisc(std::unique_ptr<Disc> disc);

  uint8_t ReadRegister8(uint32_t addr);
  uint16_t ReadRegister16(uint32_t addr);
  uint32_t ReadRegister32(uint32_t addr);

  void WriteRegister8(uint32_t addr, uint8_t value);
  void WriteRegister16(uint32_t addr, uint16_t value);
  void WriteRegister32(uint32_t addr, uint32_t value);

 private:
  void TriggerEvent(GDEvent ev);
  void TriggerEvent(GDEvent ev, intptr_t arg0, intptr_t arg1);
  void ProcessATACommand(ATACommand cmd);
  void ProcessSPICommand(uint8_t *data);

  void GetTOC(AreaType area_type, TOC *toc);
  void GetSession(int session, Session *ses);
  void GetSubcode(int format, uint8_t *data);
  int ReadSectors(int fad, SectorFormat format, DataMask mask, int num_sectors,
                  uint8_t *dst);

  emu::Dreamcast *dc_;
  hw::Memory *memory_;
  hw::holly::Holly *holly_;
  emu::Register *holly_regs_;

  GD_FEATURES_T features_;
  GD_INTREASON_T intreason_;
  GD_SECTNUM_T sectnum_;
  GD_BYTECT_T byte_count_;
  GD_STATUS_T status_;

  uint8_t pio_buffer_[0xfa00];
  int pio_idx_;
  int pio_size_;
  uint8_t *dma_buffer_;
  int dma_size_;
  GDState state_;
  int spi_read_offset_;

  std::unique_ptr<Disc> current_disc_;
};
}
}
}

#endif
