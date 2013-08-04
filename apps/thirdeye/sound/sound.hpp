#ifndef SOUND_HPP
#define SOUND_HPP

#include "xmidi.hpp"

#include <map>
#include <vector>
#include <stdint.h>

#include <AL/al.h>
#include <AL/alc.h>

namespace MIXER {

class Mixer {
private:
	const ALCchar *defaultDeviceName;
	ALCdevice *device;
	ALCcontext *context;
	bool mt32;
	std::map<ALuint, std::vector<uint8_t> > buffer;
	std::map<ALuint, ALuint> source;
	void cleanup(std::map<ALuint, ALuint> deleteQueue);
	void play(std::vector<uint8_t> pcmData, ALuint size, ALuint format, ALuint sampleRate, ALuint bps);
public:
	Mixer();
	virtual ~Mixer();
	void update();
	void playMusic(std::vector<uint8_t> xmidi);
	void playSound(std::vector<uint8_t> snd);

	void list_audio_devices(const ALCchar *devices);
};

}
#endif //SOUND_HPP
