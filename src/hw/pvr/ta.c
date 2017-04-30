#include "hw/pvr/ta.h"
#include "core/list.h"
#include "core/string.h"
#include "hw/holly/holly.h"
#include "hw/pvr/pixel_convert.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/tr.h"
#include "hw/pvr/trace.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"
#include "ui/nuklear.h"

DEFINE_AGGREGATE_COUNTER(ta_data);
DEFINE_AGGREGATE_COUNTER(ta_renders);

#define TA_MAX_CONTEXTS 8
#define TA_YUV420_MACROBLOCK_SIZE 384
#define TA_YUV422_MACROBLOCK_SIZE 512
#define TA_MAX_MACROBLOCK_SIZE \
  MAX(TA_YUV420_MACROBLOCK_SIZE, TA_YUV422_MACROBLOCK_SIZE)

struct ta_texture_entry {
  struct texture_entry;
  struct ta *ta;
  struct list_node free_it;
  struct rb_node live_it;

  struct memory_watch *texture_watch;
  struct memory_watch *palette_watch;
  struct list_node invalid_it;
  int invalidated;
};

struct ta {
  struct device;
  struct texture_provider provider;
  uint8_t *video_ram;
  struct trace_writer *trace_writer;

  /* yuv data converter state */
  uint8_t *yuv_data;
  int yuv_width;
  int yuv_height;
  int yuv_macroblock_size;
  int yuv_macroblock_count;

  /* tile context pool */
  struct tile_ctx contexts[TA_MAX_CONTEXTS];
  struct list free_contexts;
  struct list live_contexts;
  struct tile_ctx *curr_context;

  /* texture cache state */
  unsigned frame;
  int num_textures;

  /* textures for the current context are uploaded to the render backend by
     the video thread in parallel to the main emulation thread executing,
     which may erroneously write to a texture before receiving the end of
     render interrupts. in order to avoid race conditions around the texture's
     dirty state in these situations, textures are not immediately marked dirty
     by the emulation thread. instead, they are added to this invalidated list
     which will be processed the next time the two threads are synchronized */
  struct list invalidated_entries;
  int num_invalidated;

  struct ta_texture_entry entries[8192];
  struct list free_entries;
  struct rb_tree live_entries;
};

int g_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERTS];
int g_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
int g_vertex_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static holly_interrupt_t list_interrupts[] = {
    HOLLY_INTC_TAEOINT,  /* TA_LIST_OPAQUE */
    HOLLY_INTC_TAEOMINT, /* TA_LIST_OPAQUE_MODVOL */
    HOLLY_INTC_TAETINT,  /* TA_LIST_TRANSLUCENT */
    HOLLY_INTC_TAETMINT, /* TA_LIST_TRANSLUCENT_MODVOL */
    HOLLY_INTC_TAEPTIN   /* TA_LIST_PUNCH_THROUGH */
};

static int ta_entry_cmp(const struct rb_node *rb_lhs,
                        const struct rb_node *rb_rhs) {
  const struct ta_texture_entry *lhs =
      rb_entry(rb_lhs, const struct ta_texture_entry, live_it);
  texture_key_t lhs_key = tr_texture_key(lhs->tsp, lhs->tcw);

  const struct ta_texture_entry *rhs =
      rb_entry(rb_rhs, const struct ta_texture_entry, live_it);
  texture_key_t rhs_key = tr_texture_key(rhs->tsp, rhs->tcw);

  if (lhs_key < rhs_key) {
    return -1;
  } else if (lhs_key > rhs_key) {
    return 1;
  } else {
    return 0;
  }
}

static struct rb_callbacks ta_entry_cb = {&ta_entry_cmp, NULL, NULL};

