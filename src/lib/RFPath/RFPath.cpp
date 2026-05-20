#include "RFPath.h"
#include "logging.h"

#if FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK

#include "AuditLog.h"

static const char *const s_state_names[] = {
    "DISABLED",
    "IDLE",
    "SCANNING",
    "CANDIDATE_LOCK",
    "ACTIVE",
    "GUARD",
    "FAILED",
};

void RFPath_Init(RFPath_t paths[RF_PATH_COUNT])
{
    for (uint8_t i = 0; i < RF_PATH_COUNT; i++)
    {
        paths[i].path_id         = (rfpath_id_e)i;
        paths[i].lr1121_instance = i;
        paths[i].antenna_id      = i;
        paths[i].pa_profile_id   = 0;
        paths[i].current_freq_mhz = 0;
        paths[i].state           = RFPATH_DISABLED;
        paths[i].state_entered_ms = 0;
        paths[i].health          = {0, -120, 0, 100, 0, 0, 0, 0};
    }
}

void RFPath_SetState(RFPath_t *path, rfpath_state_e new_state, const char *reason)
{
    if (!path) return;
    rfpath_state_e old_state = path->state;
    path->state              = new_state;
    path->state_entered_ms   = millis();  // Arduino/ESP32 millis()

    DBGLN("RFPath[%c]: %s → %s  reason: %s  freq: %u MHz",
          (path->path_id == RF_PATH_A) ? 'A' : 'B',
          RFPath_StateName(old_state),
          RFPath_StateName(new_state),
          reason ? reason : "",
          path->current_freq_mhz);

    AuditLog_RFPathTransition(path->path_id, old_state, new_state,
                              path->current_freq_mhz, reason);
}

void RFPath_UpdateHealth(RFPath_t *path, uint8_t lq, int16_t rssi, int8_t snr, uint8_t pkt_loss)
{
    if (!path) return;
    path->health.lq          = lq;
    path->health.rssi_dbm    = rssi;
    path->health.snr_db      = snr;
    path->health.pkt_loss_pct = pkt_loss;
}

const char *RFPath_StateName(rfpath_state_e state)
{
    if ((uint8_t)state < sizeof(s_state_names) / sizeof(s_state_names[0]))
        return s_state_names[(uint8_t)state];
    return "UNKNOWN";
}

#endif // FEATURE_DUAL_LR1121_MAKE_BEFORE_BREAK
