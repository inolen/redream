#include "hw/gdrom/gdi.h"
#include "core/assert.h"

static const int GDI_PREGAP_SIZE = 150;

#define CDI_V2  0x80000004
#define CDI_V3  0x80000005
#define CDI_V35 0x80000006

struct cdi_track {
  struct track;
  uint32_t length, total_length;
  uint32_t file_offset;
  uint16_t sector_size;
};

struct cdi {
  struct disc;
  struct cdi_track tracks[64];

  FILE * fd;
  unsigned filesize;

  // CDI specific fields
  uint32_t version, header_offset;
  uint16_t sessions, numtracks;
};

static int cdi_get_num_tracks(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;
  return cdi->numtracks;
}

static struct track *cdi_get_track(struct disc *disc, int n) {
  struct cdi *cdi = (struct cdi *)disc;
  return (struct track*)&cdi->tracks[n];
}

static int cdi_read_sector(struct disc *disc, int fad, void *dst) {
  struct cdi *cdi = (struct cdi *)disc;

  // find the track to read from
  struct cdi_track *track = NULL;
  for (int i = 0; i < cdi->numtracks; i++) {
    struct cdi_track *curr_track = &cdi->tracks[i];
    struct cdi_track *next_track =
        i < cdi->numtracks - 1 ? &cdi->tracks[i + 1] : NULL;

    if (fad >= curr_track->fad && (!next_track || fad < next_track->fad)) {
      track = curr_track;
      break;
    }
  }
  CHECK_NOTNULL(track);

  // Seek to the right track and position
  LOG_INFO("Access to %d Seeking at %d", fad, track->file_offset + fad * SECTOR_SIZE, SEEK_SET);
  int res =
      fseek(cdi->fd, track->file_offset + fad * SECTOR_SIZE, SEEK_SET);
  CHECK_EQ(res, 0);

  res = (int)fread(dst, SECTOR_SIZE, 1, cdi->fd);
  CHECK_EQ(res, 1);

  return 1;
}

static void cdi_destroy(struct disc *disc) {
  struct cdi *cdi = (struct cdi *)disc;
  if (cdi->fd)
    fclose(cdi->fd);
}

static struct cdi *cdi_create(const char *filename) {
  struct cdi *cdi = calloc(1, sizeof(struct cdi));

