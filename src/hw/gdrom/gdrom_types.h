#ifndef GDROM_TYPES_H
#define GDROM_TYPES_H

namespace re {
namespace hw {
namespace gdrom {

// registers
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

}
}
}

#endif
