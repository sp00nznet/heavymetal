/*
 * Crimson Skies Static Recompilation - Core Type Definitions
 *
 * Global register model, memory access macros, stack operations,
 * condition macros, and indirect call dispatch.
 *
 * All recompiled functions include this header.
 */

#ifndef RECOMP_TYPES_H
#define RECOMP_TYPES_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/* ============================================================
 * Global Register Model
 *
 * x86 registers are global variables. ebp is local per-function
 * since most VC6 code uses FPO (Frame Pointer Omission).
 * ============================================================ */

/* Volatile (caller-saved) registers */
extern uint32_t g_eax, g_ecx, g_edx, g_esp;

/* Callee-saved registers (also global for implicit parameter passing).
 * ebp is global too, not a per-function local: MSVC's __EH_prolog sets its
 * CALLER's frame pointer (lea ebp, [esp+0xC]) and returns, so a local ebp
 * would discard it and every C++ EH function would then address off zero.
 * Real x86 has one ebp; FPO functions that never touch it simply pass the
 * caller's through, which is exactly the behaviour we want. */
extern uint32_t g_ebx, g_esi, g_edi, g_ebp;

/* The x87 FPU stack is GLOBAL (8 shared registers), not per-function: helpers
 * like __ftol receive their argument on the FPU stack from the caller, and the
 * control word persists across calls. A per-function local stack would make every
 * cross-call FPU value read as 0. */
extern double   g_st[8];
extern int      g_fp_top;
extern uint16_t g_fpu_cw;

/* Segment registers (flat mode Win32 - effectively unused) */
/* Flags are global, like the registers.
 *
 * x86 has one EFLAGS and it survives a jmp, so a lifted function that is
 * entered by a branch from another one must see the flags its predecessor set.
 * As per-function locals these reset on every transfer, which breaks any
 * function discovery splits at a point where flags are live, and every
 * RECOMP_ITAIL into a block beginning with a jcc. */
extern uint32_t g_flag_a, g_flag_b;
extern int      g_flag_k;
extern uint32_t g_cf;
extern int      g_df;       /* direction flag: cld/std persist across calls */
extern int      g_fpu_cmp;

extern uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;

/* Function pointer type for recompiled functions */
typedef void (*recomp_func_t)(void);

/* Dispatch table entry */
typedef struct {
    uint32_t address;
    recomp_func_t func;
} recomp_dispatch_entry_t;

/* Dispatch table (generated) */
extern const recomp_dispatch_entry_t recomp_dispatch_table[];
extern const uint32_t recomp_dispatch_count;

/* ============================================================
 * Register Name Aliases (used in generated code)
 * ============================================================ */

#ifdef RECOMP_GENERATED_CODE
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define ebx g_ebx
#define esp g_esp
#define esi g_esi
#define edi g_edi
#define ebp g_ebp
/* x87 FPU stack is global; _fpu_cmp stays per-function (set+used within one fn) */
#define _st g_st
#define _fp_top g_fp_top
#define _fpu_cw g_fpu_cw
#define _mm g_mm   /* MMX file is global too */
#define _flag_a g_flag_a
#define _flag_b g_flag_b
#define _flag_k g_flag_k
#define _cf     g_cf
#define _df     g_df
#define _fpu_cmp g_fpu_cmp
#define _seg_cs g_seg_cs
#define _seg_ds g_seg_ds
#define _seg_es g_seg_es
#define _seg_fs g_seg_fs
#define _seg_gs g_seg_gs
#define _seg_ss g_seg_ss
#endif

/* ============================================================
 * Sub-register Access
 * ============================================================ */

#define LO8(r)       ((uint8_t)((r) & 0xFF))
#define HI8(r)       ((uint8_t)(((r) >> 8) & 0xFF))
#define LO16(r)      ((uint16_t)((r) & 0xFFFF))

#define SET_LO8(r, v)   ((r) = ((r) & 0xFFFFFF00u) | ((uint32_t)(uint8_t)(v)))
#define SET_HI8(r, v)   ((r) = ((r) & 0xFFFF00FFu) | (((uint32_t)(uint8_t)(v)) << 8))
#define SET_LO16(r, v)  ((r) = ((r) & 0xFFFF0000u) | ((uint32_t)(uint16_t)(v)))

