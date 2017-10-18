/*
 * the HOLLY contained two graphics-related units:
 *
 * 1.) the tile accelerator. the ta acted as a frontend which received data from
 *     programs and converted / sanitized this data into display lists which
 *     were written back out to texture memory
 * 2.) the core. the core acted as the backend, which took the display lists
 *     generated by the ta, rendered them, and wrote the results out to the
 *     framebuffer
 *
 * in our world, the display list generation used by the ta and core hardware
 * is not emulated. instead, the parameters submitted to the ta are recorded
 * to our ta_context structures, which are later converted to an appropriate
 * format for the host's render backend in tr.c
 *
 * this file is responsible for processing the data fed to the ta into our
 * internal ta_context format, and passing these contexts to the host for
 * rendering when initiated
 */

#include "guest/pvr/ta.h"
#include "core/core.h"
#include "core/exception_handler.h"
#include "core/filesystem.h"
#include "core/list.h"
#include "guest/holly/holly.h"
#include "guest/memory.h"
#include "guest/pvr/pvr.h"
#include "guest/pvr/tr.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "stats.h"

struct ta {
  struct device;
  uint8_t *vram;

  /* yuv data converter state */
  uint8_t *yuv_data;
  int yuv_width;
  int yuv_height;
  int yuv_macroblock_size;
  int yuv_macroblock_count;

  /* tile context pool */
  struct ta_context contexts[8];
  struct list free_contexts;
  struct list live_contexts;
  struct ta_context *curr_context;
};

/*
 * texture info helpers
 */
int ta_texture_stride(union tsp tsp, union tcw tcw, int stride) {
  int twiddled = ta_texture_twiddled(tcw);

  if (!tcw.stride_select || twiddled) {
    return ta_texture_width(tsp, tcw);
  }

  return stride;
}

int ta_texture_height(union tsp tsp, union tcw tcw) {
  int mipmaps = ta_texture_mipmaps(tcw);
  int height = 8 << tsp.texture_v_size;
  if (mipmaps) {
    height = ta_texture_width(tsp, tcw);
  }
  return height;
}

int ta_texture_width(union tsp tsp, union tcw tcw) {
  return 8 << tsp.texture_u_size;
}

int ta_texture_mipmaps(union tcw tcw) {
  return ta_texture_twiddled(tcw) && tcw.mip_mapped;
}

int ta_texture_twiddled(union tcw tcw) {
  return !tcw.scan_order ||
         /* paletted textures are always twiddled */
         tcw.pixel_fmt == PVR_PXL_8BPP || tcw.pixel_fmt == PVR_PXL_4BPP;
}

int ta_texture_compressed(union tcw tcw) {
  return tcw.vq_compressed;
}

int ta_texture_format(union tcw tcw) {
  int compressed = ta_texture_compressed(tcw);
  int twiddled = ta_texture_twiddled(tcw);
  int mipmaps = ta_texture_mipmaps(tcw);

  int texture_fmt = PVR_TEX_INVALID;

  if (compressed) {
    if (mipmaps) {
      texture_fmt = PVR_TEX_VQ_MIPMAPS;
    } else {
      texture_fmt = PVR_TEX_VQ;
    }
  } else if (tcw.pixel_fmt == PVR_PXL_4BPP) {
    if (mipmaps) {
      texture_fmt = PVR_TEX_PALETTE_4BPP_MIPMAPS;
    } else {
      texture_fmt = PVR_TEX_PALETTE_4BPP;
    }
  } else if (tcw.pixel_fmt == PVR_PXL_8BPP) {
    if (mipmaps) {
      texture_fmt = PVR_TEX_PALETTE_8BPP_MIPMAPS;
    } else {
      texture_fmt = PVR_TEX_PALETTE_8BPP;
    }
  } else if (twiddled) {
    if (mipmaps) {
      texture_fmt = PVR_TEX_TWIDDLED_MIPMAPS;
    } else {
      texture_fmt = PVR_TEX_TWIDDLED;
    }
  } else {
    texture_fmt = PVR_TEX_BITMAP;
  }

  return texture_fmt;
}

