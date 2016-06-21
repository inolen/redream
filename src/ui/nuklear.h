#ifndef NUKLEAR_H
#define NUKLEAR_H

struct window;
struct nuklear;
struct nk_context;

struct nk_context *nuklear_context(struct nuklear *nk);

struct nuklear *nuklear_create(struct window *window);
void nuklear_destroy(struct nuklear *nk);

#endif
