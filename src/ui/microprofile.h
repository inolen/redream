#ifndef MICROPROFILE_IMPL_H
#define MICROPROFILE_IMPL_H

struct microprofile;
struct render_backend;
struct window;

struct microprofile *mp_create(struct window *window, struct render_backend *r);
void mp_destroy(struct microprofile *mp);

void mp_render(struct microprofile *mp);

#endif
