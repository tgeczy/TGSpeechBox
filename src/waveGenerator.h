/*
This file is a part of the NV Speech Player project. 
URL: https://bitbucket.org/nvaccess/speechplayer
Copyright 2014 NV Access Limited.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#ifndef SPEECHPLAYERWAVEGENERATOR_H
#define SPEECHPLAYERWAVEGENERATOR_H

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
