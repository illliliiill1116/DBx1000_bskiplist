#ifndef SIMD_ATOMICS
#define SIMD_ATOMICS

#include <stdint.h>
#include <emmintrin.h>   // SSE2 (_mm_load_si128)
#include <smmintrin.h>   // SSE4.1 (_mm_extract_epi64)
#include <assert.h>

static inline __attribute__((always_inline)) void read_16_bytes_atomic(const void *src, uint64_t *dest_lo, uint64_t *dest_hi) {
    // MOVDQA requires 16-byte alignment
    assert(((uintptr_t)src & 0xF) == 0);
#if defined(__SSE4_1__)
    __m128i xmm = _mm_load_si128((const __m128i *)src); // atomic 128-bit load
    // extract lower and upper 64 bits
    *dest_lo = _mm_extract_epi64(xmm, 0); // low 64
    *dest_hi = _mm_extract_epi64(xmm, 1); // high 64
#else
    unsigned long low, high;
    asm volatile(
        "movdqa   (%[src]), %%xmm0\n\t"
        "movq     %%xmm0, %[lo]\n\t"       /* move low 64 bits */
        "psrldq   $8, %%xmm0\n\t"          /* shift high down */
        "movq     %%xmm0, %[hi]\n\t"       /* move high 64 bits */
        : [lo]"=r"(low), [hi]"=r"(high)
        : [src]"r"(src)
        : "xmm0", "memory"
    );
    *dest_lo = low;
    *dest_hi = high;
#endif // __SSE4_1__
}

static inline __attribute__((always_inline)) void write_16_bytes_atomic(uint64_t src_lo, uint64_t src_hi, void *dest) {
    // MOVDQA requires 16-byte alignment
    assert(((uintptr_t)dest & 0xF) == 0);
#if defined(__SSE4_1__)
    __m128i xmm = _mm_set_epi64x(src_hi, src_lo); // hi in upper 64, lo in lower 64
    _mm_store_si128((__m128i *)dest, xmm);        // atomic 128-bit store
#else
    asm volatile(
        "movq %[lo], %%xmm0\n\t"        /* low 64 -> xmm0 low */
        "movq %[hi], %%xmm1\n\t"        /* high 64 -> xmm1 low */
        "pslldq $8, %%xmm1\n\t"         /* shift high 64 into upper half */
        "por %%xmm1, %%xmm0\n\t"        /* combine into xmm0 */
        "movdqa %%xmm0, (%[dest])\n\t"  /* atomic 128-bit store */
        :
        : [lo]"r"(src_lo), [hi]"r"(src_hi), [dest]"r"(dest)
        : "xmm0", "xmm1", "memory"
    );
#endif // __SSE4_1__
}

#endif // SIMD_ATOMICS