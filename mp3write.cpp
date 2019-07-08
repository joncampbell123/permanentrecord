
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

            /* WAV only supports 8-bit unsigned or 16/24/32-bit signed PCM */
            if (fmt.bits_per_sample == 8 && fmt.format_tag == AFMT_PCMS)
                flip_sign = true;
            else if (fmt.bits_per_sample != 8 && fmt.format_tag == AFMT_PCMU)
                flip_sign = true;
            else
                flip_sign = false;

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

int MP3Writer::Write(const void *buffer,unsigned int len) {
    if (IsOpen()) {
#if 0
        if (flip_sign)
            return _write_xlat(buffer,len);
        else
            return _write_raw(buffer,len);
#endif
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

