/* endian.h for platforms that do not provide it */
#ifndef __ENDIAN_FILLIN_H
#define __ENDIAN_FILLIN_H

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

#if defined(WIN32)
# define __BYTE_ORDER   __LITTLE_ENDIAN
#else
# error I do not know
#endif

#include <stdint.h>

static inline uint16_t hax_bswap_16(uint16_t v) {
    v = ((v >> 8u) & 0xFFu) | ((v << 8u) & 0xFF00u);
    return v;
}

static inline uint32_t hax_bswap_32(uint32_t v) {
    v = ((v >> 16u) & 0x0000FFFFu) | ((v << 16u) & 0xFFFF0000u);
    v = ((v >>  8u) & 0x00FF00FFu) | ((v <<  8u) & 0xFF00FF00u);
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

