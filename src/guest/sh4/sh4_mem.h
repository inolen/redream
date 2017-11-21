#ifndef SH4_MEM_H
#define SH4_MEM_H

/* clang-format off */
#define SH4_AREA_SIZE        0x20000000
#define SH4_ADDR_MASK        (SH4_AREA_SIZE-1)

/* area 0 */
#define SH4_AREA0_BEGIN      0x00000000
#define SH4_AREA0_END        0x03ffffff
#define SH4_AREA0_ADDR_MASK  0x01ffffff
#define SH4_BOOT_ROM_BEGIN   0x00000000
#define SH4_BOOT_ROM_END     0x001fffff
#define SH4_FLASH_ROM_BEGIN  0x00200000
#define SH4_FLASH_ROM_END    0x0021ffff
#define SH4_HOLLY_REG_BEGIN  0x005f0000
#define SH4_HOLLY_REG_END    0x005f7fff
#define SH4_PVR_REG_BEGIN    0x005f8000
#define SH4_PVR_REG_END      0x005f9fff
#define SH4_MODEM_BEGIN      0x00600000
#define SH4_MODEM_END        0x0067ffff
#define SH4_AICA_REG_BEGIN   0x00700000
#define SH4_AICA_REG_END     0x00710fff
#define SH4_AICA_MEM_BEGIN   0x00800000
#define SH4_AICA_MEM_END     0x009fffff
#define SH4_HOLLY_EXT_BEGIN  0x01000000
#define SH4_HOLLY_EXT_END    0x01ffffff

/* area 1 */
#define SH4_AREA1_BEGIN      0x04000000
#define SH4_AREA1_END        0x07ffffff 
#define SH4_AREA1_ADDR_MASK  0x05ffffff
#define SH4_PVR_VRAM64_BEGIN 0x04000000
#define SH4_PVR_VRAM64_END   0x047fffff
#define SH4_PVR_VRAM32_BEGIN 0x05000000
#define SH4_PVR_VRAM32_END   0x057fffff

/* area 2 */
#define SH4_AREA2_BEGIN      0x08000000
#define SH4_AREA2_END        0x0bffffff

/* area 3 */
#define SH4_AREA3_BEGIN      0x0c000000
#define SH4_AREA3_END        0x0fffffff
#define SH4_AREA3_ADDR_MASK  0x00ffffff
#define SH4_AREA3_RAM0_BEGIN 0x0c000000
#define SH4_AREA3_RAM0_END   0x0cffffff
#define SH4_AREA3_RAM1_BEGIN 0x0d000000
#define SH4_AREA3_RAM1_END   0x0dffffff
#define SH4_AREA3_RAM2_BEGIN 0x0e000000
#define SH4_AREA3_RAM2_END   0x0effffff
#define SH4_AREA3_RAM3_BEGIN 0x0f000000
#define SH4_AREA3_RAM3_END   0x0fffffff

/* area 4 */
#define SH4_AREA4_BEGIN      0x10000000
#define SH4_AREA4_END        0x13ffffff
#define SH4_AREA4_ADDR_MASK  0x11ffffff
#define SH4_TA_POLY_BEGIN    0x10000000
#define SH4_TA_POLY_END      0x107fffff
#define SH4_TA_YUV_BEGIN     0x10800000
#define SH4_TA_YUV_END       0x10ffffff
#define SH4_TA_TEXTURE_BEGIN 0x11000000
#define SH4_TA_TEXTURE_END   0x11ffffff

/* area 5 */
#define SH4_AREA5_BEGIN      0x14000000
#define SH4_AREA5_END        0x17ffffff

/* area 6 */
#define SH4_AREA6_BEGIN      0x18000000
#define SH4_AREA6_END        0x1bffffff

/* area 7 */
#define SH4_AREA7_BEGIN      0x1c000000
#define SH4_AREA7_END        0x1fffffff
#define SH4_REG_BEGIN        0x1c000000
#define SH4_REG_END          0x1fffffff
#define SH4_CACHE_BEGIN      0x7c000000
#define SH4_CACHE_END        0x7fffffff

/* p0 */
#define SH4_P0_00_BEGIN      0x00000000
#define SH4_P0_00_END        0x1fffffff

#define SH4_P0_01_BEGIN      0x20000000
#define SH4_P0_01_END        0x3fffffff

#define SH4_P0_10_BEGIN      0x40000000
#define SH4_P0_10_END        0x5fffffff

#define SH4_P0_11_BEGIN      0x60000000
#define SH4_P0_11_END        0x7fffffff

/* p1 */
#define SH4_P1_BEGIN         0x80000000
#define SH4_P1_END           0x9fffffff

/* p2 */
#define SH4_P2_BEGIN         0xa0000000
#define SH4_P2_END           0xbfffffff

/* p3 */
#define SH4_P3_BEGIN         0xc0000000
#define SH4_P3_END           0xdfffffff

/* p4 */
#define SH4_P4_BEGIN         0xe0000000
#define SH4_P4_END           0xffffffff
#define SH4_SQ_BEGIN         0xe0000000
#define SH4_SQ_END           0xe3ffffff
#define SH4_ICACHE_BEGIN     0xf0000000
#define SH4_ICACHE_END       0xf1ffffff
#define SH4_ITLB_BEGIN       0xf2000000
#define SH4_ITLB_END         0xf3ffffff
#define SH4_OCACHE_BEGIN     0xf4000000
#define SH4_OCACHE_END       0xf5ffffff
#define SH4_UTLB_BEGIN       0xf6000000
#define SH4_UTLB_END         0xf7ffffff
/* clang-format on */

uint32_t sh4_area0_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_area0_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask);

uint32_t sh4_area1_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_area1_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask);

uint32_t sh4_area4_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_area4_write(struct sh4 *sh4, uint32_t addr, const uint8_t *ptr,
                     int size);

uint32_t sh4_area7_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_area7_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask);

uint32_t sh4_p4_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_p4_write(struct sh4 *sh4, uint32_t addr, uint32_t data, uint32_t mask);

#endif
