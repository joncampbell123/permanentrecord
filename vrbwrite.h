
#include "config.h"
#include "wavstruc.h"
#include "wavwrite.h"

#if defined(HAVE_VORBISENC)

extern "C" {
#include <vorbis/vorbisenc.h>
}

class VorbisWriter : public WAVWriter {
public:
    VorbisWriter();
    virtual ~VorbisWriter();
public:
    virtual bool Open(const std::string &path);
    virtual void Close(void);
    virtual bool SetFormat(const AudioFormat &fmt);
    virtual bool IsOpen(void) const;
    virtual int Write(const void *buffer,unsigned int len);
private:
    int             fd;
    bool            flip_sign;
    off_t           vrb_write_pos;
    unsigned int    source_format = 0;
    uint32_t        source_rate = 0;
    uint8_t         source_bits_per_sample = 0;
    uint8_t         source_channels = 0;
private:
    ogg_stream_state        ogg_os = {0};
    ogg_page                ogg_og = {0};
    ogg_packet              ogg_op = {0};
    vorbis_info             vrb_vi = {0};
    vorbis_comment          vrb_vc = {0};
    vorbis_dsp_state        vrb_vd = {0};
    vorbis_block            vrb_vb = {0};
    bool                    vrb_init = false;
private:
    void free_vorbis(void);
    bool setup_vorbis(void);
    bool _convert(const size_t bpf,const void* &buffer,unsigned int tmp_len_samples);
    bool _flush_ogg_os(void);
};
#endif

