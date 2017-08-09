#ifndef REDREAM_HASH_H
#define REDREAM_HASH_H

#include "core/list.h"
#include "core/math.h"

#define DECLARE_HASHTABLE(name, bits) struct list name[1 << bits]

#define HASH_SIZE(name) ARRAY_SIZE(name)
#define HASH_BITS(name) ctz64(HASH_SIZE(name))

/*
 * hash method taken from Chuck Lever's paper:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * the general idea is to multiply the input by -phi, and since multiplication
 * propagates changes towards the high order bits, shifting off the low order
 * bits when less bits are needed
 *
 * 2^64 / -PHI = -11400714819323198485.95 = 0x61c8864680b583eb
 */
#define GOLDEN_RATIO_64 UINT64_C(0x61c8864680b583eb)

#define hash_key(x, bits) (((uint64_t)(x)*GOLDEN_RATIO_64) >> (64 - bits))

#define hash_bkt(table, key) &table[hash_key((key), HASH_BITS(table))]

#define hash_add(bkt, node) list_add((bkt), (node))

#define hash_del(bkt, node) list_remove((bkt), (node))

#define hash_bkt_for_each_entry(it, bkt, type, member) \
  list_for_each_entry(it, bkt, type, member)

#endif
