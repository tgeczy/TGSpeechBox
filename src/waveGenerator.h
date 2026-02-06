/*
TGSpeechBox — Wave generator abstract interface.

Originally part of the NV Speech Player project by NV Access Limited (2014).
Extended 2025-2026 by Tamas Geczy.
Licensed under GNU General Public License version 2.0.
*/

#ifndef TGSPEECHBOX_WAVEGENERATOR_H
#define TGSPEECHBOX_WAVEGENERATOR_H

#include <list>
#include "sample.h"
#include "speechPlayer.h"
#include "lock.h"

class WaveGenerator {
	public:
	virtual unsigned int generate(const unsigned int bufSize, sample* buffer)=0;
	// Pure virtual, but still needs a definition.
	virtual ~WaveGenerator()=0;
};

// MSVC accepts `=0 {}` in-class, but GCC/Clang reject it.
// Keep the same ABI/intent while staying standard-compliant.
inline WaveGenerator::~WaveGenerator() {}

#endif
