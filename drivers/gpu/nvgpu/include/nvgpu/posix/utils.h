/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef NVGPU_POSIX_UTILS_H
#define NVGPU_POSIX_UTILS_H

#include <nvgpu/static_analysis.h>

/**
 * @brief Minimum of two values using the specified type.
 *
 * @param type	Type of the input values.
 * @param a	First value.
 * @param b	Second value.
 *
 * @return Returns the minimum value from \a a and \a b.
 */
#define min_t(type, a, b)		\
	({				\
		type __a = (a);		\
		type __b = (b);		\
		__a < __b ? __a : __b;	\
	})

#if defined(min)
#undef min
#endif
#if defined(max)
#undef max
#endif

/**
 * @brief Minimum of two values.
 *
 * @param a	First value.
 * @param b	Second value.
 *
 * @return Returns the minimum value from \a a and \a b.
 */
#define min(a, b)			\
	({				\
		(a) < (b) ? (a) : (b);	\
	})

/**
 * @brief Maximum of two values.
 *
 * @param a	First value.
 * @param b	Second value.
 *
 * @return Returns the maximum value from \a a and \a b.
 */
#define max(a, b)			\
	({				\
		(a) > (b) ? (a) : (b);	\
	})

/**
 * @brief Minimum of three values.
 *
 * @param a	First value.
 * @param b	Second value.
 * @param c	Third value.
 *
 * @return Returns the minimum value from \a a, \a b and \a c.
 */
#define min3(a, b, c)			min(min(a, b), c)

/** Size of a Page. */
#define PAGE_SIZE	4096U

/**
 * @brief Number of elements in an array.
 *
 * @param array	Input array.
 *
 * @return Returns the number of elements in \a array.
 */
#define ARRAY_SIZE(array)			\
	(sizeof(array) / sizeof((array)[0]))

/** Maximum schedule timeout. */
#define MAX_SCHEDULE_TIMEOUT	LONG_MAX

/**
 * @brief Round up division for unsigned long.
 *
 * @param n	Numerator.
 * @param d	Denominator.
 *
 * @return Returns the rounded up 64 bit division result.
 */
#define DIV_ROUND_UP_U64(n, d)					\
({								\
	u64 divr = (u64)(d);					\
	u64 divm = nvgpu_safe_sub_u64((divr), U64(1));		\
	u64 rup = nvgpu_safe_add_u64((u64)(n), (divm));		\
	u64 ret_val = ((rup) / (divr));				\
	ret_val ;						\
})

/**
 * @brief Round up division.
 *
 * @param n	Numerator.
 * @param d	Denominator.
 *
 * @return Returns the rounded up division result.
 */
#define DIV_ROUND_UP(n, d)						\
({									\
	typeof(n) val =  ((sizeof(typeof(n))) == (sizeof(u64))) ?	\
		(typeof(n))(DIV_ROUND_UP_U64(n, d)) :			\
		nvgpu_safe_cast_u64_to_u32(DIV_ROUND_UP_U64(n, d));	\
	val;								\
})

/** Round up division for unsigned long long. */
#define DIV_ROUND_UP_ULL	DIV_ROUND_UP

/**
 * Divide positive or negative dividend by positive or negative divisor
 * and round to closest integer.
 */
#define DIV_ROUND_CLOSEST(a, divisor)(                  \
{                                                       \
	typeof(a) val = (a);                            \
	typeof(divisor) div = (divisor);                \
	(((typeof(a))-1) > 0 ||                         \
	((typeof(divisor))-1) > 0 || (val) > 0) ?       \
		(((val) + ((div) / 2)) / (div)) :       \
		(((val) - ((div) / 2)) / (div));        \
}                                                       \
)
/*
 * Joys of userspace: usually division just works since the compiler can link
 * against external division functions implicitly.
 */

/**
 * @brief Division of two values.
 *
 * @param a	Dividend, should be an lvalue.
 * @param b	Divisor.
 *
 * @return Param \a a is updated with the quotient value of the division.
 */
#define do_div(a, b)		((a) /= (b))

/**
 * @brief Division of two 64 bit values.
 *
 * @param a	Dividend.
 * @param b	Divisor.
 *
 * @return Quotient is returned.
 */
