#include "ConnexInsights.h"

#if FEATURE_SUBGHZ_ONLY_LR1121

#include "logging.h"
#include "FHSS.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Global singletons
// ---------------------------------------------------------------------------
RFProfile_t   g_activeRFProfile;
RFPath_t      g_rfPaths[RF_PATH_COUNT];
MBBContext_t  g_mbbCtx;

// ---------------------------------------------------------------------------
bool ConnexInsights_Init(void)
{
    // 1. Load and validate the compiled-in RF profile
    if (!RFProfile_Load(&g_activeRFProfile))
    {
        DBGLN("[CX] FATAL: RF profile load failed — staying offline");
        AuditLog_ProfileRejected("RFProfile_Load failed at boot");
        return false;
    }

    uint32_t hash = RFProfile_GetConfigHash(&g_activeRFProfile);
    AuditLog_Init(hash, RFProfile_GetLegalId(&g_activeRFProfile));
    AuditLog_Boot();
    AuditLog_ProfileLoaded(hash, RFProfile_GetLegalId(&g_activeRFProfile));

    DBGLN("[CX] Profile loaded: %s  hash=0x%08X",
          RFProfile_GetLegalId(&g_activeRFProfile), hash);

    // 2. Load FHSS frequency tables for both zones from the active profile.
    //    Default: path A = LOW zone, path B = MID zone.
    rfzone_name_e zone_a = (rfzone_name_e)g_activeRFProfile.pair_mode.primary_zone->name;
    rfzone_name_e zone_b = (rfzone_name_e)g_activeRFProfile.pair_mode.secondary_zone->name;

    if (!FHSS_LoadSubGHzZone(0, (uint8_t)zone_a) ||
        !FHSS_LoadSubGHzZone(1, (uint8_t)zone_b))
    {
        DBGLN("[CX] FATAL: FHSS zone load failed");
        AuditLog_ProfileRejected("FHSS_LoadSubGHzZone failed");
        return false;
    }

    AuditLog_ZoneSelected(zone_a, RF_PATH_A);
    AuditLog_ZoneSelected(zone_b, RF_PATH_B);

    // 3. Initialize RF paths and MBB state machine
    RFPath_Init(g_rfPaths);
    MBB_Init(&g_mbbCtx, g_rfPaths, &g_activeRFProfile.pair_mode);

    DBGLN("[CX] Init complete. Active=%c Candidate=%c  MBB=%s",
          (g_mbbCtx.active_path_id == RF_PATH_A) ? 'A' : 'B',
          (g_mbbCtx.candidate_path_id == RF_PATH_A) ? 'A' : 'B',
          MBB_StateName(g_mbbCtx.state));

    return true;
}

void ConnexInsights_Tick(void)
{
    MBB_Tick(&g_mbbCtx);
}

RFPath_t *ConnexInsights_GetActivePath(void)
{
    return &g_rfPaths[g_mbbCtx.active_path_id];
}

RFPath_t *ConnexInsights_GetCandidatePath(void)
{
    if (g_mbbCtx.state == MBB_STABLE_ACTIVE) return nullptr;
    return &g_rfPaths[g_mbbCtx.candidate_path_id];
}

void ConnexInsights_UpdateHealth(rfpath_id_e path_id,
                                  uint8_t lq, int16_t rssi, int8_t snr, uint8_t pkt_loss)
{
    if (path_id >= RF_PATH_COUNT) return;
    RFPath_UpdateHealth(&g_rfPaths[path_id], lq, rssi, snr, pkt_loss);
}

bool ConnexInsights_RequestTransition(rfzone_name_e target_zone)
{
    bool ok = MBB_RequestTransition(&g_mbbCtx, target_zone);
    if (ok)
    {
        DBGLN("[CX] Transition requested to zone %u", (uint8_t)target_zone);
    }
    return ok;
}

void ConnexInsights_AbortTransition(void)
{
    MBB_AbortTransition(&g_mbbCtx, MBB_REASON_COMMANDED_ABORT);
}

mbb_state_e ConnexInsights_GetMBBState(void)
{
    return g_mbbCtx.state;
}

bool ConnexInsights_IsFreqAllowed(uint32_t freq_mhz)
{
    return !RFProfile_IsExcluded(freq_mhz);
}

// ---------------------------------------------------------------------------
// Telemetry serialization
// Compact binary format for CRSF custom telemetry params 0x74–0x79.
//
// Byte layout (14 bytes total):
//   [0]    active_path   (0=A, 1=B)
//   [1]    candidate_path (0=A, 1=B, 0xFF=none)
//   [2]    mbb_state
//   [3]    LQ path A
//   [4]    RSSI path A (unsigned, bias -200 → stored as rssi+200)
//   [5]    SNR path A  (signed, stored as int8)
//   [6]    LQ path B
//   [7]    RSSI path B
//   [8]    SNR path B
//   [9]    audit_status  (0=OK 1=WARN 2=FAIL — reserved, always 0 for now)
//   [10-13] config_hash  (big-endian uint32)
// ---------------------------------------------------------------------------
uint8_t ConnexInsights_SerializeTelemetry(uint8_t *buf, uint8_t max_len)
{
    if (!buf || max_len < 14) return 0;

    const RFPath_t *a = &g_rfPaths[RF_PATH_A];
    const RFPath_t *b = &g_rfPaths[RF_PATH_B];
    uint32_t hash = ConnexInsights_GetConfigHash();

    buf[0]  = (uint8_t)g_mbbCtx.active_path_id;
    buf[1]  = MBB_IsStable(&g_mbbCtx) ? 0xFF : (uint8_t)g_mbbCtx.candidate_path_id;
    buf[2]  = (uint8_t)g_mbbCtx.state;
    buf[3]  = a->health.lq;
    buf[4]  = (uint8_t)(a->health.rssi_dbm + 200);   // bias encode
    buf[5]  = (uint8_t)(int8_t)a->health.snr_db;
    buf[6]  = b->health.lq;
    buf[7]  = (uint8_t)(b->health.rssi_dbm + 200);
    buf[8]  = (uint8_t)(int8_t)b->health.snr_db;
    buf[9]  = 0; // audit_status placeholder
    buf[10] = (uint8_t)(hash >> 24);
    buf[11] = (uint8_t)(hash >> 16);
    buf[12] = (uint8_t)(hash >> 8);
    buf[13] = (uint8_t)(hash);

    return 14;
}

uint32_t ConnexInsights_GetConfigHash(void)
{
    return RFProfile_GetConfigHash(&g_activeRFProfile);
}

#endif // FEATURE_SUBGHZ_ONLY_LR1121
