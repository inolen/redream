#ifndef GDROM_TYPES_H
#define GDROM_TYPES_H

enum gd_drive_status {
  DST_BUSY,     /* State transition */
  DST_PAUSE,    /* Pause */
  DST_STANDBY,  /* Standby (drive stop) */
  DST_PLAY,     /* CD playback */
  DST_SEEK,     /* Seeking */
  DST_SCAN,     /* Scanning */
  DST_OPEN,     /* Tray is open */
  DST_NODISC,   /* No disc */
  DST_RETRY,    /* Read retry in progress (option) */
  DST_ERROR,    /* Reading of disc TOC failed (state does not allow access) */
};

enum gd_disc {
  DISC_CDDA = 0x00,
  DISC_CDROM = 0x01,
  DISC_CDROM_XA = 0x02,
  DISC_CDROM_EX = 0x03,
  DISC_CDROM_CDI = 0x04,
  DISC_GDROM = 0x08
};

/* ata / spi commands */
enum gd_ata_cmd {
  ATA_NOP = 0x00,
  ATA_SOFT_RESET = 0x08,
  ATA_EXEC_DIAG = 0x90,
  ATA_PACKET = 0xa0,
  ATA_IDENTIFY_DEV = 0xa1,
  ATA_SET_FEATURES = 0xef,
};

enum gd_spi_cmd {
  SPI_TEST_UNIT = 0x00,  /* Verify access readiness */
  SPI_REQ_STAT = 0x10,   /* Get CD status */
  SPI_REQ_MODE = 0x11,   /* Get various settings */
  SPI_SET_MODE = 0x12,   /* Make various settings */
  SPI_REQ_ERROR = 0x13,  /* Get error details */
  SPI_GET_TOC = 0x14,    /* Get all TOC data */
  SPI_REQ_SES = 0x15,    /* Get specified session data */
  SPI_CD_OPEN = 0x16,    /* Open tray */
  SPI_CD_PLAY = 0x20,    /* Play CD */
  SPI_CD_SEEK = 0x21,    /* Seek for playback position */
  SPI_CD_SCAN = 0x22,    /* Perform scan */
  SPI_CD_READ = 0x30,    /* Read CD */
  SPI_CD_READ2 = 0x31,   /* CD read (pre-read position) */
  SPI_GET_SCD = 0x40,    /* Get subcode */
  SPI_UNKNOWN_70 = 0x70,
  SPI_UNKNOWN_71 = 0x71,
};

enum gd_area {
  AREA_SINGLE,
  AREA_DOUBLE,
};

enum gd_audio_status {
  AST_INVALID = 0x00,
  AST_INPROGRESS = 0x11,
  AST_PAUSED = 0x12,
  AST_ENDED = 0x13,
  AST_ERROR = 0x14,
  AST_NOSTATUS = 0x15,
};

enum gd_secmask {
  MASK_HEADER = 0x8,
  MASK_SUBHEADER = 0x4,
  MASK_DATA = 0x2,
  MASK_OTHER = 0x1
};

enum gd_secfmt {
  SECTOR_ANY,
  SECTOR_CDDA,
  SECTOR_M1,
  SECTOR_M2,
  SECTOR_M2F1,
  SECTOR_M2F2,
  SECTOR_M2_NOXA
};

union gd_tocentry {
  uint32_t full;
  struct {
    uint32_t adr : 4;
    uint32_t ctrl : 4;
    uint32_t fad : 24;
  };
};

struct gd_toc {
  union gd_tocentry entries[99];
  union gd_tocentry start;
  union gd_tocentry end;
  union gd_tocentry leadout;
};

struct gd_session {
  uint8_t status : 8;
  uint8_t reserved : 8;
  uint8_t first_track : 8;
  uint32_t start_fad : 24;
};

union gd_features {
  uint32_t full;
  struct {
    uint32_t dma : 1;
    uint32_t reserved : 31;
  };
};

union gd_intreason {
  uint32_t full;
  struct {
    uint32_t CoD : 1;  /* "0" indicates data and "1" indicates a command. */
    uint32_t IO : 1;   /* "1" indicates transfer from device to host, and "0"
                          from host to device. */
    uint32_t reserved : 30;
  };
};

union gd_sectnum {
  uint32_t full;
  struct {
    uint32_t status : 4;
    uint32_t format : 4;
    uint32_t reserved : 24;
  };
};

union gd_status {
  uint32_t full;
  struct {
    uint32_t CHECK : 1; /* Becomes "1" when an error has occurred during
                           execution of the command the previous time. */
    uint32_t res : 1;   /* Reserved */
    uint32_t CORR : 1;  /* Indicates that a correctable error has occurred. */
    uint32_t DRQ : 1;   /* Becomes "1" when preparations for data transfer
                           between drive and host are completed. Information held
                           in the Interrupt Reason Register becomes valid in the
                           packet command when DRQ is set. */
    uint32_t DSC : 1;   /* Becomes "1" when seek processing is completed. */
    uint32_t DF : 1;    /* Returns drive fault information. */
    uint32_t DRDY : 1;  /* Set to "1" when the drive is able to respond to an
                           ATA command. */
    uint32_t BSY : 1;   /* BSY is always set to "1" when the drive accesses the
                           command block. */
    uint32_t reserved : 24;
  };
};

union gd_bytect {
  uint32_t full;
  struct {
    uint32_t lo : 8;
    uint32_t hi : 8;
    uint32_t reserved : 16;
  };
};

#endif