/* ============================================================
 * Memory Access
 *
 * The original Crimson Skies binary uses a fixed image base of 0x00400000.
 * We map the original data sections at their original VAs using
 * VirtualAlloc/CreateFileMapping so that address-dependent code
 * works correctly.
 *
 * g_mem_base is the offset from original VA to actual mapped address.
 * For fixed-base mapping, this is 0.
 * ============================================================ */

extern ptrdiff_t g_mem_base;

#define ADDR(va)     ((uintptr_t)(uint32_t)(va) + g_mem_base)

/* Thread-relative segment bases. In Win32, fs: points at the TIB/TEB; gs is
 * unused on x86. The lifter emits fs:/gs: accesses as FS_BASE/GS_BASE + addr,
 * so the runtime points g_fs_base at a simulated TIB (VA). cs/ds/es/ss are flat. */
extern uint32_t g_fs_base;
extern uint32_t g_gs_base;
/* Crimson: the simulated TIB itself. main.c sets g_fs_base to its VA, so the
 * lifter's MEM32(FS_BASE + 0) (SEH chain head) and FS_BASE + 0x18 (self
 * pointer) land here instead of faulting on real VA 0. */
extern uint32_t g_fs_seg[256];
#define FS_BASE  g_fs_base
#define GS_BASE  g_gs_base

#define MEM8(addr)   (*(volatile uint8_t  *)ADDR(addr))
#define MEM16(addr)  (*(volatile uint16_t *)ADDR(addr))
#define MEM32(addr)  (*(volatile uint32_t *)ADDR(addr))
#define MEM64(addr)  (*(volatile uint64_t *)ADDR(addr))
#define MEMF(addr)   (*(volatile float    *)ADDR(addr))
#define MEMD(addr)   (*(volatile double   *)ADDR(addr))

/* Set 32-bit values in memory (for rep stosd) */
static inline void MEMSET32(void* dst, uint32_t val, uint32_t count) {
    uint32_t* p = (uint32_t*)dst;
    for (uint32_t i = 0; i < count; i++) p[i] = val;
}

/* ============================================================
 * Stack Operations
 * ============================================================ */

/* Evaluate the operand BEFORE moving esp. x86 reads the source of
 * push dword ptr [esp+N] at the OLD esp; decrementing first makes every
 * esp-relative push read one slot too low -- which lands on the return
 * address the caller pushed, so the callee sees RECOMP_RETADDR as an
 * argument. */
#define PUSH32(sp, val) do { \
    uint32_t _pv = (uint32_t)(val); \
    (sp) -= 4; \
    MEM32(sp) = _pv; \
} while(0)

#define POP32_VAL(sp) ({ \
    uint32_t _v = MEM32(sp); \
    (sp) += 4; \
    _v; \
})

/* MSVC doesn't support statement expressions, so use a function */
#ifdef _MSC_VER
static inline uint32_t _pop32(uint32_t* sp) {
    uint32_t v = MEM32(*sp);
    *sp += 4;
    return v;
}
#undef POP32_VAL
#define POP32_VAL(sp) _pop32(&(sp))
#endif

/* Two-arg pop: store the popped value into an lvalue destination (register or
 * MEM32(...)). The lifter emits this form for `pop r/m32`. Defined via POP32_VAL
 * so it picks up the GCC statement-expr / MSVC inline-function variant above. */
#define POP32(sp, dest) do { (dest) = POP32_VAL(sp); } while(0)

#define PUSHAD() do { \
    uint32_t _tmp_esp = esp; \
    PUSH32(esp, eax); PUSH32(esp, ecx); PUSH32(esp, edx); PUSH32(esp, ebx); \
    PUSH32(esp, _tmp_esp); PUSH32(esp, ebp); PUSH32(esp, esi); PUSH32(esp, edi); \
} while(0)

#define POPAD() do { \
    edi = POP32_VAL(esp); esi = POP32_VAL(esp); ebp = POP32_VAL(esp); \
    esp += 4; /* skip saved ESP */ \
    ebx = POP32_VAL(esp); edx = POP32_VAL(esp); ecx = POP32_VAL(esp); eax = POP32_VAL(esp); \
} while(0)

/* ============================================================
 * Condition Macros
 *
 * Pattern-matched from flag-setter (cmp/test/sub/etc.) to
 * flag-consumer (jcc/setcc/cmovcc).
 * ============================================================ */

