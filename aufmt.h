
#ifndef __AUFMT_H
#define __AUFMT_H

enum {
    AFMT_PCMU=1,
    AFMT_PCMS=2
};

struct AudioFormat {
    uint16_t            format_tag;
    uint32_t            sample_rate;
    uint8_t             channels;
    uint8_t             bits_per_sample;

    uint32_t            bytes_per_frame;
    uint32_t            samples_per_frame;

    void updateFrameInfo_PCM(void);
    void updateFrameInfo_NONE(void);
    void updateFrameInfo(void);
};

#endif //__AUFMT_H

