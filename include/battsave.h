#ifndef BATTSAVE_H
#define BATTSAVE_H

// CHDK Battery Saver: tiered idle power saving.
// The state machine (core/battsave.c) is platform independent and never calls
// camera functions or reads a clock directly: time is passed in, and all
// hardware access goes through battsave_hw_t. See
// docs/superpowers/specs/2026-11-01-battery-saver-design.md

typedef enum {
    BS_AWAKE,
    BS_LIGHT,           // screen off, CHDK overlays paused
    BS_DEEP,            // playback mode (lens retracted), LED blink
    BS_WAKING,          // switching back to rec mode
    BS_WAKE_FAILED,     // rec mode did not come back; display on, fast blink
    BS_POWER_OFF
} battsave_state_t;

#define BS_NEVER 0u     // timeout value meaning "never"

typedef struct {
    int      enabled;
    unsigned t1_ms;     // idle time before LIGHT
    unsigned t2_ms;     // idle time before DEEP (BS_NEVER allowed)
    unsigned t3_ms;     // time in DEEP before POWER_OFF (BS_NEVER allowed)
    int      blink;     // 0 also when the port has no suitable LED
    int      deep_allowed;
    int      zoom_allowed;
    int      low_batt_mv; // 0 = no check
} battsave_config_t;

typedef struct {
    void (*display)(int on);
    void (*led)(int on);
    int  (*is_rec_mode)(void);
    void (*set_rec_mode)(int rec);
    int  (*get_zoom)(void);
    void (*set_zoom)(int z);
    void (*power_off)(void);
    int  (*vbatt_mv)(void);
    int  (*inhibit)(void);  // script running || movie recording || USB connected
} battsave_hw_t;

void             battsave_init(const battsave_hw_t *hw, const battsave_config_t *cfg, unsigned now_ms);
void             battsave_set_config(const battsave_config_t *cfg, unsigned now_ms);
void             battsave_tick(unsigned now_ms);
int              battsave_key(int any_key_down, unsigned now_ms); // 1 = hide keys from Canon firmware
battsave_state_t battsave_state(void);
int              battsave_overlays_paused(void);                  // 1 in LIGHT, DEEP, WAKING
int              battsave_enabled(void);

#endif