#define div64_u64(a, b)		((a) / (b))

/**
 *  @brief Generate mask value for round operations.
 *
 *  @param x	Data type of this param is used to type cast.
 *  @param y	Value for which the mask is generated.
 *
 *  @return Mask value based on \a y is returned.
 */
#define round_mask(x, y)	((__typeof__(x))((y) - 1U))

/**
 * @brief Round up the value of its argument \a x.
 *
 * @param x	Value to be rounded.
 * @param y	Value to be used to round up x.
 *
 * @return Rounded up value of \a x.
 */
#define round_up(x, y)		((((x) - 1U) | round_mask(x, y)) + 1U)

/**
 * @brief Wrapper define for #round_up.
 *
 * @param x	Value to be rounded.
 * @param y	Value to be used to round up x.
 *
 * @return Rounded up value of \a x.
 */
#define roundup(x, y)		round_up(x, y)

/**
 * @brief Round down the value of its argument \a x.
 *
 * @param x	Value to be rounded.
 * @param y	Value to be used to round down x.
 *
 * @return Rounded down value of \a x.
 */
#define round_down(x, y)	((x) & ~round_mask(x, y))

/**
 * @brief To identify whether the data type of input value is unsigned.
 *
 * @param x	Input value.
 *
 * @return Returns TRUE for unsigned data types, FALSE otherwise.
 */
#define IS_UNSIGNED_TYPE(x)						\
	(__builtin_types_compatible_p(typeof(x), unsigned int) ||	\
		__builtin_types_compatible_p(typeof(x), unsigned long) || \
		__builtin_types_compatible_p(typeof(x), unsigned long long))

/**
 * @brief To identify whether the data type of input value is unsigned long.
 *
 * @param x	Input value.
 *
 * @return Returns TRUE for unsigned long data types, FALSE otherwise.
 */
#define IS_UNSIGNED_LONG_TYPE(x)					\
	(__builtin_types_compatible_p(typeof(x), unsigned long) ||	\
		__builtin_types_compatible_p(typeof(x), unsigned long long))

/**
 * @brief To identify whether the data type of input value is signed long.
 *
 * @param x	Input value.
 *
 * @return Returns TRUE for signed long data types, FALSE otherwise.
 */
#define IS_SIGNED_LONG_TYPE(x)						\
	(__builtin_types_compatible_p(typeof(x), signed long) ||	\
		__builtin_types_compatible_p(typeof(x), signed long long))

/**
 * @brief Align with mask value.
 *
 * @param x	Value to be aligned.
 * @param mask	Mask value to align with.
 *
 * @return Returns \a x aligned with \a mask.
 */
#define ALIGN_MASK(x, mask)						\
	__builtin_choose_expr(						\
		(IS_UNSIGNED_TYPE(x) && IS_UNSIGNED_TYPE(mask)),	\
		__builtin_choose_expr(					\
			IS_UNSIGNED_LONG_TYPE(x),			\
			(nvgpu_safe_add_u64((x), (mask)) & ~(mask)),	\
			(nvgpu_safe_add_u32((x), (mask)) & ~(mask))),	\
		/* Results in build error. Make x/mask type unsigned */ \
		(void)0)

/**
 * @brief Align the parameter \a x with \a a.
 *
 * @param x	Value to be aligned.
 * @param mask	Value to align with.
 *
 * @return Returns \a x aligned with the value mentioned in \a a.
 */
#define ALIGN(x, a)							\
	__builtin_choose_expr(						\
		(IS_UNSIGNED_TYPE(x) && IS_UNSIGNED_TYPE(a)),		\
		__builtin_choose_expr(					\
			IS_UNSIGNED_LONG_TYPE(x),			\
				ALIGN_MASK((x),				\
				(nvgpu_safe_sub_u64((typeof(x))(a), 1))), \
				ALIGN_MASK((x),				\
				(nvgpu_safe_sub_u32((typeof(x))(a), 1)))), \
			/* Results in build error. Make x/a type unsigned */ \
			(void)0)

/**
 * @brief Align with #PAGE_SIZE.
 *
 * @param x	Input value to be aligned.
 *
 * @return Returns \a x aligned with the page size value.
 */
