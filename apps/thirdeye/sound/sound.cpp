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

void MIXER::Mixer::update(){
	std::map<ALuint, ALuint>::iterator iter;
	std::map<ALuint, ALuint> deleteQueue;
	//ALenum state = 0;
	/*
	for (iter = source.begin(); iter != source.end(); iter++){
		alGetSourcei(iter->second, AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING){
			std::cout << "We are done playing buffer " << iter->first << " with source " << iter->second << std::endl;
			deleteQueue[iter->first] = iter->second;
		}
	}

	if (deleteQueue.size() > 0){
		std::cout << "Time for cleanup.." << std::endl;
		cleanup(deleteQueue);
		std::cout << "Did we get here?" << std::endl;
	}
	*/
}

void MIXER::Mixer::cleanup(std::map<ALuint, ALuint> deleteQueue){
	std::map<ALuint, ALuint>::iterator iter;
	/*
	for (iter = deleteQueue.begin(); iter != deleteQueue.end(); iter++){
		std::cout << "Cleaning up source: " << iter->second << " and buffer: " << iter->first << std::endl;
		alDeleteSources(1,&iter->second);
		alDeleteBuffers(1,&iter->first);
		source.erase(iter->first);
		buffer.erase(iter->first);
		std::cout << "finished cleaning up this batch" << std::endl;
	}
	*/
	std::cout << "Finished everything." << std::endl;
}

void MIXER::Mixer::play(std::vector<uint8_t> pcmData, ALuint size, ALuint format, ALuint sampleRate, ALuint bps){

	ALuint bufferid;
    alGenBuffers(1, &bufferid);
    printf("bufferid: %d\n", bufferid);
    mSources[bufferid].buffer = pcmData;
    alBufferData(bufferid, format, &mSources[bufferid].buffer[0], size, sampleRate);

    ALuint sourceid;
    alGenSources(1, &sourceid);
    //source[bufferid] = sourceid;
    alSourcei(sourceid, AL_BUFFER, bufferid);
	alSourcePlay(sourceid);
    std::cout << "pcm size: " << size << std::endl;
/*
	alSource3f(sourceid,AL_POSITION,0,0,0);
	alSourcei(sourceid,AL_LOOPING,AL_FALSE);
	float f[]={1,0,0,0,1,0};
	alListenerfv(AL_ORIENTATION,f);
*/
}

void MIXER::Mixer::playMusic(std::vector<uint8_t> xmidi) {
	std::string config_file = WILDMIDI_CFG;
	uint32_t mixer_options = 0;
	uint8_t music_volume = 100;
	int errorNum = 0;

	// is there music loaded? If so, we stop and unload.
	std::cout << "Error: " << alGetError() << std::endl;

	if (mSources[1].bufferId > 0){
		ALenum state = 0;
		alGetSourcei(mSources[1].sourceId, AL_SOURCE_STATE, &state);
		std::cout << "Error: " << errorNum << std::endl;
		if (state == AL_PLAYING)
			alSourceStop(mSources[1].sourceId);
		alSourcei(mSources[1].sourceId, AL_BUFFER, 0);
		alDeleteSources(1, &mSources[1].sourceId );
		std::cout << "Error: " << alGetError() << std::endl;
		alDeleteBuffers(1, &mSources[1].bufferId);
		std::cout << "Error: " << alGetError() << std::endl;
		std::cout << "Deleted music buffer id: " << mSources[1].bufferId << std::endl;
		mSources[1].bufferId = 0;
	}
	std::cout << "Error: " << alGetError() << std::endl;
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

	mSources[1].buffer.resize(wm_info->approx_total_samples*4);

	//std::vector<uint8_t> pcmData(wm_info->approx_total_samples*4);

	std::cout << "approx samples: " << wm_info->approx_total_samples << std::endl;

	WildMidi_GetOutput(midi_ptr, reinterpret_cast<char*>(&mSources[1].buffer[0]), mSources[1].buffer.size());
	WildMidi_Close (midi_ptr);
	WildMidi_Shutdown();
	std::cout << "done converting xmi to midi" << std::endl;

	// play our new music
    alGenBuffers(1, &mSources[1].bufferId);
    alBufferData(mSources[1].bufferId, AL_FORMAT_STEREO16, &mSources[1].buffer[0], mSources[1].buffer.size(), MUSIC_RATE);
    std::cout << "Error: " << alGetError() << std::endl;

    alGenSources(1, &mSources[1].sourceId);
    alSourcei(mSources[1].sourceId, AL_BUFFER, mSources[1].bufferId);
    std::cout << "Error: " << alGetError() << std::endl;
	alSourcePlay(mSources[1].sourceId);

	std::cout << mSources[1].sourceId << " " << mSources[1].bufferId << " "
			<< mSources[1].buffer.size() << " "
			<< alGetError()
			<< std::endl;
	//play(mSources[1].buffer, mSources[1].buffer.size(), AL_FORMAT_STEREO16, MUSIC_RATE, 16);
}

void MIXER::Mixer::playSound(std::vector<uint8_t> snd) {
    play(snd, snd.size(), AL_FORMAT_MONO8, 22000, 8);
    std::cout << "Got here." << std::endl;
}

MIXER::Mixer::Mixer() {
	defaultDeviceName = "OpenAL Soft";
	mt32 = true;

	// setup our audio devices and contexts
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

	// setup our game music, this will always be available
	mSources[1] = Sources();
	mSources[1].name = "Game Music";
	//alGenSources(1, &mSources[1].sourceId);
	//std::cout << "Error: " << alGetError() << std::endl;
}

MIXER::Mixer::~Mixer() {
    alcDestroyContext(context);
    alcCloseDevice(device);
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
