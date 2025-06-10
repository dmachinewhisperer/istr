/*
 * This impelementation is based on the interned string system of the MicroPython 
 project at http://micropython.org/
 */

 #include <assert.h>
 #include <string.h>
 #include <stdio.h>
 
 #include "istr.h"


 #if ISTR_ENABLE_DEBUG
 #define DEBUG_printf DEBUG_printf
 #else 
 #define DEBUG_printf(...) (void)0
 #endif
 
#if ISTR_BYTES_IN_HASH
 #define Q_HASH_MASK ((1 << (8 * ISTR_BYTES_IN_HASH)) - 1)
#else
 #define Q_HASH_MASK (0xffff)
#endif
 
//TODO: implement mutexs for entering critical section
#define ISTR_ENTER() 
#define ISTR_EXIT() 
 
  // A qstr is an index into the qstr pool.
 // The data for a qstr is \0 terminated (so they can be printed using printf)
 typedef struct {
    char *qstr_last_chunk;      // Current chunk for string storage
    size_t qstr_last_alloc;     // Total allocated size of current chunk
    size_t qstr_last_used;      // Bytes used in current chunk
} pool_state_t;

static pool_state_t pool_state = {0};
qstr_pool_t *pool_head = NULL;  


 // this must match the equivalent function in makeqstrdata.py
 // djb2 algorithm; see http://www.cse.yorku.ca/~oz/hash.html
 size_t qstr_compute_hash(const byte *data, size_t len) {
     size_t hash = 5381;
     for (const byte *top = data + len; data < top; data++) {
         hash = ((hash << 5) + hash) ^ (*data); // hash * 33 ^ data
     }
     hash &= Q_HASH_MASK;
     // Make sure that valid hash is never zero, zero means "hash not computed"
     if (hash == 0) {
         hash++;
     }
     return hash;
 }
 
 //static pools are constructed at compile time and name *_0, *_1 etc
 #if ISTR_BYTES_IN_HASH
 const qstr_hash_t istr_const_hashes_static_0[] = {
     #ifndef NO_QSTR
 #define QDEF0(id, hash, len, str) hash,
 #define QDEF1(id, hash, len, str)
     #include "genhdr/qstrdefs.generated.h"
 #undef QDEF0
 #undef QDEF1
     #endif
 };
 #endif 

 const qstr_len_t istr_const_lens_static_0[] = {
    #ifndef NO_QSTR
#define QDEF0(id, hash, len, str) len,
#define QDEF1(id, hash, len, str)
    #include "genhdr/qstrdefs.generated.h"
#undef QDEF0
#undef QDEF1
    #endif
};

const qstr_pool_t istr_const_pool_static_0 = {
    NULL,               // no previous pool
    0,                  // no previous pool
    true,              // is_sorted
    ISTR_ALLOC_ENTRIES_INIT,
    ISTRnumber_of_static,   // corresponds to number of strings in array just below
    #if ISTR_BYTES_IN_HASH
    (qstr_hash_t *)istr_const_hashes_static_0,
    #endif
    (qstr_len_t *)istr_const_lens_static_0,
    {
        #ifndef NO_QSTR
#define QDEF0(id, hash, len, str) str,
#define QDEF1(id, hash, len, str)
        #include "genhdr/qstrdefs.generated.h"
#undef QDEF0
#undef QDEF1
        #endif
    },
}; 
 