uint32_t ta_palette_addr(union tcw tcw, int *size) {
  uint32_t palette_addr = 0;
  int palette_size = 0;

  if (tcw.pixel_fmt == PVR_PXL_4BPP || tcw.pixel_fmt == PVR_PXL_8BPP) {
    /* palette ram is 4096 bytes, with each palette entry being 4 bytes each,
       resulting in 1 << 10 indexes */
    if (tcw.pixel_fmt == PVR_PXL_4BPP) {
      /* in 4bpp mode, the palette selector represents the upper 6 bits of the
         palette index, with the remaining 4 bits being filled in by the
         texture */
      palette_addr = tcw.p.palette_selector << 6;
      palette_size = 1 << 6;
    } else if (tcw.pixel_fmt == PVR_PXL_8BPP) {
      /* in 8bpp mode, the palette selector represents the upper 2 bits of the
         palette index, with the remaining 8 bits being filled in by the
         texture */
      palette_addr = (tcw.p.palette_selector >> 4) << 10;
      palette_size = 1 << 10;
    }
  }

  if (size) {
    *size = palette_size;
  }

  return palette_addr;
}

uint32_t ta_texture_addr(union tsp tsp, union tcw tcw, int *size) {
  uint32_t texture_addr = tcw.texture_addr << 3;
  int texture_size = 0;

  /* compressed textures have the additional fixed-size codebook */
  if (ta_texture_compressed(tcw)) {
    texture_size += PVR_CODEBOOK_SIZE;
  }

  /* calculate the size of each mipmap level */
  int width = ta_texture_width(tsp, tcw);
  int height = ta_texture_height(tsp, tcw);
  int bpp = 16;
  if (tcw.pixel_fmt == PVR_PXL_8BPP) {
    bpp = 8;
  } else if (tcw.pixel_fmt == PVR_PXL_4BPP) {
    bpp = 4;
  }
  int mipmaps = ta_texture_mipmaps(tcw);
  int levels = mipmaps ? ctz32(width) + 1 : 1;
  while (levels--) {
    int mip_width = width >> levels;
    int mip_height = height >> levels;
    texture_size += (mip_width * mip_height * bpp) >> 3;
  }

  if (size) {
    *size = texture_size;
  }

  return texture_addr;
}

/*
 * parameter stream processing helpers
 */
int ta_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERTS];
int ta_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
int ta_vert_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static int ta_poly_type_raw(union pcw pcw) {
  if (pcw.list_type == TA_LIST_OPAQUE_MODVOL ||
      pcw.list_type == TA_LIST_TRANSLUCENT_MODVOL) {
    return 6;
  }

  if (pcw.para_type == TA_PARAM_SPRITE) {
    return 5;
  }

  if (pcw.volume) {
    if (pcw.col_type == 0) {
      return 3;
    }
    if (pcw.col_type == 2) {
      return 4;
    }
    if (pcw.col_type == 3) {
      return 3;
    }
  }

  if (pcw.col_type == 0 || pcw.col_type == 1 || pcw.col_type == 3) {
    return 0;
  }
  if (pcw.col_type == 2 && pcw.texture && !pcw.offset) {
    return 1;
  }
  if (pcw.col_type == 2 && pcw.texture && pcw.offset) {
    return 2;
  }
  if (pcw.col_type == 2 && !pcw.texture) {
    return 1;
  }

  return 0;
}

static int ta_vert_type_raw(union pcw pcw) {
  if (pcw.list_type == TA_LIST_OPAQUE_MODVOL ||
      pcw.list_type == TA_LIST_TRANSLUCENT_MODVOL) {
    return 17;
  }

  if (pcw.para_type == TA_PARAM_SPRITE) {
    return pcw.texture ? 16 : 15;
  }

  if (pcw.volume) {
    if (pcw.texture) {
      if (pcw.col_type == 0) {
        return pcw.uv_16bit ? 12 : 11;
      }
      if (pcw.col_type == 2 || pcw.col_type == 3) {
        return pcw.uv_16bit ? 14 : 13;
      }
    }

    if (pcw.col_type == 0) {
      return 9;
    }
    if (pcw.col_type == 2 || pcw.col_type == 3) {
      return 10;
    }
  }

  if (pcw.texture) {
    if (pcw.col_type == 0) {
      return pcw.uv_16bit ? 4 : 3;
    }
    if (pcw.col_type == 1) {
      return pcw.uv_16bit ? 6 : 5;
    }
    if (pcw.col_type == 2 || pcw.col_type == 3) {
      return pcw.uv_16bit ? 8 : 7;
    }
  }

  if (pcw.col_type == 0) {
    return 0;
  }
  if (pcw.col_type == 1) {
    return 1;
  }
  if (pcw.col_type == 2 || pcw.col_type == 3) {
    return 2;
  }

  return 0;
}

