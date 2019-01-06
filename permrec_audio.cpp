
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <endian.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "common.h"
#include "monclock.h"
#include "aufmt.h"
#include "aufmtui.h"
#include "audev.h"
#include "ausrc.h"
#include "ausrcls.h"
#include "dbfs.h"
#include "autocut.h"

#include "as_alsa.h"

static std::string          ui_command;
static std::string          ui_source;
static std::string          ui_device;
static int                  ui_want_fmt = 0;
static long                 ui_want_rate = 0;
static int                  ui_want_channels = 0;
static int                  ui_want_bits = 0;

static void help(void) {
    fprintf(stderr," -h --help      Help text\n");
    fprintf(stderr," -ch <channels>\n");
    fprintf(stderr," -sr <sample rate>\n");
    fprintf(stderr," -bs <bits/sample>\n");
    fprintf(stderr," -fmt <format>\n");
    fprintf(stderr,"    pcmu    unsigned PCM\n");
    fprintf(stderr,"    pcms    signed PCM\n");
    fprintf(stderr," -d <device>\n");
    fprintf(stderr," -s <source>\n");
    fprintf(stderr," -c <command>\n");
    fprintf(stderr,"    rec          Record\n");
    fprintf(stderr,"    test         Test format\n");
    fprintf(stderr,"    listsrc      List audio sources\n");
    fprintf(stderr,"    listdev      List audio devices\n");
}

static int parse_argv(int argc,char **argv) {
    char *a;
    int i=1;

    while (i < argc) {
        a = argv[i++];
        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else if (!strcmp(a,"fmt")) {
                a = argv[i++];
                if (a == NULL) return 1;

                if (!strcmp(a,"pcmu"))
                    ui_want_fmt = AFMT_PCMU;
                else if (!strcmp(a,"pcms"))
                    ui_want_fmt = AFMT_PCMS;
                else
                    return 1;
            }
            else if (!strcmp(a,"bs")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_bits = atoi(a);
                if (ui_want_bits < 1 || ui_want_bits > 255) return 1;
            }
            else if (!strcmp(a,"ch")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_channels = atoi(a);
                if (ui_want_channels < 1 || ui_want_channels > 255) return 1;
            }
            else if (!strcmp(a,"sr")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_rate = strtol(a,NULL,0);
                if (ui_want_rate < 1l || ui_want_rate > 1000000l) return 1;
            }
            else if (!strcmp(a,"c")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_command = a;
            }
            else if (!strcmp(a,"s")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_source = a;
            }
            else if (!strcmp(a,"d")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_device = a;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg\n");
            return 1;
        }
    }

    if (ui_command.empty()) {
        help();
        return 1;
    }

    return 0;
}

void ui_apply_format(AudioFormat &fmt) {
    if (ui_want_fmt > 0)
        fmt.format_tag = (uint16_t)ui_want_fmt;
    if (ui_want_rate > 0l)
        fmt.sample_rate = (uint32_t)ui_want_rate;
    if (ui_want_channels > 0)
        fmt.channels = (uint8_t)ui_want_channels;
    if (ui_want_bits > 0)
        fmt.bits_per_sample = (uint8_t)ui_want_bits;
}

bool ui_apply_options(AudioSource* alsa,AudioFormat &fmt) {
    if (alsa->SelectDevice(ui_device.c_str()) < 0) {
        fprintf(stderr,"Unable to set device\n");
        return false;
    }

    fmt.format_tag = 0;
    if (alsa->GetFormat(fmt) < 0) {
        /* some sources don't have a default */
        fmt.format_tag = AFMT_PCMS;
        fmt.bits_per_sample = 16;
        fmt.sample_rate = 48000;
        fmt.channels = 2;
    }

    ui_apply_format(fmt);
    if (alsa->SetFormat(fmt) < 0) {
        fprintf(stderr,"Unable to set format\n");
        return false;
    }

    if (alsa->GetFormat(fmt) < 0) {
        fprintf(stderr,"Unable to get format\n");
        return false;
    }

    printf("Recording format: %s\n",ui_print_format(fmt).c_str());

    if (alsa->Open() < 0) {
        fprintf(stderr,"Unable to open\n");
        return false;
    }

    return true;
}

#define OVERREAD (16u)

