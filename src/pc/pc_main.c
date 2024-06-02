#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sm64.h"

#include "pc/lua/smlua.h"

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "game/memory.h"
#include "audio/external.h"

#include "network/network.h"
#include "lua/smlua.h"

#include "audio/audio_api.h"
#include "audio/audio_sdl.h"
#include "audio/audio_null.h"

#include "pc_main.h"
#include "loading.h"
#include "cliopts.h"
#include "configfile.h"
#include "controller/controller_api.h"
#include "controller/controller_keyboard.h"
#include "fs/fs.h"

#include "game/display.h" // for gGlobalTimer
#include "game/game_init.h"
#include "game/main.h"
#include "game/rumble_init.h"

// CLEANSE THY BASS
// #include "include/bass/bass.h"
// #include "include/bass/bass_fx.h"
// #include "src/bass_audio/bass_audio_helpers.h"
// #include "pc/lua/utils/smlua_audio_utils.h"

#include "pc/network/version.h"
#include "pc/network/socket/domain_res.h"
#include "pc/network/network_player.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_unicode.h"
#include "pc/djui/djui_panel.h"
#include "pc/djui/djui_panel_modlist.h"
#include "pc/djui/djui_fps_display.h"
#include "pc/debuglog.h"
#include "pc/utils/misc.h"

#include "pc/mods/mods.h"

#include "debug_context.h"
#include "menu/intro_geo.h"

#ifdef DISCORD_SDK
#include "pc/discord/discord.h"
#endif

OSMesg D_80339BEC;
OSMesgQueue gSIEventMesgQueue;

s8 gResetTimer;
s8 D_8032C648;
s8 gDebugLevelSelect;
s8 gShowProfiler;
s8 gShowDebugText;

s32 gRumblePakPfs;
u32 gNumVblanks = 0;

u8 gRenderingInterpolated = 0;
f32 gRenderingDelta = 0;

f64 gGameSpeed = 1.0f; // TODO: should probably remove

#define FRAMERATE 30
static const f64 sFrameTime = (1.0 / ((double)FRAMERATE));
static f64 sFrameTargetTime = 0;
static f64 sFrameTimeStart;

bool gGameInited = false;
bool gGfxInited = false;

static struct AudioAPI *audio_api;
struct GfxWindowManagerAPI *wm_api = &WAPI;

extern void gfx_run(Gfx *commands);
extern void thread5_game_loop(void *arg);
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);
void game_loop_one_iteration(void);

void dispatch_audio_sptask(UNUSED struct SPTask *spTask) {}
void set_vblank_handler(UNUSED s32 index, UNUSED struct VblankHandler *handler, UNUSED OSMesgQueue *queue, UNUSED OSMesg *msg) {}