static int ta_param_size_raw(union pcw pcw, int vert_type) {
  switch (pcw.para_type) {
    case TA_PARAM_END_OF_LIST:
      return 32;
    case TA_PARAM_USER_TILE_CLIP:
      return 32;
    case TA_PARAM_OBJ_LIST_SET:
      return 32;
    case TA_PARAM_POLY_OR_VOL: {
      int type = ta_poly_type_raw(pcw);
      return type == 0 || type == 1 || type == 3 ? 32 : 64;
    }
    case TA_PARAM_SPRITE:
      return 32;
    case TA_PARAM_VERTEX: {
      return vert_type == 0 || vert_type == 1 || vert_type == 2 ||
                     vert_type == 3 || vert_type == 4 || vert_type == 7 ||
                     vert_type == 8 || vert_type == 9 || vert_type == 10
                 ? 32
                 : 64;
    }
    default:
      return 0;
  }
}

/*
 * ta parameter handling
 *
 * ta contexts are an encapsulation of all the state necessary to render a given
 * frame submitted to the ta. this includes the raw poly and vertex parameters,
 * as well as the relevant pvr register state at the time of rendering
 *
 * to understand this code, it's important to know how programs submitted data:
 *
 * 1.) initialize the TA_ISP_BASE / TA_ISP_LIMIT to an address range in memory
 *     where the poly and vertex parameters were to be stored
 * 2.) initialize the TA_OL_BASE / TA_OL_LIMIT to an address in memory where the
 *     object lists generated by core would be stored
 * 3.) write to TA_LIST_INIT to initialize the ta's internal registers
 * 4.) start dma'ing poly / vertex parameters to 0x10000000
 * 5.) wait for interrupts confirming that all of the data for a particular list
 *     has been transferred to texture memory
 *
 * due to the TA_ISP_BASE register, it was possible to have multiple frames of
 * data in memory at a time, which is why a tree is maintained mapping a guest
 * address to its respective ta_context
 */
static holly_interrupt_t list_interrupts[] = {
    HOLLY_INT_TAEOINT,  /* TA_LIST_OPAQUE */
    HOLLY_INT_TAEOMINT, /* TA_LIST_OPAQUE_MODVOL */
    HOLLY_INT_TAETINT,  /* TA_LIST_TRANSLUCENT */
    HOLLY_INT_TAETMINT, /* TA_LIST_TRANSLUCENT_MODVOL */
    HOLLY_INT_TAEPTIN   /* TA_LIST_PUNCH_THROUGH */
};

static struct ta_context *ta_get_context(struct ta *ta, uint32_t addr) {
  list_for_each_entry(ctx, &ta->live_contexts, struct ta_context, it) {
    if (ctx->addr == addr) {
      return ctx;
    }
  }
  return NULL;
}

static struct ta_context *ta_demand_context(struct ta *ta, uint32_t addr) {
  struct ta_context *ctx = ta_get_context(ta, addr);

  if (ctx) {
    return ctx;
  }

  /* remove from the object pool */
  ctx = list_first_entry(&ta->free_contexts, struct ta_context, it);
  CHECK_NOTNULL(ctx);
  list_remove(&ta->free_contexts, &ctx->it);

  /* reset context */
  ctx->addr = addr;
  ctx->cursor = 0;
  ctx->size = 0;
  ctx->list_type = 0;
  ctx->vert_type = 0;

  /* add to live list */
  list_add(&ta->live_contexts, &ctx->it);

  return ctx;
}

static void ta_unlink_context(struct ta *ta, struct ta_context *ctx) {
  /* remove from live list, but don't add back to object pool */
  list_remove(&ta->live_contexts, &ctx->it);
}

