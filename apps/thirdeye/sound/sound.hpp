#ifndef SOUND_HPP
#define SOUND_HPP

/* Define this to the location of the wildmidi config file */
#define WILDMIDI_CFG "/etc/wildmidi/wildmidi.cfg"
#define MUSIC_RATE 32072

extern  "C"{
#include <alsa/asoundlib.h>
#include <wildmidi_lib.h>
}

#include "xmidi.hpp"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>
#include <iostream>
#include <stdint.h>

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
