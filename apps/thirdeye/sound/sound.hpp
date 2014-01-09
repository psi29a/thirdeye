#ifndef SOUND_HPP
#define SOUND_HPP

#include "xmidi.hpp"

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

#include "al.h"
#include "alc.h"

namespace MIXER {

struct position{
	ALfloat x;
	ALfloat y;
	ALfloat z;
};

struct Sources
{
	Sources(ALuint sourceId,
			ALuint bufferId,
			position pos,
			std::vector<uint8_t> buffers
			)
: sourceId(sourceId)
, bufferId(bufferId)
, pos(pos)
, buffer(0)

{}
ALuint sourceId;	// id of source
ALuint bufferId;	// id of buffer
position pos;		// position of source
std::vector<uint8_t> buffer; // buffers of source

Sources() {
	sourceId = 0;
	bufferId = 0;
	pos.x = 0.0f;
	pos.y = 0.0f;
	pos.z = 0.0f;
	buffer.clear();
}
};

class Mixer {
private:
	const ALCchar *defaultDeviceName;
	ALCdevice *device;
	ALCcontext *context;
	bool mt32;
	std::map<ALuint, Sources> mSources;
	std::vector<std::string> enumerate();
public:
	Mixer();
	virtual ~Mixer();
	void update();
	void playMusic(std::vector<uint8_t> xmidi);
	void stopMusic();
	void playSound(std::vector<uint8_t> snd);
};

}
#endif //SOUND_HPP