void send_display_list(struct SPTask *spTask) {
    if (!gGameInited) { return; }
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

#ifdef VERSION_EU
#define SAMPLES_HIGH 560 // gAudioBufferParameters.maxAiBufferLength
#define SAMPLES_LOW 528 // gAudioBufferParameters.minAiBufferLength
#else
#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528
#endif

extern void patch_mtx_before(void);
extern void patch_screen_transition_before(void);
extern void patch_title_screen_before(void);
extern void patch_dialog_before(void);
extern void patch_hud_before(void);
extern void patch_paintings_before(void);
extern void patch_bubble_particles_before(void);
extern void patch_snow_particles_before(void);
extern void patch_djui_before(void);
extern void patch_djui_hud_before(void);

extern void patch_mtx_interpolated(f32 delta);
extern void patch_screen_transition_interpolated(f32 delta);
extern void patch_title_screen_interpolated(f32 delta);
extern void patch_dialog_interpolated(f32 delta);
extern void patch_hud_interpolated(f32 delta);
extern void patch_paintings_interpolated(f32 delta);
extern void patch_bubble_particles_interpolated(f32 delta);
extern void patch_snow_particles_interpolated(f32 delta);
extern void patch_djui_interpolated(f32 delta);
extern void patch_djui_hud(f32 delta);

static void patch_interpolations_before(void) {
    patch_mtx_before();
    patch_screen_transition_before();
    patch_title_screen_before();
    patch_dialog_before();
    patch_hud_before();
    patch_paintings_before();
    patch_bubble_particles_before();
    patch_snow_particles_before();
    patch_djui_before();
    patch_djui_hud_before();
}

static inline void patch_interpolations(f32 delta) {
    patch_mtx_interpolated(delta);
    patch_screen_transition_interpolated(delta);
    patch_title_screen_interpolated(delta);
    patch_dialog_interpolated(delta);
    patch_hud_interpolated(delta);
    patch_paintings_interpolated(delta);
    patch_bubble_particles_interpolated(delta);
    patch_snow_particles_interpolated(delta);
    patch_djui_interpolated(delta);
    patch_djui_hud(delta);
}

void produce_interpolation_frames_and_delay(void) {
    u64 frames = 0;
    f64 curTime = clock_elapsed_f64();

    #ifndef TARGET_WEB
    gRenderingInterpolated = true;
    #else
    gRenderingInterpolated = false;
    #endif

    // sanity check target time to deal with hangs and such
    if (fabs(sFrameTargetTime - curTime) > 1) { sFrameTargetTime = curTime - 0.01f; }

    // interpolate and render
    // this causes massive performance issues on web versions. Begone
    #ifndef TARGET_WEB
    while ((curTime = clock_elapsed_f64()) < sFrameTargetTime) {
    #endif
        gfx_start_frame();
        f32 delta = MIN((curTime - sFrameTimeStart) / (sFrameTargetTime - sFrameTimeStart), 1);
        gRenderingDelta = delta;
        #ifndef TARGET_WEB
        if (!gSkipInterpolationTitleScreen && (configFrameLimit > 30 || configUncappedFramerate)) { patch_interpolations(delta); }
        #endif
        send_display_list(gGfxSPTask);
        gfx_end_frame();

        // delay
        #ifndef TARGET_WEB
        if (!configUncappedFramerate) {
        #endif
            f64 targetDelta = 1.0 / (f64) configFrameLimit;
            f64 now = clock_elapsed_f64();
            f64 actualDelta = now - curTime;
            if (actualDelta < targetDelta) {
                f64 delay = ((targetDelta - actualDelta) * 1000.0);
                WAPI.delay((u32) delay);
            }
        #ifndef TARGET_WEB
        }
        #endif

        frames++;
    #ifndef TARGET_WEB
    }
    #endif

    static u64 sFramesSinceFpsUpdate = 0;
    static u64 sLastFpsUpdateTime = 0;

    sFramesSinceFpsUpdate += frames;

    u64 sCurrentFpsUpdateTime = (u64)clock_elapsed_f64();
    if (sLastFpsUpdateTime != sCurrentFpsUpdateTime) {
        u32 fps = sFramesSinceFpsUpdate / ((f32)(sCurrentFpsUpdateTime - sLastFpsUpdateTime));
        sLastFpsUpdateTime = sCurrentFpsUpdateTime;
        sFramesSinceFpsUpdate = 0;

        djui_fps_display_update(floor(fps));
    }

    sFrameTimeStart = sFrameTargetTime;
    sFrameTargetTime += sFrameTime * gGameSpeed;
    gRenderingInterpolated = false;
}

inline static void buffer_audio(void) {
    const f32 master_mod = (f32)configMasterVolume / 127.0f;
    set_sequence_player_volume(SEQ_PLAYER_LEVEL, (f32)configMusicVolume / 127.0f * master_mod);
    set_sequence_player_volume(SEQ_PLAYER_SFX, (f32)configSfxVolume / 127.0f * master_mod);
    set_sequence_player_volume(SEQ_PLAYER_ENV, (f32)configEnvVolume / 127.0f * master_mod);

    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    s16 audio_buffer[SAMPLES_HIGH * 2 * 2];
    for (s32 i = 0; i < 2; i++) {
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);
}

void produce_one_frame(void) {
    CTX_EXTENT(CTX_NETWORK, network_update);

    CTX_EXTENT(CTX_INTERP, patch_interpolations_before);

    CTX_EXTENT(CTX_GAME_LOOP, game_loop_one_iteration);

    CTX_EXTENT(CTX_SMLUA, smlua_update);

    CTX_EXTENT(CTX_AUDIO, buffer_audio);

    CTX_EXTENT(CTX_RENDER, produce_interpolation_frames_and_delay);
}

void audio_shutdown(void) {
    // CLEANSE THY BASS
    // audio_custom_shutdown();
    // if (audio_api) {
    //     if (audio_api->shutdown) audio_api->shutdown();
    //     audio_api = NULL;
    // }
}

void game_deinit(void) {
    if (gGameInited) configfile_save(configfile_name());
    controller_shutdown();
    // CLEANSE THY BASS
    // audio_custom_shutdown();
    audio_shutdown();
    gfx_shutdown();
    network_shutdown(true, true, false, false);
    smlua_shutdown();
    mods_shutdown();
    gGameInited = false;
}

void game_exit(void) {
    LOG_INFO("exiting cleanly");
    game_deinit();
    #ifndef TARGET_WEB
    exit(0);
    #endif
}

#ifdef TARGET_WEB
// NEW STUFF! Sets up the canvas to recieve frames
// copied (mostly) from sm64ex's web port
static void em_main_loop(void) {
}

static void request_anim_frame(void (*func)(double time)) {
    EM_ASM(requestAnimationFrame(function(time) {
        dynCall("vd", $0, [time]);
    }), func);
}

static void on_anim_frame(double time) {
    static double target_time;

    time *= 0.03; // milliseconds to frame count (33.333 ms -> 1)

    if (time >= target_time + 10.0) {
        // We are lagging 10 frames behind, probably due to coming back after inactivity,
        // so reset, with a small margin to avoid potential jitter later.
        target_time = time - 0.010;
    }

    for (int i = 0; i < 2; i++) {
        // If refresh rate is 15 Hz or something we might need to generate two frames
        if (time >= target_time) {
            produce_one_frame();
            target_time = target_time + 1.0;
        }
    }

    if (gGameInited) // only continue if the init flag is still set
        request_anim_frame(on_anim_frame);
}
#endif TARGET_WEB

void* main_game_init(UNUSED void* arg) {
    const char *gamedir = FS_BASEDIR;
    const char *userpath = gCLIOpts.savePath[0] ? gCLIOpts.savePath : sys_user_path();
    fs_init(sys_ropaths, gamedir, userpath);

    if (gIsThreaded) { REFRESH_MUTEX(snprintf(gCurrLoadingSegment.str, 256, "Loading")); }
    dynos_gfx_init();

    // load config
    configfile_load();
    configWindow.settings_changed = true;
    if (!djui_language_init(configLanguage)) { snprintf(configLanguage, MAX_CONFIG_STRING, "%s", ""); }
    dynos_packs_init();
    sync_objects_init_system();

    mods_init();
    enable_queued_mods();
    if (gIsThreaded) {
        REFRESH_MUTEX(
            gCurrLoadingSegment.percentage = 0;
            snprintf(gCurrLoadingSegment.str, 256, "Starting game");
        );
    }

    // If coop_custom_palette_* values are not found in sm64config.txt, the custom palette config will use the default values (Mario's palette)
    // But if no preset is found, that means the current palette is a custom palette
    for (int i = 0; i <= PALETTE_PRESET_MAX; i++) {
        if (i == PALETTE_PRESET_MAX) {
            configCustomPalette = configPlayerPalette;
            configfile_save(configfile_name());
        } else if (memcmp(&configPlayerPalette, &gPalettePresets[i], sizeof(struct PlayerPalette)) == 0) {
            break;
        }
    }

    if (configPlayerModel >= CT_MAX) { configPlayerModel = 0; }

    if (gCLIOpts.fullscreen == 1) { configWindow.fullscreen = true; }
    else if (gCLIOpts.fullscreen == 2) { configWindow.fullscreen = false; }

    if (gCLIOpts.playerName[0] != '\0') {
        snprintf(configPlayerName, MAX_PLAYER_STRING, "%s", gCLIOpts.playerName);
        printf("\nCustom Playername (Start-Parameter): %s\n\n", configPlayerName);
    }

    if (!gGfxInited) {
        gfx_init(&WAPI, &RAPI, TITLE);
        WAPI.set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up, keyboard_on_text_input);
    }

#if defined(AAPI_SDL1) || defined(AAPI_SDL2)
    if (audio_api == NULL && audio_sdl.init()) { audio_api = &audio_sdl; }
#endif
    if (audio_api == NULL) { audio_api = &audio_null; }

    audio_init();
    sound_init();
    // CLEANSE THY BASS
    //bassh_init();
    network_player_init();

#ifdef EXTERNAL_DATA
    // precache data if needed
    if (configPrecacheRes) {
        fprintf(stdout, "precaching data\n");
        fflush(stdout);
        gfx_precache_textures();
    }
#endif

    gGameInited = true;
}

