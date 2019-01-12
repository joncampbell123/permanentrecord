
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
#include "wavwrite.h"
#include "recpath.h"
#include "ole32.h"

#include "as_alsa.h"
#include "as_pulse.h"

#ifdef TARGET_GUI_WINDOWS
# include "winres/resource.h"
# include "commctrl.h"
#endif

#ifndef TARGET_GUI
static std::string          ui_command;
#endif
static std::string          ui_source;
static std::string          ui_device;
static int                  ui_want_fmt = 0;
static long                 ui_want_rate = 0;
static int                  ui_want_channels = 0;
static int                  ui_want_bits = 0;

#ifdef TARGET_GUI_WINDOWS
DWORD WinCapThreadID = 0;
HANDLE WinCapThread = INVALID_HANDLE_VALUE;
AudioSource* active_source = NULL;
AudioFormat active_source_fmt;
HINSTANCE myInstance;
HWND hwndMain;
#endif

#ifndef TARGET_GUI
static void help(void) {
    fprintf(stderr," -h --help      Help text\n");
    fprintf(stderr," -ch <channels>\n");
    fprintf(stderr," -sr <sample rate>\n");
    fprintf(stderr," -bs <bits/sample>\n");
    fprintf(stderr," -fmt <format>\n");
    fprintf(stderr,"    pcmu    unsigned PCM\n");
    fprintf(stderr,"    pcms    signed PCM\n");
    fprintf(stderr," -d <device>\n");
    fprintf(stderr," -s <source>\n");
    fprintf(stderr," -c <command>\n");
    fprintf(stderr,"    rec          Record\n");
    fprintf(stderr,"    test         Test format\n");
    fprintf(stderr,"    listsrc      List audio sources\n");
    fprintf(stderr,"    listdev      List audio devices\n");
}

static int parse_argv(int argc,char **argv) {
    char *a;
    int i=1;

    while (i < argc) {
        a = argv[i++];
        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else if (!strcmp(a,"fmt")) {
                a = argv[i++];
                if (a == NULL) return 1;

                if (!strcmp(a,"pcmu"))
                    ui_want_fmt = AFMT_PCMU;
                else if (!strcmp(a,"pcms"))
                    ui_want_fmt = AFMT_PCMS;
                else
                    return 1;
            }
            else if (!strcmp(a,"bs")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_bits = atoi(a);
                if (ui_want_bits < 1 || ui_want_bits > 255) return 1;
            }
            else if (!strcmp(a,"ch")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_channels = atoi(a);
                if (ui_want_channels < 1 || ui_want_channels > 255) return 1;
            }
            else if (!strcmp(a,"sr")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_want_rate = strtol(a,NULL,0);
                if (ui_want_rate < 1l || ui_want_rate > 1000000l) return 1;
            }
            else if (!strcmp(a,"c")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_command = a;
            }
            else if (!strcmp(a,"s")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_source = a;
            }
            else if (!strcmp(a,"d")) {
                a = argv[i++];
                if (a == NULL) return 1;
                ui_device = a;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg\n");
            return 1;
        }
    }

    if (ui_command.empty()) {
        help();
        return 1;
    }

    return 0;
}
#endif

void ui_apply_format(AudioFormat &fmt) {
    if (ui_want_fmt > 0)
        fmt.format_tag = (uint16_t)ui_want_fmt;
    if (ui_want_rate > 0l)
        fmt.sample_rate = (uint32_t)ui_want_rate;
    if (ui_want_channels > 0)
        fmt.channels = (uint8_t)ui_want_channels;
    if (ui_want_bits > 0)
        fmt.bits_per_sample = (uint8_t)ui_want_bits;
}

