
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

# include <pthread.h>

# include <queue>

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
    AudioSourceAPPLECORE() : bytes_per_frame(0), isUserOpen(false), audio_queue_obj(NULL) {
        pthread_mutex_init(&audio_bufs_mutex,NULL);
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceAPPLECORE() { applecore_force_close(); pthread_mutex_destroy(&audio_bufs_mutex); }
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

                bool isInput = false;

                dataSize = 0;
                propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
                AudioObjectGetPropertyDataSize(adid, &propertyAddress, 0, NULL, &dataSize);
                {
                    unsigned char *buf = new unsigned char[dataSize];
                    AudioBufferList *blist = (AudioBufferList*)buf;

                    if (AudioObjectGetPropertyData(adid, &propertyAddress,
                        0, NULL, &dataSize, blist) == kAudioHardwareNoError) {
                        if (blist->mNumberBuffers != 0) isInput = true;
                    }

                    delete[] buf;
                }

                if (isInput) {
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
                }

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

            assert(audio_queue_obj != NULL);

            if (!refill_buffers()) {
                fprintf(stderr,"Unable to refill buffers\n");
                applecore_close();
                return false;
            }

            if (AudioQueueStart(audio_queue_obj,NULL) != noErr) {
                fprintf(stderr,"Unable to start queue\n");
                applecore_close();
                return false;
            }

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

        AudioFormat old = chosen_format;

        chosen_format = fmt;
        chosen_format.updateFrameInfo();
        bytes_per_frame = chosen_format.bytes_per_frame;
        if (!applecore_open()) {/*may change format*/
            chosen_format = old;
            return -ENODEV;
        }

        applecore_close();
        return 0;
    }
    virtual int GetFormat(struct AudioFormat &fmt) {
        if (fmt.format_tag == 0)
            return -EINVAL;

        fmt = chosen_format;
        return 0;
    }
    virtual int QueryFormat(struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        AudioFormat old = chosen_format;

        chosen_format = fmt;
        chosen_format.updateFrameInfo();
        bytes_per_frame = chosen_format.bytes_per_frame;
        if (!applecore_open()) {
            chosen_format = old;
            return -ENODEV;
        }

        fmt.updateFrameInfo();
        applecore_close();
        chosen_format = old;
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
            unsigned char *d = (unsigned char*)buffer;
            int rd = 0;

            bytes -= bytes % bytes_per_frame;

            refill_buffers();

            while (bytes > 0) {
                if (ab_reading == NULL) {
                    AudioQueueBufferRef buf = NULL;

                    pthread_mutex_lock(&audio_bufs_mutex);
                    if (!audio_bufs.empty()) {
                        buf = audio_bufs.front();
                        audio_bufs.pop();
                    }
                    pthread_mutex_unlock(&audio_bufs_mutex);

                    if (buf == NULL) break;

                    ab_reading = buf;
                    ab_readpos = 0;
                }

                assert(ab_reading != NULL);

                if (ab_reading->mAudioDataByteSize % bytes_per_frame) {
                    fprintf(stderr,"WARNING: returned data not a multiple of nBlockAlign\n");
                    ab_reading->mAudioDataByteSize -= ab_reading->mAudioDataByteSize % bytes_per_frame;
                }

                if (ab_readpos >= ab_reading->mAudioDataByteSize) {
                    ab_readpos = 0;
                    AudioQueueFreeBuffer(audio_queue_obj,ab_reading);
                    ab_reading = NULL;
                    continue;
                }

                assert(ab_readpos < ab_reading->mAudioDataByteSize);

                unsigned int howmuch = ab_reading->mAudioDataByteSize - ab_readpos;
                if (howmuch > bytes) howmuch = bytes;

                if (howmuch > 0) {
                    memcpy(d,(unsigned char*)(ab_reading->mAudioData) + ab_readpos,howmuch);
                    ab_readpos += howmuch;
                    bytes -= howmuch;
                    rd += howmuch;
                    d += howmuch;

                    assert(ab_readpos <= ab_reading->mAudioDataByteSize);
                }
                else {
                    break;
                }
            }

            return rd;
        }

        return -EINVAL;
    }