/* Compare-based conditions (from cmp a, b) */
#define CMP_EQ(a, b)   ((uint32_t)(a) == (uint32_t)(b))
#define CMP_NE(a, b)   ((uint32_t)(a) != (uint32_t)(b))
#define CMP_B(a, b)    ((uint32_t)(a) < (uint32_t)(b))      /* unsigned < */
#define CMP_BE(a, b)   ((uint32_t)(a) <= (uint32_t)(b))     /* unsigned <= */
#define CMP_A(a, b)    ((uint32_t)(a) > (uint32_t)(b))      /* unsigned > */
#define CMP_AE(a, b)   ((uint32_t)(a) >= (uint32_t)(b))     /* unsigned >= */
#define CMP_L(a, b)    ((int32_t)(a) < (int32_t)(b))        /* signed < */
#define CMP_LE(a, b)   ((int32_t)(a) <= (int32_t)(b))       /* signed <= */
#define CMP_G(a, b)    ((int32_t)(a) > (int32_t)(b))        /* signed > */
#define CMP_GE(a, b)   ((int32_t)(a) >= (int32_t)(b))       /* signed >= */
#define CMP_S(a, b)    ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)  /* sign flag */
#define CMP_NS(a, b)   ((int32_t)((uint32_t)(a) - (uint32_t)(b)) >= 0)
#define CMP_O(a, b)    0  /* TODO: overflow detection */
#define CMP_NO(a, b)   1
#define CMP_P(a, b)    0  /* TODO: parity */
#define CMP_NP(a, b)   1

/* Test-based conditions (from test a, b) */
#define TEST_Z(a, b)   (((uint32_t)(a) & (uint32_t)(b)) == 0)
#define TEST_NZ(a, b)  (((uint32_t)(a) & (uint32_t)(b)) != 0)
#define TEST_S(a, b)   ((int32_t)((uint32_t)(a) & (uint32_t)(b)) < 0)
#define TEST_NS(a, b)  ((int32_t)((uint32_t)(a) & (uint32_t)(b)) >= 0)
#define TEST_G(a, b)   ((int32_t)((uint32_t)(a) & (uint32_t)(b)) > 0)
#define TEST_LE(a, b)  ((int32_t)((uint32_t)(a) & (uint32_t)(b)) <= 0)

/* Bit test (from bt) */
#define BT_CF(base, bit) (((uint32_t)(base) >> ((uint32_t)(bit) & 31)) & 1)

/*
 * Runtime flag kind.
 *
 * A jcc is normally paired with its flag-setter at lift time, but the setter
 * is not always statically known: MSVC routinely branches into a block whose
 * predecessors set the flags with different instructions (the signed-modulo
 * idiom `and/jns/dec/or/inc/je` is one join, 64-bit compares another). The
 * lifter used to fall back to a stale _cf at those sites, which made the
 * branch read whatever carry happened to be lying around.
 *
 * _flag_a/_flag_b already survive across blocks -- they are plain function
 * locals -- so recording which *kind* of instruction wrote them is enough to
 * evaluate any condition exactly, wherever the branch turns up.
 */
enum {
    FK_NONE = 0,
    FK_CMP,     /* cmp, sub, dec  -- a - b            */
    FK_ADD,     /* add, inc       -- a + b            */
    FK_TEST,    /* and/or/xor/test -- a & b, CF=OF=0  */
    FK_BT,      /* bt             -- CF = bit b of a  */
    FK_FCOM     /* fcom           -- a is -1/0/1      */
};

enum {
    CC_E = 0, CC_NE, CC_S, CC_NS, CC_G, CC_GE, CC_L, CC_LE,
    CC_A, CC_AE, CC_B, CC_BE, CC_O, CC_NO
};