static void ta_free_context(struct ta *ta, struct ta_context *ctx) {
  /* add back to object pool */
  list_add(&ta->free_contexts, &ctx->it);
}

static void ta_cont_context(struct ta *ta, struct ta_context *ctx) {
  ctx->list_type = TA_NUM_LISTS;
  ctx->vert_type = TA_NUM_VERTS;
}

static void ta_init_context(struct ta *ta, struct ta_context *ctx) {
  ctx->cursor = 0;
  ctx->size = 0;
  ctx->list_type = TA_NUM_LISTS;
  ctx->vert_type = TA_NUM_VERTS;
}

static void ta_write_context(struct ta *ta, struct ta_context *ctx,
                             const void *ptr, int size) {
  CHECK_LT(ctx->size + size, (int)sizeof(ctx->params));
  memcpy(&ctx->params[ctx->size], ptr, size);
  ctx->size += size;

  /* each TA command is either 32 or 64 bytes, with the pcw being in the first
     32 bytes always. check every 32 bytes to see if the command has been
     completely received or not */
  if (ctx->size % 32 == 0) {
    void *param = &ctx->params[ctx->cursor];
    union pcw pcw = *(union pcw *)param;

    int size = ta_param_size(pcw, ctx->vert_type);
    int recv = ctx->size - ctx->cursor;

    if (recv < size) {
      /* wait for the entire command */
      return;
    }

    if (ta_pcw_list_type_valid(pcw, ctx->list_type)) {
      ctx->list_type = pcw.list_type;
    }

    switch (pcw.para_type) {
      /* control params */
      case TA_PARAM_END_OF_LIST:
        /* it's common that a TA_PARAM_END_OF_LIST is sent before a valid list
           type has been set */
        if (ctx->list_type != TA_NUM_LISTS) {
          holly_raise_interrupt(ta->holly, list_interrupts[ctx->list_type]);
        }
        ctx->list_type = TA_NUM_LISTS;
        ctx->vert_type = TA_NUM_VERTS;
        break;

      case TA_PARAM_USER_TILE_CLIP:
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
        break;

      /* global params */
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE:
        ctx->vert_type = ta_vert_type(pcw);
        break;

      /* vertex params */
      case TA_PARAM_VERTEX:
        break;

      default:
        LOG_FATAL("Unsupported TA parameter %d", pcw.para_type);
        break;
    }

    ctx->cursor += recv;
  }
}

/*
 * ta rendering flow
 *
 * after dma'ing input parameters to texture memory, rendering is initiated by:
 * 1.) writing the address of the parameters in the PARAM_BASE register
 * 2.) writing to the STARTRENDER register
 *
 * once rendering is done, an interrupt is raised to signal so. many games take
 * advantage of this time between start and end render to run additional cpu
 * code, making it very adventageous to also render asynchronously at the host
 * level in order to squeeze out extra free performance
 */
