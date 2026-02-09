#define UNICODE
#define _UNICODE

#include "AppController.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  AppController app;
  if (!app.Initialize(hInstance, nCmdShow)) return 1;
  return app.RunMessageLoop();
}
