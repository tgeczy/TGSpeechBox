// The SAPI settings app's own code for loading and saving settings.ini,
// compiled into the tests so that "what the app writes when OK is pressed"
// is what the app writes and not a copy of it kept in a test (#205).  The
// app is one source file with its dialog; its entry point is renamed and
// never called, and the functions below stand in for the dialog: they do
// what it does between opening and OK, with the app's own functions.

#define wWinMain tgsbSapiSettingsWinMain
#include "../../tools/tgsbSapiSettings/settings_app.cpp"

namespace settings_app_test {

// Where the inflection slider stands when the dialog opens on iniPath.
int inflectionSliderOnOpen(const std::wstring& iniPath)
{
    return inflection_slider_pos(load_settings(iniPath).pitchInflectionScale);
}

// The dialog opened on iniPath and OK pressed, with the inflection slider
// moved to inflectionSlider first (-1: left where it stood) and nothing else
// touched.
bool pressOk(const std::wstring& iniPath, int inflectionSlider)
{
    Settings s = load_settings(iniPath);
    const int shown = inflection_slider_pos(s.pitchInflectionScale);
    s.pitchInflectionScale = inflection_from_slider(inflectionSlider < 0 ? shown : inflectionSlider,
                                                    s.pitchInflectionScale);
    return save_settings(iniPath, s);
}

}  // namespace settings_app_test
