
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
#include "vrbwrite.h"

#include "as_alsa.h"

#if defined(HAVE_VORBISENC)
VorbisWriter::VorbisWriter() : WAVWriter(), fd(-1), vrb_write_pos(0) {
}

VorbisWriter::~VorbisWriter() {
    Close();
}

bool VorbisWriter::Open(const std::string &path) {
    if (IsOpen())
        return true;
    if (!setup_vorbis())
        return false;

    fd = open(path.c_str(),O_RDWR|O_CREAT|O_TRUNC|O_BINARY,0644);
    if (fd < 0) {
        fprintf(stderr,"Failed to open WAV output, %s\n",strerror(errno));
        return false;
    }

    /* header */
    {
        ogg_packet header={0},header_comm={0},header_code={0};

        vorbis_analysis_headerout(&vrb_vd,&vrb_vc,&header,&header_comm,&header_code);

        ogg_stream_packetin(&ogg_os, &header);
        ogg_stream_packetin(&ogg_os, &header_comm);
        ogg_stream_packetin(&ogg_os, &header_code);

        if (!_flush_ogg_os()) {
            fprintf(stderr,"Failed to flush OGG output\n");
            Close();
            return false;
        }
    }

    vrb_write_pos = lseek(fd,0,SEEK_END);
    return true;
}

bool VorbisWriter::_flush_ogg_os(void) {
    while (1) {
        if (fd < 0) return false;

        int result = ogg_stream_flush(&ogg_os, &ogg_og);
        if (result == 0) break;
        if (result < 0) return false; // error condition, right?

        if (write(fd, ogg_og.header, (size_t)ogg_og.header_len) != ogg_og.header_len) return false;
        if (write(fd, ogg_og.body,   (size_t)ogg_og.body_len)   != ogg_og.body_len)   return false;

        vrb_write_pos += (off_t)(ogg_og.header_len + ogg_og.body_len);
    }

    return true;
}

void VorbisWriter::Close(void) {
    if (fd >= 0) {
        if (vrb_write_pos != (off_t)0 && vrb_init) {
            bool eos = false;

            /* write closing page */
            vorbis_analysis_wrote(&vrb_vd, 0);

            /* write out until the EOS page */
            while (vorbis_analysis_blockout(&vrb_vd, &vrb_vb) == 1) {
                vorbis_analysis(&vrb_vb, NULL);
                vorbis_bitrate_addblock(&vrb_vb);

                while (vorbis_bitrate_flushpacket(&vrb_vd, &ogg_op)) {
                    ogg_stream_packetin(&ogg_os, &ogg_op);
                    if (!_flush_ogg_os()) break;
                    if (ogg_page_eos(&ogg_og)) {
                        eos = true;
                        break;
                    }
                }
            }

            if (!eos) fprintf(stderr,"Ogg Vorbis warning, last page did not signal EOS\n");
        }

        close(fd);
        fd = -1;
    }
    vrb_write_pos = 0;
    free_vorbis();
}

