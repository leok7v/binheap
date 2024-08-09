#ifndef huff_definition
#define huff_definition

#include <errno.h>
#include <stdint.h>

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

typedef struct huff_s huff_t;

typedef struct huff_s {
    // `that` see: https://gist.github.com/leok7v/8d118985d3236b0069d419166f4111cf
    void*    that;  // caller supplied data
    errno_t  error; // sticky; for read()/write() compress() and decompress()
    // caller supplied read()/write() must error via .error field
    uint64_t (*read)(huff_t*); //  reads 64 bits
    void     (*write)(huff_t*, uint64_t b64); // writes 64 bits
    uint64_t written;
} huff_t;

typedef struct huff_if {
    // `window_bits` is a log2 of window size in bytes must be in range [10..20]
    void (*write_header)(huff_t* huff, size_t bytes, uint8_t window_bits);
    void (*compress)(huff_t* huff, const uint8_t* data, size_t bytes,
                     uint8_t window_bits);
    void (*read_header)(huff_t* huff, size_t *bytes, uint8_t *window_bits);
    void (*decompress)(huff_t* huff, uint8_t* data, size_t bytes,
                       uint8_t window_bits);
    // Writing and reading envelope of source data `bytes` and
    // `window_bits` is caller's responsibility.
} huff_if;

extern huff_if huff;

#endif // huff_definition

#if defined(huff_implementation) && !defined(huff_implemented)

#define huff_implemented

#ifndef huff_assert
#define huff_assert(...) do { } while (0)
#endif

#ifndef huff_println
#define huff_println(...) do { } while (0)
#endif

#define huff_if_error_return(lz);do {                   \
    if (lz->error) { return; }                          \
} while (0)

#define huff_return_invalid(lz) do {                    \
    lz->error = EINVAL;                                 \
    return;                                             \
} while (0)

#ifdef huff_historgram

static inline uint32_t huff_bit_count(size_t v) {
    uint32_t count = 0;
    while (v) { count++; v >>= 1; }
    return count;
}

static size_t huff_hist_len[64];
static size_t huff_hist_pos[64];

#define huff_init_histograms() do {                     \
    memset(huff_hist_pos, 0x00, sizeof(huff_hist_pos)); \
    memset(huff_hist_len, 0x00, sizeof(huff_hist_len)); \
} while (0)

#define  huff_histogram_pos_len(pos, len) do {          \
    huff_hist_pos[huff_bit_count(pos)]++;               \
    huff_hist_len[huff_bit_count(len)]++;               \
} while (0)

#define huff_dump_histograms() do {                                 \
    huff_println("Histogram log2(len):");                           \
    for (int8_t i_ = 0; i_ < 64; i_++) {                            \
        if (huff_hist_len[i_] > 0) {                                \
            huff_println("len[%d]: %lld", i_, huff_hist_len[i_]);   \
        }                                                           \
    }                                                               \
    huff_println("Histogram log2(pos):");                           \
    for (int8_t i_ = 0; i_ < 64; i_++) {                            \
        if (huff_hist_pos[i_] > 0) {                                \
            huff_println("pos[%d]: %lld", i_, huff_hist_pos[i_]);   \
        }                                                           \
    }                                                               \
} while (0)

#else

#define huff_init_histograms()           do { } while (0)
#define huff_histogram_pos_len(pos, len) do { } while (0)
#define huff_dump_histograms()           do { } while (0)

#endif

static inline void huff_write_bit(huff_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bit) {
    if (*bp == 64 && lz->error == 0) {
        lz->write(lz, *b64);
        *b64 = 0;
        *bp = 0;
        if (lz->error == 0) { lz->written += 8; }
    }
    *b64 |= bit << *bp;
    (*bp)++;
}

static inline void huff_write_bits(huff_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint32_t n) {
    rt_assert(n <= 64);
    while (n > 0) {
        huff_write_bit(lz, b64, bp, bits & 1);
        bits >>= 1;
        n--;
    }
}

static inline void huff_write_number(huff_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint8_t base) {
    do {
        huff_write_bits(lz, b64, bp, bits, base);
        bits >>= base;
        huff_write_bit(lz, b64, bp, bits != 0); // stop bit
    } while (bits != 0);
}

static inline void huff_flush(huff_t* lz, uint64_t b64, uint32_t bp) {
    if (bp > 0 && lz->error == 0) {
        lz->write(lz, b64);
        if (lz->error == 0) { lz->written += 8; }
    }
}

static void huff_write_header(huff_t* lz, size_t bytes, uint8_t window_bits) {
    huff_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { huff_return_invalid(lz); }
    lz->write(lz, (uint64_t)bytes);
    huff_if_error_return(lz);
    lz->write(lz, (uint64_t)window_bits);
}

