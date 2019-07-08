// TODO: Some way for the user to specify MP3 bitrate and other options.
// TODO: When closing the MP3 file, flush audio from LAME encoder to file.
//       Keep a buffer of the last 256KB of MP3 audio and re-play it to the
//       next file so that no audio is lost in the transition from MP3 to
//       MP3 file.

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
MP3Writer::MP3Writer() : WAVWriter(), fd(-1), mp3_write_pos(0) {
}

MP3Writer::~MP3Writer() {
    Close();
}

bool MP3Writer::Open(const std::string &path) {
    if (IsOpen())
        return true;
    if (!setup_lame())
        return false;

    fd = open(path.c_str(),O_RDWR|O_CREAT|O_TRUNC|O_BINARY,0644);
    if (fd < 0) {
        fprintf(stderr,"Failed to open WAV output, %s\n",strerror(errno));
        return false;
    }

    mp3_write_pos = 0;
    return true;
}

void MP3Writer::Close(void) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    mp3_write_pos = 0;
    free_lame();
}

bool MP3Writer::SetFormat(const AudioFormat &fmt) {
    if (IsOpen()) return false;

    // PCM formats ONLY
    switch (fmt.format_tag) {
        case AFMT_PCMU:
        case AFMT_PCMS:
            if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
                return false;
            if (fmt.channels < 1 || fmt.channels > 2)
                return false;
            if (fmt.sample_rate < 1000 || fmt.sample_rate > 192000)
                return false;

            if (fmt.format_tag == AFMT_PCMS)
                flip_sign = false;
            else
                flip_sign = true;

            source_rate = fmt.sample_rate;
            source_format = fmt.format_tag;
            source_bits_per_sample = fmt.bits_per_sample;
            source_channels = fmt.channels;
            break;
        default:
            return false;
    }

    return true;
}

bool MP3Writer::IsOpen(void) const {
    return (fd >= 0);
}

template <typename T,typename sT,const bool flip_sign> static void _convert_type(long *dst[2],const T* buffer,unsigned int tmp_len_samples,const unsigned int channels) {
    static_assert(sizeof(long) >= sizeof(T), "long type not large enough");
    const long half = ((T)1u << ((T)((sizeof(T) * size_t(8u)) - size_t(1u))));
    const long shf = (long)((sizeof(long) - sizeof(T)) * size_t(8u));
    const T xorT = flip_sign ? half : (T)0;

    for (unsigned int c=0;c < channels;c++) {
        assert(dst[c] != NULL);

        const T* sp = buffer + c;
        long *dp = dst[c];

        for (unsigned int s=0;s < tmp_len_samples;s++,sp += channels)
            *dp++ = ((long)((sT)(*sp ^ xorT))) << (long)shf;
    }
}

