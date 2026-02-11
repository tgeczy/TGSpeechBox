#pragma once

// -----------------
// Menus
// -----------------
#define IDR_MAINMENU 101
#define IDR_ACCEL    102

// -----------------
// Dialogs
// -----------------
#define IDD_ADD_MAPPING   201
#define IDD_CLONE_PHONEME 202
#define IDD_EDIT_PHONEME  203
#define IDD_EDIT_VALUE    204
#define IDD_EDIT_SETTINGS 205
#define IDD_EDIT_SETTING  206
#define IDD_SPEECH_SETTINGS 207
#define IDD_PHONEMIZER_SETTINGS 208

// -----------------
// Menu commands
// -----------------
#define IDM_FILE_OPEN_PACKROOT    40001
#define IDM_FILE_SAVE_LANGUAGE    40002
#define IDM_FILE_SAVE_PHONEMES    40003
#define IDM_FILE_RELOAD_LANGUAGE  40005
#define IDM_FILE_RELOAD_PHONEMES  40006
#define IDM_FILE_EXIT             40004

#define IDM_SETTINGS_ESPEAK_DIR   40101
#define IDM_SETTINGS_DLL_DIR      40102
#define IDM_SETTINGS_SPEECH_SETTINGS 40103
#define IDM_SETTINGS_PHONEMIZER 40104

#define IDM_HELP_ABOUT            40201

// -----------------
// Main window controls
// -----------------
#define IDC_EDIT_FILTER           1001
#define IDC_LIST_PHONEMES         1002
#define IDC_BTN_PLAY_PHONEME      1003
#define IDC_BTN_CLONE_PHONEME     1004
#define IDC_BTN_EDIT_PHONEME      1005
#define IDC_BTN_ADD_TO_LANGUAGE   1006

#define IDC_COMBO_LANGUAGE        1101
#define IDC_LIST_LANG_PHONEMES    1102
#define IDC_LIST_MAPPINGS         1103
#define IDC_BTN_ADD_MAPPING       1104
#define IDC_BTN_EDIT_MAPPING      1105
#define IDC_BTN_REMOVE_MAPPING    1106
#define IDC_BTN_LANG_EDIT_PHONEME 1107
#define IDC_BTN_LANG_PLAY_PHONEME 1108
#define IDC_BTN_LANG_SETTINGS    1109

#define IDC_EDIT_TEXT             1201
#define IDC_CHK_INPUT_IS_IPA      1202
#define IDC_BTN_CONVERT_IPA       1203
#define IDC_BTN_SPEAK             1204
#define IDC_BTN_SAVE_WAV          1205
#define IDC_EDIT_IPA              1206

// -----------------
// Dialog controls: Add mapping
// -----------------
#define IDC_MAP_FROM              2101
#define IDC_MAP_TO                2102
#define IDC_MAP_WORDSTART         2103
#define IDC_MAP_WORDEND           2104
#define IDC_MAP_BEFORECLASS       2105
#define IDC_MAP_AFTERCLASS        2106
#define IDC_MAP_NOTBEFORECLASS    2107
#define IDC_MAP_NOTAFTERCLASS     2108

// -----------------
// Dialog controls: Clone phoneme
// -----------------
#define IDC_CLONE_NEWKEY          2201
#define IDC_CLONE_FROM            2202

// -----------------
// Dialog controls: Edit phoneme
// -----------------
#define IDC_PHONEME_KEY_LABEL     2303
#define IDC_PHONEME_FIELDS        2301
#define IDC_PHONEME_EDIT_VALUE    2302

// -----------------
// Dialog controls: Edit value
// -----------------
#define IDC_VAL_FIELD             2401
#define IDC_VAL_VALUE             2402
#define IDC_VAL_LIVE_PREVIEW      2403

// -----------------
// Dialog controls: Edit settings
// -----------------
#define IDC_SETTINGS_LIST         2501
#define IDC_SETTINGS_ADD          2502
#define IDC_SETTINGS_EDIT         2503
#define IDC_SETTINGS_REMOVE       2504

// -----------------
// Dialog controls: Edit setting
// -----------------
#define IDC_SETTING_KEY           2601
#define IDC_SETTING_VALUE         2602

