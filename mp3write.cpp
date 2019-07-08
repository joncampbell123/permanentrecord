
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
#include "wavstruc.h"
#include "mp3write.h"

#include "as_alsa.h"

#if defined(HAVE_LAME)
MP3Writer::MP3Writer() : fd(-1), fmt_size(0), wav_data_start(0), wav_data_limit((uint32_t)0x7F000000ul) {
}

MP3Writer::~MP3Writer() {
    Close();
}

bool MP3Writer::Open(const std::string &path) {
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

void MP3Writer::Close(void) {
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

bool MP3Writer::SetFormat(const AudioFormat &fmt) {
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

bool MP3Writer::IsOpen(void) const {
    return (fd >= 0);
}

int MP3Writer::Write(const void *buffer,unsigned int len) {
    if (IsOpen()) {
        if (flip_sign)
            return _write_xlat(buffer,len);
        else
            return _write_raw(buffer,len);
    }

    return -EINVAL;
}

void MP3Writer::_xlat(unsigned char *d,const unsigned char *s,unsigned int len) {
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

int MP3Writer::_write_xlat(const void *buffer,unsigned int len) {
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

int MP3Writer::_write_raw(const void *buffer,unsigned int len) {
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

windows_WAVEFORMAT *MP3Writer::waveformat(void) {
    return (windows_WAVEFORMAT*)fmt;
}

windows_WAVEFORMATEX *MP3Writer::waveformatex(void) {
    return (windows_WAVEFORMATEX*)fmt;
}

windows_WAVEFORMATEXTENSIBLE *MP3Writer::waveformatextensible(void) {
    return (windows_WAVEFORMATEXTENSIBLE*)fmt;
}

void MP3Writer::free_lame(void) {
    if (lame_global != NULL) {
        lame_close(lame_global);
        lame_global = NULL;
    }
}

bool MP3Writer::setup_lame(void) {
    if (lame_global == NULL) {
        lame_global = lame_init();
        if (lame_global == NULL) return false;

        // TODO

        if (lame_init_params(lame_global)) {
            fprintf(stderr,"LAME MP3 encoder rejected setup\n");
            free_lame();
            return false;
        }
    }

    return (lame_global != NULL);
}
#endif

