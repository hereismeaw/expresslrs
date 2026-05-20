#pragma once

// ============================================================
// RFProfile — Sub-GHz zone and pair-mode data model
// Only compiled when FEATURE_CUSTOM_150_960_PROFILE is active.
// ============================================================

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Zone names
// ---------------------------------------------------------------------------
typedef enum rfzone_name_e : uint8_t {
    RF_ZONE_LOW  = 0,  // 151–450 MHz
    RF_ZONE_MID  = 1,  // 451–760 MHz
    RF_ZONE_HIGH = 2,  // 761–959 MHz
    RF_ZONE_COUNT,
    RF_ZONE_INVALID = 0xFF
} rfzone_name_e;

// ---------------------------------------------------------------------------
// Dwell policy for FHSS inside a zone
// ---------------------------------------------------------------------------
typedef enum : uint8_t {
    DWELL_FIXED    = 0,  // stay on configured frequency
    DWELL_ADAPTIVE = 1,  // hop within zone following FHSS sequence
} dwell_policy_e;

// ---------------------------------------------------------------------------
// Zone descriptor — all values populated by the frequency_plan_compiler
// The freq arrays are const; the pointers come from generated rf_zone_table.h
// ---------------------------------------------------------------------------
typedef struct {
    rfzone_name_e  name;
    uint16_t       min_mhz;
    uint16_t       max_mhz;
    const uint16_t *allowed_freq_mhz; // sorted ascending
    uint16_t       freq_count;
    uint16_t       guard_band_mhz;
    int8_t         max_power_dbm;
    dwell_policy_e dwell_policy;
    uint32_t       legal_profile_id;  // matches RF_PROFILE_LEGAL_ID hash
} RFZone_t;

// ---------------------------------------------------------------------------
// Pair-mode diversity strategy
// ---------------------------------------------------------------------------
typedef enum : uint8_t {
    DIVERSITY_MAKE_BEFORE_BREAK = 0,
    DIVERSITY_SINGLE_PATH       = 1,
    DIVERSITY_SWITCH            = 2,
} diversity_mode_e;

// ---------------------------------------------------------------------------
// When a zone transition is triggered
// ---------------------------------------------------------------------------
typedef enum : uint8_t {
    TRANSITION_THRESHOLD_BASED = 0, // auto, driven by LQ/RSSI thresholds
    TRANSITION_COMMANDED       = 1, // explicit Lua/CLI request
    TRANSITION_PERIODIC        = 2, // fixed interval (testing only)
} transition_policy_e;

// ---------------------------------------------------------------------------
// RF pair mode — which two zones are active and how they relate
// ---------------------------------------------------------------------------
typedef struct {
    const RFZone_t    *primary_zone;
    const RFZone_t    *secondary_zone;
    diversity_mode_e   diversity_mode;
    transition_policy_e transition_policy;
    uint8_t            lq_threshold;      // trigger transition if LQ < this
    int8_t             rssi_threshold_dbm; // trigger if RSSI < this
    uint16_t           confirm_window_ms;  // secondary must hold lock this long
    uint16_t           scan_timeout_ms;
    uint16_t           lock_timeout_ms;
} RFPairMode_t;

// ---------------------------------------------------------------------------
// Top-level active RF profile (singleton in firmware)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t       config_hash;
    const char    *legal_profile_id;
    uint8_t        schema_version;
    const RFZone_t zones[RF_ZONE_COUNT];
    RFPairMode_t   pair_mode;
    uint32_t       exclusion_mask_version; // matches digital_tv_exclusion.json metadata
} RFProfile_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Load the compiled-in profile (generated from legal_profile + DTV exclusion).
// Returns false and leaves profile unchanged if validation fails.
bool RFProfile_Load(RFProfile_t *out_profile);

// Validate a profile struct.  Returns false and fills reason (max 64 chars) on error.
bool RFProfile_Validate(const RFProfile_t *profile, char *reason_out);

// Look up a zone by name.  Returns NULL if name is RF_ZONE_INVALID.
const RFZone_t *RFProfile_GetZone(const RFProfile_t *profile, rfzone_name_e name);

// Check whether a frequency is in the exclusion mask (true = excluded/protected).
bool RFProfile_IsExcluded(uint32_t freq_mhz);

// Return the legal profile ID string.
const char *RFProfile_GetLegalId(const RFProfile_t *profile);

// Return the config hash.
uint32_t RFProfile_GetConfigHash(const RFProfile_t *profile);

#ifdef __cplusplus
}
#endif
