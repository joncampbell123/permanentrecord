
#include "config.h"
#include "wavstruc.h"
#include "wavwrite.h"

#if defined(HAVE_LAME)

extern "C" {
#include <lame/lame.h>
}

class MP3Writer : public WAVWriter {
public:
    MP3Writer();
    virtual ~MP3Writer();
public:
    virtual bool Open(const std::string &path);
    virtual void Close(void);
    virtual bool SetFormat(const AudioFormat &fmt);
    virtual bool IsOpen(void) const;
    virtual int Write(const void *buffer,unsigned int len);
private:
    int             fd;
    bool            flip_sign;
    off_t           mp3_write_pos;
    unsigned int    source_format = 0;
    uint32_t        source_rate = 0;
    uint8_t         source_bits_per_sample = 0;
    uint8_t         source_channels = 0;
    lame_global_flags*  lame_global = NULL;
private:
    void free_lame(void);
    bool setup_lame(void);
};
#endif

