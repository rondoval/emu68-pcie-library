// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _COMPAT_H
#define _COMPAT_H

#include <exec/types.h>

#define EINVAL 1
#define EIO 2
#define ETIMEDOUT 3
#define ENODEV 4
#define ENOENT 5
#define ENOSYS 6
#define ENOMEM 7
#define EPERM 8

typedef uint64_t u64;

#define ARCH_DMA_MINALIGN	64

#define SZ_1M				0x00100000
#define SZ_64M				0x04000000

/**
 * upper_32_bits - return bits 32-63 of a number
 * @n: the number we're accessing
 *
 * A basic shift-right of a 64- or 32-bit quantity.  Use this to suppress
 * the "right shift count >= width of type" warning when that quantity is
 * 32-bits.
 */
#define upper_32_bits(n) ((ULONG)(((n) >> 16) >> 16))

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#define lower_32_bits(n) ((ULONG)(n))

static inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

/**
 * fls64 - find last set bit in a 64-bit word
 * @x: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin
 * ffsll, but returns the position of the most significant set bit.
 *
 * fls64(value) returns 0 if value is 0 or the position of the last
 * set bit if value is nonzero. The last (most significant) bit is
 * at position 64.
 */
static inline int fls64(u64 x)
{
	ULONG h = x >> 32;
	if (h)
		return fls(h) + 32;
	return fls(x);
}

static inline __attribute__((const))
int __ilog2_u32(ULONG n)
{
	return fls(n) - 1;
}

static inline __attribute__((const))
int __ilog2_u64(u64 n)
{
	return fls64(n) - 1;
}

/**
 * ilog2 - log base 2 of 32-bit or a 64-bit unsigned value
 * @n: parameter
 *
 * constant-capable log of base 2 calculation
 * - this can be used to initialise global variables from constant data, hence
 * the massive ternary operator construction
 *
 * selects the appropriately-sized optimised version depending on sizeof(n)
 */
#define ilog2(n)				\
(						\
	__builtin_constant_p(n) ? (		\
		(n) < 2 ? 0 :			\
		(n) & (1ULL << 63) ? 63 :	\
		(n) & (1ULL << 62) ? 62 :	\
		(n) & (1ULL << 61) ? 61 :	\
		(n) & (1ULL << 60) ? 60 :	\
		(n) & (1ULL << 59) ? 59 :	\
		(n) & (1ULL << 58) ? 58 :	\
		(n) & (1ULL << 57) ? 57 :	\
		(n) & (1ULL << 56) ? 56 :	\
		(n) & (1ULL << 55) ? 55 :	\
		(n) & (1ULL << 54) ? 54 :	\
		(n) & (1ULL << 53) ? 53 :	\
		(n) & (1ULL << 52) ? 52 :	\
		(n) & (1ULL << 51) ? 51 :	\
		(n) & (1ULL << 50) ? 50 :	\
		(n) & (1ULL << 49) ? 49 :	\
		(n) & (1ULL << 48) ? 48 :	\
		(n) & (1ULL << 47) ? 47 :	\
		(n) & (1ULL << 46) ? 46 :	\
		(n) & (1ULL << 45) ? 45 :	\
		(n) & (1ULL << 44) ? 44 :	\
		(n) & (1ULL << 43) ? 43 :	\
		(n) & (1ULL << 42) ? 42 :	\
		(n) & (1ULL << 41) ? 41 :	\
		(n) & (1ULL << 40) ? 40 :	\
		(n) & (1ULL << 39) ? 39 :	\
		(n) & (1ULL << 38) ? 38 :	\
		(n) & (1ULL << 37) ? 37 :	\
		(n) & (1ULL << 36) ? 36 :	\
		(n) & (1ULL << 35) ? 35 :	\
		(n) & (1ULL << 34) ? 34 :	\
		(n) & (1ULL << 33) ? 33 :	\
		(n) & (1ULL << 32) ? 32 :	\
		(n) & (1ULL << 31) ? 31 :	\
		(n) & (1ULL << 30) ? 30 :	\
		(n) & (1ULL << 29) ? 29 :	\
		(n) & (1ULL << 28) ? 28 :	\
		(n) & (1ULL << 27) ? 27 :	\
		(n) & (1ULL << 26) ? 26 :	\
		(n) & (1ULL << 25) ? 25 :	\
		(n) & (1ULL << 24) ? 24 :	\
		(n) & (1ULL << 23) ? 23 :	\
		(n) & (1ULL << 22) ? 22 :	\
		(n) & (1ULL << 21) ? 21 :	\
		(n) & (1ULL << 20) ? 20 :	\
		(n) & (1ULL << 19) ? 19 :	\
		(n) & (1ULL << 18) ? 18 :	\
		(n) & (1ULL << 17) ? 17 :	\
		(n) & (1ULL << 16) ? 16 :	\
		(n) & (1ULL << 15) ? 15 :	\
		(n) & (1ULL << 14) ? 14 :	\
		(n) & (1ULL << 13) ? 13 :	\
		(n) & (1ULL << 12) ? 12 :	\
		(n) & (1ULL << 11) ? 11 :	\
		(n) & (1ULL << 10) ? 10 :	\
		(n) & (1ULL <<  9) ?  9 :	\
		(n) & (1ULL <<  8) ?  8 :	\
		(n) & (1ULL <<  7) ?  7 :	\
		(n) & (1ULL <<  6) ?  6 :	\
		(n) & (1ULL <<  5) ?  5 :	\
		(n) & (1ULL <<  4) ?  4 :	\
		(n) & (1ULL <<  3) ?  3 :	\
		(n) & (1ULL <<  2) ?  2 :	\
		1) :				\
	(sizeof(n) <= 4) ?			\
	__ilog2_u32(n) :			\
	__ilog2_u64(n)				\
 )

