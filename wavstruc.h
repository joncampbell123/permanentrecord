
#include <stdint.h>

/* Windows WAVE format specifications. All fields little Endian */
#pragma pack(push,1)

typedef struct {
    uint32_t            fourcc;         /* ASCII 4-char ident such as 'fmt ' or 'data' */
    uint32_t            length;         /* length of chunk */
} RIFF_chunk;

typedef struct {
    uint32_t            listcc;         /* ASCII 4-char ident 'RIFF' or 'LIST' */
    uint32_t            length;         /* length of chunk including next field */
    uint32_t            fourcc;         /* ASCII 4-char ident such as 'WAVE' */
} RIFF_LIST_chunk;

typedef struct {                        /* (sizeof) (offset hex) (offset dec) */
    uint32_t            a;              /* (4)   +0x00 +0 */
    uint16_t            b,c;            /* (2,2) +0x04 +4 */
    uint8_t             d[2];           /* (2)   +0x08 +8 */
    uint8_t             e[6];           /* (6)   +0x0A +10 */
} windows_GUID;                         /* (16)  =0x10 =16 */
#define windows_GUID_size (16)

typedef struct {						/* (sizeof) (offset hex) (offset dec) */
    uint16_t            wFormatTag;     /* (2)  +0x00 +0 */
    uint16_t            nChannels;      /* (2)  +0x02 +2 */
    uint32_t            nSamplesPerSec; /* (4)  +0x04 +4 */
    uint32_t            nAvgBytesPerSec;/* (4)  +0x08 +8 */
    uint16_t            nBlockAlign;    /* (2)  +0x0C +12 */
} windows_WAVEFORMATOLD;                /* (14) =0x0E =14 */
#define windows_WAVEFORMATOLD_size (14)

typedef struct {                        /* (sizeof) (offset hex) (offset dec) */
    uint16_t            wFormatTag;     /* (2)  +0x00 +0 */
    uint16_t            nChannels;      /* (2)  +0x02 +2 */
    uint32_t            nSamplesPerSec; /* (4)  +0x04 +4 */
    uint32_t            nAvgBytesPerSec;/* (4)  +0x08 +8 */
    uint16_t            nBlockAlign;    /* (2)  +0x0C +12 */
    uint16_t            wBitsPerSample; /* (2)  +0x0E +14 */
} windows_WAVEFORMAT;                   /* (16) +0x10 +16 */
#define windows_WAVEFORMAT_size (16)

typedef struct {                        /* (sizeof) (offset hex) (offset dec) */
    uint16_t            wFormatTag;     /* (2)  +0x00 +0 */
    uint16_t            nChannels;      /* (2)  +0x02 +2 */
    uint32_t            nSamplesPerSec; /* (4)  +0x04 +4 */
    uint32_t            nAvgBytesPerSec;/* (4)  +0x08 +8 */
    uint16_t            nBlockAlign;    /* (2)  +0x0C +12 */
    uint16_t            wBitsPerSample; /* (2)  +0x0E +14 */
    uint16_t            cbSize;         /* (2)  +0x10 +16 */
} windows_WAVEFORMATEX;                 /* (18) =0x12 =18 */
#define windows_WAVEFORMATEX_size (18)

typedef struct {                                            /* (sizeof) (offset hex) (offset dec) */
    windows_WAVEFORMATEX            Format;                 /* (18) +0x00 +0 */
    union {
        uint16_t                    wValidBitsPerSample;    /* <- if it's PCM */
        uint16_t                    wSamplesPerBlock;       /* <- if it's not PCM, and compressed */
        uint16_t                    wReserved;              /* <- if ??? */
    } Samples;                                              /* (2)  +0x12 +18 */
    uint32_t                        dwChannelMask;          /* (4)  +0x14 +20 */
    windows_GUID                    SubFormat;              /* (16) +0x18 +24 */
} windows_WAVEFORMATEXTENSIBLE;                             /* (40) =0x28 =40 */
#define windows_WAVEFORMATEXTENSIBLE_size (40)

#pragma pack(pop)

static const uint32_t _RIFF_listcc_RIFF = 0x52494646;       /* 'RIFF' */
#define RIFF_listcc_RIFF            be32toh(_RIFF_listcc_RIFF)
static const uint32_t _RIFF_fourcc_WAVE = 0x57415645;       /* 'WAVE' */
#define RIFF_fourcc_WAVE            be32toh(_RIFF_fourcc_WAVE)
static const uint32_t _RIFF_fourcc_fmt  = 0x666D7420;       /* 'fmt ' */
#define RIFF_fourcc_fmt             be32toh(_RIFF_fourcc_fmt)
static const uint32_t _RIFF_fourcc_data = 0x64617461;       /* 'data' */
#define RIFF_fourcc_data            be32toh(_RIFF_fourcc_data)

const windows_GUID windows_KSDATAFORMAT_SUBTYPE_PCM = /* 00000001-0000-0010-8000-00aa00389b71 */
	{htole32(0x00000001),htole16(0x0000),htole16(0x0010),{0x80,0x00},{0x00,0xaa,0x00,0x38,0x9b,0x71}};