/* See "57.1.1.2 Parameter Combinations" for information on the poly types. */
static int ta_get_poly_type_raw(union pcw pcw) {
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

/* See "57.1.1.2 Parameter Combinations" for information on the vertex types. */
static int ta_get_vert_type_raw(union pcw pcw) {
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

/* Parameter size can be determined by only the union pcw for every parameter
   other than vertex parameters. For vertex parameters, the vertex type derived
   from the last poly or modifier volume parameter is needed. */
static int ta_get_param_size_raw(union pcw pcw, int vertex_type) {
  switch (pcw.para_type) {
    case TA_PARAM_END_OF_LIST:
      return 32;
    case TA_PARAM_USER_TILE_CLIP:
      return 32;
    case TA_PARAM_OBJ_LIST_SET:
      return 32;
    case TA_PARAM_POLY_OR_VOL: {
      int type = ta_get_poly_type_raw(pcw);
      return type == 0 || type == 1 || type == 3 ? 32 : 64;
    }
    case TA_PARAM_SPRITE:
      return 32;
    case TA_PARAM_VERTEX: {
      return vertex_type == 0 || vertex_type == 1 || vertex_type == 2 ||
                     vertex_type == 3 || vertex_type == 4 || vertex_type == 7 ||
                     vertex_type == 8 || vertex_type == 9 || vertex_type == 10
                 ? 32
                 : 64;
    }
    default:
      return 0;
  }
}

static void ta_soft_reset(struct ta *ta) {
  /* FIXME what are we supposed to do here? */
}

static void ta_clear_textures(struct ta *ta) {
  LOG_INFO("Texture cache cleared");

  struct rb_node *it = rb_first(&ta->live_entries);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct ta_texture_entry *entry =
        rb_entry(it, struct ta_texture_entry, live_it);

    entry->dirty = 1;

    it = next;
  }
}

static void ta_dirty_invalidated_textures(struct ta *ta) {
  list_for_each_entry(entry, &ta->invalidated_entries, struct ta_texture_entry,
                      invalid_it) {
    entry->dirty = 1;
    entry->invalidated = 0;
  }

  list_clear(&ta->invalidated_entries);
}

static void ta_texture_invalidated(const struct exception *ex, void *data) {
  struct ta_texture_entry *entry = data;
  entry->texture_watch = NULL;

  if (!entry->invalidated) {
    list_add(&entry->ta->invalidated_entries, &entry->invalid_it);
    entry->invalidated = 1;
  }
}

static void ta_palette_invalidated(const struct exception *ex, void *data) {
  struct ta_texture_entry *entry = data;
  entry->palette_watch = NULL;

  if (!entry->invalidated) {
    list_add(&entry->ta->invalidated_entries, &entry->invalid_it);
    entry->invalidated = 1;
  }
}

static struct ta_texture_entry *ta_alloc_texture(struct ta *ta, union tsp tsp,
                                                 union tcw tcw) {
  /* remove from free list */
  struct ta_texture_entry *entry =
      list_first_entry(&ta->free_entries, struct ta_texture_entry, free_it);
  CHECK_NOTNULL(entry);
  list_remove(&ta->free_entries, &entry->free_it);

  /* reset entry */
  memset(entry, 0, sizeof(*entry));
  entry->ta = ta;
  entry->tsp = tsp;
  entry->tcw = tcw;

  /* add to live tree */
  rb_insert(&ta->live_entries, &entry->live_it, &ta_entry_cb);

  ta->num_textures++;

  return entry;
}

static struct ta_texture_entry *ta_find_texture(struct ta *ta, union tsp tsp,
                                                union tcw tcw) {
  struct ta_texture_entry search;
  search.tsp = tsp;
  search.tcw = tcw;

  return rb_find_entry(&ta->live_entries, &search, struct ta_texture_entry,
                       live_it, &ta_entry_cb);
}

static struct tile_ctx *ta_get_context(struct ta *ta, uint32_t addr) {
  list_for_each_entry(ctx, &ta->live_contexts, struct tile_ctx, it) {
    if (ctx->addr == addr) {
      return ctx;
    }
  }
  return NULL;
}

static struct tile_ctx *ta_alloc_context(struct ta *ta, uint32_t addr) {
  /* remove from free list */
  struct tile_ctx *ctx =
      list_first_entry(&ta->free_contexts, struct tile_ctx, it);
  CHECK_NOTNULL(ctx);
  list_remove(&ta->free_contexts, &ctx->it);

  /* reset context */
  ctx->addr = addr;
  ctx->cursor = 0;
  ctx->size = 0;
  ctx->list_type = 0;
  ctx->vertex_type = 0;

  /* add to live tree */
  list_add(&ta->live_contexts, &ctx->it);

  return ctx;
}

static void ta_unlink_context(struct ta *ta, struct tile_ctx *ctx) {
  list_remove(&ta->live_contexts, &ctx->it);
}

static void ta_free_context(struct ta *ta, struct tile_ctx *ctx) {
  list_add(&ta->free_contexts, &ctx->it);
}

static struct tile_ctx *ta_demand_context(struct ta *ta, uint32_t addr) {
  struct tile_ctx *ctx = ta_get_context(ta, addr);

  if (!ctx) {
    ctx = ta_alloc_context(ta, addr);
  }

  return ctx;
}

static void ta_cont_context(struct ta *ta, struct tile_ctx *ctx) {
  ctx->list_type = TA_NUM_LISTS;
  ctx->vertex_type = TA_NUM_VERTS;
}

static void ta_init_context(struct ta *ta, struct tile_ctx *ctx) {
  ctx->cursor = 0;
  ctx->size = 0;
  ctx->list_type = TA_NUM_LISTS;
  ctx->vertex_type = TA_NUM_VERTS;
}

static void ta_write_context(struct ta *ta, struct tile_ctx *ctx, void *ptr,
                             int size) {
  CHECK_LT(ctx->size + size, (int)sizeof(ctx->params));
  memcpy(&ctx->params[ctx->size], ptr, size);
  ctx->size += size;

  /* track how much TA data is written per second */
  prof_counter_add(COUNTER_ta_data, size);

  /* each TA command is either 32 or 64 bytes, with the pcw being in the first
     32 bytes always. check every 32 bytes to see if the command has been
     completely received or not */
  if (ctx->size % 32 == 0) {
    void *param = &ctx->params[ctx->cursor];
    union pcw pcw = *(union pcw *)param;

    int size = ta_get_param_size(pcw, ctx->vertex_type);
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
        ctx->vertex_type = TA_NUM_VERTS;
        break;

      case TA_PARAM_USER_TILE_CLIP:
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
        break;

      /* global params */
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE:
        ctx->vertex_type = ta_get_vert_type(pcw);
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

static void ta_register_texture_source(struct ta *ta, union tsp tsp,
                                       union tcw tcw) {
  struct ta_texture_entry *entry = ta_find_texture(ta, tsp, tcw);

  if (!entry) {
    entry = ta_alloc_texture(ta, tsp, tcw);
    entry->dirty = 1;
  }

  /* mark texture source valid for the current frame */
  int first_registration_this_frame = entry->frame != ta->frame;
  entry->frame = ta->frame;

  /* set texture address */
  if (!entry->texture) {
    uint32_t texture_addr = ta_texture_addr(tcw);
    entry->texture = &ta->video_ram[texture_addr];
    entry->texture_size = ta_texture_size(tsp, tcw);
  }

  /* set palette address */
  if (!entry->palette) {
    if (tcw.pixel_format == TA_PIXEL_4BPP ||
        tcw.pixel_format == TA_PIXEL_8BPP) {
      uint32_t palette_addr = 0;
      int palette_size = 0;

      /* palette ram is 4096 bytes, with each palette entry being 4 bytes each,
         resulting in 1 << 10 indexes */
      if (tcw.pixel_format == TA_PIXEL_4BPP) {
        /* in 4bpp mode, the palette selector represents the upper 6 bits of the
           palette index, with the remaining 4 bits being filled in by the
           texture */
        palette_addr = (tcw.p.palette_selector << 4) * 4;
        palette_size = (1 << 4) * 4;
      } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
        /* in 8bpp mode, the palette selector represents the upper 2 bits of the
           palette index, with the remaining 8 bits being filled in by the
           texture */
        palette_addr = ((tcw.p.palette_selector & 0x30) << 4) * 4;
        palette_size = (1 << 8) * 4;
      }

      entry->palette = &ta->pvr->palette_ram[palette_addr];
      entry->palette_size = palette_size;
    }
  }

#ifdef NDEBUG
  /* add write callback in order to invalidate on future writes. the callback
     address will be page aligned, therefore it will be triggered falsely in
     some cases. over invalidate in these cases */
  if (!entry->texture_watch) {
    entry->texture_watch = add_single_write_watch(
        entry->texture, entry->texture_size, &ta_texture_invalidated, entry);
  }

  if (entry->palette && !entry->palette_watch) {
    entry->palette_watch = add_single_write_watch(
        entry->palette, entry->palette_size, &ta_palette_invalidated, entry);
  }
#endif

  /* add dirty textures to the trace */
  if (ta->trace_writer && entry->dirty && first_registration_this_frame) {
    trace_writer_insert_texture(ta->trace_writer, tsp, tcw, entry->frame,
                                entry->palette, entry->palette_size,
                                entry->texture, entry->texture_size);
  }
}

static void ta_register_texture_sources(struct ta *ta, struct tile_ctx *ctx) {
  const uint8_t *data = ctx->params;
  const uint8_t *end = ctx->params + ctx->size;
  int vertex_type = 0;

  while (data < end) {
    union pcw pcw = *(union pcw *)data;

    switch (pcw.para_type) {
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE: {
        const union poly_param *param = (const union poly_param *)data;

        vertex_type = ta_get_vert_type(param->type0.pcw);

        if (param->type0.pcw.texture) {
          ta_register_texture_source(ta, param->type0.tsp, param->type0.tcw);
        }
      } break;

      default:
        break;
    }

    data += ta_get_param_size(pcw, vertex_type);
  }
}

static void ta_save_state(struct ta *ta, struct tile_ctx *ctx) {
  struct pvr *pvr = ta->pvr;
  struct address_space *space = ta->sh4->memory_if->space;

  /* mark context valid for the current frame */
  ctx->frame = ta->frame;

  /* autosort */
  if (pvr->FPU_PARAM_CFG->region_header_type) {
    /* region array data type 2 */
    uint32_t region_data = as_read32(space, 0x05000000 + *pvr->REGION_BASE);
    ctx->autosort = !(region_data & 0x20000000);
  } else {
    /* region array data type 1 */
    ctx->autosort = !pvr->ISP_FEED_CFG->presort;
  }

  /* texture stride */
  ctx->stride = pvr->TEXT_CONTROL->stride * 32;

  /* texture palette pixel format */
  ctx->pal_pxl_format = pvr->PAL_RAM_CTRL->pixel_format;

  /* save out video width / height in order to unproject the screen space
     coordinates */
  if (!(pvr->SPG_CONTROL->NTSC || pvr->SPG_CONTROL->PAL) ||
      pvr->SPG_CONTROL->interlace) {
    /* interlaced and VGA mode both render at full resolution */
    ctx->video_width = 640;
    ctx->video_height = 480;
  } else {
    ctx->video_width = 320;
    ctx->video_height = 240;
  }

  /* scale_x signals to scale the framebuffer down by half. do so by scaling
     up the width used by the projection matrix */
  if (pvr->SCALER_CTL->scale_x) {
    ctx->video_width *= 2;
  }

  /* scale_y is a fixed-point scaler, with 6-bits in the integer and 10-bits
     in the decimal. this scale value is ignored when used for interlacing
     which is not emulated */
  if (!pvr->SCALER_CTL->interlace) {
    ctx->video_height = (ctx->video_height * pvr->SCALER_CTL->scale_y) >> 10;
  }

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
  ctx->bg_isp.full = as_read32(space, vram_offset);
  ctx->bg_tsp.full = as_read32(space, vram_offset + 4);
  ctx->bg_tcw.full = as_read32(space, vram_offset + 8);
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

    as_memcpy_to_host(space, &ctx->bg_vertices[bg_offset], vram_offset,
                      vertex_size);

    bg_offset += vertex_size;
    vram_offset += vertex_size;
  }
}

static void ta_finish_render(void *data) {
  struct tile_ctx *ctx = data;
  struct ta *ta = ctx->userdata;

  /* ensure the client has finished rendering */
  dc_finish_render(ta->dc);

  /* texture entries are only valid between each start / finish render pair,
     increment frame number again to invalidate */
  ta->frame++;

  /* return context back to pool */
  ta_free_context(ta, ctx);

  /* let the game know rendering is complete */
  holly_raise_interrupt(ta->holly, HOLLY_INTC_PCEOVINT);
  holly_raise_interrupt(ta->holly, HOLLY_INTC_PCEOIINT);
  holly_raise_interrupt(ta->holly, HOLLY_INTC_PCEOTINT);
}

static void ta_start_render(struct ta *ta, struct tile_ctx *ctx) {
  prof_counter_add(COUNTER_ta_renders, 1);

  /* remove context from pool */
  ta_unlink_context(ta, ctx);

  /* incement internal frame number. this frame number is assigned to the
     context and each texture source it registers to assert synchronization
     between the emulator and video thread is working as expected */
  ta->frame++;

  /* now that the video thread is sure to not be accessing the texture data,
     mark any textures dirty that were invalidated by a memory watch */
  ta_dirty_invalidated_textures(ta);

  /* register the source of each texture referenced by the context with the
     tile renderer. note, uploading the texture to the render backend happens
     lazily while rendering the context. this registration just lets the
     backend know where the texture's source data is */
  ta_register_texture_sources(ta, ctx);

  /* save off required state that may be modified by the time the context is
     rendered */
  ta_save_state(ta, ctx);

  /* let the client know to start rendering the context */
  dc_start_render(ta->dc, ctx);

  /* give each frame 10 ms to finish rendering
     TODO figure out a heuristic involving the number of polygons rendered */
  int64_t end = INT64_C(10000000);
  ctx->userdata = ta;
  scheduler_start_timer(ta->scheduler, &ta_finish_render, ctx, end);

  if (ta->trace_writer) {
    trace_writer_render_context(ta->trace_writer, ctx);
  }
}

static void ta_yuv_init(struct ta *ta) {
  struct pvr *pvr = ta->pvr;

  /* FIXME only YUV420 -> YUV422 supported for now */
  CHECK_EQ(pvr->TA_YUV_TEX_CTRL->format, 0);

  /* FIXME only format 0 supported for now */
  CHECK_EQ(pvr->TA_YUV_TEX_CTRL->tex, 0);

  int u_size = pvr->TA_YUV_TEX_CTRL->u_size + 1;
  int v_size = pvr->TA_YUV_TEX_CTRL->v_size + 1;

  /* setup internal state for the data conversion */
  ta->yuv_data = &ta->video_ram[pvr->TA_YUV_TEX_BASE->base_address];
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

static void ta_yuv_process_macroblock(struct ta *ta, void *data) {
  struct pvr *pvr = ta->pvr;
  struct address_space *space = ta->sh4->memory_if->space;

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
    ta_yuv_init(ta);

    /* raise DMA end interrupt */
    holly_raise_interrupt(ta->holly, HOLLY_INTC_TAYUVINT);
  }
}

static void ta_poly_fifo_write(struct ta *ta, uint32_t dst, void *ptr,
                               int size) {
  PROF_ENTER("cpu", "ta_poly_fifo_write");

  CHECK(size % 32 == 0);

  uint8_t *src = ptr;
  uint8_t *end = src + size;
  while (src < end) {
    ta_write_context(ta, ta->curr_context, src, 32);
    src += 32;
  }

  PROF_LEAVE();
}

static void ta_yuv_fifo_write(struct ta *ta, uint32_t dst, void *ptr,
                              int size) {
  PROF_ENTER("cpu", "ta_yuv_fifo_write");

  struct holly *holly = ta->holly;
  struct pvr *pvr = ta->pvr;

  CHECK(size % ta->yuv_macroblock_size == 0);

  uint8_t *src = ptr;
  uint8_t *end = src + size;
  while (src < end) {
    ta_yuv_process_macroblock(ta, src);
    src += ta->yuv_macroblock_size;
  }

  PROF_LEAVE();
}

static void ta_texture_fifo_write(struct ta *ta, uint32_t dst, void *ptr,
                                  int size) {
  PROF_ENTER("cpu", "ta_texture_fifo_write");

  uint8_t *src = ptr;
  dst &= 0xeeffffff;
  memcpy(&ta->video_ram[dst], src, size);

  PROF_LEAVE();
}

static int ta_init(struct device *dev) {
  struct ta *ta = (struct ta *)dev;
  struct dreamcast *dc = ta->dc;

  ta->video_ram = memory_translate(dc->memory, "video ram", 0x00000000);

  for (int i = 0; i < array_size(ta->entries); i++) {
    struct ta_texture_entry *entry = &ta->entries[i];
    list_add(&ta->free_entries, &entry->free_it);
  }

  for (int i = 0; i < array_size(ta->contexts); i++) {
    struct tile_ctx *ctx = &ta->contexts[i];
    list_add(&ta->free_contexts, &ctx->it);
  }

  return 1;
}

static void ta_toggle_tracing(struct ta *ta) {
  if (!ta->trace_writer) {
    char filename[PATH_MAX];
    get_next_trace_filename(filename, sizeof(filename));

    ta->trace_writer = trace_writer_open(filename);

    if (!ta->trace_writer) {
      LOG_INFO("Failed to start tracing");
      return;
    }

    /* clear texture cache in order to generate insert events for all
       textures referenced while tracing */
    ta_clear_textures(ta);

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    trace_writer_close(ta->trace_writer);
    ta->trace_writer = NULL;

    LOG_INFO("End tracing");
  }
}

static void ta_debug_menu(struct device *dev, struct nk_context *ctx) {
  struct ta *ta = (struct ta *)dev;

  nk_layout_row_push(ctx, 30.0f);

  if (nk_menu_begin_label(ctx, "TA", NK_TEXT_LEFT, nk_vec2(140.0f, 200.0f))) {
    nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

    nk_value_int(ctx, "num textures", ta->num_textures);

    if (!ta->trace_writer && nk_button_label(ctx, "start trace")) {
      ta_toggle_tracing(ta);
    } else if (ta->trace_writer && nk_button_label(ctx, "stop trace")) {
      ta_toggle_tracing(ta);
    }

    if (nk_button_label(ctx, "clear texture cache")) {
      ta_clear_textures(ta);
    }

    nk_menu_end(ctx);
  }
}

void ta_build_tables() {
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
        g_param_sizes[i * TA_NUM_PARAMS * TA_NUM_VERTS + j * TA_NUM_VERTS + k] =
            ta_get_param_size_raw(pcw, k);
      }
    }
  }

  for (int i = 0; i < 0x100; i++) {
    union pcw pcw = *(union pcw *)&i;

    for (int j = 0; j < TA_NUM_PARAMS; j++) {
      pcw.para_type = j;

      for (int k = 0; k < TA_NUM_LISTS; k++) {
        pcw.list_type = k;

        g_poly_types[i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS + k] =
            ta_get_poly_type_raw(pcw);
        g_vertex_types[i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS +
                       k] = ta_get_vert_type_raw(pcw);
      }
    }
  }
}

