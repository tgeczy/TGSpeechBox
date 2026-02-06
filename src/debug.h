/*
TGSpeechBox — Debug logging macros.

Originally part of the NV Speech Player project by NV Access Limited (2014).
Extended 2025-2026 by Tamas Geczy.
Licensed under GNU General Public License version 2.0.
*/

#ifndef TGSPEECHBOX_DEBUG_H
#define TGSPEECHBOX_DEBUG_H

#include <iostream>

#define DEBUG(msg) cerr<<__FILE__<<" line "<<__LINE__<<" in "<<__FUNCTION__<<":"<<endl<<msg<<endl

#endif
