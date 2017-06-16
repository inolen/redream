#ifndef ISO_H
#define ISO_H

enum {
  ISO_PVD_SECTOR = 16,
};

/* iso 9660 file flags */
enum {
  ISO_HIDDEN = 0x1,
  ISO_DIRECTORY = 0x2,
  ISO_ASSOCIATED = 0x4,
  ISO_RECORD = 0x8,
  ISO_PROTECTION = 0x10,
  ISO_RESERVED1 = 0x20,
  ISO_RESERVED2 = 0x40,
  ISO_MULTIEXTENT = 0x80,
};

/* iso 9660 data types */
typedef char achar_t;

typedef char dchar_t;

typedef uint8_t iso711_t;

typedef int8_t iso712_t;

typedef struct iso723 {
  uint16_t le;
  uint16_t be;
} iso723_t;

typedef uint32_t iso731_t;

typedef uint32_t iso732_t;

typedef struct iso733 {
  uint32_t le;
  uint32_t be;
} iso733_t;

/* iso 9660 data structures */
#pragma pack(push, 1)

struct iso_ltime {
  char year[4];
  char month[2];
  char day[2];
  char hour[2];
  char minute[2];
  char second[2];
  char fractions[2];
  iso712_t gmt_offset;
};

struct iso_dir {
  iso711_t length;
  iso711_t xa_length;
  iso733_t extent;
  iso733_t size;
  iso711_t date[7];
  iso711_t file_flags;
  iso711_t file_unit_size;
  iso711_t interleave_gap;
  iso723_t volume_sequence_number;
  iso711_t name_len;
};

struct iso_pvd {
  iso711_t type;
  unsigned char id[5];
  iso711_t version;
  uint8_t unused1;
  achar_t system_id[32];
  dchar_t volume_id[32];
  uint8_t unused2[8];
  iso733_t volume_space_size;
  uint8_t unused3[32];
  iso723_t volume_set_size;
  iso723_t volume_sequence_number;
  iso723_t logical_block_size;
  iso733_t path_table_size;
  iso731_t type_l_path_table;
  iso731_t opt_type_l_path_table;
  iso732_t type_m_path_table;
  iso732_t opt_type_m_path_table;
  struct iso_dir root_directory_record;
  char root_directory_name;
  dchar_t volume_set_id[128];
  achar_t publisher_id[128];
  achar_t preparer_id[128];
  achar_t application_id[128];
  dchar_t copyright_file_id[37];
  dchar_t abstract_file_id[37];
  dchar_t bibliographic_file_id[37];
  struct iso_ltime creation_date;
  struct iso_ltime modification_date;
  struct iso_ltime expiration_date;
  struct iso_ltime effective_date;
  iso711_t file_structure_version;
  uint8_t unused4;
  uint8_t application_data[512];
  uint8_t unused5[653];
};

#pragma pack(pop)

#endif
