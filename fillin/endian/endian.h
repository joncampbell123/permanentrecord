/* endian.h for platforms that do not provide it */
#ifndef __ENDIAN_FILLIN_H
#define __ENDIAN_FILLIN_H

#if defined(__APPLE__)
# include <machine/endian.h>
#else
# define __LITTLE_ENDIAN 1234
# define __BIG_ENDIAN    4321
# define __PDP_ENDIAN    3412
#endif

#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
# define __BYTE_ORDER   __LITTLE_ENDIAN
#elif defined(__APPLE__)
# include <libkern/OSByteOrder.h>
#else
# error I do not know
#endif

#include <stdint.h>

static inline uint16_t hax_bswap_16(uint16_t v) {
    v = (uint16_t)((((unsigned int)v >> 8u) & 0xFFu) | (((unsigned int)v << 8u) & 0xFF00u));
    return v;
}

static inline uint32_t hax_bswap_32(uint32_t v) {
    v = (uint32_t)((((unsigned long)v >> 16ul) & 0x0000FFFFul) | (((unsigned long)v << 16ul) & 0xFFFF0000ul));
    v = (uint32_t)((((unsigned long)v >>  8ul) & 0x00FF00FFul) | (((unsigned long)v <<  8ul) & 0xFF00FF00ul));
    return v;
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define htobe16(x) hax_bswap_16 (x)
# define htole16(x) (x)
# define be16toh(x) hax_bswap_16 (x)
# define le16toh(x) (x)

# define htobe32(x) hax_bswap_32 (x)
# define htole32(x) (x)
# define be32toh(x) hax_bswap_32 (x)
# define le32toh(x) (x)

# define htobe64(x) hax_bswap_64 (x)
# define htole64(x) (x)
# define be64toh(x) hax_bswap_64 (x)
# define le64toh(x) (x)
#else
# define htobe16(x) (x)
# define htole16(x) hax_bswap_16 (x)
# define be16toh(x) (x)
# define le16toh(x) hax_bswap_16 (x)

# define htobe32(x) (x)
# define htole32(x) hax_bswap_32 (x)
# define be32toh(x) (x)
# define le32toh(x) hax_bswap_32 (x)

# define htobe64(x) (x)
# define htole64(x) hax_bswap_64 (x)
# define be64toh(x) (x)
# define le64toh(x) hax_bswap_64 (x)
#endif

#endif //__ENDIAN_FILLIN_H

