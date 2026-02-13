/*
TGSpeechBox — Phoneme editor Win32 entry point.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#define UNICODE
#define _UNICODE

#include "AppController.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  AppController app;
  if (!app.Initialize(hInstance, nCmdShow)) return 1;
  return app.RunMessageLoop();
}