static inline int recomp_cond(uint32_t kind, uint32_t a, uint32_t b, int cc) {
    uint32_t r;
    int zf, sf, cf, of;

    if (kind == FK_FCOM) {
        int32_t v = (int32_t)a;          /* -1 less, 0 equal, 1 greater */
        switch (cc) {
        case CC_E:                return v == 0;
        case CC_NE:               return v != 0;
        case CC_B:  case CC_L:    return v <  0;
        case CC_BE: case CC_LE:   return v <= 0;
        case CC_A:  case CC_G:    return v >  0;
        case CC_AE: case CC_GE:   return v >= 0;
        default:                  return 0;
        }
    }

    switch (kind) {
    case FK_ADD:
        r  = a + b;
        cf = (r < a);
        of = (int)((~(a ^ b) & (a ^ r)) >> 31);
        break;
    case FK_TEST:
        r  = a & b;
        cf = 0;
        of = 0;
        break;
    case FK_BT:
        r  = 0;
        cf = (int)((a >> (b & 31)) & 1u);
        of = 0;
        break;
    default:                              /* FK_CMP and FK_NONE */
        r  = a - b;
        cf = (a < b);
        of = (int)(((a ^ b) & (a ^ r)) >> 31);
        break;
    }
    zf = (r == 0);
    sf = (int)(r >> 31);

    switch (cc) {
    case CC_E:   return zf;
    case CC_NE:  return !zf;
    case CC_S:   return sf;
    case CC_NS:  return !sf;
    case CC_G:   return !zf && (sf == of);
    case CC_GE:  return sf == of;
    case CC_L:   return sf != of;
    case CC_LE:  return zf || (sf != of);
    case CC_A:   return !cf && !zf;
    case CC_AE:  return !cf;
    case CC_B:   return cf;
    case CC_BE:  return cf || zf;
    case CC_O:   return of;
    case CC_NO:  return !of;
    }
    return 0;
}

/* ============================================================
 * Bit Manipulation
 * ============================================================ */

#define ROL32(val, n) (((uint32_t)(val) << ((n) & 31)) | ((uint32_t)(val) >> (32 - ((n) & 31))))
#define ROR32(val, n) (((uint32_t)(val) >> ((n) & 31)) | ((uint32_t)(val) << (32 - ((n) & 31))))
#define BSWAP32(val)  ( (((val) & 0xFF) << 24) | (((val) & 0xFF00) << 8) | \
                        (((val) >> 8) & 0xFF00) | (((val) >> 24) & 0xFF) )

/* ============================================================
 * MMX
 *
 * Eight 64-bit registers holding packed bytes/words/dwords. On real hardware
 * they alias the x87 stack, which is why MMX code must `emms` before touching
 * the FPU again; we keep the two apart, so `emms` is a no-op here and code that
 * deliberately interleaved them would behave differently. POD does not: it is
 * the MMX build, and its rasterizer is a well-formed MMX block per span.
 *
 * Global, like the rest of the register file here; a multi-threaded target
 * wants them thread-local instead.
 * ============================================================ */

extern uint64_t g_mm[8];

#define MM_W(v, i)  ((int16_t)((uint64_t)(v) >> ((i) * 16)))
#define MM_D(v, i)  ((int32_t)((uint64_t)(v) >> ((i) * 32)))
#define MM_PUT_W(i, x) ((uint64_t)(uint16_t)(x) << ((i) * 16))
#define MM_PUT_D(i, x) ((uint64_t)(uint32_t)(x) << ((i) * 32))

static inline int16_t mmx_sat16(int32_t v) {
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static inline uint64_t mmx_paddw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, MM_W(a, i) + MM_W(b, i));
    return r;
}
static inline uint64_t mmx_psubw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, MM_W(a, i) - MM_W(b, i));
    return r;
}
static inline uint64_t mmx_paddd(uint64_t a, uint64_t b) {
    return MM_PUT_D(0, MM_D(a, 0) + MM_D(b, 0)) | MM_PUT_D(1, MM_D(a, 1) + MM_D(b, 1));
}
static inline uint64_t mmx_psubd(uint64_t a, uint64_t b) {
    return MM_PUT_D(0, MM_D(a, 0) - MM_D(b, 0)) | MM_PUT_D(1, MM_D(a, 1) - MM_D(b, 1));
}
static inline uint64_t mmx_paddsw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, mmx_sat16(MM_W(a, i) + MM_W(b, i)));
    return r;
}
static inline uint64_t mmx_psubsw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, mmx_sat16(MM_W(a, i) - MM_W(b, i)));
    return r;
}
/* High half of each signed 16x16 product -- the fixed-point multiply an MMX
 * rasterizer scales colour and texture coordinates with. */
