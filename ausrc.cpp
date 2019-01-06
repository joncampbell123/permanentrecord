
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

#include "ausrc.h"

AudioSource::AudioSource() {
}

AudioSource::~AudioSource() {
}

int AudioSource::EnumOptions(std::vector<AudioOptionPair> &names) {
    (void)names;
    return -ENOSPC;
}

int AudioSource::SetOption(const char *name,const char *value) {
    (void)name;
    (void)value;
    return -ENOSPC;
}

int AudioSource::SelectDevice(const char *str) { (void)str; return -ENOSPC; }

int AudioSource::EnumDevices(std::vector<AudioDevicePair> &names) { (void)names; return -ENOSPC; }

int AudioSource::SetFormat(const struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }

int AudioSource::GetFormat(struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }

int AudioSource::QueryFormat(struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }

int AudioSource::Open(void) { return -ENOSPC; }

int AudioSource::Close(void) { return -ENOSPC; }

bool AudioSource::IsOpen(void) { return false; }

int AudioSource::GetAvailable(void) { return -ENOSPC; }

int AudioSource::Read(void *buffer,unsigned int bytes) { (void)buffer; (void)bytes; return -ENOSPC; }

const char *AudioSource::GetSourceName(void) { return "baseclass"; }

const char *AudioSource::GetDeviceName(void) { return ""; }

