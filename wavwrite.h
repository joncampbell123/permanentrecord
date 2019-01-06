
#include "config.h"
#include "wavstruc.h"

class WAVWriter {
public:
    WAVWriter();
    ~WAVWriter();
public:
    bool Open(const std::string &path);
    void Close(void);
    bool SetFormat(const AudioFormat &fmt);
    bool IsOpen(void) const;
    int Write(const void *buffer,unsigned int len);
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

