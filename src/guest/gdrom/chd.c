#include <chd.h>
#include "core/core.h"
#include "guest/gdrom/disc.h"
#include "guest/gdrom/gdrom_types.h"

struct chd {
  struct disc;

  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;

  chd_file *chd;
  /* hunk data cache */
  uint8_t *hunkmem;
  /* last hunknum read */
  int oldhunk;
};

static void chd_read_sector(struct disc *disc, struct track *track, int fad,
                            void *dst) {
  struct chd *chd = (struct chd *)disc;
  const chd_header *head = chd_get_header(chd->chd);

  int cad = fad - track->file_offset;
  int hunknum = (cad * head->unitbytes) / head->hunkbytes;
  int hunkofs = (cad * head->unitbytes) % head->hunkbytes;

  /* each hunk holds ~8 sectors, optimize when reading contiguous sectors */
  if (hunknum != chd->oldhunk) {
    int err = chd_read(chd->chd, hunknum, chd->hunkmem);
    CHECK_EQ(err, CHDERR_NONE, "chd_read_sector failed fad=%d", fad);
  }

  memcpy(dst, chd->hunkmem + hunkofs + track->header_size, 2048);
}

static void chd_get_toc(struct disc *disc, int area, struct track **first_track,
                        struct track **last_track, int *leadin_fad,
                        int *leadout_fad) {
  struct chd *chd = (struct chd *)disc;

  /* chd's have one toc per area, and there is one session per area */
  struct session *session = &chd->sessions[area];

  *first_track = &chd->tracks[session->first_track];
  *last_track = &chd->tracks[session->last_track];
  *leadin_fad = session->leadin_fad;
  *leadout_fad = session->leadout_fad;
}

static struct track *chd_get_track(struct disc *disc, int n) {
  struct chd *chd = (struct chd *)disc;
  CHECK_LT(n, chd->num_tracks);
  return &chd->tracks[n];
}

static int chd_get_num_tracks(struct disc *disc) {
  struct chd *chd = (struct chd *)disc;
  return chd->num_tracks;
}

static struct session *chd_get_session(struct disc *disc, int n) {
  struct chd *chd = (struct chd *)disc;
  CHECK_LT(n, chd->num_sessions);
  return &chd->sessions[n];
}

static int chd_get_num_sessions(struct disc *disc) {
  struct chd *chd = (struct chd *)disc;
  return chd->num_sessions;
}

static int chd_get_format(struct disc *disc) {
  return GD_DISC_GDROM;
}

static void chd_destroy(struct disc *disc) {
  struct chd *chd = (struct chd *)disc;

  free(chd->hunkmem);

  chd_close(chd->chd);
}