  cdi->destroy = &cdi_destroy;
  cdi->get_num_tracks = &cdi_get_num_tracks;
  cdi->get_track = &cdi_get_track;
  cdi->read_sector = &cdi_read_sector;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    free(cdi);
    return NULL;
  }
  cdi->fd = fp;

  // Parse the CDI headers
  fseek(fp, 0, SEEK_END);
  cdi->filesize = ftell(fp);

  if (cdi->filesize < 8) {
    fclose(fp);
    free(cdi);
    return NULL;
  }

  fseek(fp, cdi->filesize - 8, SEEK_SET);
  fread(&cdi->version, 4, 1, fp);
  fread(&cdi->header_offset, 4, 1, fp);

  // Check header values
  if (cdi->header_offset == 0) {
    LOG_WARNING("CDI header_offset == 0, bad image?");
    fclose(fp);
    free(cdi);
    return NULL;
  }

  switch (cdi->version) {
  case CDI_V2:
    LOG_INFO("CDI file version 2 detected");
    break;
  case CDI_V3:
    LOG_INFO("CDI file version 3 detected");
    break;
  case CDI_V35:
    LOG_INFO("CDI file version 3.5 detected");
    break;
  default:
    LOG_WARNING("CDI file version unknown %X", cdi->version);
    fclose(fp);
    free(cdi);
    return NULL;
  };

  // Read sessions, for 3.5 offset counts from file EOF
  if (cdi->version == CDI_V35)
    fseek(fp, (cdi->filesize - cdi->header_offset), SEEK_SET);
  else
    fseek(fp, cdi->header_offset, SEEK_SET);

  fread(&cdi->sessions, 2, 1, fp);

  if (cdi->sessions == 0) {
    LOG_WARNING("CDI disc has zero sessions, cannot load");
    fclose(fp);
    free(cdi);
    return NULL;
  }
  else {
    LOG_INFO("CDI disc found %u sessions", (unsigned int)cdi->sessions);
  }

  uint32_t position = 0, currtrack = 0;
  for (unsigned snum = 0; snum < cdi->sessions; snum++) {
    // Read num tracks
    uint16_t numtracks_session;
    fread(&numtracks_session, 2, 1, fp);
    cdi->numtracks += numtracks_session;

    long header_position = ftell(fp);

    if (numtracks_session > 0) {
      for (unsigned tnum = 0; tnum < numtracks_session; tnum++) {
        struct cdi_track *track = &cdi->tracks[currtrack];
        track->num = ++currtrack;

        const char start_mark[10] = {0, 0, 1, 0, 0, 0, 255, 255, 255, 255};
        char current_start_mark[10];

        uint32_t tval;        
        fread(&tval, 4, 1, fp);
        if (tval != 0)
           fseek(fp, 8, SEEK_CUR); // extra data (DJ 3.00.780 and up)

        fread(&current_start_mark, 10, 1, fp);
        if (memcmp(start_mark, current_start_mark, 10))
          LOG_WARNING("Mark does not match");

        fread(&current_start_mark, 10, 1, fp);
        if (memcmp(start_mark, current_start_mark, 10))
          LOG_WARNING("Mark does not match");

        fseek(fp, 4, SEEK_CUR);
        uint8_t fnlen;
        fread(&fnlen, 1, 1, fp);
        fseek(fp, fnlen + 11 + 4 + 4, SEEK_CUR);  // Skip filename + other fields

        fread(&tval, 4, 1, fp);
        if (tval == 0x80000000)
          fseek(fp, 8, SEEK_CUR); // DJ4

        fseek(fp, 2, SEEK_CUR);
        uint32_t pregap_length, mode, lba, sectorsize_idx;
        fread(&pregap_length, 4, 1, fp);
        fread(&track->length, 4, 1, fp);
        fseek(fp, 6, SEEK_CUR);
        fread(&mode, 4, 1, fp);
        fseek(fp, 12, SEEK_CUR);
        fread(&lba, 4, 1, fp);
        fread(&track->total_length, 4, 1, fp);
        fseek(fp, 16, SEEK_CUR);
        fread(&sectorsize_idx, 4, 1, fp);

        switch(sectorsize_idx) {
          case 0 : track->sector_size = 2048; break;
          case 1 : track->sector_size = 2336; break;
          case 2 : track->sector_size = 2352; break;
          default: break; // error_exit(ERR_GENERIC, "Unsupported sector size");
        }

        if (mode > 2)
          LOG_WARNING("Track mode %u is unknown, assuming it's data", mode);

        if (pregap_length != 150 && pregap_length != 0)
          LOG_WARNING("Non-standard pregap size %u!", pregap_length);

        track->fad = lba + pregap_length;
        track->ctrl = (mode == 0) ? 0 : 4;  // CDI mode = 0 means audio, otherwise 4 (data)
        //track->adr = 0; // WTF not used?
        track->file_offset = position - track->fad * SECTOR_SIZE + pregap_length * SECTOR_SIZE;

        fseek(fp, 29, SEEK_CUR);
        if (cdi->version != CDI_V2) {
          fseek(fp, 5, SEEK_CUR);
          fread(&tval, 4, 1, fp);
          if (tval == 0xffffffff)
            fseek(fp, 78, SEEK_CUR); // extra data (DJ 3.00.780 and up)
        }

        LOG_INFO("CDI session %d: track number %d, LBA %d", snum, track->num, lba);
        position += track->total_length * track->sector_size;
       }
    }

    // Jump to the next session
    fseek(fp, 4 + 8 + (cdi->version != CDI_V2 ? 1 : 0), SEEK_CUR);
  }

  return cdi;
}

struct disc *disc_create_cdi(const char *filename) {
  return (struct disc *)cdi_create(filename);
}