bool ui_apply_options(AudioSource* alsa,AudioFormat &fmt) {
    if (alsa->SelectDevice(ui_device.c_str()) < 0) {
        fprintf(stderr,"Unable to set device\n");
        return false;
    }

    fmt.format_tag = 0;
    if (alsa->GetFormat(fmt) < 0) {
        /* some sources don't have a default */
        fmt.format_tag = AFMT_PCMS;
        fmt.bits_per_sample = 16;
        fmt.sample_rate = 48000;
        fmt.channels = 2;
    }

    ui_apply_format(fmt);
    if (alsa->SetFormat(fmt) < 0) {
        fprintf(stderr,"Unable to set format '%s'\n",ui_print_format(fmt).c_str());
        return false;
    }

    if (alsa->GetFormat(fmt) < 0) {
        fprintf(stderr,"Unable to get format\n");
        return false;
    }

    printf("Recording format: %s\n",ui_print_format(fmt).c_str());

    if (alsa->Open() < 0) {
        fprintf(stderr,"Unable to open\n");
        return false;
    }

    return true;
}

#define OVERREAD (16u)

static unsigned char audio_tmp[4096u + OVERREAD];

AudioFormat rec_fmt;
unsigned int VU_dec = 1;
unsigned long long framecount = 0;
unsigned long VUclip[8];
unsigned int VU[8];

std::string rec_path_wav;
std::string rec_path_info;
std::string rec_path_base;
WAVWriter* wav_out = NULL;
FILE *wav_info = NULL;

void ui_recording_draw(void) {
#ifdef TARGET_GUI_WINDOWS
    std::string msg;

    {
        unsigned long long samples = framecount * (unsigned long long)rec_fmt.samples_per_frame;
        unsigned int H,M,S,ss;
	char tmp[128];

        ss = (unsigned int)(samples % (unsigned long long)rec_fmt.sample_rate);
        ss = (unsigned int)(((unsigned long)ss * 100ul) / (unsigned long)rec_fmt.sample_rate);

        S = (unsigned int)(samples / (unsigned long long)rec_fmt.sample_rate);

        M = S / 60u;
        S %= 60u;

        H = M / 60u;
        M %= 60u;

        sprintf(tmp,"%02u:%02u:%02u.%02u %uHz %uch %ubps",H,M,S,ss,(unsigned int)rec_fmt.sample_rate,(unsigned int)rec_fmt.channels,(unsigned int)rec_fmt.bits_per_sample);

	msg = tmp;
    }

    SetDlgItemText(hwndMain,IDC_STATUS,msg.c_str());

    {
	    unsigned int L,R;
            double d;

            d = dBFS_measure((double)VU[0] / 65535);
            d = (d + 48) / 48; // VU meters are much longer in Windows GUI
            if (d < 0) d = 0;
            if (d > 1) d = 1;
            L = (unsigned int)((d * 0x7FFFul) + 0.5);

	    if (rec_fmt.channels >= 2) {
		    d = dBFS_measure((double)VU[0] / 65535);
		    d = (d + 48) / 48; // vu meters are much longer in windows gui
		    if (d < 0) d = 0;
		    if (d > 1) d = 1;
		    R = (unsigned int)((d * 0x7FFFul) + 0.5);
	    }
	    else {
		    R = L;
	    }

	    SendDlgItemMessage(hwndMain,IDC_VU1,PBM_SETRANGE,0,0ul/*min*/ | (0x7FFFul << 16ul));
	    SendDlgItemMessage(hwndMain,IDC_VU1,PBM_SETPOS,L,0);

	    SendDlgItemMessage(hwndMain,IDC_VU2,PBM_SETRANGE,0,0ul/*min*/ | (0x7FFFul << 16ul));
	    SendDlgItemMessage(hwndMain,IDC_VU2,PBM_SETPOS,R,0);
    }
#else
    printf("\x0D");

    {
        unsigned long long samples = framecount * (unsigned long long)rec_fmt.samples_per_frame;
        unsigned int H,M,S,ss;

        ss = (unsigned int)(samples % (unsigned long long)rec_fmt.sample_rate);
        ss = (unsigned int)(((unsigned long)ss * 100ul) / (unsigned long)rec_fmt.sample_rate);

        S = (unsigned int)(samples / (unsigned long long)rec_fmt.sample_rate);

        M = S / 60u;
        S %= 60u;

        H = M / 60u;
        M %= 60u;

        printf("%02u:%02u:%02u.%02u ",H,M,S,ss);
    }

    {
        unsigned int barl = 34u / rec_fmt.channels;
        unsigned int i,im,ch,chmax;
        char tmp[36];
        double d;

        chmax = rec_fmt.channels;
        if (chmax > 2) chmax = 2;

        for (ch=0;ch < chmax;ch++) {
            d = dBFS_measure((double)VU[ch] / 65535);
            d = (d + 48) / 48;
            if (d < 0) d = 0;
            if (d > 1) d = 1;
            im = (unsigned int)((d * barl) + 0.5);
            for (i=0;i < im;i++) tmp[i] = '=';
            for (   ;i < barl;i++) tmp[i] = ' ';
            tmp[i++] = VUclip[ch] > 0l ? '@' : '|';
            tmp[i++] = 0;
            assert(i <= sizeof(tmp));

            printf("%s",tmp);
        }
    }

    printf("\x0D");
    fflush(stdout);
#endif
}

