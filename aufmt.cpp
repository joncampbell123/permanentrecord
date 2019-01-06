
#include "common.h"
#include "aufmt.h"

void AudioFormat::updateFrameInfo_PCM(void) {
    bytes_per_frame = ((bits_per_sample + 7u) / 8u) * channels;
    samples_per_frame = 1;
}

void AudioFormat::updateFrameInfo_NONE(void) {
    bytes_per_frame = 0;
    samples_per_frame = 0;
}

void AudioFormat::updateFrameInfo(void) {
    switch (format_tag) {
        case AFMT_PCMU:
        case AFMT_PCMS:
            updateFrameInfo_PCM();
            break;
        default:
            updateFrameInfo_NONE();
            break;
    }
}

