/*
TGSpeechBox — Accessibility utilities interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>

#include <string>

// Set a stable accessible name for a SysListView32 so screen readers announce it well.
void installAccessibleNameForListView(HWND lv, const std::wstring& name);
