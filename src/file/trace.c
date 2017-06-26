#include <limits.h>
#include "file/trace.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/math.h"
#include "guest/pvr/tr.h"

void trace_writer_close(struct trace_writer *writer) {
  if (writer->file) {
    fclose(writer->file);
  }

  free(writer);
}

void trace_writer_render_context(struct trace_writer *writer,
                                 struct tile_context *ctx) {
  struct trace_cmd cmd = {0};
  cmd.type = TRACE_CMD_CONTEXT;
  cmd.context.autosort = ctx->autosort;
  cmd.context.stride = ctx->stride;
  cmd.context.pal_pxl_format = ctx->pal_pxl_format;
  cmd.context.video_width = ctx->video_width;
  cmd.context.video_height = ctx->video_height;
  cmd.context.bg_isp = ctx->bg_isp;
  cmd.context.bg_tsp = ctx->bg_tsp;
  cmd.context.bg_tcw = ctx->bg_tcw;
  cmd.context.bg_depth = ctx->bg_depth;
  cmd.context.bg_vertices_size = sizeof(ctx->bg_vertices);
  cmd.context.bg_vertices = (const uint8_t *)(intptr_t)sizeof(cmd);
  cmd.context.params_size = ctx->size;
  cmd.context.params =
      (const uint8_t *)(intptr_t)(sizeof(cmd) + sizeof(ctx->bg_vertices));

  CHECK_EQ(fwrite(&cmd, sizeof(cmd), 1, writer->file), 1);
  CHECK_EQ(fwrite(ctx->bg_vertices, sizeof(ctx->bg_vertices), 1, writer->file),
           1);
  if (ctx->size) {
    CHECK_EQ(fwrite(ctx->params, ctx->size, 1, writer->file), 1);
  }
}

void trace_writer_insert_texture(struct trace_writer *writer, union tsp tsp,
                                 union tcw tcw, unsigned frame,
                                 const uint8_t *palette, int palette_size,
                                 const uint8_t *texture, int texture_size) {
  struct trace_cmd cmd = {0};
  cmd.type = TRACE_CMD_TEXTURE;
  cmd.texture.tsp = tsp;
  cmd.texture.tcw = tcw;
  cmd.texture.frame = frame;
  cmd.texture.palette_size = palette_size;
  cmd.texture.palette = (const uint8_t *)(intptr_t)sizeof(cmd);
  cmd.texture.texture_size = texture_size;
  cmd.texture.texture = (const uint8_t *)(intptr_t)(sizeof(cmd) + palette_size);

  CHECK_EQ(fwrite(&cmd, sizeof(cmd), 1, writer->file), 1);
  if (palette_size) {
    CHECK_EQ(fwrite(palette, palette_size, 1, writer->file), 1);
  }
  if (texture_size) {
    CHECK_EQ(fwrite(texture, texture_size, 1, writer->file), 1);
  }
}

struct trace_writer *trace_writer_open(const char *filename) {
  struct trace_writer *writer = calloc(1, sizeof(struct trace_writer));

  writer->file = fopen(filename, "wb");

  if (!writer->file) {
    trace_writer_close(writer);
    return NULL;
  }

  return writer;
}

/* for commands which mutate global state, the previous state needs to be
   tracked in order to support unwinding. To do so, each command is iterated
   and tagged with the previous command that it overrides */
static int trace_patch_overrides(struct trace_cmd *cmd) {
  while (cmd) {
    if (cmd->type == TRACE_CMD_TEXTURE) {
      tr_texture_key_t texture_key =
          tr_texture_key(cmd->texture.tsp, cmd->texture.tcw);

      /* walk backwards and see if this texture overrode a previous command
         TODO could cache this information in a map */
      struct trace_cmd *prev = cmd->prev;

      while (prev) {
        if (prev->type == TRACE_CMD_TEXTURE) {
          tr_texture_key_t prev_texture_key =
              tr_texture_key(prev->texture.tsp, prev->texture.tcw);

          if (prev_texture_key == texture_key) {
            cmd->override = prev;
            break;
          }
        }

        prev = prev->prev;
      }
    }

    cmd = cmd->next;
  }

  return 1;
}

