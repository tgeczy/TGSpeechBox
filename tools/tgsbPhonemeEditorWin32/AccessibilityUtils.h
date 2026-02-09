#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>

#include <string>

// Set a stable accessible name for a SysListView32 so screen readers announce it well.
void installAccessibleNameForListView(HWND lv, const std::wstring& name);