#define CONST_POOL istr_const_pool_static_0
 
 void qstr_init(void) {
     pool_head = (qstr_pool_t *)&CONST_POOL;
     pool_state.qstr_last_chunk = NULL; 

 }
 
 //returns pool where qstr if found
 static const qstr_pool_t *find_qstr(qstr *q) {
     // search pool for this qstr
     // total_prev_len==0 in the final pool, so the loop will always terminate
     const qstr_pool_t *pool = pool_head;
     while (*q < pool->total_prev_len) {
         pool = pool->prev;
     }
     *q -= pool->total_prev_len;
     assert(*q < pool->len);
     return pool;
 }
 
 //returns the qstr id of the newly added string
 static qstr qstr_add(size_t len, const char *q_ptr) {
    ISTR_ENTER();
     #if ISTR_BYTES_IN_HASH
     size_t hash = qstr_compute_hash((const byte *)q_ptr, len);
     DEBUG_printf("QSTR: add hash=%d len=%d data=%.*s\n", hash, len, len, q_ptr);
     #else
     DEBUG_printf("QSTR: add len=%d data=%.*s\n", len, len, q_ptr);
     #endif
 
     // make sure we have room in the pool for a new qstr
     if (pool_head->len >= pool_head->alloc) {
         size_t new_alloc = pool_head->alloc * 2;
         size_t pool_size = sizeof(qstr_pool_t)
             + (sizeof(const char *)
                 #if ISTR_BYTES_IN_HASH
                 + sizeof(qstr_hash_t)
                 #endif
                 + sizeof(qstr_len_t)) * new_alloc;

         qstr_pool_t *pool = (qstr_pool_t *)ISTR_PLAT_MALLOC(pool_size);
         if (pool == NULL) {
             // Keep qstr_last_chunk consistent with qstr_pool_t: qstr_last_chunk is not scanned
             // at garbage collection since it's reachable from a qstr_pool_t.  And the caller of
             // this function expects q_ptr to be stored in a qstr_pool_t so it can be reached
             // by the collector.  If qstr_pool_t allocation failed, qstr_last_chunk needs to be
             // NULL'd.  Otherwise it may become a dangling pointer at the next garbage collection.
             pool_state.qstr_last_chunk = NULL; 
             ISTR_EXIT();
             return ISTRnull; 
         }
         #if ISTR_BYTES_IN_HASH
         pool->hashes = (qstr_hash_t *)(pool->qstrs + new_alloc);
         pool->lengths = (qstr_len_t *)(pool->hashes + new_alloc);
         #else
         pool->lengths = (qstr_len_t *)(pool->qstrs + new_alloc);
         #endif
         pool->prev = (pool_head);
         pool->total_prev_len = (pool_head)->total_prev_len + (pool_head)->len;
         pool->alloc = new_alloc;
         pool->len = 0;
         pool_head = pool;
         DEBUG_printf("QSTR: allocate new pool of size %d\n", (pool_head)->alloc);
     }
 
     // add the new qstr
     size_t at = pool_head->len;
     #if ISTR_BYTES_IN_HASH
     pool_head->hashes[at] = hash;
     #endif
     pool_head->lengths[at] = len;
     pool_head->qstrs[at] = q_ptr;
     pool_head->len++;
 
     // return id for the newly-added qstr
     return pool_head->total_prev_len + at;
 }
 
 qstr qstr_find_strn(const char *str, size_t str_len) {
     if (str_len == 0) {
         // strncmp behaviour is undefined for str==NULL.
         return ISTR_;
     }
 
     #if ISTR_BYTES_IN_HASH
     // work out hash of str
     size_t str_hash = qstr_compute_hash((const byte *)str, str_len);
     #endif
 
     // search pools for the data
     for (const qstr_pool_t *pool = pool_head; pool != NULL; pool = pool->prev) {
         size_t low = 0;
         size_t high = pool->len - 1;
 
         // binary search inside the pool
         if (pool->is_sorted) {
             while (high - low > 1) {
                 size_t mid = (low + high) / 2;
                 int cmp = strncmp(str, pool->qstrs[mid], str_len);
                 if (cmp <= 0) {
                     high = mid;
                 } else {
                     low = mid;
                 }
             }
         }
 
         // sequential search for the remaining strings
         for (size_t at = low; at < high + 1; at++) {
             if (
                 #if ISTR_BYTES_IN_HASH
                 pool->hashes[at] == str_hash &&
                 #endif
                 pool->lengths[at] == str_len
                 && memcmp(pool->qstrs[at], str, str_len) == 0) {
                 return pool->total_prev_len + at;
             }
         }
     }
 
     // not found; return null qstr
     return ISTRnull;
 }
 
 qstr qstr_from_str(const char *str) {
     return qstr_from_strn(str, strlen(str));
 }
 
 static qstr qstr_from_strn_helper(const char *str, size_t len, bool data_is_static) {
     ISTR_ENTER();
     qstr q = qstr_find_strn(str, len);
     if (q == 0) {
         // qstr does not exist in interned pool so need to add it
 
         // check that len is not too big
         if (len >= (1 << (8 * ISTR_BYTES_IN_LEN))) {
             ISTR_EXIT();
             return ISTRnull;
         }
 
         if (data_is_static) {
             // Given string data will be forever available so use it directly.
             assert(str[len] == '\0');
             goto add;
         }
 
         // compute number of bytes needed to intern this string
         size_t n_bytes = len + 1;
 
         if (pool_state.qstr_last_chunk != NULL && pool_state.qstr_last_used + n_bytes > pool_state.qstr_last_alloc) {   
             // not enough room at end of previously interned string so try to grow
             char *new_p = ISTR_PLAT_REALLOC(pool_state.qstr_last_chunk, pool_state.qstr_last_alloc + n_bytes);
             if (new_p == NULL) {
                 // could not grow existing memory; shrink it to fit previous
                 ISTR_PLAT_REALLOC(pool_state.qstr_last_chunk, pool_state.qstr_last_alloc);
                 pool_state.qstr_last_chunk = NULL; 
             } else {
                 // could grow existing memory
                 pool_state.qstr_last_alloc +=n_bytes; 
             }
         }
 
         if (pool_state.qstr_last_chunk == NULL) {   
             // no existing memory for the string to be interned so allocate a new chunk
             size_t al = n_bytes;
             if (al < ISTR_ALLOC_ENTRIES_INIT) {
                 al = ISTR_ALLOC_ENTRIES_INIT;
             }
             pool_state.qstr_last_chunk = (char*)ISTR_PLAT_MALLOC(al);
             if (pool_state.qstr_last_chunk == NULL) {
                 // failed to allocate a large chunk so try with exact size
                 pool_state.qstr_last_chunk = (char*)ISTR_PLAT_MALLOC(n_bytes);
                 if (pool_state.qstr_last_chunk == NULL) {
                     ISTR_EXIT();
                     return ISTRnull;
                 }
                 al = n_bytes;
             }
             pool_state.qstr_last_alloc = al;
             pool_state.qstr_last_used = 0;
         }
 
         // allocate memory from the chunk for this new interned string's data
         char *q_ptr = pool_state.qstr_last_chunk + pool_state.qstr_last_used;
         pool_state.qstr_last_used += n_bytes;
 
         // store the interned strings' data
         memcpy(q_ptr, str, len);
         q_ptr[len] = '\0';
         str = q_ptr;
 
     add:
         q = qstr_add(len, str);
     }
     ISTR_EXIT();
     return q;
 }
 
 qstr qstr_from_strn(const char *str, size_t len) {
     return qstr_from_strn_helper(str, len, false);
 }
 

 // Create a new qstr that can forever reference the given string data.
 qstr qstr_from_strn_static(const char *str, size_t len) {
     return qstr_from_strn_helper(str, len, true);
 }

 
 size_t qstr_hash(qstr q) {
     const qstr_pool_t *pool = find_qstr(&q);
     #if ISTR_BYTES_IN_HASH
     return pool->hashes[q];
     #else
     return qstr_compute_hash((byte *)pool->qstrs[q], pool->lengths[q]);
     #endif
 }
 
 size_t qstr_len(qstr q) {
     const qstr_pool_t *pool = find_qstr(&q);
     return pool->lengths[q];
 }
 
 const char *qstr_str(qstr q) {
     const qstr_pool_t *pool = find_qstr(&q);
     return pool->qstrs[q];
 }
 
 const byte *qstr_data(qstr q, size_t *len) {
     const qstr_pool_t *pool = find_qstr(&q);
     *len = pool->lengths[q];
     return (byte *)pool->qstrs[q];
 }

 #if ISTR_ENABLE_DEBUG
 void qstr_pool_info(size_t *n_pool, size_t *n_qstr, size_t *n_str_data_bytes, size_t *n_total_bytes) {
     ISTR_ENTER();
     *n_pool = 0;
     *n_qstr = 0;
     *n_str_data_bytes = 0;
     *n_total_bytes = 0;
     for (const qstr_pool_t *pool = pool_head; pool != NULL && pool != &CONST_POOL; pool = pool->prev) {
         *n_pool += 1;
         *n_qstr += pool->len;
         for (qstr_len_t *l = pool->lengths, *l_top = pool->lengths + pool->len; l < l_top; l++) {
             *n_str_data_bytes += *l + 1;
         }
         *n_total_bytes += sizeof(qstr_pool_t)
             + (sizeof(const char *)
                 #if ISTR_BYTES_IN_HASH
                 + sizeof(qstr_hash_t)
                 #endif
                 + sizeof(qstr_len_t)) * pool->alloc;
     }
     *n_total_bytes += *n_str_data_bytes;
     ISTR_EXIT();
 }
 
 void qstr_dump_data(void) {
     ISTR_ENTER();
     for (const qstr_pool_t *pool = pool_head; pool != NULL && pool != &CONST_POOL; pool = pool->prev) {
         for (const char *const *q = pool->qstrs, *const *q_top = pool->qstrs + pool->len; q < q_top; q++) {
             ISTR_PLAT_PRINTF("Q(%s)\n", *q);
         }
     }
     ISTR_EXIT();
 }
 #endif
 
 #if ISTR_ROM_TEXT_COMPRESSION
 
 #ifdef NO_QSTR
 
 // If NO_QSTR is set, it means we're doing QSTR extraction.
 // So we won't yet have "genhdr/compressed.data.h"
 
 #else
 
 // Emit the compressed_string_data string.
 #define MP_COMPRESSED_DATA(x) static const char *compressed_string_data = x;
 #define MP_MATCH_COMPRESSED(a, b)
 #include "genhdr/compressed.data.h"
 #undef MP_COMPRESSED_DATA
 #undef MP_MATCH_COMPRESSED
 
 #endif // NO_QSTR
 
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
 
 #endif // ISTR_ROM_TEXT_COMPRESSION