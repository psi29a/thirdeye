#ifndef SOUND_HPP
#define SOUND_HPP

#include "xmidi.hpp"

#include <vector>
#include <stdint.h>
#include <alsa/asoundlib.h>

namespace MIXER {

class Mixer {
public:
	//Mixer();
	//virtual ~Mixer();
	void playMusic(std::vector<uint8_t> xmidi);
	void playSound(std::vector<uint8_t> snd);
};

class Music {
private:
	bool mt32;
	snd_pcm_t  *music;

	int send_music_output (char * output_data, int output_size);
	void close_music_output ();
	int open_music_output();

public:
	Music();
	virtual ~Music();
	void play(std::vector<uint8_t> xmidi);
	//void playSound(std::vector<uint8_t> snd);
	//void operator()();
};

}
#endif //SOUND_HPP
