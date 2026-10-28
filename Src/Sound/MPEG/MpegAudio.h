#ifndef _MPEG_AUDIO_H_
#define _MPEG_AUDIO_H_

#include "Game.h"
#include <cstdint>


namespace MpegDec
{
  void  LoadCustomTracks(const std::string &music_filepath, const Game &game);
  void	SetMemory(const uint8_t *data, int offset, int length, bool loop);
	void	UpdateMemory(const uint8_t *data, int offset, int length, bool loop);
	int		GetPosition();
	void	SetPosition(int pos);
	// Active window as the decoder holds it, for the optional save-state block. The offset is
	// relative to the MPEG ROM and is -1 when the window belongs to a custom track (which is
	// keyed by file, not by ROM offset) or when nothing is loaded.
	int		GetActiveOffset(const uint8_t *romBase);
	int		GetActiveSize();
	bool	GetLoop();
	void	DecodeAudio(int16_t* left, int16_t* right, int numStereoSamples);
	void	Stop();
	bool	IsLoaded();
}

#endif