int main(int argc, char *argv[]) {

    // Handle terminal arguments
    if (!parse_cli_opts(argc, argv)) { return 0; }

    // Create the window straight away
    if (!gGfxInited) {
        gfx_init(&WAPI, &RAPI, TITLE);
        WAPI.set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up, keyboard_on_text_input);
    }

    // Start the thread for setting up the game
#ifndef WAPI_DXGI
    if (pthread_mutex_init(&gLoadingThreadMutex, NULL) == 0 && pthread_create(&gLoadingThreadId, NULL, main_game_init, (void*) 1) == 0) {
        gIsThreaded = true;
        render_loading_screen(); // Render the loading screen while the game is setup
        gIsThreaded = false;
    } else {
#else
    {
#endif
        main_game_init(NULL); // Failsafe incase threading doesn't work
    }
    pthread_mutex_destroy(&gLoadingThreadMutex);

    // initialize sm64 data and controllers
    thread5_game_loop(NULL);

    // Initialize djui
    djui_init();
    djui_unicode_init();
    djui_init_late();
    djui_console_message_dequeue();

    // Init network
    if (gCLIOpts.network == NT_CLIENT) {
        network_set_system(NS_SOCKET);
        snprintf(gGetHostName, MAX_CONFIG_STRING, "%s", gCLIOpts.joinIp);
        snprintf(configJoinIp, MAX_CONFIG_STRING, "%s", gCLIOpts.joinIp);
        configJoinPort = gCLIOpts.networkPort;
        network_init(NT_CLIENT, false);
    } else if (gCLIOpts.network == NT_SERVER) {
        network_set_system(NS_SOCKET);
        configHostPort = gCLIOpts.networkPort;

        // Horrible, hacky fix for mods that access marioObj straight away
        // best fix: host with the standard main menu method
        static struct Object sHackyObject = { 0 };
        gMarioStates[0].marioObj = &sHackyObject;

        network_init(NT_SERVER, false);
        djui_panel_shutdown();
        djui_panel_modlist_create(NULL);
    } else {
        network_init(NT_NONE, false);
    }

    #ifndef TARGET_WEB
    // Main loop
    while (true) {
        debug_context_reset();
        CTX_BEGIN(CTX_FRAME);
        WAPI.main_loop(produce_one_frame);
#ifdef DISCORD_SDK
        discord_update();
#endif
#ifdef DEBUG
        fflush(stdout);
        fflush(stderr);
#endif
        CTX_END(CTX_FRAME);
    }
    #endif

    #ifdef TARGET_WEB
    // Main loop (emscripten edition)
    //Sets the main loop to do nothing, starts requesting frames
    emscripten_set_main_loop(em_main_loop, 0, 0);
    request_anim_frame(on_anim_frame);
    #endif

    // CLEANSE THY BASS
    //bassh_deinit();
    return 0;
}