static unsigned char audio_tmp[4096u + OVERREAD];

AudioFormat rec_fmt;
unsigned int VU_dec = 1;
unsigned long long framecount = 0;
unsigned long VUclip[8];
unsigned int VU[8];

void ui_recording_draw(void) {
    printf("\x0D");

    {
        unsigned long long samples = framecount * (unsigned long long)rec_fmt.samples_per_frame;
        unsigned int H,M,S,ss;

        ss = (unsigned int)(samples % (unsigned long long)rec_fmt.sample_rate);
        ss = (unsigned int)(((unsigned long)ss * 100ul) / (unsigned long)rec_fmt.sample_rate);

        S = (unsigned int)(samples / (unsigned long long)rec_fmt.sample_rate);

        M = S / 60u;
        S %= 60u;

        H = M / 60u;
        M %= 60u;

        printf("%02u:%02u:%02u.%02u ",H,M,S,ss);
    }

    {
        unsigned int barl = 34u / rec_fmt.channels;
        unsigned int i,im,ch,chmax;
        char tmp[36];
        double d;

        chmax = rec_fmt.channels;
        if (chmax > 2) chmax = 2;

        for (ch=0;ch < chmax;ch++) {
            d = dBFS_measure((double)VU[ch] / 65535);
            d = (d + 48) / 48;
            if (d < 0) d = 0;
            if (d > 1) d = 1;
            im = (unsigned int)((d * barl) + 0.5);
            for (i=0;i < im;i++) tmp[i] = '=';
            for (   ;i < barl;i++) tmp[i] = ' ';
            tmp[i++] = VUclip[ch] > 0l ? '@' : '|';
            tmp[i++] = 0;
            assert(i <= sizeof(tmp));

            printf("%s",tmp);
        }
    }

    printf("\x0D");
    fflush(stdout);
}

void VU_init(const AudioFormat &fmt) {
    VU_dec = (unsigned int)((4410000ul / fmt.sample_rate) / 10ul);
    if (VU_dec == 0) VU_dec = 1;
}

void VU_advance_ch(const unsigned int ch,const unsigned int val) {
    if (VU[ch] < val)
        VU[ch] = val;
    else if (VU[ch] >= VU_dec)
        VU[ch] -= VU_dec;
    else if (VU[ch] > 0u)
        VU[ch] = 0;

    if (VU[ch] >= 0xFFF0u)
        VUclip[ch] = rec_fmt.sample_rate;
    else if (VUclip[ch] > 0u)
        VUclip[ch]--;
}

