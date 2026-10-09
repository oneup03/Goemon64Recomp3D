// The game options the 3DS settings menu sets (main_3ds.cpp) and the host
// functions read (recomp_api_3ds.cpp), in the desktop's units
// (include/goemon_config.h, src/game/config.cpp). The desktop's other options
// (targeting, autosave, the analog camera and the camera inversions) came with
// the Majora's Mask template and no Goemon patch reads them: the host functions
// return the desktop's defaults.
#ifndef GOEMON_SETTINGS_H
#define GOEMON_SETTINGS_H

extern int g_goemon_bgm_volume;             // 0..100 percent
extern int g_goemon_se_volume;              // 0..100 percent
extern int g_goemon_sky_3d;                 // 1: the 3D sky cylinder, 0: the game's flat scroll

#endif
