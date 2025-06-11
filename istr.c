/*
 * This impelementation is based on the interned string system of the MicroPython 
 project at http://micropython.org/
 */

 #include <assert.h>
 #include <string.h>
 #include <stdio.h>
 #include <stdbool.h>

 #include "istr.h"
 
 // Debug loggin
 #if ENABLE_DEBUG
     #define DEBUG_printf printf
 #else
     #define DEBUG_printf(...) (void)0
 #endif
 
 // Hash mask
 #if BYTES_IN_HASH
     #define Q_HASH_MASK ((1 << (8 * BYTES_IN_HASH)) - 1)
 #else
     #define Q_HASH_MASK (0xffff)
 #endif
 
 // Critical section stubs
 #define ENTER()
 #define EXIT()
 
 // Pool state
 typedef struct {
     char   *istr_last_chunk;
     size_t  istr_last_alloc;
     size_t  istr_last_used;
 } pool_state_t;
 
 static pool_state_t pool_state = {0};
 istr_pool_t *pool_head = NULL;
 
 // djb2 algorithm variant; see http://www.cse.yorku.ca/~oz/hash.html
  // this must match the equivalent function in makeistrdata.py
 size_t istr_compute_hash(const byte *data, size_t len) {
     size_t hash = 5381;
     const byte *end = data + len;
     while (data < end) {
         hash = ((hash << 5) + hash) ^ *data++;
     }
     hash &= Q_HASH_MASK;
     return (hash == 0) ? 1 : hash;
 }
 
 #if BYTES_IN_HASH
 const istr_hash_t istr_const_hashes_static_0[] = {
 #ifndef NO_ISTR
     #define QDEF(id, hash, len, str) hash,
     #include "istrdefs.generated.h"
     #undef QDEF
 #endif
 };
 #endif
 
 const istr_len_t istr_const_lens_static_0[] = {
 #ifndef NO_ISTR
     #define QDEF(id, hash, len, str) len,
     #include "istrdefs.generated.h"
     #undef QDEF
 #endif
 };
 
 const istr_pool_t istr_const_pool_static_0 = {
     .prev = NULL,
     .total_prev_len = 0,
     .is_sorted = true,
     .alloc = ALLOC_ENTRIES_INIT,
     .len = ISTRnumber_of_static,
 #if BYTES_IN_HASH
     .hashes = (istr_hash_t *)istr_const_hashes_static_0,
 #endif
     .lengths = (istr_len_t *)istr_const_lens_static_0,
     .istrs = {
 #ifndef NO_ISTR
         #define QDEF(id, hash, len, str) str,
         #include "istrdefs.generated.h"
         #undef QDEF
 #endif
     },
 };
 
 #define CONST_POOL istr_const_pool_static_0
 
 // --- api implementation ---
 
 void istr_init(void) {
     pool_head = (istr_pool_t *)&CONST_POOL;
     pool_state.istr_last_chunk = NULL;
 }
 
 static const istr_pool_t *find_istr(istr *q) {
     const istr_pool_t *pool = pool_head;
     while (*q < pool->total_prev_len) {
         pool = pool->prev;
     }
     *q -= pool->total_prev_len;
     assert(*q < pool->len);
     return pool;
 }
 
 static istr istr_add(size_t len, const char *q_ptr) {
     ENTER();
 
 #if BYTES_IN_HASH
     size_t hash = istr_compute_hash((const byte *)q_ptr, len);
     DEBUG_printf("ISTR: add hash=%zu len=%zu data=%.*s\n", hash, len, (int)len, q_ptr);
 #else
     DEBUG_printf("ISTR: add len=%zu data=%.*s\n", len, (int)len, q_ptr);
 #endif
 
     if (pool_head->len >= pool_head->alloc) {
         size_t new_alloc = pool_head->alloc * 2;
         size_t pool_size = sizeof(istr_pool_t)
             + (sizeof(const char *) + sizeof(istr_len_t)
 #if BYTES_IN_HASH
             + sizeof(istr_hash_t)
 #endif
             ) * new_alloc;
 
         istr_pool_t *pool = (istr_pool_t *)PLAT_MALLOC(pool_size);
         if (!pool) {
             pool_state.istr_last_chunk = NULL;
             EXIT();
             return ISTRnull;
         }
 
 #if BYTES_IN_HASH
         pool->hashes = (istr_hash_t *)(pool->istrs + new_alloc);
         pool->lengths = (istr_len_t *)(pool->hashes + new_alloc);
 #else
         pool->lengths = (istr_len_t *)(pool->istrs + new_alloc);
 #endif
 
         pool->prev = pool_head;
         pool->total_prev_len = pool_head->total_prev_len + pool_head->len;
         pool->alloc = new_alloc;
         pool->len = 0;
         pool->is_sorted = false;
 
         pool_head = pool;
         DEBUG_printf("ISTR: allocated new pool with %zu entries\n", new_alloc);
     }
 
     size_t at = pool_head->len;
 #if BYTES_IN_HASH
     pool_head->hashes[at] = hash;
 #endif
     pool_head->lengths[at] = len;
     pool_head->istrs[at] = q_ptr;
     pool_head->len++;
 
     EXIT();
     return pool_head->total_prev_len + at;
 }
 
 istr istr_find_strn(const char *str, size_t len) {
     if (len == 0) return ISTRnull;
 
 #if BYTES_IN_HASH
     size_t str_hash = istr_compute_hash((const byte *)str, len);
 #endif
 
     for (const istr_pool_t *pool = pool_head; pool; pool = pool->prev) {
         size_t low = 0, high = pool->len - 1;
         
         // bsearch for sorted pools
         if (pool->is_sorted) {
             while (high - low > 1) {
                 size_t mid = (low + high) / 2;
                 int cmp = strncmp(str, pool->istrs[mid], len);
                 if (cmp <= 0) high = mid; else low = mid;
             }
         }
 
         // lsearch for remaining string
         for (size_t at = low; at <= high; ++at) {
             if (
 #if BYTES_IN_HASH
                 pool->hashes[at] == str_hash &&
 #endif
                 pool->lengths[at] == len &&
                 memcmp(pool->istrs[at], str, len) == 0) {
                 return pool->total_prev_len + at;
             }
         }
     }
 
     return ISTRnull;
 }
 
 istr istr_from_str(const char *str) {
     return istr_from_strn(str, strlen(str));
 }
 
 static istr istr_from_strn_helper(const char *str, size_t len, bool data_is_static) {
     ENTER();
 
     istr q = istr_find_strn(str, len);
     if (q != ISTRnull) {
         EXIT();
         return q;
     }
 
     if (len >= (1 << (8 * BYTES_IN_LEN))) {
         EXIT();
         return ISTRnull;
     }
 
     if (data_is_static) {
         // string persistence is guranteed, no alloc needed.
         assert(str[len] == '\0');
         q = istr_add(len, str);
         EXIT();
         return q;
     }
 
     size_t n_bytes = len + 1;
 
     //try to fit string in prealloced block 
     //if we are short of some bytes, expand the block
     //if block expansion fails, shrink the block to fit existing entries
     if (pool_state.istr_last_chunk && pool_state.istr_last_used + n_bytes > pool_state.istr_last_alloc) {
         char *new_chunk = PLAT_REALLOC(pool_state.istr_last_chunk, pool_state.istr_last_alloc + n_bytes);
         if (!new_chunk) {
             (void)PLAT_REALLOC(pool_state.istr_last_chunk, pool_state.istr_last_alloc);
             pool_state.istr_last_chunk = NULL;
         } else {
             pool_state.istr_last_chunk = new_chunk;
             pool_state.istr_last_alloc += n_bytes;
         }
     }
 
     if (!pool_state.istr_last_chunk) {
         size_t alloc_size = n_bytes > ALLOC_ENTRIES_INIT ? n_bytes : ALLOC_ENTRIES_INIT;
         pool_state.istr_last_chunk = (char *)PLAT_MALLOC(alloc_size);
         if (!pool_state.istr_last_chunk) {
             EXIT();
             return ISTRnull;
         }
         pool_state.istr_last_alloc = alloc_size;
         pool_state.istr_last_used = 0;
     }
 
     char *q_ptr = pool_state.istr_last_chunk + pool_state.istr_last_used;
     memcpy(q_ptr, str, len);
     q_ptr[len] = '\0';
     pool_state.istr_last_used += n_bytes;
 
     q = istr_add(len, q_ptr);
     EXIT();
     return q;
 }
 
 istr istr_from_strn(const char *str, size_t len) {
     return istr_from_strn_helper(str, len, false);
 }
 
 istr istr_from_strn_static(const char *str, size_t len) {
     return istr_from_strn_helper(str, len, true);
 }
 
 size_t istr_hash(istr q) {
     const istr_pool_t *pool = find_istr(&q);
 #if BYTES_IN_HASH
     return pool->hashes[q];
 #else
     return istr_compute_hash((const byte *)pool->istrs[q], pool->lengths[q]);
 #endif
 }
 
 size_t istr_len(istr q) {
     const istr_pool_t *pool = find_istr(&q);
     return pool->lengths[q];
 }
 
 const char *istr_str(istr q) {
     const istr_pool_t *pool = find_istr(&q);
     return pool->istrs[q];
 }
 
 const byte *istr_data(istr q, size_t *len) {
     const istr_pool_t *pool = find_istr(&q);
     *len = pool->lengths[q];
     return (const byte *)pool->istrs[q];
 }
 

 #if ENABLE_DEBUG
 void istr_pool_info(size_t *n_pool, size_t *n_istr, size_t *n_str_data_bytes, size_t *n_total_bytes) {
     ENTER();
     *n_pool = 0;
     *n_istr = 0;
     *n_str_data_bytes = 0;
     *n_total_bytes = 0;
     for (const istr_pool_t *pool = pool_head; pool != NULL && pool != &CONST_POOL; pool = pool->prev) {
         *n_pool += 1;
         *n_istr += pool->len;
         for (istr_len_t *l = pool->lengths, *l_top = pool->lengths + pool->len; l < l_top; l++) {
             *n_str_data_bytes += *l + 1;
         }
         *n_total_bytes += sizeof(istr_pool_t)
             + (sizeof(const char *)
                 #if BYTES_IN_HASH
                 + sizeof(istr_hash_t)
                 #endif
                 + sizeof(istr_len_t)) * pool->alloc;
     }
     *n_total_bytes += *n_str_data_bytes;
     EXIT();
 }
 
 void istr_dump_data(void) {
     ENTER();
     for (const istr_pool_t *pool = pool_head; pool != NULL && pool != &CONST_POOL; pool = pool->prev) {
         for (const char *const *q = pool->istrs, *const *q_top = pool->istrs + pool->len; q < q_top; q++) {
             PLAT_PRINTF("Q(%s)\n", *q);
         }
     }
     EXIT();
 }
 #endif
 
 #if ROM_TEXT_COMPRESSION
 
 #ifdef NO_ISTR
 
 // If NO_ISTR is set, it means we're doing ISTR extraction.
 // So we won't yet have "genhdr/compressed.data.h"
 
 #else
 
 // Emit the compressed_string_data string.
 #define MP_COMPRESSED_DATA(x) static const char *compressed_string_data = x;
 #define MP_MATCH_COMPRESSED(a, b)
 #include "genhdr/compressed.data.h"
 #undef MP_COMPRESSED_DATA
 #undef MP_MATCH_COMPRESSED
 
 #endif // NO_ISTR
 
 // This implements the "common word" compression scheme (see makecompresseddata.py) where the most
 // common 128 words in error messages are replaced by their index into the list of common words.
 
 // The compressed string data is delimited by setting high bit in the final char of each word.
 // e.g. aaaa<0x80|a>bbbbbb<0x80|b>....
 // This method finds the n'th string.
 static const byte *find_uncompressed_string(uint8_t n) {
     const byte *c = (byte *)compressed_string_data;
     while (n > 0) {
         while ((*c & 0x80) == 0) {
             ++c;
         }
         ++c;
         --n;
     }
     return c;
 }
 
 // Given a compressed string in src, decompresses it into dst.
 // dst must be large enough (use MP_MAX_UNCOMPRESSED_TEXT_LEN+1).
 void mp_decompress_rom_string(byte *dst, const mp_rom_error_text_t src_chr) {
     // Skip past the 0xff marker.
     const byte *src = (byte *)src_chr + 1;
     // Need to add spaces around compressed words, except for the first (i.e. transition from 1<->2).
     // 0 = start, 1 = compressed, 2 = regular.
     int state = 0;
     while (*src) {
         if ((byte) * src >= 128) {
             if (state != 0) {
                 *dst++ = ' ';
             }
             state = 1;
 
             // High bit set, replace with common word.
             const byte *word = find_uncompressed_string(*src & 0x7f);
             // The word is terminated by the final char having its high bit set.
             while ((*word & 0x80) == 0) {
                 *dst++ = *word++;
             }
             *dst++ = (*word & 0x7f);
         } else {
             // Otherwise just copy one char.
             if (state == 1) {
                 *dst++ = ' ';
             }
             state = 2;
 
             *dst++ = *src;
         }
         ++src;
     }
     // Add null-terminator.
     *dst = 0;
 }
 
 #endif // ROM_TEXT_COMPRESSION