void VU_advance_pcmu_8(const uint8_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs((int)audio_tmp[ch] - 0x80) * 2u * 256u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu_16(const uint16_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs((long)audio_tmp[ch] - 0x8000l) * 2u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu_32(const uint32_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(((long)audio_tmp[ch] - 0x80000000l) / 32768l);
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_8(const int8_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch]) * 2u * 256u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_16(const int16_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch]) * 2u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_32(const int32_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch] / 32768l);
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu(const void *audio_tmp,unsigned int rds) {
         if (rec_fmt.bits_per_sample == 8)
        VU_advance_pcmu_8((const uint8_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 16)
        VU_advance_pcmu_16((const uint16_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 32)
        VU_advance_pcmu_32((const uint32_t*)audio_tmp,rds);
}

void VU_advance_pcms(const void *audio_tmp,unsigned int rds) {
         if (rec_fmt.bits_per_sample == 8)
        VU_advance_pcms_8((const int8_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 16)
        VU_advance_pcms_16((const int16_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 32)
        VU_advance_pcms_32((const int32_t*)audio_tmp,rds);
}

void VU_advance(const void *audio_tmp,unsigned int rd) {
    if (rec_fmt.format_tag == AFMT_PCMU) {
        VU_advance_pcmu(audio_tmp,rd / rec_fmt.bytes_per_frame);
    }
    else if (rec_fmt.format_tag == AFMT_PCMS) {
        VU_advance_pcms(audio_tmp,rd / rec_fmt.bytes_per_frame);
    }
}

std::string make_recording_path_now(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm == NULL) return std::string();

    std::string rec;
    char tmp[128];

    rec = "PERMREC";
    if (mkdir(rec.c_str(),0755) < 0) {
        if (errno != EEXIST)
            return std::string();
    }

    /* tm->tm_year + 1900 = current year
     * tm->tm_mon + 1     = current month (1=January)
     * tm->tm_mday        = current day of the month */
    sprintf(tmp,"/%04u%02u%02u",tm->tm_year + 1900,tm->tm_mon + 1,tm->tm_mday);
    rec += tmp;
    if (mkdir(rec.c_str(),0755) < 0) {
        if (errno != EEXIST)
            return std::string();
    }

    /* caller must add file extension needed */
    sprintf(tmp,"/TM%02u%02u%02u",tm->tm_hour,tm->tm_min,tm->tm_sec);
    rec += tmp;

    return rec;
}

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

class WAVWriter {
public:
    WAVWriter() : fd(-1), fmt_size(0), wav_data_start(0), wav_data_limit((uint32_t)0x7F000000ul) {
    }
    ~WAVWriter() {
        Close();
    }
public:
    bool Open(const std::string &path) {
        RIFF_LIST_chunk lchk;
        RIFF_chunk chk;

        if (IsOpen())
            return true;
        if (fmt_size == 0)
            return false;

        fd = open(path.c_str(),O_RDWR|O_CREAT|O_TRUNC|O_BINARY,0644);
        if (fd < 0) {
            fprintf(stderr,"Failed to open WAV output, %s\n",strerror(errno));
            return false;
        }

        lchk.listcc = RIFF_listcc_RIFF;
        lchk.length = 0xFFFFFFFFul; /* placeholder until finalized. no byte swapping needed, value is a palindrome */
        lchk.fourcc = RIFF_fourcc_WAVE;
        if (write(fd,&lchk,sizeof(lchk)) != sizeof(lchk)) {
            Close();
            return false;
        }

        /* within the 'RIFF:WAVE' chunk write 'fmt ' */
        chk.fourcc = RIFF_fourcc_fmt;
        chk.length = htole32((uint32_t)fmt_size);
        if (write(fd,&chk,sizeof(chk)) != sizeof(chk)) {
            Close();
            return false;
        }
        if ((size_t)write(fd,fmt,fmt_size) != fmt_size) {
            Close();
            return false;
        }

        /* then start the 'data' chunk. WAVE output will follow. */
        chk.fourcc = RIFF_fourcc_data;
        chk.length = (uint32_t)(0xFFFFFFFFul + 1ul - 12ul - 8ul - (unsigned long)fmt_size); /* placeholder until finalized */
        if (write(fd,&chk,sizeof(chk)) != sizeof(chk)) {
            Close();
            return false;
        }

        wav_data_start = wav_write_pos = (uint32_t)lseek(fd,0,SEEK_CUR);
        return true;
    }
    void Close(void) {
        if (fd >= 0) {
            if (wav_data_start != 0) {
                uint32_t length = (uint32_t)lseek(fd,0,SEEK_END);
                uint32_t v;

                if (length < wav_data_start)
                    length = wav_data_start;

                /* finalize the WAV file by updating chunk lengths */

                /* RIFF:WAVE length */
                v = length - 8;
                v = htole32(v);
                lseek(fd,4,SEEK_SET); // length field of RIFF:WAVE
                write(fd,&v,4);

                /* data length */
                v = length - wav_data_start;
                v = htole32(v);
                lseek(fd,(off_t)(wav_data_start - 4ul),SEEK_SET); // length field of 'data'
                write(fd,&v,4);
            }

            close(fd);
            fd = -1;
        }
        wav_data_start = wav_write_pos = 0;
    }
    bool SetFormat(const AudioFormat &fmt) {
        if (IsOpen()) return false;

        // PCM formats ONLY
        switch (fmt.format_tag) {
            case AFMT_PCMU:
            case AFMT_PCMS:
                if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
                    return false;
                if (fmt.channels < 1 || fmt.channels > 8)
                    return false;
                if (fmt.sample_rate < 1000 || fmt.sample_rate > 192000)
                    return false;

                /* WAV only supports 8-bit unsigned or 16/24/32-bit signed PCM */
                if (fmt.bits_per_sample == 8 && fmt.format_tag == AFMT_PCMS)
                    flip_sign = true;
                else if (fmt.bits_per_sample != 8 && fmt.format_tag == AFMT_PCMU)
                    flip_sign = true;
                else
                    flip_sign = false;

                {
                    windows_WAVEFORMAT *w = waveformat();

                    /* mono/stereo 8/16 should use WAVEFORMAT */
                    if (fmt.bits_per_sample <= 16 && fmt.channels <= 2) {
                        fmt_size = sizeof(windows_WAVEFORMAT);
                        w->wFormatTag = htole16(0x0001); // WAVE_FORMAT_PCM
                    }
                    /* anything else should use WAVEFORMATEXTENSIBLE.
                     * WAVEFORMATEXTENSIBLE contains WAVEFORMATEX in the first 22 bytes. */
                    else {
                        windows_WAVEFORMATEXTENSIBLE *wx = waveformatextensible();
                        fmt_size = sizeof(windows_WAVEFORMATEXTENSIBLE);
                        w->wFormatTag = htole16(0xFFFE); // WAVE_FORMAT_EXTENSIBLE
                        wx->Format.cbSize = (uint16_t)(sizeof(windows_WAVEFORMATEXTENSIBLE) - sizeof(windows_WAVEFORMATEX)); /* 22 */
                        wx->Format.cbSize = htole16(wx->Format.cbSize);

                        wx->Samples.wValidBitsPerSample = htole16(fmt.bits_per_sample);

                        wx->dwChannelMask = (1u << fmt.channels) - 1u; /*FIXME*/
                        wx->dwChannelMask = htole32(wx->dwChannelMask);

                        wx->SubFormat = windows_KSDATAFORMAT_SUBTYPE_PCM;
                    }

                    w->nChannels = htole16(fmt.channels);
                    w->nSamplesPerSec = htole32(fmt.sample_rate);

                    bytes_per_sample = (fmt.bits_per_sample + 7u) / 8u;
                    w->nBlockAlign = (uint16_t)(((fmt.bits_per_sample + 7u) / 8u) * fmt.channels);
                    w->nAvgBytesPerSec = ((uint32_t)w->nBlockAlign * (uint32_t)fmt.sample_rate);
                    block_align = w->nBlockAlign;

                    w->nBlockAlign = htole16(w->nBlockAlign);
                    w->nAvgBytesPerSec = htole32(w->nAvgBytesPerSec);
                    w->wBitsPerSample = htole16(fmt.bits_per_sample);
                }
                break;
            default:
                return false;
        }

        return true;
    }
    bool IsOpen(void) const {
        return (fd >= 0);
    }
    int Write(const void *buffer,unsigned int len) {
        if (IsOpen()) {
            if (flip_sign)
                return _write_xlat(buffer,len);
            else
                return _write_raw(buffer,len);
        }

        return -EINVAL;
    }
private:
    void _xlat(unsigned char *d,const unsigned char *s,unsigned int len) {
        if (bytes_per_sample == 1) {
            unsigned char x = flip_sign ? 0x80u : 0x00u;
            while (len >= bytes_per_sample) {
                *d++ = (*s++ ^ x);
                len -= bytes_per_sample;
            }
        }
        else if (bytes_per_sample == 2) {
            uint16_t x = flip_sign ? 0x8000u : 0x0000u;
            const uint16_t *s16 = (const uint16_t*)s;
            uint16_t *d16 = (uint16_t*)d;

            while (len >= bytes_per_sample) {
                *d16++ = htole16(*s16++ ^ x);
                len -= bytes_per_sample;
            }
        }
        else if (bytes_per_sample == 4) {
            uint32_t x = flip_sign ? 0x80000000ul : 0x00000000ul;
            const uint32_t *s32 = (const uint32_t*)s;
            uint32_t *d32 = (uint32_t*)d;

            while (len >= bytes_per_sample) {
                *d32++ = htole16(*s32++ ^ x);
                len -= bytes_per_sample;
            }
        }
        else {
            abort();
        }
    }
    int _write_xlat(const void *buffer,unsigned int len) {
        int wd = 0,swd;

        unsigned int tmpsz = 4096;
        tmpsz -= tmpsz % block_align;
        const unsigned char *s = (const unsigned char*)buffer;
        unsigned char *tmp = new(std::nothrow) unsigned char [tmpsz];
        if (tmp == NULL) return -ENOMEM;

        while (len >= tmpsz) {
            _xlat(tmp,s,tmpsz);
            swd = _write_raw(tmp,tmpsz);
            if (swd < 0) {
                delete[] tmp;
                return swd;
            }
            wd += swd;
            if ((unsigned int)swd != tmpsz) break;
            len -= tmpsz;
            s += tmpsz;
        }

        if (len > 0) {
            _xlat(tmp,s,len);
            swd = _write_raw(tmp,len);
            if (swd < 0) {
                delete[] tmp;
                return swd;
            }
            wd += swd;
            len -= len;
            s += len;
        }

        delete[] tmp;
        return wd;
    }
    int _write_raw(const void *buffer,unsigned int len) {
        int wd = 0;

        /* for simplicity sake require nBlockAlign alignment */
        len -= len % block_align;

        if (len > 0) {
            wav_write_pos = (uint32_t)lseek(fd,0,SEEK_CUR);

            if ((wav_write_pos+(uint32_t)len) > wav_data_limit)
                return -ENOSPC;

            wd = (int)write(fd,buffer,len);
            if (wd < 0) return -errno;

            wav_write_pos += (uint32_t)wd;
        }

        return wd;
    }
private:
    windows_WAVEFORMAT *waveformat(void) {
        return (windows_WAVEFORMAT*)fmt;
    }
    windows_WAVEFORMATEX *waveformatex(void) {
        return (windows_WAVEFORMATEX*)fmt;
    }
    windows_WAVEFORMATEXTENSIBLE *waveformatextensible(void) {
        return (windows_WAVEFORMATEXTENSIBLE*)fmt;
    }
private:
    int             fd;
    unsigned char   fmt[64];
    size_t          fmt_size;
    bool            flip_sign;
    uint32_t        wav_data_start;
    uint32_t        wav_data_limit;
    uint32_t        wav_write_pos;
    unsigned int    bytes_per_sample;
    unsigned int    block_align;
};

std::string rec_path_wav;
std::string rec_path_info;
std::string rec_path_base;
WAVWriter* wav_out = NULL;
FILE *wav_info = NULL;

void close_recording(void) {
    if (wav_info != NULL) {
        {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);

            if (tm != NULL) {
                fprintf(wav_info,"Recording stopped Y-M-D-H-M-S %04u-%02u-%02u %02u:%02u:%02u\n",
                        tm->tm_year+1900,
                        tm->tm_mon+1,
                        tm->tm_mday,
                        tm->tm_hour,
                        tm->tm_min,
                        tm->tm_sec);
            }
        }

        fclose(wav_info);
        wav_info = NULL;
    }
    if (wav_out) {
        delete wav_out;
        wav_out = NULL;
    }
}

bool open_recording(void) {
    if (wav_out != NULL || wav_info != NULL)
        return true;

    rec_path_base = make_recording_path_now();
    if (rec_path_base.empty()) {
        fprintf(stderr,"Unable to make recording path\n");
        return false;
    }

    rec_path_wav = rec_path_base + ".WAV";
    rec_path_info = rec_path_base + ".TXT";

    wav_info = fopen(rec_path_info.c_str(),"w");
    if (wav_info == NULL) {
        fprintf(stderr,"Unable to open %s, %s\n",rec_path_info.c_str(),strerror(errno));
        close_recording();
        return false;
    }

    wav_out = new WAVWriter();
    if (wav_out == NULL) {
        close_recording();
        return false;
    }
    if (!wav_out->SetFormat(rec_fmt)) {
        fprintf(stderr,"WAVE format rejected\n");
        close_recording();
        return false;
    }
    if (!wav_out->Open(rec_path_wav)) {
        fprintf(stderr,"WAVE open failed\n");
        close_recording();
        return false;
    }

    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);

        if (tm != NULL) {
            fprintf(wav_info,"Recording began Y-M-D-H-M-S %04u-%02u-%02u %02u:%02u:%02u\n",
                    tm->tm_year+1900,
                    tm->tm_mon+1,
                    tm->tm_mday,
                    tm->tm_hour,
                    tm->tm_min,
                    tm->tm_sec);
            fprintf(wav_info,"Recording format is: %s\n",
                    ui_print_format(rec_fmt).c_str());
        }
    }

    compute_auto_cut();

    printf("Recording to: %s\n",rec_path_wav.c_str());

    return true;
}