// -----------------
// Dialog controls: Speech settings
// -----------------
#define IDC_SPEECH_VOICE              2701
#define IDC_SPEECH_RATE_SLIDER        2702
#define IDC_SPEECH_RATE_VAL           2703
#define IDC_SPEECH_PITCH_SLIDER       2704
#define IDC_SPEECH_PITCH_VAL          2705
#define IDC_SPEECH_VOLUME_SLIDER      2706
#define IDC_SPEECH_VOLUME_VAL         2707
#define IDC_SPEECH_INFLECTION_SLIDER  2708
#define IDC_SPEECH_INFLECTION_VAL     2709
#define IDC_SPEECH_PARAM_LIST         2710
#define IDC_SPEECH_PARAM_SLIDER       2711
#define IDC_SPEECH_PARAM_VAL          2712
#define IDC_SPEECH_PARAM_RESET        2713

#define IDC_SPEECH_VOICING_LIST       2720
#define IDC_SPEECH_VOICING_SLIDER     2721
#define IDC_SPEECH_VOICING_VAL        2722
#define IDC_SPEECH_VOICING_RESET      2723
#define IDC_SPEECH_VOICING_RESET_ALL  2724
#define IDC_SPEECH_RESET_ALL          2714

// FrameEx voice quality controls
#define IDC_SPEECH_FRAMEEX_LIST       2730
#define IDC_SPEECH_FRAMEEX_SLIDER     2731
#define IDC_SPEECH_FRAMEEX_VAL        2732
#define IDC_SPEECH_FRAMEEX_RESET      2733
#define IDC_SPEECH_FRAMEEX_RESET_ALL  2734

// Save to profile button
#define IDC_SPEECH_SAVE_TO_PROFILE    2740


// -----------------
// Dialog controls: Phonemizer settings
// -----------------
#define IDC_PHONEMIZER_TEMPLATE      2801
#define IDC_PHONEMIZER_EXE           2802
#define IDC_PHONEMIZER_BROWSE        2803
#define IDC_PHONEMIZER_MODE          2804
#define IDC_PHONEMIZER_ARGS_STDIN    2805
#define IDC_PHONEMIZER_ARGS_CLI      2806
#define IDC_PHONEMIZER_MAXCHUNK      2807

// -----------------
// Dialog: Voice profile list (main editor)
// -----------------
#define IDD_VOICE_PROFILES           209
#define IDC_VP_LIST                  2901
#define IDC_VP_ADD                   2902
#define IDC_VP_EDIT                  2903
#define IDC_VP_DELETE                2904
#define IDC_VP_DUPLICATE             2905

// -----------------
// Dialog: Edit voice profile
// -----------------
#define IDD_EDIT_VOICE_PROFILE       210
#define IDC_EVP_NAME                 3001
#define IDC_EVP_CLASS_COMBO          3002
#define IDC_EVP_CLASS_REMOVE         3004
#define IDC_EVP_SCALES_LIST          3005
#define IDC_EVP_SCALE_FIELD          3006
#define IDC_EVP_SCALE_VALUE          3007
#define IDC_EVP_SCALE_SET            3008
#define IDC_EVP_SCALE_REMOVE         3009
#define IDC_EVP_OVERRIDES_LIST       3010
#define IDC_EVP_OVERRIDE_PHONEME     3011
#define IDC_EVP_OVERRIDE_ADD         3012
#define IDC_EVP_OVERRIDE_EDIT        3013
#define IDC_EVP_OVERRIDE_REMOVE      3014

// -----------------
// Dialog: Edit phoneme override (for voice profile)
// -----------------
#define IDD_EDIT_PHONEME_OVERRIDE    211
#define IDC_EPO_PHONEME              3101
#define IDC_EPO_FIELDS_LIST          3102
#define IDC_EPO_FIELD_COMBO          3103
#define IDC_EPO_FIELD_VALUE          3104
#define IDC_EPO_FIELD_SET            3105
#define IDC_EPO_FIELD_REMOVE         3106

// -----------------
// Menu command: Edit Voices
// -----------------
#define IDM_SETTINGS_EDIT_VOICES     40105