static void ta_save_state(struct ta *ta, struct ta_context *ctx) {
  struct pvr *pvr = ta->pvr;

  /* autosort */
  if (pvr->FPU_PARAM_CFG->region_header_type) {
    /* region array data type 2 */
    uint32_t region_data = sh4_read32(ta->mem, 0x05000000 + *pvr->REGION_BASE);
    ctx->autosort = !(region_data & 0x20000000);
  } else {
    /* region array data type 1 */
    ctx->autosort = !pvr->ISP_FEED_CFG->presort;
  }

  /* texture stride */
  ctx->stride = pvr->TEXT_CONTROL->stride * 32;

  /* texture palette pixel format */
  ctx->palette_fmt = pvr->PAL_RAM_CTRL->pixel_fmt;

  /* save video resolution in order to unproject the screen space coordinates */
  pvr_video_size(pvr, &ctx->video_width, &ctx->video_height);

  /* according to the hardware docs, this is the correct calculation of the
     background ISP address. however, in practice, the second TA buffer's ISP
     address comes out to be 0x800000 when booting the bios and the vram is
     only 8mb total. by examining a raw memory dump, the ISP data is only ever
     available at 0x0 when booting the bios, so masking this seems to be the
     correct solution */
  uint32_t vram_offset =
      0x05000000 +
      ((ctx->addr + pvr->ISP_BACKGND_T->tag_address * 4) & 0x7fffff);

  /* get surface parameters */
  ctx->bg_isp.full = sh4_read32(ta->mem, vram_offset);
  ctx->bg_tsp.full = sh4_read32(ta->mem, vram_offset + 4);
  ctx->bg_tcw.full = sh4_read32(ta->mem, vram_offset + 8);
  vram_offset += 12;

  /* get the background depth */
  ctx->bg_depth = *(float *)pvr->ISP_BACKGND_D;

  /* get the punch through polygon alpha test value */
  ctx->pt_alpha_ref = *pvr->PT_ALPHA_REF;

  /* get the byte size for each vertex. normally, the byte size is
     ISP_BACKGND_T.skip + 3, but if parameter selection volume mode is in
     effect and the shadow bit is 1, then the byte size is
     ISP_BACKGND_T.skip * 2 + 3 */
  int vertex_size = pvr->ISP_BACKGND_T->skip;
  if (!pvr->FPU_SHAD_SCALE->intensity_volume_mode &&
      pvr->ISP_BACKGND_T->shadow) {
    vertex_size *= 2;
  }
  vertex_size = (vertex_size + 3) * 4;

  /* skip to the first vertex */
  vram_offset += pvr->ISP_BACKGND_T->tag_offset * vertex_size;

  /* copy vertex data to context */
  for (int i = 0, bg_offset = 0; i < 3; i++) {
    CHECK_LE(bg_offset + vertex_size, (int)sizeof(ctx->bg_vertices));

    sh4_memcpy_to_host(ta->mem, &ctx->bg_vertices[bg_offset], vram_offset,
                       vertex_size);

    bg_offset += vertex_size;
    vram_offset += vertex_size;
  }
}

static void ta_render_context_end(void *data) {
  struct ta_context *ctx = data;
  struct ta *ta = ctx->userdata;

  /* ensure the client has finished rendering */
  dc_finish_render(ta->dc);

  /* return context back to pool */
  ta_free_context(ta, ctx);

  /* let the game know rendering is complete */
  holly_raise_interrupt(ta->holly, HOLLY_INT_PCEOVINT);
  holly_raise_interrupt(ta->holly, HOLLY_INT_PCEOIINT);
  holly_raise_interrupt(ta->holly, HOLLY_INT_PCEOTINT);
}

static void ta_render_context(struct ta *ta, struct ta_context *ctx) {
  prof_counter_add(COUNTER_ta_renders, 1);

  /* remove context from pool */
  ta_unlink_context(ta, ctx);

  /* save off required state that may be modified by the time the context is
     rendered */
  ta_save_state(ta, ctx);

  /* let the client know to start rendering the context */
  dc_start_render(ta->dc, ctx);

  /* give each frame 10 ms to finish rendering
     TODO figure out a heuristic involving the number of polygons rendered */
  int64_t end = INT64_C(10000000);
  ctx->userdata = ta;
  scheduler_start_timer(ta->scheduler, &ta_render_context_end, ctx, end);
}

/*
 * yuv420 -> yuv422 conversion routines
 */
#define TA_YUV420_MACROBLOCK_SIZE 384
#define TA_YUV422_MACROBLOCK_SIZE 512
#define TA_MAX_MACROBLOCK_SIZE \
  MAX(TA_YUV420_MACROBLOCK_SIZE, TA_YUV422_MACROBLOCK_SIZE)

static void ta_yuv_reset(struct ta *ta) {
  struct pvr *pvr = ta->pvr;

  /* FIXME only YUV420 -> YUV422 supported for now */
  CHECK_EQ(pvr->TA_YUV_TEX_CTRL->format, 0);

  /* FIXME only format 0 supported for now */
  CHECK_EQ(pvr->TA_YUV_TEX_CTRL->tex, 0);

  int u_size = pvr->TA_YUV_TEX_CTRL->u_size + 1;
  int v_size = pvr->TA_YUV_TEX_CTRL->v_size + 1;

  /* setup internal state for the data conversion */
  ta->yuv_data = &ta->vram[pvr->TA_YUV_TEX_BASE->base_address];
  ta->yuv_width = u_size * 16;
  ta->yuv_height = v_size * 16;
  ta->yuv_macroblock_size = TA_YUV420_MACROBLOCK_SIZE;
  ta->yuv_macroblock_count = u_size * v_size;

  /* reset number of macroblocks processed */
  pvr->TA_YUV_TEX_CNT->num = 0;
}