static struct texture_entry *ta_texture_provider_find_texture(void *data,
                                                              union tsp tsp,
                                                              union tcw tcw) {
  struct ta *ta = (struct ta *)data;
  struct ta_texture_entry *entry = ta_find_texture(ta, tsp, tcw);

  if (!entry) {
    return NULL;
  }

  /* sanity check that the texture source is valid for the current frame. video
     ram will be modified between frames, if these values don't match something
     is broken in the thread synchronization */
  CHECK_EQ(entry->frame, ta->frame);

  return (struct texture_entry *)entry;
}

struct texture_provider *ta_texture_provider(struct ta *ta) {
  if (!ta->provider.userdata) {
    ta->provider.userdata = ta;
    ta->provider.find_texture = &ta_texture_provider_find_texture;
  }
  return &ta->provider;
}

void ta_destroy(struct ta *ta) {
  dc_destroy_window_interface(ta->window_if);
  dc_destroy_device((struct device *)ta);
}

struct ta *ta_create(struct dreamcast *dc) {
  ta_build_tables();

  struct ta *ta = dc_create_device(dc, sizeof(struct ta), "ta", &ta_init);
  ta->window_if = dc_create_window_interface(&ta_debug_menu, NULL, NULL, NULL);
  ta->provider =
      (struct texture_provider){ta, &ta_texture_provider_find_texture};

