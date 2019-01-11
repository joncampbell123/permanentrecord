
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
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
#include "ole32.h"

#include "as_applecore.h"

#if defined(HAVE_COREAUDIO_COREAUDIO_H)
# include <AudioToolbox/AudioServices.h>
# include <AudioToolbox/AudioToolbox.h>

static bool applecore_atexit_set = false;

void applecore_atexit(void) {
    // TODO: Needed?
}

void applecore_atexit_init(void) {
    if (!applecore_atexit_set) {
        applecore_atexit_set = 1;
        atexit(applecore_atexit);
    }
}

class AudioSourceAPPLECORE : public AudioSource {
public:
    AudioSourceAPPLECORE() : bytes_per_frame(0), isUserOpen(false) {
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceAPPLECORE() { applecore_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "";

            if (applecore_device_string != sel) {
                applecore_close();
                applecore_device_string = sel;
            }

            return 0;
        }

        return -EBUSY;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        names.clear();

        // Dear Apple: If I have to dig up random code from StackOverflow and use
        //             very obscure parameters to function calls that are very vague
        //             and undocumented, then that should be a wakeup call that your
        //             API and documentation sucks and should be improved.
        //
        //             Come on, even Microsoft MSDN documentation is way ahead of
        //             yours (and Microsoft used to have problems documenting their
        //             Windows APIs in a meaningful way)
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };

        UInt32 dataSize = 0;
        OSStatus status;

        status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize);
        if (status != kAudioHardwareNoError) return -ENODEV;

        UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);

        fprintf(stderr,"AppleCore: %u devices\n",(unsigned int)deviceCount);

        if (deviceCount == 0) return 0;

        AudioDeviceID* audioDevices = new AudioDeviceID[deviceCount];

        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress,
            0, NULL, &dataSize, audioDevices) == kAudioHardwareNoError) {
            propertyAddress.mScope = kAudioDevicePropertyScopeInput;
            for (unsigned int ai=0;ai < deviceCount;ai++) {
                /* an AudioDeviceID is an AudioObjectID which is a UInt32 */
                const AudioDeviceID adid = audioDevices[ai];

                CFStringRef deviceUID = NULL;
                dataSize = sizeof(deviceUID);
                propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
                AudioObjectGetPropertyData(adid, &propertyAddress, 0, NULL, &dataSize, &deviceUID);
 
                CFStringRef deviceName = NULL;
                dataSize = sizeof(deviceName);
                propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
                AudioObjectGetPropertyData(adid, &propertyAddress, 0, NULL, &dataSize, &deviceName);
 
                CFStringRef deviceManufacturer = NULL;
                dataSize = sizeof(deviceManufacturer);
                propertyAddress.mSelector = kAudioDevicePropertyDeviceManufacturerCFString;
                AudioObjectGetPropertyData(adid, &propertyAddress, 0, NULL, &dataSize, &deviceManufacturer);

                AudioDevicePair p;

                if (deviceUID != NULL) {
                    p.name = CFStringGetCStringPtr(deviceUID,kCFStringEncodingUTF8);
                }

                if (deviceName != NULL) {
                    p.desc = CFStringGetCStringPtr(deviceName,kCFStringEncodingUTF8);
                }

                if (deviceManufacturer != NULL) {
                    p.desc += " [";
                    p.desc += CFStringGetCStringPtr(deviceManufacturer,kCFStringEncodingUTF8);
                    p.desc += "]";
                }

                if (!p.name.empty())
                    names.push_back(p);

                CFRelease(deviceUID);
                CFRelease(deviceName);
                CFRelease(deviceManufacturer);
            }
        }
        else {
            fprintf(stderr,"AppleCore: Unable to get kAudioObjectSystemObject property data\n");
        }

        delete[] audioDevices;
        return 0;
    }
    virtual bool IsOpen(void) { return isUserOpen; }
    virtual const char *GetSourceName(void) { return "Mac OS X Core Audio"; }
    virtual const char *GetDeviceName(void) { return applecore_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (!applecore_open())
                return -1;

            if (!applecore_apply_format(chosen_format)) {
                applecore_close();
                return -1;
            }

            bytes_per_frame = chosen_format.bytes_per_frame;

            isUserOpen = true;
        }

        return 0;
    }
    virtual int Close(void) {
        isUserOpen = false;
        applecore_close();
        return 0;
    }

    virtual int SetFormat(const struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!applecore_open())
            return -ENODEV;

        AudioFormat tmp = fmt;
        if (!applecore_apply_format(/*&*/tmp)) {
            applecore_close();
            return -EINVAL;
        }

        chosen_format = tmp;
        chosen_format.updateFrameInfo();
        applecore_close();
        return 0;
    }
    virtual int GetFormat(struct AudioFormat &fmt) {
        return -EINVAL;
    }
    virtual int QueryFormat(struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!applecore_open())
            return -ENODEV;

        if (!applecore_apply_format(/*&*/fmt)) {
            applecore_close();
            return -EINVAL;
        }

        fmt.updateFrameInfo();
        applecore_close();
        return 0;
    }
    virtual int GetAvailable(void) {
        if (IsOpen()) {
            return 0;
        }

        return 0;
    }
    virtual int Read(void *buffer,unsigned int bytes) {
        if (IsOpen()) {
            return 0;
        }

        return -EINVAL;
    }
private:
    std::string                 applecore_device_string;
    AudioFormat                 chosen_format;
    unsigned int                bytes_per_frame;
    bool                        isUserOpen;
private:
    bool format_is_valid(const AudioFormat &fmt) {
        if (fmt.format_tag == AFMT_PCMU || fmt.format_tag == AFMT_PCMS) {
            if (fmt.sample_rate == 0)
                return false;
            if (fmt.channels == 0)
                return false;
            if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
                return false;

            return true;
        }

        return false;
    }
    bool applecore_apply_format(AudioFormat &fmt) {
        if (fmt.format_tag == 0)
            return false;

        if (!applecore_open())
            return false;

        return false;
    }
    void applecore_force_close(void) {
        Close();
        applecore_close();
    }
    bool applecore_open(void) { // does NOT start capture
        return false;
    }
    void applecore_close(void) {
    }
};

AudioSource* AudioSourceAPPLECORE_Alloc(void) {
    return new AudioSourceAPPLECORE();
}
#endif