// -----------------
// Dialog: Class Editor (for normalization classes)
// -----------------
#define IDD_CLASS_EDITOR             212
#define IDC_CE_CLASS_LIST            3201
#define IDC_CE_CLASS_ADD             3202
#define IDC_CE_CLASS_REMOVE          3203
#define IDC_CE_MEMBERS_LABEL         3204
#define IDC_CE_MEMBERS_EDIT          3205

// -----------------
// Add Mapping dialog: Edit Classes button
// -----------------
#define IDC_MAP_EDIT_CLASSES         2109

// -----------------
// Menu commands: Rule editors
// -----------------
#define IDM_SETTINGS_EDIT_ALLOPHONES       40106
#define IDM_SETTINGS_EDIT_SPECIAL_COARTIC  40107

// -----------------
// Dialog: Allophone rules list
// -----------------
#define IDD_ALLOPHONE_RULES_LIST    214
#define IDC_AR_LIST          3401
#define IDC_AR_ADD           3402
#define IDC_AR_EDIT          3403
#define IDC_AR_REMOVE        3404
#define IDC_AR_MOVEUP        3405
#define IDC_AR_MOVEDOWN      3406

// -----------------
// Dialog: Allophone rule edit
// -----------------
#define IDD_ALLOPHONE_RULE_EDIT     215
#define IDC_AR_NAME          3410
#define IDC_AR_PHONEMES      3411
#define IDC_AR_FLAGS         3412
#define IDC_AR_NOTFLAGS      3413
#define IDC_AR_TOKENTYPE     3414
#define IDC_AR_POSITION      3415
#define IDC_AR_STRESS        3416
#define IDC_AR_AFTER         3417
#define IDC_AR_BEFORE        3418
#define IDC_AR_ACTION        3419
// Neighbor flag filter controls
#define IDC_AR_AFTERFLAGS        3470
#define IDC_AR_NOTAFTERFLAGS     3471
#define IDC_AR_BEFOREFLAGS       3472
#define IDC_AR_NOTBEFOREFLAGS    3473
// Replace action controls
#define IDC_AR_REPLACE_TO         3420
#define IDC_AR_REPLACE_DURMS      3421
#define IDC_AR_REPLACE_RMCLOSURE  3422
#define IDC_AR_REPLACE_RMASP      3423
#define IDC_AR_REPLACE_CLOSCALE   3424
#define IDC_AR_REPLACE_ASPSCALE   3425
// Scale action controls
#define IDC_AR_SCALE_DUR          3430
#define IDC_AR_SCALE_FADE         3431
#define IDC_AR_SCALE_FIELDS       3432
// Shift action controls
#define IDC_AR_SHIFT_FIELDS       3440
// Insert action controls
#define IDC_AR_INSERT_PHONEME     3450
#define IDC_AR_INSERT_DURMS       3451
#define IDC_AR_INSERT_FADEMS      3452
#define IDC_AR_INSERT_CONTEXTS    3453
// Groupboxes for action sections
#define IDC_AR_GRP_REPLACE   3460
#define IDC_AR_GRP_SCALE     3461
#define IDC_AR_GRP_SHIFT     3462
#define IDC_AR_GRP_INSERT    3463

// -----------------
// Dialog: Special coarticulation rules list
// -----------------
#define IDD_SPECIAL_COARTIC_LIST    216
#define IDC_SC_LIST          3501
#define IDC_SC_ADD           3502
#define IDC_SC_EDIT          3503
#define IDC_SC_REMOVE        3504
#define IDC_SC_MOVEUP        3505
#define IDC_SC_MOVEDOWN      3506

// -----------------
// Dialog: Special coarticulation rule edit
// -----------------
#define IDD_SPECIAL_COARTIC_EDIT    217
#define IDC_SC_NAME          3510
#define IDC_SC_TRIGGERS      3511
#define IDC_SC_VOWELFILTER   3512
#define IDC_SC_FORMANT       3513
#define IDC_SC_DELTAHZ       3514
#define IDC_SC_SIDE          3515
#define IDC_SC_CUMULATIVE    3516
#define IDC_SC_UNSTRESSED    3517
#define IDC_SC_PHRASEFINAL   3518