void VU_init(const AudioFormat &fmt) {
    VU_dec = (unsigned int)((4410000ul / fmt.sample_rate) / 10ul);
    if (VU_dec == 0) VU_dec = 1;
}

void VU_advance_ch(const unsigned int ch,const unsigned int val) {
    if (VU[ch] < val)
        VU[ch] = val;
    else if (VU[ch] >= VU_dec)
        VU[ch] -= VU_dec;
    else if (VU[ch] > 0u)
        VU[ch] = 0;

    if (VU[ch] >= 0xFFF0u)
        VUclip[ch] = rec_fmt.sample_rate;
    else if (VUclip[ch] > 0u)
        VUclip[ch]--;
}

void VU_advance_pcmu_8(const uint8_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs((int)audio_tmp[ch] - 0x80) * 2u * 256u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu_16(const uint16_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs((long)audio_tmp[ch] - 0x8000l) * 2u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu_32(const uint32_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(((long)((int32_t)(audio_tmp[ch] ^ (uint32_t)0x80000000ul))) / 32768l);
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_8(const int8_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch]) * 2u * 256u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_16(const int16_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch]) * 2u;
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcms_32(const int32_t *audio_tmp,unsigned int rds) {
    unsigned int ch;

    while (rds-- > 0u) {
        for (ch=0;ch < rec_fmt.channels;ch++) {
            unsigned int val = (unsigned int)labs(audio_tmp[ch] / 32768l);
            VU_advance_ch(ch,val);
        }

        audio_tmp += rec_fmt.channels;
    }
}

void VU_advance_pcmu(const void *audio_tmp,unsigned int rds) {
         if (rec_fmt.bits_per_sample == 8)
        VU_advance_pcmu_8((const uint8_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 16)
        VU_advance_pcmu_16((const uint16_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 32)
        VU_advance_pcmu_32((const uint32_t*)audio_tmp,rds);
}

void VU_advance_pcms(const void *audio_tmp,unsigned int rds) {
         if (rec_fmt.bits_per_sample == 8)
        VU_advance_pcms_8((const int8_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 16)
        VU_advance_pcms_16((const int16_t*)audio_tmp,rds);
    else if (rec_fmt.bits_per_sample == 32)
        VU_advance_pcms_32((const int32_t*)audio_tmp,rds);
}

void VU_advance(const void *audio_tmp,unsigned int rd) {
    if (rec_fmt.format_tag == AFMT_PCMU) {
        VU_advance_pcmu(audio_tmp,rd / rec_fmt.bytes_per_frame);
    }
    else if (rec_fmt.format_tag == AFMT_PCMS) {
        VU_advance_pcms(audio_tmp,rd / rec_fmt.bytes_per_frame);
    }
}

void close_recording(void) {
    if (wav_info != NULL) {
        {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);

            if (tm != NULL) {
                fprintf(wav_info,"Recording stopped Y-M-D-H-M-S %04u-%02u-%02u %02u:%02u:%02u\n",
                        tm->tm_year+1900,
                        tm->tm_mon+1,
                        tm->tm_mday,
                        tm->tm_hour,
                        tm->tm_min,
                        tm->tm_sec);
            }
        }

        fclose(wav_info);
        wav_info = NULL;
    }
    if (wav_out) {
        delete wav_out;
        wav_out = NULL;
    }
}

bool open_recording(void) {
    if (wav_out != NULL || wav_info != NULL)
        return true;

    rec_path_base = make_recording_path_now();
    if (rec_path_base.empty()) {
        fprintf(stderr,"Unable to make recording path\n");
        return false;
    }

    rec_path_wav = rec_path_base + ".WAV";
    rec_path_info = rec_path_base + ".TXT";

    wav_info = fopen(rec_path_info.c_str(),"w");
    if (wav_info == NULL) {
        fprintf(stderr,"Unable to open %s, %s\n",rec_path_info.c_str(),strerror(errno));
        close_recording();
        return false;
    }

    wav_out = new WAVWriter();
    if (wav_out == NULL) {
        close_recording();
        return false;
    }
    if (!wav_out->SetFormat(rec_fmt)) {
        fprintf(stderr,"WAVE format rejected\n");
        close_recording();
        return false;
    }
    if (!wav_out->Open(rec_path_wav)) {
        fprintf(stderr,"WAVE open failed\n");
        close_recording();
        return false;
    }

    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);

        if (tm != NULL) {
            fprintf(wav_info,"Recording began Y-M-D-H-M-S %04u-%02u-%02u %02u:%02u:%02u\n",
                    tm->tm_year+1900,
                    tm->tm_mon+1,
                    tm->tm_mday,
                    tm->tm_hour,
                    tm->tm_min,
                    tm->tm_sec);
            fprintf(wav_info,"Recording format is: %s\n",
                    ui_print_format(rec_fmt).c_str());
        }
    }

    compute_auto_cut();

    printf("Recording to: %s\n",rec_path_wav.c_str());

    return true;
}

bool record_main(AudioSource* alsa,AudioFormat &fmt) {
    int rd,i,patience;

    for (i=0;i < 8;i++) {
        VUclip[i] = 0u;
        VU[i] = 0u;
    }
    framecount = 0;
    rec_fmt = fmt;
    VU_init(fmt);

    if (!open_recording()) {
        fprintf(stderr,"Unable to open recording\n");
        return false;
    }

    while (1) {
        if (signal_to_die) break;
        usleep(10000);

        if (time_to_auto_cut()) {
            if (wav_info) fprintf(stderr,"Auto-cut commencing\n");
            close_recording();
            open_recording();
        }

	patience = 10;
        do {
            if (signal_to_die || --patience < 0) break;

            audio_tmp[sizeof(audio_tmp) - OVERREAD] = 'x';
            rd = alsa->Read(audio_tmp,(unsigned int)(sizeof(audio_tmp) - OVERREAD));
            if (audio_tmp[sizeof(audio_tmp) - OVERREAD] != 'x') {
                fprintf(stderr,"Read buffer overrun\n");
                signal_to_die = 1;
                break;
            }

            if (rd > 0) {
                VU_advance(audio_tmp,(unsigned int)rd);

                ui_recording_draw();

                framecount += (unsigned long long)((unsigned int)rd / fmt.bytes_per_frame);

                if (wav_out != NULL) {
                    if (wav_out->Write(audio_tmp,(unsigned int)rd) != rd) {
                        fprintf(stderr,"WAV writing error, closing and reopening\n");
                        close_recording();
                    }
                }
                if (wav_out == NULL) {
                    if (!open_recording()) {
                        fprintf(stderr,"Unable to open recording\n");
                        signal_to_die = 1;
                        break;
                    }
                }
            }
        } while (rd > 0);

        if (rd < 0) {
            fprintf(stderr,"Problem with audio device\n");
            break;
        }
    }

    close_recording();
    printf("\n");
    return true;
}

#ifndef TARGET_GUI
int main(int argc,char **argv) {
#if defined(WIN32)
	ole32_coinit();
#endif

#if defined(WIN32) // HACK help me figure out what is going on
	setbuf(stdout,NULL);
	setbuf(stderr,NULL);
#endif

    if (parse_argv(argc,argv))
        return 1;

#if defined(WIN32)
#else
    /* I wrote this code in a hurry, please do not run as root. */
    /* You can enable ALSA audio access to non-root processes by modifying the user's
     * supplemental group list to add "audio", assuming the device nodes are owned by "audio" */
    if (geteuid() == 0 || getuid() == 0)
        fprintf(stderr,"WARNING: Do not run this program as root if you can help it!\n");
#endif

    /* NTS: For whatever reason, CTRL+C from the MinGW shell terminates this process immediately
     *      without calling our signal handler. */

    signal(SIGINT,sigma);
    signal(SIGTERM,sigma);
# ifdef SIGQUIT
    signal(SIGQUIT,sigma);
# endif

    if (ui_command == "test") {
        AudioSource* alsa = GetAudioSource(ui_source.c_str());
        AudioFormat fmt;

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        ui_apply_options(alsa,fmt);

        alsa->Close();
        delete alsa;
    }
    else if (ui_command == "rec") {
        AudioSource* alsa = GetAudioSource(ui_source.c_str());
        AudioFormat fmt;

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        if (ui_apply_options(alsa,fmt)) {
            if (!record_main(alsa,fmt))
                fprintf(stderr,"Recording loop failed\n");
        }

        alsa->Close();
        delete alsa;
    }
    else if (ui_command == "listsrc") {
        size_t i;

        printf("Audio sources:\n");

        for (i=0;audio_source_list[i].name != NULL;i++) {
            printf("    \"%s\" which is \"%s\"\n",audio_source_list[i].name,audio_source_list[i].desc);
        }
    }
    else if (ui_command == "listdev") {
        std::vector<AudioDevicePair> l;
        AudioSource* alsa = GetAudioSource(ui_source.c_str());

        if (alsa == NULL) {
            fprintf(stderr,"No such audio source '%s'\n",ui_source.c_str());
            return 1;
        }

        printf("Enumerating devices from \"%s\":\n",alsa->GetSourceName());

        if (alsa->EnumDevices(l) < 0) {
            fprintf(stderr,"Failed to enumerate devices\n");
            delete alsa;
            return 1;
        }

        for (auto i=l.begin();i != l.end();i++) {
            printf("    Device \"%s\":\n        which is \"%s\"\n",
                (*i).name.c_str(),(*i).desc.c_str());
        }

        printf("\n");
        printf("Default device is \"%s\"\n",alsa->GetDeviceName());

        delete alsa;
    }
    else {
        fprintf(stderr,"Unknown command '%s'\n",ui_command.c_str());
        return 1;
    }

    return 0;
}
#endif //TARGET_GUI

#ifdef TARGET_GUI_WINDOWS
DWORD WINAPI WinCapThreadProc(LPVOID param) {
	(void)param;

	assert(active_source != NULL);
	if (!record_main(active_source,active_source_fmt))
		return 1;

	return 0;
}

void EnableDlgItem(HWND hwnd,int id,BOOL en) {
	HWND h = GetDlgItem(hwnd,id);
	if (h != NULL) EnableWindow(h,en);
}

void win_stop_recording(void);

bool win_thread_stopped(void) {
	if (WinCapThread != INVALID_HANDLE_VALUE) {
		if (WaitForSingleObject(WinCapThread,0) == WAIT_OBJECT_0) {
			WinCapThread = INVALID_HANDLE_VALUE;//of course this reaps the thread too
			return true;
		}
	}

	return false;
}

bool win_is_recording(void) {
	return (active_source != NULL);
}

bool win_start_recording(void) {
	if (active_source == NULL) {
		assert(WinCapThread == INVALID_HANDLE_VALUE);
		signal_to_die = 0;

		EnableDlgItem(hwndMain,IDC_RECORD,FALSE);
		SetDlgItemText(hwndMain,IDC_RECORD,"Starting...");

		active_source = GetAudioSource(ui_source.c_str());
		if (active_source == NULL) {
			win_stop_recording();
			MessageBox(hwndMain,"No such source","",MB_OK);
			return false;
		}

		AudioFormat fmt;

		memset(&fmt,0,sizeof(fmt));

		fmt.sample_rate = (unsigned int)ui_want_rate;
		fmt.bits_per_sample = (uint8_t)ui_want_bits;
		fmt.channels = (uint8_t)ui_want_channels;

		if (fmt.bits_per_sample == 8)
			fmt.format_tag = AFMT_PCMU;
		else
			fmt.format_tag = AFMT_PCMS;

		/* also opens */
		if (!ui_apply_options(active_source,fmt)) {
			win_stop_recording();
			MessageBox(hwndMain,"Failed to start audio capture","",MB_OK);
			return false;
		}

		active_source_fmt = fmt;

		WinCapThread = CreateThread(NULL,0,WinCapThreadProc,NULL,0,&WinCapThreadID);
		if (WinCapThread == NULL) {
			win_stop_recording();
			MessageBox(hwndMain,"Failed to start audio capture thread","",MB_OK);
			return false;
		}

		EnableDlgItem(hwndMain,IDC_RECORD,TRUE);
		SetDlgItemText(hwndMain,IDC_RECORD,"&Stop");
	}

	return true;
}

void win_stop_recording(void) {
	if (active_source != NULL) {
		EnableDlgItem(hwndMain,IDC_RECORD,FALSE);
		SetDlgItemText(hwndMain,IDC_RECORD,"Stopping...");

		if (WinCapThread != INVALID_HANDLE_VALUE) {
			signal_to_die++;

			DWORD ret;
			MSG msg;

			do {
				while (PeekMessage(&msg,NULL,0,0,PM_REMOVE)) {
					if (IsDialogMessage(hwndMain,&msg)) {
					}
					else {
						TranslateMessage(&msg);
						DispatchMessage(&msg);
					}
				}

				ret = WaitForSingleObject(WinCapThread,100);
				if (ret != WAIT_TIMEOUT) break;
			} while(1);

			if (ret != WAIT_OBJECT_0)
				fprintf(stderr,"WARNING: Thread did not stop properly\n");

			WinCapThread = INVALID_HANDLE_VALUE;
		}

		active_source->Close();
		delete active_source;
		active_source = NULL;

		EnableDlgItem(hwndMain,IDC_RECORD,TRUE);
		SetDlgItemText(hwndMain,IDC_RECORD,"&Record");
	}
}

void take_user_fmt_settings(void) {
	assert(!win_is_recording());

	char tmp[512];

	tmp[0]=0;
	GetDlgItemText(hwndMain,IDC_SRC_BITS,tmp,sizeof(tmp));
	ui_want_bits = atoi(tmp);

	tmp[0]=0;
	GetDlgItemText(hwndMain,IDC_SRC_CHANNELS,tmp,sizeof(tmp));
	if (!strcmp(tmp,"Mono"))
		ui_want_channels = 1;
	else if (!strcmp(tmp,"Stereo"))
		ui_want_channels = 2;
	else
		ui_want_channels = atoi(tmp);

	tmp[0]=0;
	GetDlgItemText(hwndMain,IDC_SRC_RATE,tmp,sizeof(tmp));
	ui_want_rate = atoi(tmp);
}

void populate_sources(void) {
	HWND dlgitem = GetDlgItem(hwndMain,IDC_SOURCE);

	SendMessage(dlgitem,CB_RESETCONTENT,0,0);

        size_t i,idx=0;

	SendMessage(dlgitem,CB_ADDSTRING,0,(LPARAM)"(default)");
        for (i=0;audio_source_list[i].name != NULL;i++) {
		DWORD ret = (DWORD)SendMessage(dlgitem,CB_ADDSTRING,0,(LPARAM)audio_source_list[i].name);

		if (!ui_source.empty() && ui_source == audio_source_list[i].name)
			idx = ret;
        }

	SendMessage(dlgitem,CB_SETCURSEL,(WPARAM)idx,0);
}

std::vector<AudioDevicePair> devices_list;

void populate_devices(void) {
	HWND dlgitem = GetDlgItem(hwndMain,IDC_DEVICE);

	SendMessage(dlgitem,CB_RESETCONTENT,0,0);

	devices_list.clear();

	{
		AudioDevicePair p;

		p.desc = "(default)";
		devices_list.push_back(p);
	}

	size_t idx=0;

	SendMessage(dlgitem,CB_ADDSTRING,0,(LPARAM)"(default)");

        AudioSource* alsa = GetAudioSource(ui_source.c_str());
	if (alsa != NULL) {
		std::vector<AudioDevicePair> l;

		if (alsa->EnumDevices(l) >= 0) {
			for (auto i=l.begin();i != l.end();i++) {
				std::string fnl = (*i).desc;

				if (!((*i).name.empty())) {
					fnl += " - ";
					fnl += (*i).name;
				}

				DWORD ret = (DWORD)SendMessage(dlgitem,CB_ADDSTRING,0,(LPARAM)fnl.c_str());

				if (!ui_device.empty() && ui_device == (*i).name)
					idx = ret;

				devices_list.push_back(*i);
			}
		}
	}

	SendMessage(dlgitem,CB_SETCURSEL,(WPARAM)idx,0);
}

std::string WinGUI_CB_GetText(HWND dlgItem,WPARAM idx) {
	LRESULT len = SendMessage(dlgItem,CB_GETLBTEXTLEN,(WPARAM)idx,0);
	std::string str;

	if (len != CB_ERR && len > 0) {//LEN does not include the NUL character
		char *tmp = new char[len + 2];

		tmp[0]=0;
		tmp[len+1] = 'x';//overflow check

		SendMessage(dlgItem,CB_GETLBTEXT,(WPARAM)idx,(LPARAM)((LPCSTR)tmp));

		assert(tmp[len+1] == 'x');//overflow check
		tmp[len] = 0;

		str = tmp;

		delete[] tmp;
	}

	return str;
}

BOOL CALLBACK DlgMainProc(HWND hwndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam) {
	(void)lParam;

	if (uMsg == WM_INITDIALOG) {
		hwndMain = hwndDlg;

		SetTimer(hwndDlg,1,100,NULL);

		{
			LRESULT i;

			    SendDlgItemMessage(hwndDlg,IDC_SRC_BITS,CB_ADDSTRING,0,(LPARAM)("8-bit"));
			i = SendDlgItemMessage(hwndDlg,IDC_SRC_BITS,CB_ADDSTRING,0,(LPARAM)("16-bit"));
//			    SendDlgItemMessage(hwndDlg,IDC_SRC_BITS,CB_ADDSTRING,0,(LPARAM)("24-bit")); // allow when this code better supports it
		            SendDlgItemMessage(hwndDlg,IDC_SRC_BITS,CB_ADDSTRING,0,(LPARAM)("32-bit"));

			SendDlgItemMessage(hwndDlg,IDC_SRC_BITS,CB_SETCURSEL,(WPARAM)i,0);
		}

		{
			LRESULT i;

			    SendDlgItemMessage(hwndDlg,IDC_SRC_CHANNELS,CB_ADDSTRING,0,(LPARAM)("Mono"));
			i = SendDlgItemMessage(hwndDlg,IDC_SRC_CHANNELS,CB_ADDSTRING,0,(LPARAM)("Stereo"));
			// enable 3, 4, 5, 6, 7, and 8 channel capture later

			SendDlgItemMessage(hwndDlg,IDC_SRC_CHANNELS,CB_SETCURSEL,(WPARAM)i,0);
		}

		{
			LRESULT i;

			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("8000Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("11025Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("16000Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("22050Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("32000Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("44100Hz"));
			i = SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("48000Hz"));
			    SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_ADDSTRING,0,(LPARAM)("96000Hz"));

			SendDlgItemMessage(hwndDlg,IDC_SRC_RATE,CB_SETCURSEL,(WPARAM)i,0);
		}

		SetDlgItemText(hwndDlg,IDC_STATUS,"Ready");

		populate_sources();
		populate_devices();

		return TRUE;
	}
	else if (uMsg == WM_TIMER) {
		if (win_is_recording()) {
			if (win_thread_stopped()) {
				fprintf(stderr,"Audio capture thread stopped\n");
				win_stop_recording();
			}
		}
	}
	else if (uMsg == WM_COMMAND) {
		if (LOWORD(wParam) == IDCANCEL) {
			if (win_is_recording()) {
				if (MessageBox(hwndDlg,"Recording in progress. Exit anyway?","",MB_YESNO) != IDYES)
					return TRUE;
			}

			win_stop_recording();
			DestroyWindow(hwndDlg);
		}
		else if (LOWORD(wParam) == IDC_RECORD) {
			if (!win_is_recording()) {
				take_user_fmt_settings();
				win_start_recording();
			}
			else {
				win_stop_recording();
			}
		}
		else if (LOWORD(wParam) == IDC_SOURCE) {
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				// user changed source
				LRESULT idx = SendDlgItemMessage(hwndDlg,IDC_SOURCE,CB_GETCURSEL,0,0);
				if (idx != CB_ERR) {
					std::string str;

					if (idx > 0)//(default) is always index 0
						str = WinGUI_CB_GetText(GetDlgItem(hwndDlg,IDC_SOURCE),(WPARAM)idx);

					if (ui_source != str) {
						ui_source = str;
						populate_devices();
					}
				}
			}
		}
		else if (LOWORD(wParam) == IDC_DEVICE) {
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				// user changed source
				LRESULT idx = SendDlgItemMessage(hwndDlg,IDC_DEVICE,CB_GETCURSEL,0,0);
				if (idx != CB_ERR) {
					std::string str;

					if ((size_t)idx < devices_list.size())
						str = devices_list[(size_t)idx].name;

					if (ui_device != str) {
						ui_device = str;
					}
				}
			}
		}

		return TRUE;
	}
	else if (uMsg == WM_DESTROY) {
		win_stop_recording();
		PostQuitMessage(0);
		return TRUE;
	}

	return FALSE;
}

int CALLBACK WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow) {
	MSG msg;

#if defined(WIN32) // HACK help me figure out what is going on
	setbuf(stdout,NULL);
	setbuf(stderr,NULL);
#endif

	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	myInstance = hInstance;

	hwndMain = CreateDialogParam(myInstance,MAKEINTRESOURCE(IDD_MAIN),NULL,DlgMainProc,0);
	if (hwndMain == NULL) return 1;

	ShowWindow(hwndMain,nCmdShow);
	UpdateWindow(hwndMain);

	while (GetMessage(&msg,NULL,0,0)) {
		if (IsDialogMessage(hwndMain,&msg)) {
		}
		else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	win_stop_recording();
	return 0;
}
#endif