/* commands are written out with null list pointers, and pointers to data
   are written out relative to the command itself. Set the list pointers,
   and make the data pointers absolute */
static int trace_patch_pointers(void *begin, int size) {
  struct trace_cmd *prev_cmd = NULL;
  struct trace_cmd *curr_cmd = NULL;
  uint8_t *ptr = begin;
  uint8_t *end = ptr + size;

  while (ptr < end) {
    prev_cmd = curr_cmd;
    curr_cmd = (struct trace_cmd *)ptr;

    /* set prev / next pointers */
    if (prev_cmd) {
      prev_cmd->next = curr_cmd;
    }
    curr_cmd->prev = prev_cmd;
    curr_cmd->next = NULL;
    curr_cmd->override = NULL;

    /* patch relative data pointers */
    switch (curr_cmd->type) {
      case TRACE_CMD_TEXTURE: {
        curr_cmd->texture.palette += (intptr_t)ptr;
        curr_cmd->texture.texture += (intptr_t)ptr;
        ptr += sizeof(*curr_cmd) + curr_cmd->texture.palette_size +
               curr_cmd->texture.texture_size;
      } break;

      case TRACE_CMD_CONTEXT: {
        curr_cmd->context.bg_vertices += (intptr_t)ptr;
        curr_cmd->context.params += (intptr_t)ptr;
        ptr += sizeof(*curr_cmd) + curr_cmd->context.bg_vertices_size +
               curr_cmd->context.params_size;
      } break;

      default:
        LOG_INFO("Unexpected trace command type %d", curr_cmd->type);
        return 0;
    }
  }

  return 1;
}

void trace_destroy(struct trace *trace) {
  free(trace->cmds);
  free(trace);
}

void trace_copy_context(const struct trace_cmd *cmd, struct tile_context *ctx) {
  CHECK_EQ(cmd->type, TRACE_CMD_CONTEXT);

  ctx->autosort = cmd->context.autosort;
  ctx->stride = cmd->context.stride;
  ctx->pal_pxl_format = cmd->context.pal_pxl_format;
  ctx->bg_isp = cmd->context.bg_isp;
  ctx->bg_tsp = cmd->context.bg_tsp;
  ctx->bg_tcw = cmd->context.bg_tcw;
  ctx->bg_depth = cmd->context.bg_depth;
  ctx->video_width = cmd->context.video_width;
  ctx->video_height = cmd->context.video_height;
  memcpy(ctx->bg_vertices, cmd->context.bg_vertices,
         cmd->context.bg_vertices_size);
  memcpy(ctx->params, cmd->context.params, cmd->context.params_size);
  ctx->size = cmd->context.params_size;
}

struct trace *trace_parse(const char *filename) {
  struct trace *trace = calloc(1, sizeof(struct trace));

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    trace_destroy(trace);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  int size = (int)ftell(fp);
  fseek(fp, 0, SEEK_SET);

  trace->cmds = malloc(size);
  CHECK_EQ(fread(trace->cmds, size, 1, fp), 1);
  fclose(fp);

  if (!trace_patch_pointers(trace->cmds, size)) {
    trace_destroy(trace);
    return NULL;
  }

  if (!trace_patch_overrides(trace->cmds)) {
    trace_destroy(trace);
    return NULL;
  }

  /* count max frames */
  {
    struct trace_cmd *cmd = trace->cmds;

    while (cmd) {
      if (cmd->type == TRACE_CMD_CONTEXT) {
        trace->num_frames++;
      }

      cmd = cmd->next;
    }
  }

  return trace;
}

void get_next_trace_filename(char *filename, size_t size) {
  const char *appdir = fs_appdir();

  for (int i = 0; i < INT_MAX; i++) {
    snprintf(filename, size, "%s" PATH_SEPARATOR "%d.trace", appdir, i);

    if (!fs_exists(filename)) {
      return;
    }
  }

  LOG_FATAL("unable to find available trace filename");
}
