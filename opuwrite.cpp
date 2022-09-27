
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <endian.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_MSC_VER)
# include <io.h>
#else
# include <unistd.h>
#endif
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
#include "opuwrite.h"

#include "as_alsa.h"

#if defined(HAVE_OPUSENC)
OpusWriter::OpusWriter() : WAVWriter() {
}

OpusWriter::~OpusWriter() {
    Close();
}

bool OpusWriter::Open(const std::string &path) {
    opus_int32 error;

    if (IsOpen())
        return true;

    assert(opus_enc == NULL);
    assert(opus_comments == NULL);

    if (source_rate == 0 || source_channels == 0 || source_bits_per_sample == 0)
        return false;

    opus_comments = ope_comments_create();
    if (opus_comments == NULL) {
        Close();
        return false;
    }
    ope_comments_add(opus_comments,"ENCODER","Permanent Record");

    opus_enc = ope_encoder_create_file(path.c_str(), opus_comments, (opus_int32)source_rate, (opus_int32)source_channels, (opus_int32)0, &error);
    if (opus_enc == NULL) {
        fprintf(stderr,"Opus failed to open: error %d\n",error);
        Close();
        return false;
    }

    ope_encoder_ctl(opus_enc, OPUS_SET_BITRATE((source_channels == 2 ? 112 : 64)*1024));
    ope_encoder_ctl(opus_enc, OPUS_SET_VBR(1)); /* VBR, not hard */

    opus_init = true;
    return true;
}

void OpusWriter::Close(void) {
    free_opus();
}

bool OpusWriter::SetFormat(const AudioFormat &fmt) {
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

bool OpusWriter::IsOpen(void) const {
    return (opus_enc != NULL);
}

template <typename T,typename sT,const bool flip_sign> static void _convert_type(float *dst,const T* buffer,unsigned int raw_samples/*combined*/) {
    const long half = ((T)1u << ((T)((sizeof(T) * size_t(8u)) - size_t(1u))));
    const float fhalf = float(half);
    const T xorT = flip_sign ? half : (T)0;

    for (unsigned int s=0;s < raw_samples;s++)
        *dst++ = (float)((long)((sT)((*buffer++) ^ xorT))) / fhalf;
}

bool OpusWriter::_convert(const size_t tmpsz,float *tmp,const size_t bpf,const void* &buffer,unsigned int tmp_samples/*combined*/) {
    assert(source_channels == 1u || source_channels == 2u);
    assert(bpf == ((unsigned int)source_channels * ((unsigned int)source_bits_per_sample >> 3u)));
    const unsigned int raw_samples = tmp_samples * (unsigned int)source_channels;
    assert(tmpsz >= (sizeof(*tmp) * raw_samples));

    if (!opus_init) return false;

    if (flip_sign) {
        if (source_bits_per_sample == 8)
            _convert_type<uint8_t,int8_t,/*flipsign*/true>(tmp,(const uint8_t*)buffer,raw_samples);
        else if (source_bits_per_sample == 16)
            _convert_type<uint16_t,int16_t,/*flipsign*/true>(tmp,(const uint16_t*)buffer,raw_samples);
        else if (source_bits_per_sample == 32)
            _convert_type<uint32_t,int32_t,/*flipsign*/true>(tmp,(const uint32_t*)buffer,raw_samples);
        else
            abort();
    }
    else {
        if (source_bits_per_sample == 8)
            _convert_type<uint8_t,int8_t,/*flipsign*/false>(tmp,(const uint8_t*)buffer,raw_samples);
        else if (source_bits_per_sample == 16)
            _convert_type<uint16_t,int16_t,/*flipsign*/false>(tmp,(const uint16_t*)buffer,raw_samples);
        else if (source_bits_per_sample == 32)
            _convert_type<uint32_t,int32_t,/*flipsign*/false>(tmp,(const uint32_t*)buffer,raw_samples);
        else
            abort();
    }

    buffer = (const void*)((const unsigned char*)buffer + (bpf * tmp_samples));

    int ret = ope_encoder_write_float(opus_enc, tmp, (int)tmp_samples);
    if (ret != OPE_OK) return false;

    return true;
}

int OpusWriter::Write(const void *buffer,unsigned int len) {
    if (IsOpen()) {
        const size_t bpf = (size_t)((unsigned int)source_bits_per_sample >> 3u) * (size_t)source_channels;
        unsigned int samples = len / (unsigned int)bpf;
        constexpr unsigned int tmp_len = 4096;
        const unsigned int tmp_len_samples = tmp_len / source_channels;
        float tmp[tmp_len];

        while (samples >= tmp_len_samples) {
            if (!_convert(sizeof(tmp),tmp,bpf,buffer,tmp_len_samples)) return -ENOSPC;
            samples -= tmp_len_samples;
        }
        if (samples > 0) {
            if (!_convert(sizeof(tmp),tmp,bpf,buffer,samples)) return -ENOSPC;
        }

        return (int)len;
    }

    return -EINVAL;
}

void OpusWriter::free_opus(void) {
    if (opus_enc) {
        ope_encoder_drain(opus_enc);
        ope_encoder_destroy(opus_enc);
        opus_enc = NULL;
    }
    if (opus_comments) {
        ope_comments_destroy(opus_comments);
        opus_comments = NULL;
    }
    opus_init = false;
}
#endif

