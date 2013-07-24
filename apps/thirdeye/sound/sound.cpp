#include "sound.hpp"

#include <boost/thread.hpp>


void MIXER::Mixer::playMusic(std::vector<uint8_t> xmidi) {
	MIXER::Music music;
	//boost::thread workerThread(music);
	music.play(xmidi);
}

/*
void MIXER::Music::operator()()
{
	std::cout << "testing" << std::endl;
	return
}
*/

MIXER::Music::Music() {
	mt32 = true;
	music = NULL;
}

MIXER::Music::~Music() {
	// Cleanup
}

void MIXER::Music::play(std::vector<uint8_t> xmidi) {
	std::string config_file = WILDMIDI_CFG;
	uint32_t mixer_options = 0;
	uint32_t output_result = 0;
	uint32_t spinpoint = 0;
	std::string spinner = "|/-\\";
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

	printf("Playing test midi: %ld\n", wm_info->approx_total_samples);

	uint32_t apr_mins, apr_secs, pro_mins, pro_secs, perc_play = 0;

	apr_mins = wm_info->approx_total_samples / (MUSIC_RATE * 60);
	apr_secs = (wm_info->approx_total_samples % (MUSIC_RATE * 60)) / MUSIC_RATE;

	if (open_music_output() == -1) {
		std::cerr << "Can't open ALSA!" << std::endl;
		return;
	}

	std::vector<uint8_t> output_buffer(16384);
	uint32_t count_diff = 0;

	while (true) {
		count_diff = wm_info->approx_total_samples - wm_info->current_sample;

		if (count_diff == 0)
			break;

		if (count_diff < 4096) {
			output_result = WildMidi_GetOutput(midi_ptr, reinterpret_cast<char*>(&output_buffer[0]), (count_diff * 4));
		} else {
			output_result = WildMidi_GetOutput(midi_ptr, reinterpret_cast<char*>(&output_buffer[0]), 4096);
		}

		if (output_result <= 0)
			break;

		wm_info = WildMidi_GetInfo(midi_ptr);
		perc_play = (wm_info->current_sample * 100)
				/ wm_info->approx_total_samples;
		pro_mins = wm_info->current_sample / (MUSIC_RATE * 60);
		pro_secs = (wm_info->current_sample % (MUSIC_RATE * 60)) / MUSIC_RATE;

		if (output_result > 0)
			send_music_output(reinterpret_cast<char*>(&output_buffer[0]), output_result);
		fprintf(stderr,
				"        [Approx %2dm %2ds Total] [%3i] [%2dm %2ds Processed] [%2u%%] %c  \r",
				apr_mins, apr_secs, music_volume, pro_mins, pro_secs,
				perc_play, spinner[spinpoint++ % 4]);
	}
	std::cout << std::endl;
	close_music_output();
	output_buffer.clear();
	WildMidi_Close (midi_ptr);
	WildMidi_Shutdown();
	return;
}

int MIXER::Music::send_music_output (char * output_data, int output_size) {
	int32_t err;
	snd_pcm_uframes_t frames;
	bool alsa_first_time = true;

	while (output_size > 0) {
		frames = snd_pcm_bytes_to_frames(music, output_size);
        if ((err = snd_pcm_writei(music, output_data, frames)) < 0) {\
			if (snd_pcm_state(music) == SND_PCM_STATE_XRUN) {
				if ((err = snd_pcm_prepare(music)) < 0)
					std::cerr << "snd_pcm_prepare() failed." << std::endl;;
				alsa_first_time = true;
				continue;
			}
			return err;
		}

		output_size -= snd_pcm_frames_to_bytes(music, err);
		output_data += snd_pcm_frames_to_bytes(music, err);

		if (alsa_first_time) {
			alsa_first_time = false;
			snd_pcm_start(music);
		}
	}
	return 0;
}

void MIXER::Music::close_music_output () {
	snd_pcm_close (music);
}

int MIXER::Music::open_music_output() {
	snd_pcm_hw_params_t     *hw;
	snd_pcm_sw_params_t     *sw;
	int err;
	unsigned int alsa_buffer_time;
	unsigned int alsa_period_time;
	std::string pcmname = "default";
	uint32_t music_rate = MUSIC_RATE;

	if ((err = snd_pcm_open (&music, pcmname.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		std::cerr << "Audio open error: " << snd_strerror (-err) << std::endl;
		return -1;
	}

	snd_pcm_hw_params_alloca (&hw);

	if ((err = snd_pcm_hw_params_any(music, hw)) < 0) {
		std::cerr << "No configuration available for playback: " << snd_strerror (-err) << std::endl;

		return -1;
	}

	if ((err = snd_pcm_hw_params_set_access(music, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		std::cerr << "Cannot set mmap'ed mode: " << snd_strerror (-err) << std::endl;
		return -1;
	}

	if (snd_pcm_hw_params_set_format (music, hw, SND_PCM_FORMAT_S16_LE) < 0) {
		std::cerr << "ALSA does not support 16bit signed audio for your soundcard" << std::endl;;
		close_music_output();
		return -1;
	}

	if (snd_pcm_hw_params_set_channels (music, hw, 2) < 0) {
		std::cerr << "ALSA does not support stereo for your soundcard" << std::endl;;
		close_music_output();
		return -1;
	}

	if (snd_pcm_hw_params_set_rate_near(music, hw, &music_rate, 0) < 0) {
		std::cerr << "ALSA does not support " << MUSIC_RATE << "Hz for your soundcard" << std::endl;
		close_music_output();
		return -1;
	}

	alsa_buffer_time = 500000;
	alsa_period_time = 50000;

	if ((err = snd_pcm_hw_params_set_buffer_time_near(music, hw, &alsa_buffer_time, 0)) < 0)
	{
		std::cerr << "Set buffer time failed: " << snd_strerror(-err) << std::endl;
		return -1;
	}

	if ((err = snd_pcm_hw_params_set_period_time_near(music, hw, &alsa_period_time, 0)) < 0)
	{
		std::cerr << "Set period time failed: " << snd_strerror(-err) << std::endl;
		return -1;
	}

	if (snd_pcm_hw_params(music, hw) < 0)
	{
		std::cerr << "Unable to install hw params" << std::endl;;
		return -1;
	}

	snd_pcm_sw_params_alloca(&sw);
	snd_pcm_sw_params_current(music, sw);
	if (snd_pcm_sw_params(music, sw) < 0)
	{
		std::cerr << "Unable to install sw params" << std::endl;;
		return -1;
	}
	return 0;
}