  return ta;
}

REG_W32(pvr_cb, SOFTRESET) {
  struct ta *ta = dc->ta;

  if (!(value & 0x1)) {
    return;
  }

  ta_soft_reset(ta);
}

REG_W32(pvr_cb, STARTRENDER) {
  struct ta *ta = dc->ta;

  if (!value) {
    return;
  }

  struct tile_ctx *ctx = ta_get_context(ta, ta->pvr->PARAM_BASE->base_address);
  CHECK_NOTNULL(ctx);
  ta_start_render(ta, ctx);
}

REG_W32(pvr_cb, TA_LIST_INIT) {
  struct ta *ta = dc->ta;

  if (!(value & 0x80000000)) {
    return;
  }

  struct tile_ctx *ctx =
      ta_demand_context(ta, ta->pvr->TA_ISP_BASE->base_address);
  ta_init_context(ta, ctx);
  ta->curr_context = ctx;
}

REG_W32(pvr_cb, TA_LIST_CONT) {
  struct ta *ta = dc->ta;

  if (!(value & 0x80000000)) {
    return;
  }

  struct tile_ctx *ctx = ta_get_context(ta, ta->pvr->TA_ISP_BASE->base_address);
  CHECK_NOTNULL(ctx);
  ta_cont_context(ta, ctx);
  ta->curr_context = ctx;
}

REG_W32(pvr_cb, TA_YUV_TEX_BASE) {
  struct ta *ta = dc->ta;
  struct pvr *pvr = dc->pvr;

  pvr->TA_YUV_TEX_BASE->full = value;

  ta_yuv_init(ta);
}

/* clang-format off */
AM_BEGIN(struct ta, ta_fifo_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_HANDLE("ta poly fifo",
                                             NULL, NULL, NULL,
                                             (mmio_write_string_cb)&ta_poly_fifo_write)
  AM_RANGE(0x00800000, 0x00ffffff) AM_HANDLE("ta yuv fifo",
                                             NULL, NULL, NULL,
                                             (mmio_write_string_cb)&ta_yuv_fifo_write)
  AM_RANGE(0x01000000, 0x01ffffff) AM_HANDLE("ta texture fifo",
                                            NULL, NULL, NULL,
                                            (mmio_write_string_cb)&ta_texture_fifo_write)
AM_END();
/* clang-format on */
