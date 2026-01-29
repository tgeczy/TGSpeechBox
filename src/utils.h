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

#ifndef SPEECHPLAYER_UTILS_H
#define SPEECHPLAYER_UTILS_H

#include <cmath>

// _isnan is MSVC-specific. For Linux/Android use std::isnan.
#if defined(_MSC_VER)
  #include <float.h>
  inline bool nvsp_isnan(double v) { return _isnan(v) != 0; }
#else
  inline bool nvsp_isnan(double v) { return std::isnan(v); }
#endif

inline double calculateValueAtFadePosition(double oldVal, double newVal, double curFadeRatio) {
	if(nvsp_isnan(newVal)) return oldVal;
	return oldVal + ((newVal - oldVal) * curFadeRatio);
}

#endif
