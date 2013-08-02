#include "sound.hpp"

/* Define this to the location of the wildmidi config file */
#define WILDMIDI_CFG "/etc/wildmidi/wildmidi.cfg"
#define MUSIC_RATE 32072

#include <iostream>

extern  "C"{
#include <wildmidi_lib.h>
}

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <AL/al.h>
#include <AL/alc.h>

void MIXER::Mixer::playMusic(std::vector<uint8_t> xmidi) {
	std::string config_file = WILDMIDI_CFG;
	uint32_t mixer_options = 0;
	uint8_t music_volume = 100;

	DataSource *xmids = new BufferDataSource(reinterpret_cast<char*>(&xmidi[0]),
			xmidi.size());
	XMIDI *xmi = new XMIDI(xmids,
			mt32 ? XMIDI_CONVERT_MT32_TO_GS : XMIDI_CONVERT_NOCONVERSION);

	std::vector<uint8_t> midi(sizeof(uint8_t) * 4096);
	DataSource *xout = new BufferDataSource(reinterpret_cast<char*>(&midi[0]),
			midi.size());

	xmi->retrieve(0, xout);

	std::cout << "midi: " << midi.size() << " " << &midi[0] << " "
			<< xout->getPos() << std::endl;
	midi.resize(xout->getPos());

	std::cout << "midi: " << midi.size() << " " << &midi[0] << std::endl;

	if (WildMidi_Init(config_file.c_str(), MUSIC_RATE, mixer_options) == -1) {
		std::cerr << "Could not initialise WildMIDI." << std::endl;
		return;
	}
	WildMidi_MasterVolume(music_volume);
	void *midi_ptr = WildMidi_OpenBuffer(&midi[0], midi.size());
	struct _WM_Info * wm_info = WildMidi_GetInfo(midi_ptr);

	//std::vector<uint8_t> output_buffer(wm_info->approx_total_samples*4);
    unsigned int bufferid, format;
    alGenBuffers(1,&bufferid);
    printf("bufferid: %d\n", bufferid);

    buffer[bufferid].resize(wm_info->approx_total_samples*4);

	std::cout << "approx samples: " << wm_info->approx_total_samples << std::endl
			<< "current_sample: " << wm_info->current_sample << std::endl;

	WildMidi_GetOutput(midi_ptr, reinterpret_cast<char*>(&buffer[bufferid][0]), wm_info->approx_total_samples*4);
	WildMidi_Close (midi_ptr);
	WildMidi_Shutdown();

    int channel = 2;
    int sampleRate = MUSIC_RATE;
    int bps = 16;
    int size = wm_info->approx_total_samples*4;

    if(channel==1)
    {
            if(bps==8)
            {
                    format=AL_FORMAT_MONO8;
            }else{
                    format=AL_FORMAT_MONO16;
            }
    }else{
            if(bps==8)
            {
                    format=AL_FORMAT_STEREO8;
            }else{
                    format=AL_FORMAT_STEREO16;
            }
    }

    alBufferData(bufferid, format, (char*) &buffer[bufferid][0], size, sampleRate);

    unsigned int sourceid;
    alGenSources(1,&sourceid);

    printf("sourceid: %d\n", sourceid);
    alSourcei(sourceid, AL_BUFFER, bufferid);

	float x=0,z=0;

	alSourcePlay(sourceid);

    std::cout << "size: " << size << std::endl;

	alSource3f(sourceid,AL_POSITION,x,0,z);
	//alSourcei(sourceid,AL_LOOPING,AL_TRUE);
	alSourcei(sourceid,AL_LOOPING,AL_FALSE);
	float f[]={1,0,0,0,1,0};
	alListenerfv(AL_ORIENTATION,f);


	std::cout << "done" << std::endl;
	//output_buffer.clear();

    //alDeleteSources(1,&sourceid);
    //alDeleteBuffers(1,&bufferid);

    //alcDestroyContext(context);
    //alcCloseDevice(device);
    //delete[] data;

	return;
}

void MIXER::Mixer::playSound(std::vector<uint8_t> snd) {
    int channel = 1;
    int sampleRate = 22000;
    int bps = 8;
    int size = snd.size();

    //char* data=loadWAV("test.wav",channel,sampleRate,bps,size);
    char* data = (char*) &snd[0];

    ALCdevice* device=alcOpenDevice(NULL);
    if(device==NULL)
    {
            std::cout << "cannot open sound card" << std::endl;
            return;
    }
    ALCcontext* context=alcCreateContext(device,NULL);
    if(context==NULL)
    {
            std::cout << "cannot open context" << std::endl;
            return;
    }
    alcMakeContextCurrent(context);

    unsigned int bufferid,format;
    alGenBuffers(1,&bufferid);
    if(channel==1)
    {
            if(bps==8)
            {
                    format=AL_FORMAT_MONO8;
            }else{
                    format=AL_FORMAT_MONO16;
            }
    }else{
            if(bps==8)
            {
                    format=AL_FORMAT_STEREO8;
            }else{
                    format=AL_FORMAT_STEREO16;
            }
    }
    alBufferData(bufferid,format,data,size,sampleRate);
    unsigned int sourceid;
    alGenSources(1,&sourceid);
    alSourcei(sourceid,AL_BUFFER,bufferid);
	float x=0,z=0;
	alSourcePlay(sourceid);

    std::cout << "size: " << size << std::endl;

	alSource3f(sourceid,AL_POSITION,x,0,z);
	//alSourcei(sourceid,AL_LOOPING,AL_TRUE);
	alSourcei(sourceid,AL_LOOPING,AL_FALSE);
	float f[]={1,0,0,0,1,0};
	alListenerfv(AL_ORIENTATION,f);

    std::cout << "Got here." << std::endl;

    //usleep(10000000000);

    //alDeleteSources(1,&sourceid);
    //alDeleteBuffers(1,&bufferid);

    //alcDestroyContext(context);
    //alcCloseDevice(device);
    //delete[] data;
}

/*
void MIXER::Music::operator()()
{
	std::cout << "testing" << std::endl;
	return
}
*/

MIXER::Mixer::Mixer() {
	defaultDeviceName = "OpenAL Soft";
	mt32 = true;

	list_audio_devices(alcGetString(NULL, ALC_DEVICE_SPECIFIER));

	device = alcOpenDevice(defaultDeviceName);
	if (!device) {
		fprintf(stderr, "unable to open default device\n");
		return;
	}

	fprintf(stdout, "Audio Device: %s\n", alcGetString(device, ALC_DEVICE_SPECIFIER));

	alGetError();

	context = alcCreateContext(device, NULL);
	if (!alcMakeContextCurrent(context)) {
		fprintf(stderr, "failed to make default context\n");
		return;
	}
}

MIXER::Mixer::~Mixer() {
	// Cleanup
}

void MIXER::Mixer::list_audio_devices(const ALCchar *devices)
{
	const ALCchar *device = devices, *next = devices + 1;
	size_t len = 0;

	fprintf(stdout, "Available audio devices:\n");
	fprintf(stdout, "----------\n");
	while (device && *device != '\0' && next && *next != '\0') {
		fprintf(stdout, "%s\n", device);
		len = strlen(device);
		device += (len + 1);
		next += (len + 2);
	}
	fprintf(stdout, "----------\n");
}