static void ta_yuv_process_block(struct ta *ta, const uint8_t *in_uv,
                                 const uint8_t *in_y, uint8_t *out_uyvy) {
  uint8_t *out_row0 = out_uyvy;
  uint8_t *out_row1 = out_uyvy + (ta->yuv_width << 1);

  /* reencode 8x8 subblock of YUV420 data as UYVY422 */
  for (int j = 0; j < 8; j += 2) {
    for (int i = 0; i < 8; i += 2) {
      uint8_t u = in_uv[0];
      uint8_t v = in_uv[64];
      uint8_t y0 = in_y[0];
      uint8_t y1 = in_y[1];
      uint8_t y2 = in_y[8];
      uint8_t y3 = in_y[9];

      out_row0[0] = u;
      out_row0[1] = y0;
      out_row0[2] = v;
      out_row0[3] = y1;

      out_row1[0] = u;
      out_row1[1] = y2;
      out_row1[2] = v;
      out_row1[3] = y3;

      in_uv += 1;
      in_y += 2;
      out_row0 += 4;
      out_row1 += 4;
    }

    /* skip past adjacent 8x8 subblock */
    in_uv += 4;
    in_y += 8;
    out_row0 += (ta->yuv_width << 2) - 16;
    out_row1 += (ta->yuv_width << 2) - 16;
  }
}

static void ta_yuv_process_macroblock(struct ta *ta, const void *data) {
  struct pvr *pvr = ta->pvr;

  /* YUV420 data comes in as a series 16x16 macroblocks that need to be
     converted into a single UYVY422 texture */
  const uint8_t *in = data;
  uint32_t out_x =
      (pvr->TA_YUV_TEX_CNT->num % (pvr->TA_YUV_TEX_CTRL->u_size + 1)) * 16;
  uint32_t out_y =
      (pvr->TA_YUV_TEX_CNT->num / (pvr->TA_YUV_TEX_CTRL->u_size + 1)) * 16;
  uint8_t *out = &ta->yuv_data[(out_y * ta->yuv_width + out_x) << 1];

  /* process each 8x8 subblock individually */
  /* (0, 0) */
  ta_yuv_process_block(ta, &in[0], &in[128], &out[0]);
  /* (8, 0) */
  ta_yuv_process_block(ta, &in[4], &in[192], &out[16]);
  /* (0, 8) */
  ta_yuv_process_block(ta, &in[32], &in[256], &out[ta->yuv_width * 16]);
  /* (8, 8) */
  ta_yuv_process_block(ta, &in[36], &in[320], &out[ta->yuv_width * 16 + 16]);

  /* reset state once all macroblocks have been processed */
  pvr->TA_YUV_TEX_CNT->num++;

  if ((int)pvr->TA_YUV_TEX_CNT->num >= ta->yuv_macroblock_count) {
    ta_yuv_reset(ta);

    /* raise DMA end interrupt */
    holly_raise_interrupt(ta->holly, HOLLY_INT_TAYUVINT);
  }
}

/*
 * ta device interface
 */
static int ta_init(struct device *dev) {
  struct ta *ta = (struct ta *)dev;
  struct dreamcast *dc = ta->dc;

  ta->vram = mem_vram(dc->mem, 0x0);

  for (int i = 0; i < ARRAY_SIZE(ta->contexts); i++) {
    struct ta_context *ctx = &ta->contexts[i];
    list_add(&ta->free_contexts, &ctx->it);
  }

  return 1;
}

