#ifndef MODEM_4G_H
#define MODEM_4G_H

#include <stdint.h>

#include "at_core.h"

typedef enum
{
    MODEM4G_STATE_OFF = 0,
    MODEM4G_STATE_VBAT_SETTLE,
    MODEM4G_STATE_PWRKEY_ASSERT,
    MODEM4G_STATE_AT_SYNC,
    MODEM4G_STATE_RESULT_CODES_ON,
    MODEM4G_STATE_VERBOSE_MODE,
    MODEM4G_STATE_ECHO_OFF,
    MODEM4G_STATE_CEREG_URC_ENABLE,
    MODEM4G_STATE_CGREG_URC_ENABLE,
    MODEM4G_STATE_CREG_URC_ENABLE,
    MODEM4G_STATE_SIM_CHECK,
    MODEM4G_STATE_SIM_WAIT,
    MODEM4G_STATE_SIGNAL_CHECK,
    MODEM4G_STATE_CEREG_CHECK,
    MODEM4G_STATE_CGREG_CHECK,
    MODEM4G_STATE_CREG_CHECK,
    MODEM4G_STATE_REG_WAIT,
    MODEM4G_STATE_READY,
    MODEM4G_STATE_RESET_ASSERT,
    MODEM4G_STATE_ERROR
} Modem4G_State_T;

typedef enum
{
    MODEM4G_REG_UNKNOWN = 0xFFU,
    MODEM4G_REG_NOT_REGISTERED = 0U,
    MODEM4G_REG_HOME = 1U,
    MODEM4G_REG_SEARCHING = 2U,
    MODEM4G_REG_DENIED = 3U,
    MODEM4G_REG_STATUS_UNKNOWN = 4U,
    MODEM4G_REG_ROAMING = 5U
} Modem4G_RegStatus_T;

typedef struct
{
    Modem4G_State_T state;
    uint8_t alive;
    uint8_t sim_ready;
    uint8_t registered;
    uint8_t csq_valid;
    uint8_t csq;
    uint8_t cereg;
    uint8_t cgreg;
    uint8_t creg;
    uint8_t reset_attempts;
    uint32_t last_ok_ms;
} Modem4G_Status_T;

typedef struct
{
    uint32_t urc_count;
    uint32_t miprtcp_count;
    uint32_t miprudp_count;
    uint32_t mipstat_count;
    uint32_t tcp_event_urc_count;
    uint32_t network_urc_count;
    uint32_t startup_urc_count;
    uint32_t unknown_urc_count;
    uint32_t power_on_count;
    uint32_t reset_count;
    uint32_t state_transitions;
    uint32_t command_failures;
    uint32_t ready_count;
} Modem4G_Stats_T;

void Modem4G_Init(void);
void Modem4G_Start(void);
void Modem4G_Process(void);
uint8_t Modem4G_IsReady(void);
Modem4G_State_T Modem4G_GetState(void);
void Modem4G_GetStatus(Modem4G_Status_T *status);

AT_CoreStartResult_T Modem4G_StartTransaction(const AT_CoreTransaction_T *transaction);
AT_CoreStartResult_T Modem4G_StartCommand(const char *command,
                                           const char *response_prefix,
                                           const char *success_token,
                                           uint32_t timeout_ms);
uint8_t Modem4G_IsBusy(void);
AT_CoreResult_T Modem4G_PeekResult(void);
AT_CoreResult_T Modem4G_TakeResult(void);
const AT_CoreStats_T *Modem4G_GetAtStats(void);
void Modem4G_GetStats(Modem4G_Stats_T *stats);
uint16_t Modem4G_GetLastUrc(char *dst, uint16_t capacity);

#endif /* MODEM_4G_H */
