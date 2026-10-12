#ifndef MODEM_4G_H
#define MODEM_4G_H

#include <stdint.h>

#include "at_core.h"

#define MODEM4G_TCP_RX_CHUNK_MAX  (256U)

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

typedef enum
{
    MODEM4G_NETWORK_EVENT_NONE = 0,
    MODEM4G_NETWORK_EVENT_MIPCALL,
    MODEM4G_NETWORK_EVENT_MIPOPEN,
    MODEM4G_NETWORK_EVENT_MIPCLOSE,
    MODEM4G_NETWORK_EVENT_MIPSTAT
} Modem4G_NetworkEventType_T;

typedef struct
{
    uint32_t sequence;
    uint32_t acknowledged_bytes;
    Modem4G_NetworkEventType_T type;
    uint8_t socket_id;
    uint8_t result;
} Modem4G_NetworkEvent_T;

typedef enum
{
    MODEM4G_DATA_EVENT_NONE = 0,
    MODEM4G_DATA_EVENT_MIPSEND,
    MODEM4G_DATA_EVENT_MIPPUSH,
    MODEM4G_DATA_EVENT_MIPXOFF,
    MODEM4G_DATA_EVENT_MIPXON
} Modem4G_DataEventType_T;

typedef struct
{
    uint32_t sequence;
    uint32_t value; /* MIPSEND free size or MIPPUSH accumulated sent length. */
    Modem4G_DataEventType_T type;
    uint8_t socket_id;
    uint8_t status;
} Modem4G_DataEvent_T;

/*
 * One decoded +MIPRTCP receive indication in IPRFMT=0 mode.
 * remaining_bytes is the MC610 <Left> field and data[] is decoded from HEX.
 */
typedef struct
{
    uint32_t sequence;
    uint32_t remaining_bytes;
    uint16_t length;
    uint8_t socket_id;
    uint8_t data[MODEM4G_TCP_RX_CHUNK_MAX];
} Modem4G_TcpRxChunk_T;

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
    uint32_t network_event_overflow;
    uint32_t data_event_overflow;
    uint32_t mipsend_count;
    uint32_t mippush_count;
    uint32_t mipxoff_count;
    uint32_t mipxon_count;
    uint32_t tcp_rx_chunks;
    uint32_t tcp_rx_bytes;
    uint32_t tcp_rx_decode_errors;
    uint32_t tcp_rx_queue_overflow;
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

/*
 * Pop one structured MIPxxx network event captured by the MC610 URC router.
 * The queue is intentionally small and single-consumer: higher layers should
 * consume these events from the foreground loop, never from an ISR.
 */
uint8_t Modem4G_PopNetworkEvent(Modem4G_NetworkEvent_T *event);

/*
 * TCP data-plane events are kept separate from connection-control events so
 * MC610Tcp and the TX/RX data plane cannot consume each other's notifications.
 */
uint8_t Modem4G_PopDataEvent(Modem4G_DataEvent_T *event);

/*
 * Pop one decoded TCP receive chunk captured from +MIPRTCP in IPRFMT=0 mode.
 * This is a small bare-metal queue; the caller should drain it every loop.
 */
uint8_t Modem4G_PopTcpRxChunk(Modem4G_TcpRxChunk_T *chunk);
void Modem4G_FlushTcpRx(void);

/*
 * Reserve the AT channel while a prompt-mode raw payload is in progress.
 * The current firmware is bare-metal/single-owner; this is a lightweight
 * protocol-state guard rather than an RTOS mutex.
 */
uint8_t Modem4G_BeginRawWrite(void);
uint16_t Modem4G_WriteRaw(const uint8_t *data, uint16_t length);
void Modem4G_EndRawWrite(void);
uint8_t Modem4G_IsRawWriteActive(void);

#endif /* MODEM_4G_H */