static inline uint64_t mmx_pmulhw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++)
        r |= MM_PUT_W(i, (int16_t)(((int32_t)MM_W(a, i) * MM_W(b, i)) >> 16));
    return r;
}
static inline uint64_t mmx_pmullw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, (int16_t)((int32_t)MM_W(a, i) * MM_W(b, i)));
    return r;
}
/* Two dwords, each the sum of a neighbouring pair of 16x16 products. */
static inline uint64_t mmx_pmaddwd(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 2; i++)
        r |= MM_PUT_D(i, (int32_t)MM_W(a, i*2) * MM_W(b, i*2)
                       + (int32_t)MM_W(a, i*2+1) * MM_W(b, i*2+1));
    return r;
}
/* Interleave: the low (or high) halves of a and b, a's element first. */
static inline uint64_t mmx_punpcklwd(uint64_t a, uint64_t b) {
    return MM_PUT_W(0, MM_W(a,0)) | MM_PUT_W(1, MM_W(b,0))
         | MM_PUT_W(2, MM_W(a,1)) | MM_PUT_W(3, MM_W(b,1));
}
static inline uint64_t mmx_punpckhwd(uint64_t a, uint64_t b) {
    return MM_PUT_W(0, MM_W(a,2)) | MM_PUT_W(1, MM_W(b,2))
         | MM_PUT_W(2, MM_W(a,3)) | MM_PUT_W(3, MM_W(b,3));
}
static inline uint64_t mmx_punpcklbw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) {
        r |= (uint64_t)(uint8_t)(a >> (i*8)) << (i*16);
        r |= (uint64_t)(uint8_t)(b >> (i*8)) << (i*16 + 8);
    }
    return r;
}
static inline uint64_t mmx_punpckhbw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) {
        r |= (uint64_t)(uint8_t)(a >> (32 + i*8)) << (i*16);
        r |= (uint64_t)(uint8_t)(b >> (32 + i*8)) << (i*16 + 8);
    }
    return r;
}
static inline uint64_t mmx_punpckldq(uint64_t a, uint64_t b) {
    return (uint64_t)(uint32_t)a | ((uint64_t)(uint32_t)b << 32);
}
static inline uint64_t mmx_punpckhdq(uint64_t a, uint64_t b) {
    return (uint64_t)(uint32_t)(a >> 32) | ((uint64_t)(uint32_t)(b >> 32) << 32);
}
/* Shifts. A count wider than the element is not a wrapped shift on x86: the
 * logical forms produce zero and the arithmetic ones produce the sign. */
static inline uint64_t mmx_psllq(uint64_t a, uint32_t c) { return c > 63 ? 0 : a << c; }
static inline uint64_t mmx_psrlq(uint64_t a, uint32_t c) { return c > 63 ? 0 : a >> c; }
static inline uint64_t mmx_psllw(uint64_t a, uint32_t c) {
    uint64_t r = 0;
    if (c > 15) return 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, (uint16_t)MM_W(a, i) << c);
    return r;
}
static inline uint64_t mmx_psrlw(uint64_t a, uint32_t c) {
    uint64_t r = 0;
    if (c > 15) return 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, (uint16_t)MM_W(a, i) >> c);
    return r;
}
static inline uint64_t mmx_psraw(uint64_t a, uint32_t c) {
    uint64_t r = 0;
    if (c > 15) c = 15;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, (int16_t)(MM_W(a, i) >> c));
    return r;
}
static inline uint64_t mmx_pslld(uint64_t a, uint32_t c) {
    if (c > 31) return 0;
    return MM_PUT_D(0, (uint32_t)MM_D(a,0) << c) | MM_PUT_D(1, (uint32_t)MM_D(a,1) << c);
}
static inline uint64_t mmx_psrld(uint64_t a, uint32_t c) {
    if (c > 31) return 0;
    return MM_PUT_D(0, (uint32_t)MM_D(a,0) >> c) | MM_PUT_D(1, (uint32_t)MM_D(a,1) >> c);
}
static inline uint64_t mmx_psrad(uint64_t a, uint32_t c) {
    if (c > 31) c = 31;
    return MM_PUT_D(0, MM_D(a,0) >> c) | MM_PUT_D(1, MM_D(a,1) >> c);
}
/* Pack with saturation, a's elements low. */
static inline uint64_t mmx_packssdw(uint64_t a, uint64_t b) {
    return MM_PUT_W(0, mmx_sat16(MM_D(a,0))) | MM_PUT_W(1, mmx_sat16(MM_D(a,1)))
         | MM_PUT_W(2, mmx_sat16(MM_D(b,0))) | MM_PUT_W(3, mmx_sat16(MM_D(b,1)));
}
static inline uint64_t mmx_packuswb(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) {
        int16_t v = MM_W(a, i); uint8_t p = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        r |= (uint64_t)p << (i * 8);
        v = MM_W(b, i);         p = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        r |= (uint64_t)p << (32 + i * 8);
    }
    return r;
}
static inline uint64_t mmx_pcmpeqw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, MM_W(a,i) == MM_W(b,i) ? 0xFFFF : 0);
    return r;
}
static inline uint64_t mmx_pcmpgtw(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, MM_W(a,i) > MM_W(b,i) ? 0xFFFF : 0);
    return r;
}
/* ============================================================
 * FPU Stack Helpers
 * ============================================================ */

