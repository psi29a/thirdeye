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
	std::map<uint8_t, std::vector<uint8_t> > buffer;
public:
	Mixer();
	virtual ~Mixer();
	void playMusic(std::vector<uint8_t> xmidi);
	void playSound(std::vector<uint8_t> snd);

	void list_audio_devices(const ALCchar *devices);
};

}
#endif //SOUND_HPP