bool record_main(AudioSource* alsa,AudioFormat &fmt) {
    int rd,i;

    for (i=0;i < 8;i++) {
        VUclip[i] = 0u;
        VU[i] = 0u;
    }
    framecount = 0;
    rec_fmt = fmt;
    VU_init(fmt);

    if (!open_recording()) {
        fprintf(stderr,"Unable to open recording\n");
        return false;
    }

    while (1) {
        if (signal_to_die) break;
        usleep(10000);

        if (time_to_auto_cut()) {
            if (wav_info) fprintf(stderr,"Auto-cut commencing\n");
            close_recording();
            open_recording();
        }

        do {
            audio_tmp[sizeof(audio_tmp) - OVERREAD] = 'x';
            rd = alsa->Read(audio_tmp,(unsigned int)(sizeof(audio_tmp) - OVERREAD));
            if (audio_tmp[sizeof(audio_tmp) - OVERREAD] != 'x') {
                fprintf(stderr,"Read buffer overrun\n");
                signal_to_die = 1;
                break;
            }

            if (rd > 0) {
                VU_advance(audio_tmp,(unsigned int)rd);

                ui_recording_draw();

                framecount += (unsigned long long)((unsigned int)rd / fmt.bytes_per_frame);

                if (wav_out != NULL) {
                    if (wav_out->Write(audio_tmp,(unsigned int)rd) != rd) {
                        fprintf(stderr,"WAV writing error, closing and reopening\n");
                        close_recording();
                    }
                }
                if (wav_out == NULL) {
                    if (!open_recording()) {
                        fprintf(stderr,"Unable to open recording\n");
                        signal_to_die = 1;
                        break;
                    }
                }
            }
        } while (rd > 0);

        if (rd < 0) {
            fprintf(stderr,"Problem with audio device\n");
            break;
        }
    }

    close_recording();
    printf("\n");
    return true;
}