private:
    unsigned int                ab_readpos;
    AudioQueueBufferRef         ab_reading;
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
    void applecore_force_close(void) {
        Close();
        applecore_close();
    }
    static void aq_cb(void *inUserData,AudioQueueRef inAQ,AudioQueueBufferRef inBuffer,const AudioTimeStamp *inStartTime,UInt32 inNumberPacketDescriptions,const AudioStreamPacketDescription *inPacketDescs) {
        assert(inUserData != NULL);

        (void)inAQ;
        (void)inStartTime;
        (void)inPacketDescs;
        (void)inNumberPacketDescriptions;

        AudioSourceAPPLECORE *_this = (AudioSourceAPPLECORE*)inUserData;

        pthread_mutex_lock(&_this->audio_bufs_mutex);

        if (_this->audio_bufs_queued > 0)
            _this->audio_bufs_queued--;
        else
            fprintf(stderr,"WARNING: bufs queued underrun\n");

        _this->audio_bufs.push(inBuffer);

        pthread_mutex_unlock(&_this->audio_bufs_mutex);
    }
    bool applecore_open(void) { // does NOT start capture
        if (audio_queue_obj == NULL) {
            audio_bufs_queued = 0;
            ab_reading = NULL;

            /* unfortunately a format is REQUIRED to open an audio queue */
            if (chosen_format.format_tag == 0)
                return false;

            if (!FormatToStreamDesc(audio_stream_desc,chosen_format))
                return false;

            OSStatus err;

            if ((err=AudioQueueNewInput(&audio_stream_desc,aq_cb,(void*)this,NULL,NULL,0,&audio_queue_obj)) != noErr) {
                fprintf(stderr,"AudioQueueNewInput err %d\n",(int)err);
                applecore_close();
                return false;
            }

            UInt32 dataSize = sizeof(audio_stream_desc);

            // confirm format
            if ((err=AudioQueueGetProperty(audio_queue_obj, kAudioQueueProperty_StreamDescription, &audio_stream_desc, &dataSize)) != noErr) {
                fprintf(stderr,"Failed to retrieve format from queue\n");
                applecore_close();
                return false;
            }

            if (!StreamDescToFormat(chosen_format,audio_stream_desc)) {
                fprintf(stderr,"Chosen format not representable\n");
                applecore_close();
                return false;
            }

            // make sure to talk to the device wanted
            if (!applecore_device_string.empty()) {
                CFStringRef dv = CFStringCreateWithCString(NULL,applecore_device_string.c_str(),kCFStringEncodingUTF8);

                if (AudioQueueSetProperty(audio_queue_obj, kAudioQueueProperty_CurrentDevice, &dv, sizeof(dv)) != noErr) {
                    fprintf(stderr,"Failed to select device\n");
                    CFRelease(dv);
                    return false;
                }

                CFRelease(dv);
            }
        }

        return true;
    }
    void applecore_close(void) {
        if (audio_queue_obj != NULL) {
            AudioQueueStop(audio_queue_obj, true);
            AudioQueueDispose(audio_queue_obj, true);
            audio_queue_obj = NULL;
        }

        /* "Disposing of an audio queue also disposes of it's buffers" */
        ab_reading = NULL;
        audio_bufs_queued = 0;
        while (!audio_bufs.empty())
            audio_bufs.pop();
    }
    bool StreamDescToFormat(AudioFormat &f,AudioStreamBasicDescription &d) {
        if (d.mFormatID == kAudioFormatLinearPCM) {
            if (d.mFormatFlags & kAudioFormatFlagIsFloat)
                return false;

            if (d.mFormatFlags & kAudioFormatFlagIsSignedInteger)
                f.format_tag = AFMT_PCMS;
            else
                f.format_tag = AFMT_PCMU;

            f.bits_per_sample = (uint8_t)d.mBitsPerChannel;
            f.channels = (uint8_t)d.mChannelsPerFrame;
            f.sample_rate = (unsigned int)d.mSampleRate; // is double (float) in Mac OS X
            f.updateFrameInfo();
            return true;
        }

        return false;
    }
    bool FormatToStreamDesc(AudioStreamBasicDescription &d,AudioFormat &f) {
        if (f.format_tag == AFMT_PCMU || f.format_tag == AFMT_PCMS) {
            memset(&d,0,sizeof(d));
            d.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
            d.mFormatID = kAudioFormatLinearPCM;
            d.mBitsPerChannel = f.bits_per_sample;
            d.mChannelsPerFrame = f.channels;
            d.mFramesPerPacket = 1;
            d.mSampleRate = f.sample_rate;
            d.mBytesPerFrame = ((f.bits_per_sample + 7u) / 8u) * f.channels;
            d.mBytesPerPacket = d.mBytesPerFrame;
            return true;
        }

        return false;
    }
    bool refill_buffers(void) {
        if (audio_queue_obj == NULL)
            return false;

        UInt32 bufsz = bytes_per_frame * (chosen_format.sample_rate / 30);
        if (bufsz == 0) return true;

        pthread_mutex_lock(&audio_bufs_mutex);
        while ((audio_bufs_queued + audio_bufs.size()) < 90) {
            AudioQueueBufferRef br;

            if (AudioQueueAllocateBuffer(audio_queue_obj, bufsz, &br) != noErr) {
                fprintf(stderr,"Failed to allocate buffer\n");
                break;
            }

            if (AudioQueueEnqueueBuffer(audio_queue_obj, br, 0, NULL) != noErr) {
                AudioQueueFreeBuffer(audio_queue_obj, br);
                fprintf(stderr,"Failed to enqueue buffer\n");
                break;
            }

            audio_bufs_queued++;
        }
        pthread_mutex_unlock(&audio_bufs_mutex);

        return true;
    }
private:
    AudioQueueRef                   audio_queue_obj;
    AudioStreamBasicDescription     audio_stream_desc;
    std::queue<AudioQueueBufferRef> audio_bufs; // audio buffers ready
    unsigned int                    audio_bufs_queued;
    pthread_mutex_t                 audio_bufs_mutex;
};

AudioSource* AudioSourceAPPLECORE_Alloc(void) {
    return new AudioSourceAPPLECORE();
}
#endif