static inline void fp_push_impl(double* st, int* top, double val) {
    /* Shift stack down, push new value */
    for (int i = 7; i > 0; i--) st[i] = st[i-1];
    st[0] = val;
    (*top)++;
}

static inline double fp_pop_impl(double* st, int* top) {
    double val = st[0];
    for (int i = 0; i < 7; i++) st[i] = st[i+1];
    st[7] = 0.0;
    (*top)--;
    return val;
}

#define fp_push(val) fp_push_impl(_st, &_fp_top, (val))
#define fp_pop()     fp_pop_impl(_st, &_fp_top)

/* ============================================================
 * CPUID stub
 * ============================================================ */

static inline void CPUID(uint32_t eax_val, uint32_t ebx_val, uint32_t ecx_val, uint32_t edx_val) {
    /* Return something reasonable for a Pentium III era check */
#ifdef _MSC_VER
    int info[4];
    __cpuid(info, eax_val);
    g_eax = info[0]; g_ebx = info[1]; g_ecx = info[2]; g_edx = info[3];
#else
    (void)eax_val; (void)ebx_val; (void)ecx_val; (void)edx_val;
#endif
}

/* ============================================================
 * Indirect Call Dispatch
 * ============================================================ */

/* The VA of the function currently executing (see RECOMP_ENTER below); an
 * unresolved dispatch is far more useful with its caller named. */
extern uint32_t g_cur_func;

/* ICALL trace ring buffer for crash diagnostics */
#define ICALL_TRACE_SIZE 32
extern uint32_t g_icall_trace[ICALL_TRACE_SIZE];
extern uint32_t g_icall_trace_idx;
extern uint32_t g_icall_count;

/* Lookup functions */
recomp_func_t recomp_lookup(uint32_t va);          /* binary search in dispatch table */
recomp_func_t recomp_lookup_manual(uint32_t va);    /* manual overrides */
recomp_func_t recomp_lookup_import(uint32_t va);    /* import bridges */
int  recomp_native_call(uint32_t va);               /* dynamically resolved natives */
void recomp_register_native(uint32_t addr, const char* name, int nargs);

/* A routed vtable slot holds a hybrid thunk address, not a function VA.
 * Translates it back to the original VA; 0 if addr is not one of ours. */
int  recomp_thunk_ova(uint32_t addr, uint32_t* out_ova);

/* Reports a callee that returned with an unexpected esp (see RECOMP_CALL). */
void recomp_esp_check(const char* name, uint32_t before, uint32_t after);

/* Crimson runtime instrumentation: call depth, counters, a trace ring dumped on
 * crash/hang, and an optional per-call HeapValidate. */
extern uint32_t g_call_depth, g_call_depth_max;
extern uint32_t g_total_calls, g_total_icalls;
extern int      g_heap_check_enabled;
extern uint32_t g_heap_check_every;   /* sample interval; HeapValidate is slow */
extern uint32_t g_heap_check_last_ok_call, g_heap_check_last_ok_va;

#define TRACE_RING_SIZE  16384   /* a full startup fits; 2 MB of ring */
#define TRACE_ENTRY_SIZE 128
extern char     g_trace_ring[TRACE_RING_SIZE][TRACE_ENTRY_SIZE];
extern uint32_t g_trace_ring_idx;
#define TRACE_LOG(...) do { \
    snprintf(g_trace_ring[g_trace_ring_idx & (TRACE_RING_SIZE-1)], \
             TRACE_ENTRY_SIZE, __VA_ARGS__); \
    g_trace_ring_idx++; \
} while(0)

/* Optional per-call HeapValidate. Off unless g_heap_check_enabled is set;
 * it self-disables on the first corruption so the log stays readable. */
