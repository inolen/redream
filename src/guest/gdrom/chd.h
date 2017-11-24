#ifndef CHD_H
#define CHD_H

struct disc;

struct disc *chd_create(const char *filename, int verbose);

#endif
