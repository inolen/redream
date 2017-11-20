#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include "guest/pvr/ta_types.h"

enum trace_cmd_type {
  TRACE_CMD_NONE,
  TRACE_CMD_TEXTURE,
  TRACE_CMD_CONTEXT,
};

struct trace_cmd {
  enum trace_cmd_type type;

  /* set on read */
  struct trace_cmd *prev;
  struct trace_cmd *next;
  struct trace_cmd *override;

  /* the data pointers in these structs are written out relative to the cmd,
     and patched to absolute pointers on read */
  union {
    struct {
      union tsp tsp;
      union tcw tcw;
      uint32_t frame;
      int32_t palette_size;
      const uint8_t *palette;
      int32_t texture_size;
      const uint8_t *texture;
    } texture;

    /* slimmed down version of the ta_context structure, will need to be in
       sync */
    struct {
      uint32_t frame;
      int32_t autosort;
      int32_t stride;
      int32_t palette_fmt;
      int32_t video_width;
      int32_t video_height;
      int32_t alpha_ref;
      union isp bg_isp;
      union tsp bg_tsp;
      union tcw bg_tcw;
      float bg_depth;
      int32_t bg_vertices_size;
      const uint8_t *bg_vertices;
      int32_t params_size;
      const uint8_t *params;
    } context;
  };
};

struct trace {
  struct trace_cmd *cmds;
  int num_frames;
};

struct trace_writer {
  FILE *file;
};

void get_next_trace_filename(char *filename, size_t size);

struct trace *trace_parse(const char *filename);
void trace_copy_context(const struct trace_cmd *cmd, struct ta_context *ctx);
void trace_destroy(struct trace *trace);

struct trace_writer *trace_writer_open(const char *filename);
void trace_writer_insert_texture(struct trace_writer *writer, union tsp tsp,
                                 union tcw tcw, unsigned frame,
                                 const uint8_t *palette, int palette_size,
                                 const uint8_t *texture, int texture_size);
void trace_writer_render_context(struct trace_writer *writer,
                                 struct ta_context *ctx);
void trace_writer_close(struct trace_writer *writer);

#endif
