#include "devRFProfile.h"

#if FEATURE_CUSTOM_150_960_PROFILE

#include "ConnexInsights.h"
#include "CRSFEndpoint.h"
#include "CRSFParameters.h"
#include "CRSFRouter.h"
#include "TXModuleEndpoint.h"
#include "crsf_protocol.h"
#include "logging.h"
#include "device.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Writable parameter strings
// ---------------------------------------------------------------------------
static constexpr char szZoneOpts[]   = "LOW;MID;HIGH";
static constexpr char szPolicyOpts[] = "Threshold;Commanded;Periodic";
static constexpr char szRFFolder[]   = "RF Profile";

// ---------------------------------------------------------------------------
// Writable CRSF parameters (registered in the normal parameter table)
// ---------------------------------------------------------------------------
static uint8_t  s_zonePrimary   = 0; // RF_ZONE_LOW
static uint8_t  s_zoneSecondary = 1; // RF_ZONE_MID
static uint8_t  s_transPolicy  = 0; // TRANSITION_THRESHOLD_BASED
static uint8_t  s_guardBand    = 4;

static selectionParameter luaZonePrimary = {
    {"Primary Zone", CRSF_TEXT_SELECTION},
    szZoneOpts,
    &s_zonePrimary, 0, 2, 0, UNIT_RAW
};
static selectionParameter luaZoneSecondary = {
    {"Secondary Zone", CRSF_TEXT_SELECTION},
    szZoneOpts,
    &s_zoneSecondary, 0, 2, 0, UNIT_RAW
};
static selectionParameter luaTransPolicy = {
    {"Trans Policy", CRSF_TEXT_SELECTION},
    szPolicyOpts,
    &s_transPolicy, 0, 2, 0, UNIT_RAW
};
static numberParameter luaGuardBand = {
    {"Guard Band MHz", CRSF_UINT8},
    &s_guardBand, 1, 20, 1, UNIT_RAW
};

// Folder that groups all RF Profile params
static folderParameter luaRFFolder = {
    {szRFFolder, CRSF_FOLDER}
};

// ---------------------------------------------------------------------------
// Telemetry push — RF status compact frame
// Sent periodically via the CRSF info extended payload mechanism.
//
// Payload (14 bytes) layout — matches ConnexInsights_SerializeTelemetry():
//   [0]    active_path
//   [1]    candidate_path (0xFF=none)
//   [2]    mbb_state
//   [3]    LQ path A
//   [4]    RSSI path A (bias +200)
//   [5]    SNR path A
//   [6]    LQ path B
//   [7]    RSSI path B
//   [8]    SNR path B
//   [9]    audit_status (0=OK)
//   [10-13] config_hash big-endian
// ---------------------------------------------------------------------------
static void _push_rf_telemetry(void)
{
    uint8_t payload[16];
    payload[0] = CX_TELEMETRY_TAG_RF_STATUS;
    uint8_t n = ConnexInsights_SerializeTelemetry(&payload[1], 14);
    if (n == 0) return;

    // Build a CRSF extended frame (0x2E ELRS_STATUS) carrying the tagged payload.
    // Lua receives it via crossfireTelemetryPop(); the frame type (0x2E) is returned
    // as the first return value and the payload bytes start with the tag (0xC0).
    const uint8_t total_payload = n + 1; // tag + data bytes
    uint8_t buffer[sizeof(crsf_ext_header_t) + 16 + 1]; // 16 = max payload, 1 = CRC
    memcpy(&buffer[sizeof(crsf_ext_header_t)], payload, total_payload);
    crsfRouter.SetExtendedHeaderAndCrc(
        (crsf_ext_header_t *)buffer,
        CRSF_FRAMETYPE_ELRS_STATUS,
        CRSF_EXT_FRAME_SIZE(total_payload),
        CRSF_ADDRESS_RADIO_TRANSMITTER,
        CRSF_ADDRESS_CRSF_TRANSMITTER
    );
    crsfRouter.processMessage(nullptr, (crsf_header_t *)buffer);
}

// ---------------------------------------------------------------------------
// Parameter write callbacks
// ---------------------------------------------------------------------------
static void _onZonePrimaryWrite(void)
{
    DBGLN("[RF] Zone Primary → %u", s_zonePrimary);
    // If the system is stable, update the active profile's pair mode.
    if (MBB_IsStable(&g_mbbCtx))
    {
        g_activeRFProfile.pair_mode.primary_zone =
            RFProfile_GetZone(&g_activeRFProfile, (rfzone_name_e)s_zonePrimary);
        FHSS_LoadSubGHzZone(0, s_zonePrimary);
        AuditLog_ZoneSelected((rfzone_name_e)s_zonePrimary, RF_PATH_A);
    }
}

static void _onZoneSecondaryWrite(void)
{
    DBGLN("[RF] Zone Secondary → %u", s_zoneSecondary);
    if (MBB_IsStable(&g_mbbCtx))
    {
        g_activeRFProfile.pair_mode.secondary_zone =
            RFProfile_GetZone(&g_activeRFProfile, (rfzone_name_e)s_zoneSecondary);
        FHSS_LoadSubGHzZone(1, s_zoneSecondary);
        AuditLog_ZoneSelected((rfzone_name_e)s_zoneSecondary, RF_PATH_B);
    }
}

static void _onTransPolicyWrite(void)
{
    DBGLN("[RF] Trans Policy → %u", s_transPolicy);
    g_activeRFProfile.pair_mode.transition_policy = (transition_policy_e)s_transPolicy;
}

static void _onGuardBandWrite(void)
{
    DBGLN("[RF] Guard Band → %u MHz", s_guardBand);
    // Guard band changes require a profile reload — mark for next boot.
    // Do NOT change frequency operations mid-flight without reloading the
    // full exclusion table.  Log the pending change.
    AuditLog_ProfileRejected("Guard band change requires reboot/reflash");
}

// ---------------------------------------------------------------------------
// Device lifecycle
// ---------------------------------------------------------------------------
static int _rfprofile_start(void)
{
#if defined(TARGET_TX)
    // Register parameters into the standard CRSF parameter table.
    // The folder groups them on a separate page in the Lua menu.
    crsfTransmitter.registerParameter(&luaRFFolder);
    crsfTransmitter.registerParameter(&luaZonePrimary,   _onZonePrimaryWrite,   0xFF /*folder*/);
    crsfTransmitter.registerParameter(&luaZoneSecondary, _onZoneSecondaryWrite, 0xFF);
    crsfTransmitter.registerParameter(&luaTransPolicy,   _onTransPolicyWrite,   0xFF);
    crsfTransmitter.registerParameter(&luaGuardBand,     _onGuardBandWrite,     0xFF);

    DBGLN("[RF] devRFProfile registered %u parameters", 4);
#endif
    return DURATION_IMMEDIATELY;
}

static int _rfprofile_timeout(void)
{
    // Push telemetry every 500 ms
    _push_rf_telemetry();
    return 500;
}

device_t RFProfile_device = {
    .initialize = nullptr,
    .start      = _rfprofile_start,
    .event      = nullptr,
    .timeout    = _rfprofile_timeout,
    .subscribe  = EVENT_CONNECTION_CHANGED
};

#endif // FEATURE_CUSTOM_150_960_PROFILE