#define RECOMP_HEAP_CHECK(va, what) do { \
    if (g_heap_check_enabled && (g_heap_check_every <= 1 || g_total_calls % g_heap_check_every == 0)) { \
        if (!HeapValidate(GetProcessHeap(), 0, NULL)) { \
            fprintf(stderr, "[HEAP] CORRUPTION after %s (call #%u)\n", \
                    what, g_total_calls); \
            fprintf(stderr, "    Last OK: call #%u va 0x%08X\n", \
                    g_heap_check_last_ok_call, g_heap_check_last_ok_va); \
            g_heap_check_enabled = 0; \
        } else { \
            g_heap_check_last_ok_call = g_total_calls; \
            g_heap_check_last_ok_va = (va); \
        } \
    } \
} while(0)

/* The dummy return address pushed before a recompiled call. The callee's lifted
 * `ret` pops it. 0xDEAD0000 is a recognizable marker, but if a stack imbalance
 * ever leaks it into a value (e.g. a size argument) the high bits are destructive.
 * Projects that have hit such a leak can define RECOMP_RETADDR=0u so a leak is
 * benign while it's tracked down. */
#ifndef RECOMP_RETADDR
#define RECOMP_RETADDR 0xDEAD0000u
#endif

/* Direct call to a known recompiled function.
 * ponytail: ebx/esi/edi are saved here as well as by the callee's own lifted
 * push/pop. That is ABI-correct, but it also MASKS a stack imbalance in the
 * callee -- drop the three _save_ lines to see such a bug raw. */