static int chd_parse(struct disc *disc, const char *filename, int verbose) {
  struct chd *chd = (struct chd *)disc;

  chd_error err = chd_open(filename, CHD_OPEN_READ, 0, &chd->chd);
  if (err != CHDERR_NONE) {
    return 0;
  }

  /* allocate storage for sector reads */
  const chd_header *head = chd_get_header(chd->chd);
  chd->hunkmem = malloc(head->hunkbytes);
  chd->oldhunk = -1;

  /* parse tracks */
  char tmp[512];
  int cad = 0;
  int fad = GDROM_PREGAP;

  while (1) {
    int tkid = 0, frames = 0, pad = 0, pregap = 0, postgap = 0;
    char type[64], subtype[32], pgtype[32], pgsub[32];

    /* try to read the new v5 metadata tag */
    err = chd_get_metadata(chd->chd, GDROM_TRACK_METADATA_TAG, chd->num_tracks,
                           tmp, sizeof(tmp), NULL, NULL, NULL);
    if (err == CHDERR_NONE) {
      sscanf(tmp, GDROM_TRACK_METADATA_FORMAT, &tkid, type, subtype, &frames,
             &pad, &pregap, pgtype, pgsub, &postgap);
    } else {
      /* try to read the v3/v4 metadata tag */
      err =
          chd_get_metadata(chd->chd, CDROM_TRACK_METADATA2_TAG, chd->num_tracks,
                           tmp, sizeof(tmp), NULL, NULL, NULL);
      if (err == CHDERR_NONE) {
        sscanf(tmp, CDROM_TRACK_METADATA2_FORMAT, &tkid, type, subtype, &frames,
               &pregap, pgtype, pgsub, &postgap);
      } else {
        /* try to read the old v3/v4 metadata tag */
        err = chd_get_metadata(chd->chd, CDROM_TRACK_METADATA_TAG,
                               chd->num_tracks, tmp, sizeof(tmp), NULL, NULL,
                               NULL);

        if (err == CHDERR_NONE) {
          sscanf(tmp, CDROM_TRACK_METADATA_FORMAT, &tkid, type, subtype,
                 &frames);
        } else {
          /* if there's no valid metadata, this is the end of the TOC */
          break;
        }
      }
    }

    /* sanity checks */
    CHECK_EQ(tkid, chd->num_tracks + 1);

    if (strcmp(subtype, "NONE")) {
      LOG_WARNING("chd_parse track subtype %s unsupported", subtype);
      return 0;
    }

    if (pregap != 0 || postgap != 0) {
      LOG_WARNING("chd_parse expected zero-length pre and postgap", type);
      return 0;
    }

    /* figure out sector type */
    int sector_mode = 0;
    int sector_size = 0;

    if (!strcmp(type, "AUDIO")) {
      sector_mode = 0;
      sector_size = 2352;
    } else if (!strcmp(type, "MODE1")) {
      sector_mode = 1;
      sector_size = 2336;
    } else if (!strcmp(type, "MODE1_RAW")) {
      sector_mode = 1;
      sector_size = 2352;
    } else {
      LOG_WARNING("chd_parse unexpected mode %s", type);
      return 0;
    }

    /* add track */
    CHECK_LT(chd->num_tracks, ARRAY_SIZE(chd->tracks));
    struct track *track = &chd->tracks[chd->num_tracks++];

    if (!track_set_layout(track, sector_mode, sector_size)) {
      LOG_WARNING("chd_parse unsupported track layout mode=%d sector_size=%d",
                  sector_mode, sector_size);
      return 0;
    }

    track->num = chd->num_tracks;
    track->fad = fad;
    track->ctrl = strcmp(type, "AUDIO") == 0 ? 0 : 4;
    track->file_offset = fad - cad;

    if (verbose) {
      LOG_INFO("chd_parse '%s' track=%d fad=%d secsz=%d", tmp, track->num,
               track->fad, track->sector_size);
    }

    /* chd block addresses are padded to a 4-frame boundary */
    cad += ALIGN_UP(frames, 4);
    fad += frames;
  }

  /* gdroms contains two sessions, one for the single density area (tracks 0-1)
     and one for the high density area (tracks 3+) */
  chd->num_sessions = 2;

  /* single density area starts at 00:00:00 (fad 0x0) and can hold up to 4
     minutes of data (18,000 sectors at 75 sectors per second) */
  struct session *single = &chd->sessions[0];
  single->leadin_fad = 0x0;
  single->leadout_fad = 0x4650;
  single->first_track = 0;
  single->last_track = 0;

  /* high density area starts at 10:00:00 (fad 0xb05e) and can hold up to
     504,300 sectors (112 minutes, 4 seconds at 75 sectors per second) */
  struct session *high = &chd->sessions[1];
  high->leadin_fad = 0xb05e;
  high->leadout_fad = 0x861b4;
  high->first_track = 2;
  high->last_track = chd->num_tracks - 1;

  return 1;
}

struct disc *chd_create(const char *filename, int verbose) {
  struct chd *chd = calloc(1, sizeof(struct chd));

  chd->destroy = &chd_destroy;
  chd->get_format = &chd_get_format;
  chd->get_num_sessions = &chd_get_num_sessions;
  chd->get_session = &chd_get_session;
  chd->get_num_tracks = &chd_get_num_tracks;
  chd->get_track = &chd_get_track;
  chd->get_toc = &chd_get_toc;
  chd->read_sector = &chd_read_sector;

  struct disc *disc = (struct disc *)chd;

  if (!chd_parse(disc, filename, verbose)) {
    chd_destroy(disc);
    return NULL;
  }

  return disc;
}
