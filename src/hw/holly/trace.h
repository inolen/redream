#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include "hw/holly/ta_types.h"

typedef enum {
  TRACE_CMD_NONE,
  TRACE_CMD_TEXTURE,
  TRACE_CMD_CONTEXT,
} trace_cmd_type_t;

typedef struct trace_cmd_s {
  trace_cmd_type_t type;

  // set on read
  struct trace_cmd_s *prev;
  struct trace_cmd_s *next;
  struct trace_cmd_s *override;

  // the data pointers in these structs are written out relative to the cmd,
  // and patched to absolute pointers on read
  union {
    struct {
      tsp_t tsp;
      tcw_t tcw;
      uint32_t palette_size;
      const uint8_t *palette;
      uint32_t texture_size;
      const uint8_t *texture;
    } texture;

    // slimmed down version of the tile_ctx_t structure, will need to be in
    // sync
    struct {
      int8_t autosort;
      uint32_t stride;
      uint32_t pal_pxl_format;
      uint32_t video_width;
      uint32_t video_height;
      isp_t bg_isp;
      tsp_t bg_tsp;
      tcw_t bg_tcw;
      float bg_depth;
      uint32_t bg_vertices_size;
      const uint8_t *bg_vertices;
      uint32_t data_size;
      const uint8_t *data;
    } context;
  };
} trace_cmd_t;

typedef struct { trace_cmd_t *cmds; } trace_t;

typedef struct { FILE *file; } trace_writer_t;

extern void get_next_trace_filename(char *filename, size_t size);

trace_t *trace_parse(const char *filename);
void trace_destroy(trace_t *trace);

trace_writer_t *trace_writer_open(const char *filename);
void trace_writer_insert_texture(trace_writer_t *writer, tsp_t tsp, tcw_t tcw,
                                 const uint8_t *palette, int palette_size,
                                 const uint8_t *texture, int texture_size);
void trace_writer_render_context(trace_writer_t *writer, tile_ctx_t *ctx);
void trace_writer_close(trace_writer_t *writer);

#endif
