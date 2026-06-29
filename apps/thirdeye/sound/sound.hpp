#ifndef SOUND_HPP
#define SOUND_HPP

#include "xmidi.hpp"

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

#if defined(__has_include)
#  if __has_include(<AL/al.h>)
#    include <AL/al.h>
#    include <AL/alc.h>
#    if __has_include(<AL/alext.h>)
#      include <AL/alext.h>
#    endif
#  elif __has_include(<OpenAL/al.h>)
#    include <OpenAL/al.h>
#    include <OpenAL/alc.h>
#    if __has_include(<OpenAL/alext.h>)
#      include <OpenAL/alext.h>
#    endif
#  endif
#elif !defined(__APPLE__) || defined(HAVE_AL_AL_H)
#  include <AL/al.h>
#  include <AL/alc.h>
#  include <AL/alext.h>
#else
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#  include <OpenAL/alext.h>
#endif

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
	const ALCchar *defaultDeviceName = nullptr;
	ALCdevice *device = nullptr;
	ALCcontext *context = nullptr;
	bool mt32 = false;
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
