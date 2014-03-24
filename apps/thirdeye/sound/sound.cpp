#include "sound.hpp"

/* Define this to the location of the wildmidi config file */
#define WILDMIDI_CFG "/etc/wildmidi/wildmidi.cfg"
#define MUSIC_RATE 	32072
#define SOUND_RATE 	8000
#define MAX_SOURCES 16
#define MUSIC_ID	0

#include <iostream>
#include <vector>

extern "C" {
#include <wildmidi_lib.h>
}

#include <al.h>
#include <alc.h>

void MIXER::Mixer::update() {
	ALenum state = 0;
	for (std::map<ALuint, Sources>::iterator iter = mSources.begin();
			iter != mSources.end(); iter++) {
		alGetSourcei(iter->second.sourceId, AL_SOURCE_STATE, &state);
		if (state != AL_PLAYING && iter->second.bufferId > 0) {
			std::cout << "We are done playing buffer " << iter->second.bufferId
					<< " with source " << iter->second.sourceId << std::endl;
			alSourceStop(iter->second.sourceId);			// stop playing
			alSourcei(iter->second.sourceId, AL_BUFFER, 0); // unload buffer from source
			alDeleteBuffers(1, &iter->second.bufferId);		// delete buffer
			mSources[iter->first].buffer.clear();			// purge data
			mSources[iter->first].bufferId = 0;				// reset bufferId
			//std::cout << "Error: " << alGetError() << std::endl;
		}
	}
}

// is there music loaded? If so, we stop and unload.
void MIXER::Mixer::stopMusic() {
	alSourceStop(mSources[MUSIC_ID].sourceId);				// stop music
	alSourcei(mSources[MUSIC_ID].sourceId, AL_BUFFER, 0); // unload buffer from source
	alDeleteBuffers(1, &mSources[MUSIC_ID].bufferId);	// delete buffer itself
	mSources[MUSIC_ID].buffer.clear();			// purge data
	mSources[MUSIC_ID].bufferId = 0;
	//std::cout << "Error: " << alGetError() << std::endl;
}

void MIXER::Mixer::playMusic(std::vector<uint8_t> xmidi) {
	std::string config_file = WILDMIDI_CFG;
	uint32_t mixer_options = 0;
	uint8_t music_volume = 100;

	stopMusic(); // stop any currently playing music

	DataSource *xmids = new BufferDataSource(reinterpret_cast<char*>(&xmidi[0]),
			xmidi.size());
	XMIDI *xmi = new XMIDI(xmids,
			mt32 ? XMIDI_CONVERT_MT32_TO_GS : XMIDI_CONVERT_NOCONVERSION);

	uint32_t midi_size = xmi->retrieve(0, NULL);
	std::vector<uint8_t> midi(midi_size); // buffer needs to be big enough
	DataSource *xout = new BufferDataSource(reinterpret_cast<char*>(&midi[0]),
			midi.size());

	xmi->retrieve(0, xout);

	if (WildMidi_Init(config_file.c_str(), MUSIC_RATE, mixer_options) == -1) {
		std::cerr << "Could not initialise WildMIDI." << std::endl;
		return;
	}
	WildMidi_MasterVolume(music_volume);
	void *midi_ptr = WildMidi_OpenBuffer(&midi[0], midi.size());
	struct _WM_Info * wm_info = WildMidi_GetInfo(midi_ptr);

	mSources[MUSIC_ID].buffer.resize(wm_info->approx_total_samples * 4);

	WildMidi_GetOutput(midi_ptr,
			reinterpret_cast<char*>(&mSources[MUSIC_ID].buffer[0]),
			mSources[MUSIC_ID].buffer.size());
	WildMidi_Close(midi_ptr);
	WildMidi_Shutdown();
	//std::cout << "done converting xmi to midi" << std::endl;

	// play our new music
	alGenBuffers(1, &mSources[MUSIC_ID].bufferId);
	alBufferData(mSources[MUSIC_ID].bufferId, AL_FORMAT_STEREO16,
			&mSources[MUSIC_ID].buffer[0], mSources[MUSIC_ID].buffer.size(),
			MUSIC_RATE);
	alSourcei(mSources[MUSIC_ID].sourceId, AL_BUFFER,
			mSources[MUSIC_ID].bufferId);
	alSourcePlay(mSources[MUSIC_ID].sourceId);

	/*
	 std::cout << mSources[MUSIC_ID].sourceId << " " << mSources[MUSIC_ID].bufferId << " "
	 << mSources[MUSIC_ID].buffer.size() << " "
	 << alGetError()
	 << std::endl;
	 */
}