static void huff_compress(huff_t* lz, const uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    huff_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { huff_return_invalid(lz); }
    huff_init_histograms();
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    uint64_t b64 = 0;
    uint32_t bp = 0;
    size_t i = 0;
    while (i < bytes) {
        // length and position of longest matching sequence
        size_t len = 0;
        size_t pos = 0;
        if (i >= 1) {
            size_t j = i - 1;
            size_t min_j = i > window ? i - window : 0;
            while (j > min_j) {
                rt_assert((i - j) < window);
                const size_t n = bytes - i;
                size_t k = 0;
                while (k < n && data[j + k] == data[i + k]) {
                    k++;
                }
                if (k > len) {
                    len = k;
                    pos = i - j;
                }
                j--;
            }
        }
        if (len > 2) {
            rt_assert(0 < pos && pos < window);
            rt_assert(0 < len);
            huff_write_bits(lz, &b64, &bp, 0b11, 2); // flags
            huff_if_error_return(lz);
            huff_write_number(lz, &b64, &bp, pos, base);
            huff_if_error_return(lz);
            huff_write_number(lz, &b64, &bp, len, base);
            huff_if_error_return(lz);
            huff_histogram_pos_len(pos, len);
            i += len;
        } else {
            const uint8_t b = data[i];
            // European texts are predominantly spaces and small ASCII letters:
            if (b < 0x80) {
                huff_write_bit(lz, &b64, &bp, 0); // flags
                huff_if_error_return(lz);
                // ASCII byte < 0x80 with 8th bit set to `0`
                huff_write_bits(lz, &b64, &bp, b, 7);
                huff_if_error_return(lz);
            } else {
                huff_write_bits(lz, &b64, &bp, 0b10, 2); // flags
                huff_if_error_return(lz);
                // only 7 bit because 8th bit is `1`
                huff_write_bits(lz, &b64, &bp, b, 7);
                huff_if_error_return(lz);
            }
            i++;
        }
    }
    huff_flush(lz, b64, bp);
    huff_dump_histograms();
}

static inline uint64_t huff_read_bit(huff_t* lz, uint64_t* b64, uint32_t* bp) {
    if (*bp == 0) { *b64 = lz->read(lz); }
    uint64_t bit = (*b64 >> *bp) & 1;
    *bp = *bp == 63 ? 0 : *bp + 1;
    return bit;
}

static inline uint64_t huff_read_bits(huff_t* lz, uint64_t* b64,
        uint32_t* bp, uint32_t n) {
    rt_assert(n <= 64);
    uint64_t bits = 0;
    for (uint32_t i = 0; i < n && lz->error == 0; i++) {
        uint64_t bit = huff_read_bit(lz, b64, bp);
        bits |= bit << i;
    }
    return bits;
}

static inline uint64_t huff_read_number(huff_t* lz, uint64_t* b64,
        uint32_t* bp, uint8_t base) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    do {
        bits |= (huff_read_bits(lz, b64, bp, base) << shift);
        shift += base;
        bit = huff_read_bit(lz, b64, bp);
    } while (bit && lz->error == 0);
    return bits;
}

static void huff_read_header(huff_t* lz, size_t *bytes, uint8_t *window_bits) {
    huff_if_error_return(lz);
    *bytes = (size_t)lz->read(lz);
    *window_bits = (uint8_t)lz->read(lz);
    if (*window_bits < 10 || *window_bits > 20) { huff_return_invalid(lz); }
}

static void huff_decompress(huff_t* lz, uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    huff_if_error_return(lz);
    uint64_t b64 = 0;
    uint32_t bp = 0;
    if (window_bits < 10 || window_bits > 20) { huff_return_invalid(lz); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    size_t i = 0; // output data[i]
    while (i < bytes) {
        uint64_t bit0 = huff_read_bit(lz, &b64, &bp);
        huff_if_error_return(lz);
        if (bit0) {
            uint64_t bit1 = huff_read_bit(lz, &b64, &bp);
            huff_if_error_return(lz);
            if (bit1) {
                uint64_t pos = huff_read_number(lz, &b64, &bp, base);
                huff_if_error_return(lz);
                uint64_t len = huff_read_number(lz, &b64, &bp, base);
                huff_if_error_return(lz);
                rt_assert(0 < pos && pos < window);
                if (!(0 < pos && pos < window)) { huff_return_invalid(lz); }
                rt_assert(0 < len);
                if (len == 0) { huff_return_invalid(lz); }
                // Cannot do memcpy() here because of possible overlap.
                // memcpy() may read more than one byte at a time.
                uint8_t* s = data - (size_t)pos;
                const size_t n = i + (size_t)len;
                while (i < n) { data[i] = s[i]; i++; }
            } else { // byte >= 0x80
                size_t b = huff_read_bits(lz, &b64, &bp, 7);
                huff_if_error_return(lz);
                data[i] = (uint8_t)b | 0x80;
                i++;
            }
        } else { // literal byte
            size_t b = huff_read_bits(lz, &b64, &bp, 7); // ASCII byte < 0x80
            huff_if_error_return(lz);
            data[i] = (uint8_t)b;
            i++;
        }
    }
}

huff_if huff = {
    .write_header = huff_write_header,
    .compress     = huff_compress,
    .read_header  = huff_read_header,
    .decompress   = huff_decompress,
};

#endif // huff_implementation

