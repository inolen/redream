#ifndef TYPES_H
#define TYPES_H

#define DREAVM_ROUNDDOWN(v, multiple) (v & (~(multiple - 1)))
#define DREAVM_ROUNDUP(v, multiple) ((v + multiple - 1) & ~(multiple - 1))

#endif
