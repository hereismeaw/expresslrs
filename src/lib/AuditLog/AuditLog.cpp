#include "AuditLog.h"
#include "logging.h"

#if FEATURE_RF_PROFILE_AUDIT_LOG

#include <string.h>
#include "MBBStateMachine.h"
#include "RFProfile.h"

// ---------------------------------------------------------------------------
// Circular ring buffer
// ---------------------------------------------------------------------------
static AuditEntry_t s_ring[AUDIT_LOG_RING_SIZE];
static uint8_t  s_head         = 0; // next write position
static uint32_t s_total_count  = 0;
static uint32_t s_profile_hash = 0;

static void _write(AuditEntry_t *e)
{
    e->timestamp_ms = millis();
    e->profile_hash = s_profile_hash;
    s_ring[s_head]  = *e;
    s_head = (s_head + 1) % AUDIT_LOG_RING_SIZE;
    s_total_count++;
}

// ---------------------------------------------------------------------------
void AuditLog_Init(uint32_t profile_hash, const char *legal_id)
{
    s_profile_hash = profile_hash;
    memset(s_ring, 0, sizeof(s_ring));
    s_head = 0;
    s_total_count = 0;
    DBGLN("AuditLog init: hash=0x%08X legal=%s", profile_hash, legal_id ? legal_id : "");
}

void AuditLog_Boot(void)
{
    AuditEntry_t e = {};
    e.event = AUDIT_BOOT;
    strlcpy(e.text, "System boot", AUDIT_LOG_TEXT_LEN);
    _write(&e);
    DBGLN("AuditLog: BOOT");
}

void AuditLog_RFPathTransition(rfpath_id_e path, rfpath_state_e old_s,
                               rfpath_state_e new_s, uint32_t freq_mhz,
                               const char *reason)
{
    AuditEntry_t e = {};
    e.event        = AUDIT_RFPATH_TRANSITION;
    e.active_path  = path;
    e.rfpath_state = (uint8_t)new_s;
    e.freq_mhz     = freq_mhz;
    if (reason) strlcpy(e.text, reason, AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT RFPath[%c] %s→%s freq=%uMHz %s",
          (path == RF_PATH_A) ? 'A' : 'B',
          RFPath_StateName(old_s), RFPath_StateName(new_s),
          freq_mhz, reason ? reason : "");
}

void AuditLog_MBBTransition(mbb_state_e old_s, mbb_state_e new_s,
                             mbb_reason_e reason,
                             rfpath_id_e active, rfpath_id_e candidate)
{
    AuditEntry_t e = {};
    e.event          = AUDIT_MBB_TRANSITION;
    e.mbb_state      = (uint8_t)new_s;
    e.active_path    = active;
    e.candidate_path = candidate;
    e.reason_code    = (uint8_t)reason;
    strlcpy(e.text, MBB_StateName(new_s), AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT MBB %s→%s reason=%s active=%c cand=%c",
          MBB_StateName(old_s), MBB_StateName(new_s), MBB_ReasonName(reason),
          (active == RF_PATH_A) ? 'A' : 'B',
          (candidate == RF_PATH_A) ? 'A' : 'B');
}

void AuditLog_FailsafeActivated(mbb_reason_e reason)
{
    AuditEntry_t e = {};
    e.event       = AUDIT_FAILSAFE;
    e.reason_code = (uint8_t)reason;
    strlcpy(e.text, MBB_ReasonName(reason), AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT FAILSAFE activated reason=%s", MBB_ReasonName(reason));
}

void AuditLog_ProfileLoaded(uint32_t hash, const char *legal_id)
{
    s_profile_hash = hash;
    AuditEntry_t e = {};
    e.event       = AUDIT_PROFILE_LOAD;
    e.freq_mhz    = hash; // reuse field to carry hash
    if (legal_id) strlcpy(e.text, legal_id, AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT PROFILE loaded hash=0x%08X legal=%s", hash, legal_id ? legal_id : "");
}

void AuditLog_ProfileRejected(const char *reason)
{
    AuditEntry_t e = {};
    e.event = AUDIT_PROFILE_REJECTED;
    if (reason) strlcpy(e.text, reason, AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT PROFILE REJECTED: %s", reason ? reason : "");
}

void AuditLog_ZoneSelected(rfzone_name_e zone, rfpath_id_e path)
{
    static const char *const z_names[] = {"LOW", "MID", "HIGH"};
    AuditEntry_t e = {};
    e.event       = AUDIT_ZONE_SELECTED;
    e.active_path = path;
    if (zone < RF_ZONE_COUNT) strlcpy(e.text, z_names[zone], AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT ZONE %s selected on path %c",
          (zone < RF_ZONE_COUNT) ? z_names[zone] : "?",
          (path == RF_PATH_A) ? 'A' : 'B');
}

void AuditLog_ExclusionHit(uint32_t freq_mhz, const char *reason)
{
    AuditEntry_t e = {};
    e.event    = AUDIT_EXCLUSION_HIT;
    e.freq_mhz = freq_mhz;
    if (reason) strlcpy(e.text, reason, AUDIT_LOG_TEXT_LEN);
    _write(&e);

    DBGLN("AUDIT EXCLUSION freq=%uMHz reason=%s", freq_mhz, reason ? reason : "");
}

uint8_t AuditLog_GetRecent(AuditEntry_t *out_buf, uint8_t max_count)
{
    if (!out_buf || max_count == 0) return 0;
    uint8_t count = (s_total_count < AUDIT_LOG_RING_SIZE)
                    ? (uint8_t)s_total_count
                    : AUDIT_LOG_RING_SIZE;
    if (count > max_count) count = max_count;

    // Read from oldest to newest
    uint8_t start = (s_head + AUDIT_LOG_RING_SIZE - count) % AUDIT_LOG_RING_SIZE;
    for (uint8_t i = 0; i < count; i++)
        out_buf[i] = s_ring[(start + i) % AUDIT_LOG_RING_SIZE];

    return count;
}

uint32_t AuditLog_GetTotalCount(void)
{
    return s_total_count;
}

#else // FEATURE_RF_PROFILE_AUDIT_LOG == 0

// Stub implementations for non-custom targets
void AuditLog_Init(uint32_t, const char *) {}
void AuditLog_Boot(void) {}
void AuditLog_RFPathTransition(rfpath_id_e, rfpath_state_e, rfpath_state_e, uint32_t, const char *) {}
void AuditLog_MBBTransition(mbb_state_e, mbb_state_e, mbb_reason_e, rfpath_id_e, rfpath_id_e) {}
void AuditLog_FailsafeActivated(mbb_reason_e) {}
void AuditLog_ProfileLoaded(uint32_t, const char *) {}
void AuditLog_ProfileRejected(const char *) {}
void AuditLog_ZoneSelected(rfzone_name_e, rfpath_id_e) {}
void AuditLog_ExclusionHit(uint32_t, const char *) {}
uint8_t AuditLog_GetRecent(AuditEntry_t *, uint8_t) { return 0; }
uint32_t AuditLog_GetTotalCount(void) { return 0; }

#endif // FEATURE_RF_PROFILE_AUDIT_LOG