void MIXER::Mixer::playSound(std::vector<uint8_t> snd) {
	uint8_t mSourceId = 0;
	// find first unused source
	for (std::map<ALuint, Sources>::iterator iter = mSources.begin();
			iter != mSources.end(); iter++) {
		if (iter->first > 0 && iter->second.bufferId == 0) {
			mSourceId = iter->first;
			//std::cout << "Free source found: " << iter->second.sourceId << std::endl;
			break;
		}
	}

	if (mSourceId == 0) {
		//std::cout << "Couldn't play sound, no more available sources." << std::endl;
		return;
	}

	mSources[mSourceId].buffer = snd;
	alGenBuffers(1, &mSources[mSourceId].bufferId);
	alBufferData(mSources[mSourceId].bufferId, AL_FORMAT_MONO8,
			&mSources[mSourceId].buffer[0], mSources[mSourceId].buffer.size(),
			SOUND_RATE);
	alSourcei(mSources[mSourceId].sourceId, AL_BUFFER,
			mSources[mSourceId].bufferId);
	alSourcePlay(mSources[mSourceId].sourceId);

	/*
	 std::cout << mSources[mSourceId].sourceId << " " << mSources[mSourceId].bufferId << " "
	 << mSources[mSourceId].buffer.size() << " "
	 << alGetError()
	 << std::endl;
	 */
}

MIXER::Mixer::Mixer() {
	std::vector<std::string> devices = enumerate();
	defaultDeviceName = devices[0].c_str();
	mt32 = true;

	// setup our audio devices and contexts
	device = alcOpenDevice(defaultDeviceName);
	if (!device) {
		std::cerr << "OpenAL: Unable to open default device." << std::endl;
		return;
	}

	context = alcCreateContext(device, NULL);
	if (!alcMakeContextCurrent(context)) {
		std::cerr << "OpenAL: Failed to create the default context."
				<< std::endl;
		return;
	}

	// setup our sources
	for (uint8_t i = 0; i < MAX_SOURCES; i++) {
		mSources[i] = Sources();
		alGenSources(1, &mSources[i].sourceId);
		//std::cout << "SourceId: " << mSources[i].sourceId << std::endl;
	}
	//std::cout << "Error: " << alGetError() << std::endl;
}

MIXER::Mixer::~Mixer() {
	std::map<ALuint, Sources>::iterator iter;
	for (iter = mSources.begin(); iter != mSources.end(); iter++) {
		//std::cout << "  Cleaning up source: " << iter->second.sourceId << " and buffer: " << iter->second.bufferId << std::endl;
		alSourceStop(iter->second.sourceId);			// stop playing
		alSourcei(iter->second.sourceId, AL_BUFFER, 0); // unload buffer from source
		alDeleteBuffers(1, &iter->second.bufferId);
		alDeleteSources(1, &iter->second.sourceId);
	}

	alcDestroyContext(context);
	alcCloseDevice(device);
}

//
// An OpenAL output device
//
std::vector<std::string> MIXER::Mixer::enumerate()
{
    std::vector<std::string> devlist;
    const ALCchar *devnames;

	std::cout << "Initializing Sound:" << std::endl
			<< "  Available audio devices:" << std::endl;

    if(alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        devnames = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    else
        devnames = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    while(devnames && *devnames)
    {
    	std::cout << "    * " << devnames << std::endl;
        devlist.push_back(devnames);
        devnames += strlen(devnames)+1;
    }
    return devlist;
}
