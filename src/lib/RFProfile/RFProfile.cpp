#include "RFProfile.h"
#include "logging.h"
#include <stdio.h>   // snprintf
#include <string.h>  // strlcpy (ESP-IDF) / snprintf fallback

#if FEATURE_CUSTOM_150_960_PROFILE

// Include the generated zone tables (produced by frequency_plan_compiler)
#if __has_include("generated/rf_zone_table.h")
#include "generated/rf_zone_table.h"
#else
// Fallback stub for compilation before the compiler has been run.
// A build with FEATURE_CUSTOM_150_960_PROFILE=1 MUST supply real generated headers.
#warning "rf_zone_table.h not found — using empty stub. Run compile_profile.py first."
static const uint16_t rfZoneLowFreqs[]  = {151, 200, 300, 400};
static const uint16_t rfZoneMidFreqs[]  = {451, 500, 600, 700};
static const uint16_t rfZoneHighFreqs[] = {761, 800, 900};
#define RF_ZONE_LOW_FREQ_COUNT  4U
#define RF_ZONE_MID_FREQ_COUNT  4U
#define RF_ZONE_HIGH_FREQ_COUNT 3U
#define RF_PROFILE_CONFIG_HASH  0x00000000UL
#define RF_PROFILE_LEGAL_ID     "UNKNOWN"
#define RF_FREQ_MIN_MHZ         151U
#define RF_FREQ_MAX_MHZ         959U
// Stub exclusion ranges (empty)
static const ExclusionRange_t rfExclusionRanges[] = {};
#define RF_EXCLUSION_RANGE_COUNT 0U
#endif

// ---------------------------------------------------------------------------
// Exclusion mask lookup using the generated range table
// (Prefer range table over bit mask on constrained MCUs to save RAM.)
// ---------------------------------------------------------------------------
bool RFProfile_IsExcluded(uint32_t freq_mhz)
{
    if (freq_mhz < RF_FREQ_MIN_MHZ || freq_mhz > RF_FREQ_MAX_MHZ)
        return true;  // outside hardware range → reject

    for (uint8_t i = 0; i < RF_EXCLUSION_RANGE_COUNT; i++)
    {
        if (freq_mhz >= rfExclusionRanges[i].lo_mhz &&
            freq_mhz <= rfExclusionRanges[i].hi_mhz)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Built-in profile populated from generated constants
// ---------------------------------------------------------------------------
static const RFProfile_t s_builtInProfile = {
    .config_hash            = RF_PROFILE_CONFIG_HASH,
    .legal_profile_id       = RF_PROFILE_LEGAL_ID,
    .schema_version         = 1,
    .zones = {
        [RF_ZONE_LOW] = {
            .name              = RF_ZONE_LOW,
            .min_mhz           = 151,
            .max_mhz           = 450,
            .allowed_freq_mhz  = rfZoneLowFreqs,
            .freq_count        = RF_ZONE_LOW_FREQ_COUNT,
            .guard_band_mhz    = RF_GUARD_BAND_MHZ,
            .max_power_dbm     = 27,
            .dwell_policy      = DWELL_ADAPTIVE,
            .legal_profile_id  = RF_PROFILE_CONFIG_HASH,
        },
        [RF_ZONE_MID] = {
            .name              = RF_ZONE_MID,
            .min_mhz           = 451,
            .max_mhz           = 760,
            .allowed_freq_mhz  = rfZoneMidFreqs,
            .freq_count        = RF_ZONE_MID_FREQ_COUNT,
            .guard_band_mhz    = RF_GUARD_BAND_MHZ,
            .max_power_dbm     = 27,
            .dwell_policy      = DWELL_ADAPTIVE,
            .legal_profile_id  = RF_PROFILE_CONFIG_HASH,
        },
        [RF_ZONE_HIGH] = {
            .name              = RF_ZONE_HIGH,
            .min_mhz           = 761,
            .max_mhz           = 959,
            .allowed_freq_mhz  = rfZoneHighFreqs,
            .freq_count        = RF_ZONE_HIGH_FREQ_COUNT,
            .guard_band_mhz    = RF_GUARD_BAND_MHZ,
            .max_power_dbm     = 27,
            .dwell_policy      = DWELL_ADAPTIVE,
            .legal_profile_id  = RF_PROFILE_CONFIG_HASH,
        },
    },
    .pair_mode = {
        .primary_zone        = nullptr,  // set at runtime via RFProfile_Load
        .secondary_zone      = nullptr,
        .diversity_mode      = DIVERSITY_MAKE_BEFORE_BREAK,
        .transition_policy   = TRANSITION_THRESHOLD_BASED,
        .lq_threshold        = 70,        // % — trigger MBB if primary LQ drops below
        .rssi_threshold_dbm  = -100,
        .confirm_window_ms   = 500,
        .scan_timeout_ms     = 3000,
        .lock_timeout_ms     = 2000,
    },
    .exclusion_mask_version = RF_PROFILE_CONFIG_HASH,
};

bool RFProfile_Load(RFProfile_t *out_profile)
{
    char reason[64];
    if (!RFProfile_Validate(&s_builtInProfile, reason))
    {
        DBGLN("RFProfile_Load: validation failed: %s", reason);
        return false;
    }
    *out_profile = s_builtInProfile;
    // Wire up pair_mode zone pointers (can't use address-of in designated init)
    out_profile->pair_mode.primary_zone   = &out_profile->zones[RF_ZONE_LOW];
    out_profile->pair_mode.secondary_zone = &out_profile->zones[RF_ZONE_MID];
    return true;
}

bool RFProfile_Validate(const RFProfile_t *profile, char *reason_out)
{
    if (!profile)
    {
        if (reason_out) snprintf(reason_out, 64, "null profile");
        return false;
    }

    // Config hash must be non-zero (zero indicates a stub/uninitialized build)
    if (profile->config_hash == 0)
    {
        if (reason_out) snprintf(reason_out, 64, "config_hash is zero — run compile_profile.py");
        return false;
    }

    for (uint8_t i = 0; i < RF_ZONE_COUNT; i++)
    {
        const RFZone_t *z = &profile->zones[i];
        if (z->freq_count == 0)
        {
            if (reason_out) snprintf(reason_out, 64, "zone %u has 0 valid frequencies", i);
            return false;
        }
        if (z->min_mhz < RF_FREQ_MIN_MHZ || z->max_mhz > RF_FREQ_MAX_MHZ)
        {
            if (reason_out) snprintf(reason_out, 64, "zone %u out of hardware range", i);
            return false;
        }
        // Spot-check: first and last freq must not be excluded
        if (RFProfile_IsExcluded(z->allowed_freq_mhz[0]) ||
            RFProfile_IsExcluded(z->allowed_freq_mhz[z->freq_count - 1]))
        {
            if (reason_out) snprintf(reason_out, 64, "zone %u boundary freq is in exclusion list", i);
            return false;
        }
    }

    return true;
}

const RFZone_t *RFProfile_GetZone(const RFProfile_t *profile, rfzone_name_e name)
{
    if (!profile || name >= RF_ZONE_COUNT) return nullptr;
    return &profile->zones[name];
}

const char *RFProfile_GetLegalId(const RFProfile_t *profile)
{
    if (!profile) return "";
    return profile->legal_profile_id;
}

uint32_t RFProfile_GetConfigHash(const RFProfile_t *profile)
{
    if (!profile) return 0;
    return profile->config_hash;
}

#endif // FEATURE_CUSTOM_150_960_PROFILE
