#include "jit/frontend/armv3/armv3_context.h"

/* clang-format off */

/* map mode to SPSR register */
const int armv3_spsr_table[0x20] = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    SPSR_FIQ,
    SPSR_IRQ,
    SPSR_SVC,
    0,
    0,
    0,
    SPSR_ABT,
    0,
    0,
    0,
    SPSR_UND,
    0,
    0,
    0,
    0,
};

/* map mode to register layout */
const int armv3_reg_table[0x20][16] = {
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    /* USR */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    /* FIQ */
    {0, 1, 2, 3, 4, 5, 6, 7, R8_FIQ, R9_FIQ, R10_FIQ, R11_FIQ, R12_FIQ, R13_FIQ, R14_FIQ, 15},
    /* IRQ */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, R13_IRQ, R14_IRQ, 15},
    /* SVC */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, R13_SVC, R14_SVC, 15},
    {0},
    {0},
    {0},
    /* ABT */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, R13_ABT, R14_ABT, 15},
    {0},
    {0},
    {0},
    /* UND */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, R13_UND, R14_UND, 15},
    {0},
    {0},
    {0},
    /* SYS */
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

/* clang-format on */