void MP3Writer::_convert(const size_t dstlen_b,long *dst,const size_t bpf,const void* &buffer,unsigned int tmp_len_samples) {
    assert(source_channels == 1u || source_channels == 2u);
    long *dstp[2] = {NULL,NULL};

    assert(dstlen_b >= (tmp_len_samples * source_channels * sizeof(long)));
    assert(bpf == ((unsigned int)source_channels * ((unsigned int)source_bits_per_sample >> 3u)));

    dstp[0] = dst;
    if (source_channels == 2u) dstp[1] = dst + tmp_len_samples;

    if (flip_sign) {
        if (source_bits_per_sample == 8)
            _convert_type<uint8_t,int8_t,/*flipsign*/true>(dstp,(const uint8_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 16)
            _convert_type<uint16_t,int16_t,/*flipsign*/true>(dstp,(const uint16_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 32)
            _convert_type<uint32_t,int32_t,/*flipsign*/true>(dstp,(const uint32_t*)buffer,tmp_len_samples,source_channels);
        else
            abort();
    }
    else {
        if (source_bits_per_sample == 8)
            _convert_type<uint8_t,int8_t,/*flipsign*/false>(dstp,(const uint8_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 16)
            _convert_type<uint16_t,int16_t,/*flipsign*/false>(dstp,(const uint16_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 32)
            _convert_type<uint32_t,int32_t,/*flipsign*/false>(dstp,(const uint32_t*)buffer,tmp_len_samples,source_channels);
        else
            abort();
    }

    buffer = (const void*)((const unsigned char*)buffer + (bpf * tmp_len_samples));
}

bool MP3Writer::_encode(const long *samp,unsigned int tmp_len_samples) {
    const long *dstp[2] = {NULL,NULL};
    unsigned char output[8192];

    if (lame_global == NULL || fd < 0)
        return false;

    dstp[0] = samp;
    if (source_channels == 2u) dstp[1] = samp + tmp_len_samples;
    else dstp[1] = dstp[0];

    int rd = lame_encode_buffer_long2(lame_global,dstp[0],dstp[1],(int)tmp_len_samples,output,sizeof(output));
    if (rd < 0) {
        fprintf(stderr,"LAME encoder error %d\n",rd);
        return false;
    }

    if (write(fd,output,(size_t)rd) != rd)
        return false;

    return true;
}

int MP3Writer::Write(const void *buffer,unsigned int len) {
    if (IsOpen()) {
        const size_t bpf = (size_t)((unsigned int)source_bits_per_sample >> 3u) * (size_t)source_channels;
        unsigned int samples = len / (unsigned int)bpf;
        constexpr unsigned int tmp_len = 4096;
        const unsigned int tmp_len_samples = tmp_len / source_channels;
        long tmp[tmp_len];

        while (samples >= tmp_len_samples) {
            _convert(sizeof(tmp),tmp,bpf,buffer,tmp_len_samples);
            if (!_encode(tmp,tmp_len_samples)) return -ENOSPC;
            samples -= tmp_len_samples;
        }
        if (samples > 0) {
            _convert(sizeof(tmp),tmp,bpf,buffer,samples);
            if (!_encode(tmp,samples)) return -ENOSPC;
        }

        return (int)len;
    }

    return -EINVAL;
}

void MP3Writer::free_lame(void) {
    if (lame_global != NULL) {
        lame_close(lame_global);
        lame_global = NULL;
    }
}

bool MP3Writer::setup_lame(void) {
    if (lame_global == NULL) {
        if (source_rate == 0)
            return false;
        if (source_channels == 0)
            return false;
        if (source_format == 0)
            return false;

        lame_global = lame_init();
        if (lame_global == NULL) return false;

        lame_set_in_samplerate(lame_global, (int)source_rate);
        lame_set_num_channels(lame_global, source_channels);
        lame_set_mode(lame_global, source_channels == 2 ? STEREO : MONO);

        if (source_rate >= 47000)
            lame_set_out_samplerate(lame_global, 48000);
        else if (source_rate >= 44000)
            lame_set_out_samplerate(lame_global, 44100);
        else if (source_rate >= 31000)
            lame_set_out_samplerate(lame_global, 32000);
        else if (source_rate >= 23000)
            lame_set_out_samplerate(lame_global, 24000);
        else if (source_rate >= 21000)
            lame_set_out_samplerate(lame_global, 22050);
        else if (source_rate >= 15000)
            lame_set_out_samplerate(lame_global, 16000);
        else if (source_rate >= 11500)
            lame_set_out_samplerate(lame_global, 12000);
        else if (source_rate >= 10750)
            lame_set_out_samplerate(lame_global, 11025);
        else
            lame_set_out_samplerate(lame_global, 8000);

        lame_set_original(lame_global, 1);
        lame_set_copyright(lame_global, 0);
        lame_set_quality(lame_global, 2);

        // TODO: User spec of bitrate
        if (source_rate >= 32000)
            lame_set_brate(lame_global, (source_channels == 2) ? 192 : 96);
        else if (source_rate >= 16000)
            lame_set_brate(lame_global, (source_channels == 2) ? 128 : 64);
        else
            lame_set_brate(lame_global, (source_channels == 2) ? 80 : 48);

        if (lame_init_params(lame_global)) {
            fprintf(stderr,"LAME MP3 encoder rejected setup\n");
            free_lame();
            return false;
        }
    }

    return (lame_global != NULL);
}
#endif

