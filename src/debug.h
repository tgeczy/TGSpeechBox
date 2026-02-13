/*
TGSpeechBox — Debug logging macros.
Copyright 2014 NV Access Limited.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSPEECHBOX_DEBUG_H
#define TGSPEECHBOX_DEBUG_H

#include <iostream>

#define DEBUG(msg) cerr<<__FILE__<<" line "<<__LINE__<<" in "<<__FUNCTION__<<":"<<endl<<msg<<endl

#endif
