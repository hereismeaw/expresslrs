#pragma once

// ============================================================
// AuditLog — deterministic per-event log for RF transitions
//
// Every runtime frequency decision must be traceable through
// these logs and through telemetry.  This module writes to the
// ESP32 UART log (DBGLN) and optionally to a circular RAM ring
// for retrieval via the ELRS web UI or Lua telemetry extension.
//
// Log entry fields:
//   timestamp_ms, event_type, profile_hash, active_path,
//   candidate_path, mbb_state, rfpath_state, freq_mhz,
//   reason_code, free_text (truncated at 32 bytes)
// ============================================================

#include <stdint.h>
#include <stdbool.h>

// Forward declarations to break circular include chain:
//   AuditLog ← MBBStateMachine ← RFPath ← AuditLog
// Full types are only needed in AuditLog.cpp which includes the real headers directly.
// C++11 opaque enum declarations (underlying type must match the definitions).
enum rfpath_id_e    : uint8_t;
enum rfpath_state_e : uint8_t;
enum mbb_state_e    : uint8_t;
enum mbb_reason_e   : uint8_t;
enum rfzone_name_e  : uint8_t;

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIT_LOG_RING_SIZE 64  // entries kept in RAM (circular)
#define AUDIT_LOG_TEXT_LEN  32  // max free-text per entry

typedef enum : uint8_t {
    AUDIT_RFPATH_TRANSITION = 0,
    AUDIT_MBB_TRANSITION    = 1,
    AUDIT_FAILSAFE          = 2,
    AUDIT_PROFILE_LOAD      = 3,
    AUDIT_PROFILE_REJECTED  = 4,
    AUDIT_ZONE_SELECTED     = 5,
    AUDIT_EXCLUSION_HIT     = 6,
    AUDIT_BOOT              = 7,
} audit_event_e;

typedef struct {
    uint32_t       timestamp_ms;
    audit_event_e  event;
    uint32_t       profile_hash;
    rfpath_id_e    active_path;
    rfpath_id_e    candidate_path;
    uint8_t        mbb_state;    // mbb_state_e cast to uint8_t
    uint8_t        rfpath_state; // rfpath_state_e for the affected path
    uint32_t       freq_mhz;
    uint8_t        reason_code;  // mbb_reason_e or context-specific
    char           text[AUDIT_LOG_TEXT_LEN];
} AuditEntry_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void AuditLog_Init(uint32_t profile_hash, const char *legal_id);
void AuditLog_Boot(void);

// Called from RFPath on every state transition
void AuditLog_RFPathTransition(rfpath_id_e path, rfpath_state_e old_s,
                               rfpath_state_e new_s, uint32_t freq_mhz,
                               const char *reason);

// Called from MBBStateMachine on every state transition
void AuditLog_MBBTransition(mbb_state_e old_s, mbb_state_e new_s,
                             mbb_reason_e reason,
                             rfpath_id_e active, rfpath_id_e candidate);

// Called when failsafe is activated
void AuditLog_FailsafeActivated(mbb_reason_e reason);

// Called when a profile is loaded or rejected
void AuditLog_ProfileLoaded(uint32_t hash, const char *legal_id);
void AuditLog_ProfileRejected(const char *reason);

// Called when a zone is selected
void AuditLog_ZoneSelected(rfzone_name_e zone, rfpath_id_e path);

// Called when the exclusion mask blocks a requested frequency
void AuditLog_ExclusionHit(uint32_t freq_mhz, const char *reason);

// Retrieve the N most recent entries (up to AUDIT_LOG_RING_SIZE).
// Returns count actually copied.
uint8_t AuditLog_GetRecent(AuditEntry_t *out_buf, uint8_t max_count);

// Total entries written since boot (wraps on uint32 overflow, not significant)
uint32_t AuditLog_GetTotalCount(void);

#ifdef __cplusplus
}
#endif
