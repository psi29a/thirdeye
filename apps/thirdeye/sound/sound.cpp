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

// The name of the device an open ALCdevice is playing on (OpenMW's
// getDeviceName): full name under ALC_ENUMERATE_ALL_EXT, error-checked
// fallback to the basic specifier.
static const ALCchar *deviceName(ALCdevice *dev) {
	const ALCchar *name = nullptr;
	if (alcIsExtensionPresent(dev, "ALC_ENUMERATE_ALL_EXT"))
		name = alcGetString(dev, ALC_ALL_DEVICES_SPECIFIER);
	if (alcGetError(dev) != AL_NO_ERROR || name == nullptr)
		name = alcGetString(dev, ALC_DEVICE_SPECIFIER);
	return name != nullptr ? name : "";
}

// The CURRENT system default output device's name ("" if unqueryable).
static std::string systemDefaultDevice() {
	const ALCchar *n = nullptr;
	if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
		n = alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
	if (n == nullptr || *n == '\0')
		n = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
	return n != nullptr ? n : "";
}

void MIXER::Mixer::followDefaultDevice() {
#ifdef ALC_SOFT_reopen_device
	if (mReopenFn == nullptr || device == nullptr)
		return;
	auto now = std::chrono::steady_clock::now();
	if (now - mLastDevCheck < std::chrono::seconds(2))
		return;
	mLastDevCheck = now;
	std::string def = systemDefaultDevice();
	if (def.empty() || def == mOpenedName)
		return;
	auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(mReopenFn);
	if (reopen(device, def.c_str(), nullptr)) {
		mOpenedName = def;
		std::cout << "  [audio: default output changed, now using: " << def
				<< "]" << std::endl;
	}
#endif
}

void MIXER::Mixer::update() {
	followDefaultDevice();
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
			static_cast<unsigned int>(xmidi.size()));
	XMIDI *xmi = new XMIDI(xmids,
			mt32 ? XMIDI_CONVERT_MT32_TO_GS : XMIDI_CONVERT_NOCONVERSION);

	uint32_t midi_size = xmi->retrieve(0, NULL);
	std::vector<uint8_t> midi(midi_size); // buffer needs to be big enough
	DataSource *xout = new BufferDataSource(reinterpret_cast<char*>(&midi[0]),
			static_cast<unsigned int>(midi.size()));

	xmi->retrieve(0, xout);

	if (WildMidi_Init(config_file.c_str(), MUSIC_RATE, mixer_options) == -1) {
		std::cerr << "Could not initialise WildMIDI." << std::endl;
		return;
	}
	WildMidi_MasterVolume(music_volume);
	void *midi_ptr = WildMidi_OpenBuffer(&midi[0], static_cast<uint32_t>(midi.size()));
	struct _WM_Info * wm_info = WildMidi_GetInfo(midi_ptr);

	mSources[MUSIC_ID].buffer.resize(wm_info->approx_total_samples * 4);

	WildMidi_GetOutput(midi_ptr,
			reinterpret_cast<int8_t*>(&mSources[MUSIC_ID].buffer[0]),
			static_cast<uint32_t>(mSources[MUSIC_ID].buffer.size()));
	WildMidi_Close(midi_ptr);
	WildMidi_Shutdown();
	//std::cout << "done converting xmi to midi" << std::endl;

	// play our new music
	alGenBuffers(1, &mSources[MUSIC_ID].bufferId);
	alBufferData(mSources[MUSIC_ID].bufferId, AL_FORMAT_STEREO16,
			&mSources[MUSIC_ID].buffer[0],
			static_cast<ALsizei>(mSources[MUSIC_ID].buffer.size()),
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
	ALuint mSourceId = 0;
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

	auto &source = mSources.at(mSourceId);
	source.buffer = std::move(snd);
	alGenBuffers(1, &source.bufferId);
	alBufferData(source.bufferId, AL_FORMAT_MONO8,
			source.buffer.data(),
			static_cast<ALsizei>(source.buffer.size()),
			SOUND_RATE);
	alSourcei(source.sourceId, AL_BUFFER, source.bufferId);
	alSourcePlay(source.sourceId);

	/*
	 std::cout << mSources[mSourceId].sourceId << " " << mSources[mSourceId].bufferId << " "
	 << mSources[mSourceId].buffer.size() << " "
	 << alGetError()
	 << std::endl;
	 */
}

MIXER::Mixer::Mixer() {
	enumerate(); // informational listing only
	mt32 = true;

	// Open the DEFAULT output device (NULL), never the first enumerated one:
	// enumeration order is arbitrary. On a Linux/pipewire box the GPU's HDMI
	// audio enumerates first, so hardcoding devices[0] silently sent every
	// sound to the monitor; NULL lets openal-soft route to the sink the user
	// actually has selected. (macOS masked this -- there the default device
	// happened to enumerate first.)
	device = alcOpenDevice(nullptr);
	if (!device) {
		std::cerr << "OpenAL: Unable to open default device." << std::endl;
		return;
	}
	mOpenedName = deviceName(device);
	std::cout << "  Using: " << mOpenedName << std::endl;

	context = alcCreateContext(device, NULL);
	if (!alcMakeContextCurrent(context)) {
		std::cerr << "OpenAL: Failed to create the default context."
				<< std::endl;
		return;
	}

#ifdef ALC_SOFT_reopen_device
	// Migration point for followDefaultDevice(); absent on non-openal-soft
	// implementations, in which case we simply stay on the boot-time device.
	if (alcIsExtensionPresent(device, "ALC_SOFT_reopen_device"))
		mReopenFn = reinterpret_cast<void *>(
				alcGetProcAddress(device, "alcReopenDeviceSOFT"));
#endif

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
