#ifndef INTERNED_STRING_IMPL
#define INTERNED_STRING_IMPL

#include <stdlib.h>

#define ISTR_BYTES_IN_HASH 1
#define ISTR_BYTES_IN_LEN 1
#define ISTR_ROM_TEXT_COMPRESSION 0

#define ISTR_ENABLE_DEBUG 1

#define ISTR_PLAT_MALLOC malloc
#define ISTR_PLAT_REALLOC realloc 
#define ISTR_PLAT_PRINTF printf
//#define ISTR_NO_ISTR //when this macro is defined, static istrs are not bundled
//#define NO_ISTR

 // Initial number of entries for qstr pool, set so that the first dynamically
 // allocated pool is twice this size.  The value here must be <= ISTRnumber_of.
 #define ISTR_ALLOC_ENTRIES_INIT (10)


// See qstrdefs.h for a list of qstr's that are available as constants.
// Reference them as MP_QSTR_xxxx.
//
// Note: it would be possible to define MP_QSTR_xxx as qstr_from_str("xxx")
// for qstrs that are referenced this way, but you don't want to have them in ROM.

// first entry in enum will be MP_QSTRnull=0, which indicates invalid/no qstr
enum {
    #ifndef NO_QSTR
#define QDEF0(id, hash, len, str) id,
#define QDEF1(id, hash, len, str)
    #include "genhdr/qstrdefs.generated.h"
#undef QDEF0
#undef QDEF1
    #endif
    ISTRnumber_of_static,
    ISTRstart_of_main = ISTRnumber_of_static - 1, // unused but shifts the enum counter back one

    #ifndef NO_QSTR
#define QDEF0(id, hash, len, str)
#define QDEF1(id, hash, len, str) id,
    #include "genhdr/qstrdefs.generated.h"
#undef QDEF0
#undef QDEF1
    #endif
    ISTRnumber_of, // no underscore so it can't clash with any of the above
};

typedef size_t qstr;
typedef uint8_t byte;
typedef uint16_t qstr_short_t;

#if ISTR_BYTES_IN_HASH == 0
#elif ISTR_BYTES_IN_HASH == 1
typedef uint8_t qstr_hash_t;
#elif ISTR_BYTES_IN_HASH == 2
typedef uint16_t qstr_hash_t;
#else
#error unimplemented qstr hash decoding
#endif

#if ISTR_BYTES_IN_LEN == 1
typedef uint8_t qstr_len_t;
#elif ISTR_BYTES_IN_LEN == 2
typedef uint16_t qstr_len_t;
#else
#error unimplemented qstr length decoding
#endif

typedef struct _qstr_pool_t {
    const struct _qstr_pool_t *prev;
    size_t total_prev_len : (8 * sizeof(size_t) - 1);
    size_t is_sorted : 1;
    size_t alloc;
    size_t len;
    #if ISTR_BYTES_IN_HASH
    qstr_hash_t *hashes;
    #endif
    qstr_len_t *lengths;
    const char *qstrs[];
} qstr_pool_t;

extern qstr_pool_t *pool_head; 

#define QSTR_TOTAL() ((last_pool)->total_prev_len + (last_pool)->len)

void qstr_init(void);
size_t qstr_compute_hash(const byte *data, size_t len);
qstr qstr_find_strn(const char *str, size_t str_len); // returns ISTRnull if not found
qstr qstr_from_str(const char *str);
qstr qstr_from_strn(const char *str, size_t len);

qstr qstr_from_strn_static(const char *str, size_t len);

size_t qstr_hash(qstr q);
const char *qstr_str(qstr q);
size_t qstr_len(qstr q);
const byte *qstr_data(qstr q, size_t *len);

#if ISTR_ENABLE_DEBUG
void qstr_pool_info(size_t *n_pool, size_t *n_qstr, size_t *n_str_data_bytes, size_t *n_total_bytes);
void qstr_dump_data(void);
#endif

#if ISTR_ROM_TEXT_COMPRESSION
void mp_decompress_rom_string(byte *dst, const mp_rom_error_text_t src);
#define ISTR_ROM_TEXT_COMPRESSION(s) (*(byte *)(s) == 0xff)
#endif

#endif // INTERNED_STRING_IMPL
