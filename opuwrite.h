
#include "config.h"
#include "wavstruc.h"
#include "wavwrite.h"

#if defined(HAVE_OPUSENC)

extern "C" {
#include <opus/opusenc.h>
}

class OpusWriter : public WAVWriter {
public:
    OpusWriter();
    virtual ~OpusWriter();
public:
    virtual bool Open(const std::string &path);
    virtual void Close(void);
    virtual bool SetFormat(const AudioFormat &fmt);
    virtual bool IsOpen(void) const;
    virtual int Write(const void *buffer,unsigned int len);
private:
    bool            flip_sign;
    unsigned int    source_format = 0;
    uint32_t        source_rate = 0;
    uint8_t         source_bits_per_sample = 0;
    uint8_t         source_channels = 0;
private:
    OggOpusEnc*     opus_enc = NULL;
    OggOpusComments* opus_comments = NULL;
    bool            opus_init = false;
private:
    void free_opus(void);
    bool _convert(const size_t tmpsz,float *tmp,const size_t bpf,const void* &buffer,unsigned int raw_samples/*combined*/);
};
#endif

