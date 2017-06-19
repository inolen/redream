#ifndef GDROM_TYPES_H
#define GDROM_TYPES_H

#include <stdint.h>

#define GDROM_PREGAP 150

enum {
  GD_STATUS_BUSY = 0x00,    /* State transition */
  GD_STATUS_PAUSE = 0x01,   /* Pause */
  GD_STATUS_STANDBY = 0x02, /* Standby (drive stop) */
  GD_STATUS_PLAY = 0x03,    /* CD playback */
  GD_STATUS_SEEK = 0x04,    /* Seeking */
  GD_STATUS_SCAN = 0x05,    /* Scanning */
  GD_STATUS_OPEN = 0x06,    /* Tray is open */
  GD_STATUS_NODISC = 0x07,  /* No disc */
  GD_STATUS_RETRY = 0x08,   /* Read retry in progress */
  GD_STATUS_ERROR = 0x09,   /* Reading of disc TOC failed */
};

enum {
  GD_DISC_CDDA = 0x00,
  GD_DISC_CDROM = 0x01,
  GD_DISC_CDROM_XA = 0x02,
  GD_DISC_CDROM_CDI = 0x03,
  GD_DISC_GDROM = 0x08,
};

/* ata / spi commands */
enum {
  GD_ATA_NOP = 0x00,
  GD_ATA_SOFT_RESET = 0x08,
  GD_ATA_EXEC_DIAG = 0x90,
  GD_ATA_PACKET_CMD = 0xa0,
  GD_ATA_IDENTIFY_DEV = 0xa1,
  GD_ATA_SET_FEATURES = 0xef,
};

enum {
  GD_SPI_TEST_UNIT = 0x00, /* Verify access readiness */
  GD_SPI_REQ_STAT = 0x10,  /* Get CD status */
  GD_SPI_REQ_MODE = 0x11,  /* Get various settings */
  GD_SPI_SET_MODE = 0x12,  /* Make various settings */
  GD_SPI_REQ_ERROR = 0x13, /* Get error details */
  GD_SPI_GET_TOC = 0x14,   /* Get all TOC data */
  GD_SPI_REQ_SES = 0x15,   /* Get specified session data */
  GD_SPI_CD_OPEN = 0x16,   /* Open tray */
  GD_SPI_CD_PLAY = 0x20,   /* Play CD */
  GD_SPI_CD_SEEK = 0x21,   /* Seek for playback position */
  GD_SPI_CD_SCAN = 0x22,   /* Perform scan */
  GD_SPI_CD_READ = 0x30,   /* Read CD */
  GD_SPI_CD_READ2 = 0x31,  /* CD read (pre-read position) */
  GD_SPI_GET_SCD = 0x40,   /* Get subcode */
  GD_SPI_CHK_SECU = 0x70,  /* Perform disk security check */
  GD_SPI_REQ_SECU = 0x71,  /* Get security check result */
};

enum {
  GD_SPI_CMD_SIZE = 12,
  GD_SPI_ERR_SIZE = 10,
  GD_SPI_TOC_SIZE = 408,
  GD_SPI_SES_SIZE = 6,
  GD_SPI_STAT_SIZE = 10,
  GD_SPI_SCD_SIZE = 100,
};

enum {
  GD_AREA_SINGLE,
  GD_AREA_HIGH,
};

enum {
  GD_AUDIO_INVALID = 0x00,
  GD_AUDIO_INPROGRESS = 0x11,
  GD_AUDIO_PAUSED = 0x12,
  GD_AUDIO_ENDED = 0x13,
  GD_AUDIO_ERROR = 0x14,
  GD_AUDIO_NOSTATUS = 0x15,
};

enum {
  GD_MASK_OTHER = 0x1,
  GD_MASK_DATA = 0x2,
  GD_MASK_SUBHEADER = 0x4,
  GD_MASK_HEADER = 0x8,
};

enum {
  GD_SECTOR_ANY,
  GD_SECTOR_CDDA,
  GD_SECTOR_M1,
  GD_SECTOR_M2,
  GD_SECTOR_M2F1,
  GD_SECTOR_M2F2,
  GD_SECTOR_M2_NOXA,
};

enum {
  GD_SEEK_FAD = 0x1,
  GD_SEEK_MSF = 0x2,
  GD_SEEK_STOP = 0x3,
  GD_SEEK_PAUSE = 0x4,
};

/* internal registers accessed through holly */
union gd_error {
  uint32_t full;
  struct {
    uint32_t ILI : 1;
    uint32_t EOMF : 1;
    uint32_t ABRT : 1;
    uint32_t MCR : 1;
    uint32_t sense_key : 4;
    uint32_t : 24;
  };
};

union gd_features {
  uint32_t full;
  struct {
    uint32_t dma : 1;
    uint32_t : 31;
  };
};

union gd_intreason {
  uint32_t full;
  struct {
    uint32_t CoD : 1; /* "0" indicates data and "1" indicates a command. */
    uint32_t IO : 1;  /* "1" indicates transfer from device to host, and "0"
                         from host to device. */
    uint32_t : 30;
  };
};

union gd_sectnum {
  uint32_t full;
  struct {
    uint32_t status : 4;
    uint32_t format : 4;
    uint32_t : 24;
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
    uint32_t : 24;
  };
};

union gd_bytect {
  uint32_t full;
  struct {
    uint32_t lo : 8;
    uint32_t hi : 8;
    uint32_t : 16;
  };
};

/* hardware information modified through REQ_MODE / SET_MODE */
struct gd_hw_info {
  uint8_t padding0[2];
  uint8_t speed;
  uint8_t padding1;
  uint8_t standby_hi;
  uint8_t standby_lo;
  uint8_t read_flags;
  uint8_t padding2[2];
  uint8_t read_retry;
  char drive_info[8];
  char system_version[8];
  char system_date[6];
};

#endif
