#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include "hw/holly/ta_types.h"

enum trace_cmd_type {
  TRACE_CMD_NONE,
  TRACE_CMD_TEXTURE,
  TRACE_CMD_CONTEXT,
};

struct trace_cmd {
  enum trace_cmd_type type;

  // set on read
  struct trace_cmd *prev;
  struct trace_cmd *next;
  struct trace_cmd *override;

  // the data pointers in these structs are written out relative to the cmd,
  // and patched to absolute pointers on read
  union {
    struct {
      union tsp tsp;
      union tcw tcw;
      uint32_t palette_size;
      const uint8_t *palette;
      uint32_t texture_size;
      const uint8_t *texture;
    } texture;

    // slimmed down version of the tile_ctx structure, will need to be in
    // sync
    struct {
      int8_t autosort;
      uint32_t stride;
      uint32_t pal_pxl_format;
      uint32_t video_width;
      uint32_t video_height;
      union isp bg_isp;
      union tsp bg_tsp;
      union tcw bg_tcw;
      float bg_depth;
      uint32_t bg_vertices_size;
      const uint8_t *bg_vertices;
      uint32_t data_size;
      const uint8_t *data;
    } context;
  };
};

struct trace {
  struct trace_cmd *cmds;
};

struct trace_writer {
  FILE *file;
};

extern void get_next_trace_filename(char *filename, size_t size);

struct trace *trace_parse(const char *filename);
void trace_destroy(struct trace *trace);

struct trace_writer *trace_writer_open(const char *filename);
void trace_writer_insert_texture(struct trace_writer *writer, union tsp tsp,
                                 union tcw tcw, const uint8_t *palette,
                                 int palette_size, const uint8_t *texture,
                                 int texture_size);
void trace_writer_render_context(struct trace_writer *writer,
                                 struct tile_ctx *ctx);
void trace_writer_close(struct trace_writer *writer);

#endif