#define RECOMP_CALL(func) do { \
    uint32_t _esp_before = esp; \
    uint32_t _caller = g_cur_func; \
    uint32_t _save_ebx = g_ebx, _save_esi = g_esi, _save_edi = g_edi; \
    PUSH32(esp, RECOMP_RETADDR); /* dummy return address */ \
    g_total_calls++; \
    if (++g_call_depth > g_call_depth_max) g_call_depth_max = g_call_depth; \
    TRACE_LOG("[CALL %u d%u] -> %s\n", g_total_calls, g_call_depth, #func); \
    func(); \
    TRACE_LOG("[RET  %u d%u] <- %s\n", g_total_calls, g_call_depth, #func); \
    g_call_depth--; \
    g_cur_func = _caller;  /* the callee RECOMP_ENTER clobbered it */ \
    recomp_esp_check(#func, _esp_before, esp); \
    g_ebx = _save_ebx; g_esi = _save_esi; g_edi = _save_edi; \
    RECOMP_HEAP_CHECK(0, #func); \
} while(0)

/* Indirect call through dispatch */
#define RECOMP_ICALL(target_va) do { \
    uint32_t _va = (uint32_t)(target_va); \
    uint32_t _ova; if (recomp_thunk_ova(_va, &_ova)) _va = _ova; \
    g_icall_trace[g_icall_trace_idx & (ICALL_TRACE_SIZE-1)] = _va; \
    g_icall_trace_idx++; \
    g_icall_count++; \
    g_total_icalls++; \
    uint32_t _save_ebx = g_ebx, _save_esi = g_esi, _save_edi = g_edi; \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_import(_va); \
    if (_fn) { \
        uint32_t _caller = g_cur_func; \
        PUSH32(esp, RECOMP_RETADDR); \
        if (++g_call_depth > g_call_depth_max) g_call_depth_max = g_call_depth; \
        TRACE_LOG("[ICALL %u d%u] -> 0x%08X\n", g_total_icalls, g_call_depth, _va); \
        _fn();  /* its lifted `ret` pops the dummy return address */ \
        TRACE_LOG("[IRET  %u d%u] <- 0x%08X\n", g_total_icalls, g_call_depth, _va); \
        g_call_depth--; \
        g_cur_func = _caller;  /* the callee RECOMP_ENTER clobbered it */ \
    } else { \
        /* A natively resolved address: it never sees the dummy return \
         * address, so this macro both pushes and pops it. generic_bridge \
         * does ABI argument cleanup only. */ \
        PUSH32(esp, RECOMP_RETADDR); \
        TRACE_LOG("[ICALL %u d%u] -> native 0x%08X\n", g_total_icalls, g_call_depth, _va); \
        if (!recomp_native_call(_va)) { \
            fprintf(stderr, "ICALL: unresolved VA 0x%08X from 0x%08X\n", _va, g_cur_func); \
            eax = 0; \
        } \
        esp += 4; \
    } \
    g_ebx = _save_ebx; g_esi = _save_esi; g_edi = _save_edi; \
    RECOMP_HEAP_CHECK(_va, "ICALL"); \
} while(0)

/* Indirect tail call (jmp through dispatch) */
#define RECOMP_ITAIL(target_va) do { \
    uint32_t _va = (uint32_t)(target_va); \
    uint32_t _ova; if (recomp_thunk_ova(_va, &_ova)) _va = _ova; \
    g_icall_trace[g_icall_trace_idx & (ICALL_TRACE_SIZE-1)] = _va; \
    g_icall_trace_idx++; \
    g_icall_count++; \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_import(_va); \
    if (_fn) { \
        if (++g_call_depth > g_call_depth_max) g_call_depth_max = g_call_depth; \
        TRACE_LOG("[ITAIL %u d%u] -> 0x%08X\n", g_total_icalls, g_call_depth, _va); \
        _fn(); \
        g_call_depth--; \
    } else { \
        TRACE_LOG("[ITAIL %u d%u] -> native 0x%08X\n", \
                  g_total_icalls, g_call_depth, _va); \
        if (!recomp_native_call(_va)) { \
        fprintf(stderr, "ITAIL: unresolved VA 0x%08X from 0x%08X (call #%u, esp 0x%08X)\n", \
                _va, g_cur_func, g_total_calls, g_esp); \
        } \
        esp += 4;  /* stand in for the native callee's `ret` */ \
    } \
} while(0)

/* ============================================================
 * Optional function-entry tracer (enable with -DRECOMP_TRACE).
 *
 * Each lifted function records its VA into a ring buffer on entry, so a crash
 * or unexpected exit can dump the last N functions that ran -- a poor-man's
 * backtrace when no debugger is available. Zero cost unless RECOMP_TRACE is set.
 * ============================================================ */
/* Always-on: the VA of the function currently executing. A plain global store
 * (no call), so unlike the ring tracer below it doesn't force register reloads --
 * useful for pinning a crash to a function without perturbing codegen. */
extern uint32_t g_cur_func;

#ifdef RECOMP_TRACE
#define RECOMP_ENTER_SIZE 1024
extern uint32_t g_enter_trace[RECOMP_ENTER_SIZE];
extern uint32_t g_enter_idx;
void recomp_trace_enter(uint32_t va);
#define RECOMP_ENTER(va) do { g_cur_func = (va); RECOMP_COV(va); recomp_trace_enter(va); } while (0)
#else
#define RECOMP_ENTER(va) do { g_cur_func = (va); RECOMP_COV(va); } while (0)
#endif
/* Execution coverage: one bit per byte of the original code range, set by
 * RECOMP_ENTER. Cheap enough to leave on -- a masked store per function entry
 * -- and it is the only way to distinguish "this subsystem works" from "this
 * subsystem is never reached". */
#define COV_LO   0x00401000u
#define COV_HI   0x004D5000u   /* FAKK2 .text end (0x004D4905, page-rounded) */
#define COV_BYTES ((COV_HI - COV_LO) / 8u)
extern unsigned char g_cov[COV_BYTES];

#define RECOMP_COV(va) do { \
    uint32_t _cv = (uint32_t)(va) - COV_LO; \
    if (_cv < (COV_HI - COV_LO)) g_cov[_cv >> 3] |= (unsigned char)(1u << (_cv & 7u)); \
} while (0)

/* Print "N of M functions executed" over the dispatch table. */
void recomp_report_coverage(const char* why);

/* Always-callable trace dump (no-op unless RECOMP_TRACE). */
void recomp_dump_trace(const char* why);

/* Stub macro for unimplemented imports */
#define STUB(name) do { \
    static int _warned = 0; \
    if (!_warned) { fprintf(stderr, "STUB: %s called\n", name); _warned = 1; } \
} while(0)

/* Win32 heap validation, used by RECOMP_HEAP_CHECK above. */
#ifdef _WIN32
#ifndef _WINDOWS_  /* avoid redeclaration if windows.h is already included */
__declspec(dllimport) void* __stdcall GetProcessHeap(void);
__declspec(dllimport) int   __stdcall HeapValidate(void*, unsigned long, const void*);
#endif
#else
#define GetProcessHeap()      ((void*)0)
#define HeapValidate(h, f, p) 1
#endif

#endif /* RECOMP_TYPES_H */