int main(int argc,char **argv) {
    if (parse_argv(argc,argv))
        return 1;

    /* I wrote this code in a hurry, please do not run as root. */
    /* You can enable ALSA audio access to non-root processes by modifying the user's
     * supplemental group list to add "audio", assuming the device nodes are owned by "audio" */
    if (geteuid() == 0 || getuid() == 0)
        fprintf(stderr,"WARNING: Do not run this program as root if you can help it!\n");

    signal(SIGINT,sigma);
    signal(SIGQUIT,sigma);
    signal(SIGTERM,sigma);

    if (ui_command == "test") {
        AudioSource* alsa = GetAudioSource(ui_source.c_str());
        AudioFormat fmt;

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        ui_apply_options(alsa,fmt);

        alsa->Close();
        delete alsa;
    }
    else if (ui_command == "rec") {
        AudioSource* alsa = GetAudioSource(ui_source.c_str());
        AudioFormat fmt;

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        if (ui_apply_options(alsa,fmt)) {
            if (!record_main(alsa,fmt))
                fprintf(stderr,"Recording loop failed\n");
        }

        alsa->Close();
        delete alsa;
    }
    else if (ui_command == "listsrc") {
        size_t i;

        printf("Audio sources:\n");

        for (i=0;audio_source_list[i].name != NULL;i++) {
            printf("    \"%s\" which is \"%s\"\n",audio_source_list[i].name,audio_source_list[i].desc);
        }
    }
    else if (ui_command == "listdev") {
        std::vector<AudioDevicePair> l;
        AudioSource* alsa = GetAudioSource(ui_source.c_str());

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        printf("Enumerating devices from \"%s\":\n",alsa->GetSourceName());

        if (alsa->EnumDevices(l) < 0) {
            fprintf(stderr,"Failed to enumerate devices\n");
            delete alsa;
            return 1;
        }

        for (auto i=l.begin();i != l.end();i++) {
            printf("    Device \"%s\":\n        which is \"%s\"\n",
                (*i).name.c_str(),(*i).desc.c_str());
        }

        printf("\n");
        printf("Default device is \"%s\"\n",alsa->GetDeviceName());

        delete alsa;
    }
    else {
        fprintf(stderr,"Unknown command '%s'\n",ui_command.c_str());
        return 1;
    }

    return 0;
}

