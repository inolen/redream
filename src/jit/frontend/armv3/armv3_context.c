#include "jit/frontend/armv3/armv3_context.h"

/* map mode to SPSR register */
const int armv3_spsr_table[0x100] = {
    0, 0,        0, 0, 0, 0,        0,        0,        0,        0, 0,
    0, 0,        0, 0, 0, 0,        SPSR_FIQ, SPSR_IRQ, SPSR_SVC, 0, 0,
    0, SPSR_ABT, 0, 0, 0, SPSR_UND, 0,        0,        0,        0,
};

/* map mode to register layout */
const int armv3_reg_table[0x100][7] = {
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
    {8, 9, 10, 11, 12, 13, 14},
    /* FIQ */
    {R8_FIQ, R9_FIQ, R10_FIQ, R11_FIQ, R12_FIQ, R13_FIQ, R14_FIQ},
    /* IRQ */
    {8, 9, 10, 11, 12, R13_IRQ, R14_IRQ},
    /* SVC */
    {8, 9, 10, 11, 12, R13_SVC, R14_SVC},
    {0},
    {0},
    {0},
    /* ABT */
    {8, 9, 10, 11, 12, R13_ABT, R14_ABT},
    {0},
    {0},
    {0},
    /* UND */
    {8, 9, 10, 11, 12, R13_UND, R14_UND},
    {0},
    {0},
    {0},
    /* SYS */
    {8, 9, 10, 11, 12, 13, 14}};
