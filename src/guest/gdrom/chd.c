#include "core/assert.h"
#include "guest/gdrom/disc.h"
#include "guest/gdrom/gdrom_types.h"

#include <chdr.h>

struct chd {
  struct disc;
  
  struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;
  
  chd_file* chd;
  uint8_t* hunk_mem;
  int old_hunk;

  int hunkbytes;
  int sph;
};


static int chd_read_sector(struct disc *disc, int fad, int sector_fmt,
                           int sector_mask, void *dst) {
  struct chd *chd = (struct chd *)disc;

  struct track *track = disc_lookup_track(disc, fad);
  CHECK_NOTNULL(track);
  CHECK(sector_fmt == GD_SECTOR_ANY || sector_fmt == track->sector_fmt);
  CHECK(sector_mask == GD_MASK_DATA);


  int fad_offs=fad-track->fad;
  int hunk=(fad_offs)/chd->sph + track->file_offset;
  if (chd->old_hunk!=hunk)
  {
    chd_read(chd->chd,hunk,chd->hunk_mem); //CHDERR_NONE
  }

  int hunk_ofs=fad_offs%chd->sph;

  memcpy(dst,chd->hunk_mem+hunk_ofs*(2352+96) + 16,2048);
  
  // CHECK_EQ(sector_fmt, GD_SECTOR_M1);
    
  return 2048;
}

static int chd_read_sectors(struct disc *disc, int fad, int num_sectors,
                            int sector_fmt, int sector_mask, void *dst,
                            int dst_size) {
	int read = 0;
	uint8_t* ptr = (uint8_t*)dst;
  	for (int i = 0; i < num_sectors; i++) {
		int r = chd_read_sector(disc, fad, sector_fmt, sector_mask, (void*)ptr);
		ptr += r;
		read += r;
		fad++;
	}
	return read;
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

  free(chd->hunk_mem);
  chd_close(chd->chd);
}

static int chd_parse(struct disc *disc, const char *filename) {
  struct chd *chd = (struct chd *)disc;
         
  chd_error err=chd_open(filename,CHD_OPEN_READ,0,&chd->chd);
  
  if (err!=CHDERR_NONE)
    return 0;
 
 
  const chd_header* head = chd_get_header(chd->chd);

  chd->hunkbytes = head->hunkbytes;
	chd->hunk_mem = malloc(chd->hunkbytes);
	chd->old_hunk=0xFFFFFFF;

	chd->sph = chd->hunkbytes/(2352+96);

	if (chd->hunkbytes%(2352+96)!=0) 
	{
		LOG_FATAL("chd: hunkbytes is invalid, %d\n",chd->hunkbytes);
	}


	uint32_t tag;
	uint8_t flags;
	char temp[512];
	uint32_t temp_len;
	uint32_t total_frames=150;

	uint32_t total_secs=0;
	uint32_t total_hunks=0;

  chd->num_tracks = 0;

	for(;;)
	{
		char type[64],subtype[32]="NONE",pgtype[32],pgsub[32];
		int tkid,frames,pregap=0,postgap=0;
		err=chd_get_metadata(chd->chd,CDROM_TRACK_METADATA2_TAG,chd->num_tracks,temp,sizeof(temp),&temp_len,&tag,&flags);
		if (err==CHDERR_NONE)
		{
			//"TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"
			sscanf(temp,CDROM_TRACK_METADATA2_FORMAT,&tkid,type,subtype,&frames,&pregap,pgtype,pgsub,&postgap);
		}
		else if (CHDERR_NONE== (err=chd_get_metadata(chd->chd,CDROM_TRACK_METADATA_TAG,chd->num_tracks,temp,sizeof(temp),&temp_len,&tag,&flags)) )
		{
			//CDROM_TRACK_METADATA_FORMAT	"TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d"
			sscanf(temp,CDROM_TRACK_METADATA_FORMAT,&tkid,type,subtype,&frames);
		}
		else
		{
			LOG_INFO("chd: Unable to find metadata, %d (end of toc?)\n", err);
			break;
		}

		if (tkid!=(chd->num_tracks+1) || (strcmp(type,"MODE1_RAW")!=0 && strcmp(type,"AUDIO")!=0 && strcmp(type,"MODE1")!=0) || strcmp(subtype,"NONE")!=0 || pregap!=0 || postgap!=0)
		{
			LOG_FATAL("chd: track type %s is not supported\n",type);
			return 0;
		}

    CHECK_LT(chd->num_tracks, array_size(chd->tracks));
    struct track *track = &chd->tracks[chd->num_tracks++];
    track->num = chd->num_tracks;
    track->fad = total_frames;
    track->ctrl = strcmp(type,"AUDIO")==0?0:4;
    track->sector_fmt = strcmp(type,"AUDIO") == 0 ? GD_SECTOR_CDDA:GD_SECTOR_M1;
    track->sector_size = strcmp(type,"MODE1") == 0 ? 2048:2352;
    track->file_offset = total_hunks;
    
    LOG_INFO("chd_parse '%s' track=%d filename='%s' fad=%d secsz=%d", temp, track->num,
         track->filename, track->fad, track->sector_size);
             
    total_frames+=frames;
    
    total_hunks+=frames/chd->sph;
    if (frames%chd->sph)
      total_hunks++;
  
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

struct disc *chd_create(const char *filename) {
  struct chd *chd = calloc(1, sizeof(struct chd));

  chd->destroy = &chd_destroy;
  chd->get_format = &chd_get_format;
  chd->get_num_sessions = &chd_get_num_sessions;
  chd->get_session = &chd_get_session;
  chd->get_num_tracks = &chd_get_num_tracks;
  chd->get_track = &chd_get_track;
  chd->get_toc = &chd_get_toc;
  chd->read_sectors = &chd_read_sectors;

  struct disc *disc = (struct disc *)chd;

  if (!chd_parse(disc, filename)) {
    chd_destroy(disc);
    return NULL;
  }

  return disc;
}