bool VorbisWriter::SetFormat(const AudioFormat &fmt) {
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

bool VorbisWriter::IsOpen(void) const {
    return (fd >= 0);
}

template <const bool flip_sign> static void _convert_type24(float **dst,const unsigned char* buffer,unsigned int tmp_len_samples,const unsigned int channels) {
    const long half = 1u << (24u - 1u);
    const float fhalf = float(half);
    const uint32_t xorT = flip_sign ? half : 0u;

    for (unsigned int c=0;c < channels;c++) {
        assert(dst[c] != NULL);

        const unsigned char* sp = buffer + c * 3u;
        float *dp = dst[c];

        for (unsigned int s=0;s < tmp_len_samples;s++,sp += channels*3u)
            *dp++ = (float)__lesx24(__leu24(sp) ^ xorT) / fhalf;
    }
}

template <typename T,typename sT,const bool flip_sign> static void _convert_type(float **dst,const T* buffer,unsigned int tmp_len_samples,const unsigned int channels) {
    const long half = ((T)1u << ((T)((sizeof(T) * size_t(8u)) - size_t(1u))));
    const float fhalf = float(half);
    const T xorT = flip_sign ? half : (T)0;

    for (unsigned int c=0;c < channels;c++) {
        assert(dst[c] != NULL);

        const T* sp = buffer + c;
        float *dp = dst[c];

        for (unsigned int s=0;s < tmp_len_samples;s++,sp += channels)
            *dp++ = (float)((long)((sT)(*sp ^ xorT))) / fhalf;
    }
}

bool VorbisWriter::_convert(const size_t bpf,const void* &buffer,unsigned int tmp_len_samples) {
    ogg_packet ogg_op = {0};

    assert(source_channels == 1u || source_channels == 2u);
    assert(bpf == ((unsigned int)source_channels * ((unsigned int)source_bits_per_sample >> 3u)));

    if (!vrb_init) return false;

    float **dstp = vorbis_analysis_buffer(&vrb_vd, (int)tmp_len_samples);
    if (dstp == NULL) return false;

    if (flip_sign) {
        if (source_bits_per_sample == 8)
            _convert_type<uint8_t,int8_t,/*flipsign*/true>(dstp,(const uint8_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 16)
            _convert_type<uint16_t,int16_t,/*flipsign*/true>(dstp,(const uint16_t*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 24)
            _convert_type24</*flipsign*/true>(dstp,(const unsigned char*)buffer,tmp_len_samples,source_channels);
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
        else if (source_bits_per_sample == 24)
            _convert_type24</*flipsign*/false>(dstp,(const unsigned char*)buffer,tmp_len_samples,source_channels);
        else if (source_bits_per_sample == 32)
            _convert_type<uint32_t,int32_t,/*flipsign*/false>(dstp,(const uint32_t*)buffer,tmp_len_samples,source_channels);
        else
            abort();
    }

    buffer = (const void*)((const unsigned char*)buffer + (bpf * tmp_len_samples));

    vorbis_analysis_wrote(&vrb_vd, (int)tmp_len_samples);
    while (vorbis_analysis_blockout(&vrb_vd, &vrb_vb) == 1) {
        vorbis_analysis(&vrb_vb, NULL);
        vorbis_bitrate_addblock(&vrb_vb);

        while (vorbis_bitrate_flushpacket(&vrb_vd, &ogg_op)) {
            ogg_stream_packetin(&ogg_os, &ogg_op);
            if (!_flush_ogg_os()) return false;
        }
    }

    return true;
}

int VorbisWriter::Write(const void *buffer,unsigned int len) {
    if (IsOpen()) {
        const size_t bpf = (size_t)((unsigned int)source_bits_per_sample >> 3u) * (size_t)source_channels;
        unsigned int samples = len / (unsigned int)bpf;
        constexpr unsigned int tmp_len = 4096;
        const unsigned int tmp_len_samples = tmp_len / source_channels;

        while (samples >= tmp_len_samples) {
            if (!_convert(bpf,buffer,tmp_len_samples)) return -ENOSPC;
            samples -= tmp_len_samples;
        }
        if (samples > 0) {
            if (!_convert(bpf,buffer,samples)) return -ENOSPC;
        }

        return (int)len;
    }

    return -EINVAL;
}

void VorbisWriter::free_vorbis(void) {
    if (vrb_init) {
        // ogg page and packet point to memory managed by libvorbis
        ogg_stream_clear(&ogg_os);
        vorbis_block_clear(&vrb_vb);
        vorbis_dsp_clear(&vrb_vd);
        vorbis_comment_clear(&vrb_vc);
        vorbis_info_clear(&vrb_vi);
        vrb_init = false;
    }
}

bool VorbisWriter::setup_vorbis(void) {
    if (!vrb_init) {
        if (source_rate == 0)
            return false;
        if (source_channels == 0)
            return false;
        if (source_format == 0)
            return false;

        vrb_init = true;

        vorbis_info_init(&vrb_vi);
        if (vorbis_encode_init_vbr(&vrb_vi, (int)source_channels, (int)source_rate, .55f)) {
            free_vorbis();
            return false;
        }

        vorbis_comment_init(&vrb_vc);
        vorbis_comment_add_tag(&vrb_vc, "ENCODER", "Permanent Record recorder");

        vorbis_block_clear(&vrb_vb);

        vorbis_analysis_init(&vrb_vd,&vrb_vi);
        vorbis_block_init(&vrb_vd,&vrb_vb);
        srand((unsigned int)time(NULL));
        ogg_stream_init(&ogg_os,rand());
    }

    return vrb_init;
}
#endif

