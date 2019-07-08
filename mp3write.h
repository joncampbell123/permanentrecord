
#include "config.h"
#include "wavstruc.h"
#include "wavwrite.h"

#if defined(HAVE_LAME)
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
    void _xlat(unsigned char *d,const unsigned char *s,unsigned int len);
    int _write_xlat(const void *buffer,unsigned int len);
    int _write_raw(const void *buffer,unsigned int len);
private:
    windows_WAVEFORMAT *waveformat(void);
    windows_WAVEFORMATEX *waveformatex(void);
    windows_WAVEFORMATEXTENSIBLE *waveformatextensible(void);
private:
    int             fd;
    unsigned char   fmt[64];
    size_t          fmt_size;
    bool            flip_sign;
    uint32_t        wav_data_start;
    uint32_t        wav_data_limit;
    uint32_t        wav_write_pos;
    unsigned int    bytes_per_sample;
    unsigned int    block_align;
};
#endif