#define PAGE_ALIGN(x)		ALIGN(x, PAGE_SIZE)

/**
 * @brief Convert hertz to kilo hertz.
 *
 * @param x	Value to convert.
 *
 * @return Converts \a x into kilo hertz and returns.  Fractional value is not
 * obtained in the conversion.
 */
#define HZ_TO_KHZ(x) ((x) / KHZ)

/**
 * @brief Convert hertz to mega hertz.
 *
 * @param x	Value to convert.
 *
 * @return Converts \a x into mega hertz and returns.  Fractional value is not
 * obtained in the conversion.
 */
#define HZ_TO_MHZ(a) (u16)(a/MHZ)

/**
 * @brief Convert hertz value in unsigned long long to mega hertz.
 *
 * @param x	Value to convert.
 *
 * @return Converts \a a into mega hertz and returns.
 */
#define HZ_TO_MHZ_ULL(a) (((a) > 0xF414F9CD7ULL) ? (u16) 0xffffU :\
		(((a) >> 32) > 0U) ? (u16) (((a) * 0x10C8ULL) >> 32) :\
		(u16) ((u32) (a)/MHZ))

/**
 * @brief Convert kilo hertz to hertz.
 *
 * @param x	Value to convert.
 *
 * @return Equivalent value of \a x in hertz.
 */
#define KHZ_TO_HZ(x) ((x) * KHZ)

/**
 * @brief Convert mega hertz to kilo hertz.
 *
 * @param x	Value to convert.
 *
 * @return Equivalent value of \a x in hertz.
 */
#define MHZ_TO_KHZ(x) ((x) * KHZ)

/**
 * @brief Convert kilo hertz to mega hertz.
 *
 * @param x	Value to convert.
 *
 * @return Equivalent value of \a a in mega hertz.
 */
#define KHZ_TO_MHZ(a) (u16)(a/KHZ)

/**
 * @brief Convert mega hertz to 64 bit hertz value.
 *
 * @param x	Value to convert.
 *
 * @return Equivalent value of \a a in 64 bit hertz.
 */
#define MHZ_TO_HZ_ULL(a) ((u64)(a) * MHZ)

/*
 * Caps return at the size of the buffer not what would have been written if buf
 * were arbitrarily sized.
 */
#ifdef CONFIG_NVGPU_LOGGING
/**
 * @brief Format a string and store it in buffer.
 *
 * @param buf [in,out]	Buffer to hold the formatted string.
 * @param size [in]	Size of the buf.
 * @param format [in]	Format of the input string.
 * @param args [in]	Variable length args.
 *
 * Formats the string and places it in the location referenced by input
 * parameter buf.
 *
 * @return Returns the number of characters printed in a succesful scenario,
 * Otherwise returns the error code.
 */
static inline int scnprintf(char *buf, size_t size, const char *format, ...)
{
	size_t ret;
	va_list args;

	va_start(args, format);
	ret = (size_t)vsnprintf(buf, size, format, args);
	va_end(args);

	return ret <= size ? (int)ret : (int)size;
}
#else
/**
 * @brief Dummy function for scnprintf if logging is disabled.
 *
 * @param buf [in,out]	Buffer to hold the formatted string.
 * @param size [in]	Size of the buf.
 * @param format [in]	Format of the input string.
 * @param args [in]	Variable length args.
 *
 * @return Returns 0.
 */
static inline int scnprintf(char *buf, size_t size, const char *format, ...)
{
	return 0;
}
#endif

/**
 * @brief Endian conversion.
 *
 * @param x [in] Value to be converted.
 *
 * Converts the input value x in big endian to CPU byte order.
 *
 * @return Endianness converted value of \a x.
 */
static inline u32 be32_to_cpu(u32 x)
{
	/*
	 * Conveniently big-endian happens to be network byte order as well so
	 * we can use ntohl() for this.
	 */
	return ntohl(x);
}

/*
 * Hamming weights.
 */

/**
 * @brief Hamming weight of 8 bit input value.
 *
 * @param x [in]	Input to find the hamming weight of.
 *
 * Returns the hamming weight(number of non zero bits) of the input param \a x.
 *
 * @return Hamming weight of \a x.
 */
