#ifndef INTERNED_STRING_IMPL
#define INTERNED_STRING_IMPL

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
// --- Configuration ---

#define BYTES_IN_HASH         2
#define BYTES_IN_LEN          1
#define ROM_TEXT_COMPRESSION  0
#define ENABLE_DEBUG          1

#define PLAT_MALLOC           malloc
#define PLAT_REALLOC          realloc 
#define PLAT_PRINTF           printf

// #define NO_ISTR // Disable bundling static ISTRs
// #define NO_ISTR       // Alternative flag for same purpose

// Initial size of ISTR pool (first alloc will be twice this)
#define ALLOC_ENTRIES_INIT    (10)


// First entry is ISTRnull = 0
enum {
#ifndef NO_ISTR
    #define ISTRDEF(id, hash, len, str) id,
    #include "istrdefs.generated.h"
    #undef ISTRDEF
#endif

    ISTRnumber_of_static,   //not ISTR_ to avoid clashes with user defined literals
    ISTRstart_of_main = ISTRnumber_of_static - 1, // Keeps counter aligned

    ISTRnumber_of  // Final count of ISTRs
};

typedef size_t   istr_t;
typedef uint8_t  byte;
typedef uint16_t istr_short_t;

#if BYTES_IN_HASH == 1
    typedef uint8_t istr_hash_t;
#elif BYTES_IN_HASH == 2
    typedef uint16_t istr_hash_t;
#elif BYTES_IN_HASH == 0
    // No hash field
#else
    #error "Unsupported BYTES_IN_HASH"
#endif

#if BYTES_IN_LEN == 1
    typedef uint8_t istr_len_t;
#elif BYTES_IN_LEN == 2
    typedef uint16_t istr_len_t;
#else
    #error "Unsupported BYTES_IN_LEN"
#endif

typedef struct _istr_pool_t {
    const struct _istr_pool_t *prev;
    size_t total_prev_len : (8 * sizeof(size_t) - 1);
    size_t is_sorted      : 1;
    size_t alloc;
    size_t len;

#if BYTES_IN_HASH
    istr_hash_t *hashes;
#endif

    istr_len_t *lengths;
    const char *istrs[];  // Actual strings
} istr_pool_t;

extern istr_pool_t *pool_head;

#define TOTAL() ((pool_head)->total_prev_len + (pool_head)->len)

void   istr_init(void);
istr_t   istr_from_str(const char *str);
istr_t   istr_from_strn(const char *str, size_t len);
istr_t   istr_find_strn(const char *str, size_t str_len);  // Returns ISTRnull if not found
istr_t   istr_from_strn_static(const char *str, size_t len);

const char *istr(istr_t q);
const char *istr_str(istr_t q); //wraps istr_t() to maintain original api
const byte *istr_data(istr_t q, size_t *len);
size_t      istr_len(istr_t q);
size_t      istr_hash(istr_t q);

#if ENABLE_DEBUG
void istr_pool_info(size_t *n_pool, size_t *n_istr, size_t *n_str_data_bytes, size_t *n_total_bytes);
void istr_dump_data(void);
#endif

#if ROM_TEXT_COMPRESSION
void mp_decompress_rom_string(byte *dst, const mp_rom_error_text_t src);
#define IS_COMPRESSED(s) (*(byte *)(s) == 0xff)
#endif

#endif // INTERNED_STRING_IMPL
