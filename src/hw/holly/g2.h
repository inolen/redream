#ifndef G2_H
#define G2_H

struct dreamcast;
struct g2;

struct g2 *g2_create(struct dreamcast *dc);
void g2_destroy(struct g2 *g2);

AM_DECLARE(g2_modem_map);
AM_DECLARE(g2_expansion0_map);
AM_DECLARE(g2_expansion1_map);
AM_DECLARE(g2_expansion2_map);

#endif