static inline unsigned int nvgpu_posix_hweight8(uint8_t x)
{
	unsigned int ret;
	uint8_t result = ((U8(x) >> U8(1)) & U8(0x55));

	result = nvgpu_safe_sub_u8(x, result);

	result = (result & U8(0x33)) + ((result >> U8(2)) & U8(0x33));
	result = (result + (result >> U8(4))) & U8(0x0f);
	ret = (unsigned int)result;

	return ret;
}

/**
 * @brief Hamming weight of 16 bit input value.
 *
 * @param x [in]	Input to find the hamming weight of.
 *
 * Returns the hamming weight(number of non zero bits) of the input param \a x.
 *
 * @return Hamming weight of \a x.
 */
static inline unsigned int nvgpu_posix_hweight16(uint16_t x)
{
	unsigned int ret;

	ret = nvgpu_posix_hweight8((uint8_t)(x & U8(0xff)));
	ret += nvgpu_posix_hweight8((uint8_t)((x >> U8(8)) & U8(0xff)));

	return ret;
}

/**
 * @brief Hamming weight of 32 bit input value.
 *
 * @param x [in]	Input to find the hamming weight of.
 *
 * Returns the hamming weight(number of non zero bits) of the input param \a x.
 *
 * @return Hamming weight of \a x.
 */
static inline unsigned int nvgpu_posix_hweight32(uint32_t x)
{
	unsigned int ret;

	ret = nvgpu_posix_hweight16((uint16_t)(x & U16(0xffff)));
	ret += nvgpu_posix_hweight16((uint16_t)((x >> U16(16)) & U16(0xffff)));

	return ret;
}

/**
 * @brief Hamming weight of 64 bit input value.
 *
 * @param x [in]	Input to find the hamming weight of.
 *
 * Returns the hamming weight(number of non zero bits) of the input param \a x.
 *
 * @return Hamming weight of \a x.
 */
static inline unsigned int nvgpu_posix_hweight64(uint64_t x)
{
	unsigned int ret;
	u32 lo, hi;

	lo = nvgpu_safe_cast_u64_to_u32(x & ~(u32)0);
	hi = nvgpu_safe_cast_u64_to_u32(x >> 32) & ~(u32)0;

	ret =  nvgpu_posix_hweight32(lo);
	ret += nvgpu_posix_hweight32(hi);

	return ret;
}

/** Hamming weight of 32 bit input value. */
#define hweight32		nvgpu_posix_hweight32

/** Hamming weight of 64 bit input value. */
#define hweight_long		nvgpu_posix_hweight64

/*
 * Better suited under a compiler.h type header file, but for now these can live
 * here.
 */
/** Compiler extension directive to enure that a return value is checked. */
#define __must_check		__attribute__((warn_unused_result))

#ifndef __user
/** Defined to match kernel macro names. */
#define __user
#else
#error "__user already defined !!"
#endif

/** Defined to match kernel macro names. */
#define unlikely(x)	(x)
/** Defined to match kernel macro names. */
#define likely(x)	(x)

/*
 * Prevent compiler optimizations from mangling writes. But likely most uses of
 * this in nvgpu are incorrect (i.e unnecessary).
 */
/**
 * @brief Ensure ordered writes.
 *
 * @param p	Variable to be updated.
 * @param v	Value to be written.
 */
#define WRITE_ONCE(p, v)				\
	({						\
		volatile typeof(p) *__p__ = &(p);	\
		*__p__ = (v);				\
	})

/**
 * @brief Get the container which holds the member.
 *
 * @param ptr		Address of the member.
 * @param type		Type of the container holding member.
 * @param member	The member name in the container.
 *
 * @return Reference to the container holding \a member.
 */
#define container_of(ptr, type, member) ({                    \
	typeof(((type *)0)->member) *__mptr = (ptr);    \
	(type *)((uintptr_t)__mptr - offsetof(type, member)); })

/** Define for maximum error number. */
#define MAX_ERRNO	4095

/** Error define to indicate that a system call should restart. */
#define ERESTARTSYS ERESTART

#endif /* NVGPU_POSIX_UTILS_H */