void ta_init_tables() {
  static int initialized = 0;

  if (initialized) {
    return;
  }

  initialized = 1;

  for (int i = 0; i < 0x100; i++) {
    union pcw pcw = *(union pcw *)&i;

    for (int j = 0; j < TA_NUM_PARAMS; j++) {
      pcw.para_type = j;

      for (int k = 0; k < TA_NUM_VERTS; k++) {
        int param_idx = i * TA_NUM_PARAMS * TA_NUM_VERTS + j * TA_NUM_VERTS + k;
        ta_param_sizes[param_idx] = ta_param_size_raw(pcw, k);
      }
    }
  }

  for (int i = 0; i < 0x100; i++) {
    union pcw pcw = *(union pcw *)&i;

    for (int j = 0; j < TA_NUM_PARAMS; j++) {
      pcw.para_type = j;

      for (int k = 0; k < TA_NUM_LISTS; k++) {
        pcw.list_type = k;

        int poly_idx = i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS + k;
        ta_poly_types[poly_idx] = ta_poly_type_raw(pcw);

        int vert_idx = i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS + k;
        ta_vert_types[vert_idx] = ta_vert_type_raw(pcw);
      }
    }
  }
}

/* ta data handlers
 *
 * three types of data are written to the ta:
 * 1.) polygon data - input parameters for display lists
 * 2.) yuv data - yuv macroblocks that are to be reencoded as yuv422
 * 3.) texture data - data that is written directly to vram
 */
void ta_texture_write(struct ta *ta, uint32_t dst, const uint8_t *src,
                      int size) {
  struct holly *holly = ta->holly;

  CHECK(*holly->SB_LMMODE0 == 0);

  dst &= 0xeeffffff;
  memcpy(&ta->vram[dst], src, size);
}

void ta_yuv_write(struct ta *ta, uint32_t dst, const uint8_t *src, int size) {
  struct holly *holly = ta->holly;
  struct pvr *pvr = ta->pvr;

  CHECK(*holly->SB_LMMODE0 == 0);
  CHECK(size % ta->yuv_macroblock_size == 0);

  const uint8_t *end = src + size;
  while (src < end) {
    ta_yuv_process_macroblock(ta, src);
    src += ta->yuv_macroblock_size;
  }
}

void ta_poly_write(struct ta *ta, uint32_t dst, const uint8_t *src, int size) {
  struct holly *holly = ta->holly;

  CHECK(*holly->SB_LMMODE0 == 0);
  CHECK(size % 32 == 0);

  const uint8_t *end = src + size;
  while (src < end) {
    ta_write_context(ta, ta->curr_context, src, 32);
    src += 32;
  }
}

void ta_texture_info(struct ta *ta, union tsp tsp, union tcw tcw,
                     const uint8_t **texture, int *texture_size,
                     const uint8_t **palette, int *palette_size) {
  uint32_t texture_addr = ta_texture_addr(tsp, tcw, texture_size);
  *texture = &ta->vram[texture_addr];

  uint32_t palette_addr = ta_palette_addr(tcw, palette_size);
  uint8_t *palette_ram = (uint8_t *)ta->pvr->PALETTE_RAM000 + palette_addr;
  *palette = *palette_size ? palette_ram : NULL;
}

void ta_yuv_init(struct ta *ta) {
  ta_yuv_reset(ta);
}

void ta_list_cont(struct ta *ta) {
  struct ta_context *ctx =
      ta_get_context(ta, ta->pvr->TA_ISP_BASE->base_address);
  CHECK_NOTNULL(ctx);
  ta_cont_context(ta, ctx);
  ta->curr_context = ctx;
}

void ta_list_init(struct ta *ta) {
  struct ta_context *ctx =
      ta_demand_context(ta, ta->pvr->TA_ISP_BASE->base_address);
  ta_init_context(ta, ctx);
  ta->curr_context = ctx;
}

void ta_start_render(struct ta *ta) {
  struct ta_context *ctx =
      ta_get_context(ta, ta->pvr->PARAM_BASE->base_address);
  CHECK_NOTNULL(ctx);
  ta_render_context(ta, ctx);
}

void ta_soft_reset(struct ta *ta) {
  /* FIXME what are we supposed to do here? */
}

void ta_destroy(struct ta *ta) {
  dc_destroy_device((struct device *)ta);
}

struct ta *ta_create(struct dreamcast *dc) {
  ta_init_tables();

  struct ta *ta = dc_create_device(dc, sizeof(struct ta), "ta", &ta_init, NULL);

  return ta;
}
