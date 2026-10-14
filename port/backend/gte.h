/* Adapted from DW2-PC (CC0): psyq/gte_core.h @ 816ed0d */
#ifndef PORT_BACKEND_GTE_H
#define PORT_BACKEND_GTE_H

#include <stdint.h>

/* C model of the PS1 GTE (COP2), shared by the port PsyQ functions and inline macros.
 * The 32 data and 32 control registers keep their hardware view: a write stores
 * what the register holds (16-bit registers sign or zero extended), a read returns what mfc2 /
 * cfc2 would. Commands take the COP2 instruction word, so sf, lm and the MVMVA fields come
 * from the same bits as on the PS1. Results follow the hardware rules (psx-spx "GTE"): 44-bit
 * MAC accumulation with overflow flags, IR / colour / SZ / SXY saturation, the UNR divide, and
 * FLAG bit 31 as the error summary. */

/* Data registers. */
enum {
    GTE_VXY0 = 0, GTE_VZ0, GTE_VXY1, GTE_VZ1, GTE_VXY2, GTE_VZ2, GTE_RGBC, GTE_OTZ,
    GTE_IR0, GTE_IR1, GTE_IR2, GTE_IR3, GTE_SXY0, GTE_SXY1, GTE_SXY2, GTE_SXYP,
    GTE_SZ0, GTE_SZ1, GTE_SZ2, GTE_SZ3, GTE_RGB0, GTE_RGB1, GTE_RGB2, GTE_RES1,
    GTE_MAC0, GTE_MAC1, GTE_MAC2, GTE_MAC3, GTE_IRGB, GTE_ORGB, GTE_LZCS, GTE_LZCR
};

/* Control registers. */
enum {
    GTE_RT0 = 0, GTE_TRX = 5, GTE_TRY, GTE_TRZ, GTE_LLM0, GTE_RBK = 13, GTE_GBK, GTE_BBK,
    GTE_LCM0, GTE_RFC = 21, GTE_GFC, GTE_BFC, GTE_OFX, GTE_OFY, GTE_H, GTE_DQA, GTE_DQB,
    GTE_ZSF3, GTE_ZSF4, GTE_FLAG
};

/* Command words emitted by include/gcc/inline_n.h and asm/us/main/psyq/libgte.s. */
#define GTE_CMD_RTPS 0x4A180001u
#define GTE_CMD_NCLIP 0x4B400006u
#define GTE_CMD_RTPT 0x4A280030u
#define GTE_CMD_AVSZ3 0x4B58002Du
#define GTE_CMD_AVSZ4 0x4B68002Eu
#define GTE_CMD_NCCS 0x4B08041Bu
#define GTE_CMD_NCCT 0x4B18043Fu
#define GTE_CMD_MVMVA(sf, mx, v, cv, lm) \
    (0x4A400012u | ((unsigned)(sf) << 19) | ((unsigned)(mx) << 17) | ((unsigned)(v) << 15) | \
     ((unsigned)(cv) << 13) | ((unsigned)(lm) << 10))

void gte_reset(void);
void gte_mtc2(int reg, uint32_t value);
uint32_t gte_mfc2(int reg);
void gte_ctc2(int reg, uint32_t value);
uint32_t gte_cfc2(int reg);
void gte_command(uint32_t op);

/* Memory operands may have any alignment. These inline operations do not advance time. */
void port_gte_ldv0(const void *v0);
void port_gte_ldv3(const void *v0, const void *v1, const void *v2);
void port_gte_rtps(void);
void port_gte_rtpt(void);
void port_gte_stsxy(void *sxy);
void port_gte_stsxy3(void *sxy0, void *sxy1, void *sxy2);
void port_gte_stszotz(void *otz);
void port_gte_ldlzc(long value);
void port_gte_stlzc(void *lzc);
void port_gte_SetRotMatrix(const void *m);
void port_gte_SetTransMatrix(const void *m);

#endif
