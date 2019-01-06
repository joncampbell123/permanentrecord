
#include "common.h"
#include "audev.h"

class AudioSource {
public:
                        AudioSource();
    virtual             ~AudioSource();
public:
    virtual int         EnumOptions(std::vector<AudioOptionPair> &names);
    virtual int         SetOption(const char *name,const char *value);
    virtual int         SelectDevice(const char *str);
    virtual int         EnumDevices(std::vector<AudioDevicePair> &names);
    virtual int         SetFormat(const struct AudioFormat &fmt);
    virtual int         GetFormat(struct AudioFormat &fmt);
    virtual int         QueryFormat(struct AudioFormat &fmt);
    virtual int         Open(void);
    virtual int         Close(void);
    virtual bool        IsOpen(void);
    virtual int         GetAvailable(void);
    virtual int         Read(void *buffer,unsigned int bytes);
    virtual const char* GetSourceName(void);
    virtual const char* GetDeviceName(void);
};