inline ULONG LE32(ULONG x) { return __builtin_bswap32(x); }

inline UWORD LE16(UWORD x) { return __builtin_bswap16(x); }

//TODO get the address out of device tree
#define get_time() (LE32(*(volatile ULONG *)0xf2003004))

inline void delay_us(ULONG us)
{
    ULONG timer = get_time();
    ULONG end = timer + us;

    if (end < timer)
    {
        while (end < get_time())
            asm volatile("nop");
    }
    while (end > get_time())
        asm volatile("nop");
}

inline void _memset(APTR dst, UBYTE val, ULONG len)
{
    UBYTE *d = (UBYTE *)dst;
    for (ULONG i = 0; i < len; i++)
        d[i] = val;
}

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

inline static ULONG roundup(ULONG x, ULONG y)
{
    return ((x + y - 1) / y) * y;
}

inline static ULONG rounddown(ULONG x, ULONG y)
{
    return x - (x % y);
}

#define BIT(nr) (1UL << (nr))

#define readl(addr) in_le32((volatile ULONG *)(addr))
#define writel(b, addr) out_le32((volatile ULONG *)(addr), (b))

#define readw(addr) in_le16((volatile UWORD *)(addr))
#define writew(b, addr) out_le16((volatile UWORD *)(addr), (b))

#define readb(addr) (*(volatile UBYTE *)(addr))
#define writeb(b, addr) (*(volatile UBYTE *)(addr) = (b))

static inline ULONG in_le32(volatile ULONG *addr)
{
    ULONG val = LE32(*(volatile ULONG *)addr);
    // asm volatile("nop");
    return val;
}

static inline void out_le32(volatile ULONG *addr, ULONG val)
{
    *(volatile ULONG *)addr = LE32(val);
    // asm volatile("nop");
}

static inline UWORD in_le16(volatile UWORD *addr)
{
    UWORD val = LE16(*(volatile UWORD *)addr);
    // asm volatile("nop");
    return val;
}

static inline void out_le16(volatile UWORD *addr, UWORD val)
{
    *(volatile UWORD *)addr = LE16(val);
    // asm volatile("nop");
}

#define clrbits_32(addr, clear) clrbits(le32, addr, clear)
#define setbits_32(addr, set) setbits(le32, addr, set)
#define clrsetbits_32(addr, clear, set) clrsetbits(le32, addr, clear, set)

#define clrbits_le32(addr, clear) clrbits(le32, addr, clear)
#define setbits_le32(addr, set) setbits(le32, addr, set)
#define clrsetbits_le32(addr, clear, set) clrsetbits(le32, addr, clear, set)

#define clrbits_le16(addr, clear) clrbits(le16, addr, clear)
#define setbits_le16(addr, set) setbits(le16, addr, set)
#define clrsetbits_le16(addr, clear, set) clrsetbits(le16, addr, clear, set)

#define clrbits(type, addr, clear) \
    out_##type((addr), in_##type(addr) & ~(clear))

#define setbits(type, addr, set) \
    out_##type((addr), in_##type(addr) | (set))

#define clrsetbits(type, addr, clear, set) \
    out_##type((addr), (in_##type(addr) & ~(clear)) | (set))


#define readl_poll_timeout(addr, val, cond, timeout_us) \
	readx_poll_timeout(readl, addr, val, cond, timeout_us)

#define readx_poll_timeout(op, addr, val, cond, timeout_us) \
	read_poll_timeout(op, val, cond, FALSE, timeout_us, addr)

#define time_after(a,b) ((LONG)((b) - (a)) < 0)

/**
 * read_poll_timeout - Periodically poll an address until a condition is met or a timeout occurs
 * @op: accessor function (takes @addr as its only argument)
 * @val: Variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep in us
 * @timeout_us: Timeout in us, 0 means never timeout
 * @args: arguments for @op poll
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val.
 *
 * When available, you'll probably want to use one of the specialized
 * macros defined below rather than this macro directly.
 */
#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, args...)	\
({ \
	unsigned long timeout = get_time() + timeout_us; \
	for (;;) { \
		(val) = op(args); \
		if (cond) \
			break; \
		if (timeout_us && time_after(get_time(), timeout)) { \
			(val) = op(args); \
			break; \
		} \
		if (sleep_us) \
			delay_us(sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

static inline u64 field_multiplier(u64 field)
{
	return field & -field;
}
static inline u64 field_mask(u64 field)
{
	return field / field_multiplier(field);
}

static inline ULONG u32_encode_bits(ULONG v, ULONG field)
{									
	return ((v & field_mask(field)) * field_multiplier(field));	
}									
static inline void u32p_replace_bits(ULONG *p,	ULONG val, ULONG field)		
{									
	*p = (*p & ~field) | u32_encode_bits(val, field);	
}

#endif