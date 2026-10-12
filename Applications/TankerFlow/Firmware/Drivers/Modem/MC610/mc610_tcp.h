#ifndef MC610_TCP_H
#define MC610_TCP_H

#include <stdint.h>

#define MC610_TCP_APN_MAX   (32U)
#define MC610_TCP_HOST_MAX        (48U)
#define MC610_TCP_RX_BUFFER_SIZE  (512U)
#define MC610_TCP_RX_CHUNK_SIZE   (256U)

typedef enum
{
    MC610_TCP_OK = 0,
    MC610_TCP_ERR_INVALID_ARG = -1,
    MC610_TCP_ERR_NOT_INITIALIZED = -2,
    MC610_TCP_ERR_BUSY = -3
} MC610Tcp_Result_T;

typedef enum
{
    MC610_TCP_STATE_UNINITIALIZED = 0,
    MC610_TCP_STATE_STOPPED,
    MC610_TCP_STATE_WAIT_MODEM,
    MC610_TCP_STATE_PDP_ACTIVATING,
    MC610_TCP_STATE_RX_FORMAT_CONFIGURING,
    MC610_TCP_STATE_SOCKET_OPENING,
    MC610_TCP_STATE_RX_CHUNK_CONFIGURING,
    MC610_TCP_STATE_CONNECTED,
    MC610_TCP_STATE_SOCKET_CLOSING,
    MC610_TCP_STATE_PDP_DEACTIVATING,
    MC610_TCP_STATE_RETRY_WAIT,
    MC610_TCP_STATE_ERROR
} MC610Tcp_State_T;

typedef enum
{
    MC610_TCP_ERROR_NONE = 0,
    MC610_TCP_ERROR_INVALID_CONFIG,
    MC610_TCP_ERROR_COMMAND_BUILD,
    MC610_TCP_ERROR_TRANSACTION_START,
    MC610_TCP_ERROR_PDP_ACTIVATE,
    MC610_TCP_ERROR_RX_FORMAT_CONFIG,
    MC610_TCP_ERROR_SOCKET_OPEN,
    MC610_TCP_ERROR_RX_CHUNK_CONFIG,
    MC610_TCP_ERROR_SOCKET_CLOSED,
    MC610_TCP_ERROR_PDP_DEACTIVATE
} MC610Tcp_Error_T;

typedef struct
{
    char apn[MC610_TCP_APN_MAX];
    char remote_host[MC610_TCP_HOST_MAX];
    uint16_t remote_port;
    uint8_t socket_id;       /* MC610 MIP socket ID: 1..6. */
    uint8_t auto_reconnect;  /* 0/1. */
    uint32_t reconnect_delay_ms;
} MC610Tcp_Config_T;

typedef struct
{
    MC610Tcp_State_T state;
    MC610Tcp_Error_T last_error;
    uint8_t desired_running;
    uint8_t pdp_active;
    uint8_t connected;
    uint8_t socket_id;
    uint8_t rx_overflow;
    uint16_t rx_available;
    uint32_t reconnect_count;
    uint32_t last_state_change_ms;
} MC610Tcp_Status_T;

typedef struct
{
    uint32_t pdp_activate_attempts;
    uint32_t socket_open_attempts;
    uint32_t connect_successes;
    uint32_t disconnect_events;
    uint32_t transaction_failures;
    uint32_t reconnect_scheduled;
    uint32_t network_events;
    uint32_t ignored_network_events;
    uint32_t rx_format_config_attempts;
    uint32_t rx_chunk_config_attempts;
    uint32_t rx_chunks;
    uint32_t rx_bytes;
    uint32_t rx_overflow_chunks;
    uint32_t rx_dropped_bytes;
    uint32_t rx_sequence_gaps;
    uint32_t ignored_rx_chunks;
} MC610Tcp_Stats_T;

/*
 * Bare-metal, non-blocking MC610 TCP connection controller.
 *
 * Scope:
 *   MIPCALL -> receive-format setup -> MIPOPEN -> receive-chunk setup -> CONNECTED,
 *   plus graceful MIPCLOSE/MIPCALL=0 and a fixed-size TCP RX byte stream buffer.
 * TX remains in mc610_tcp_tx.c so control, TX and RX consumers stay independent.
 *
 * Call order in a foreground loop must be:
 *   Modem4G_Process();
 *   MC610Tcp_Process();
 */
int MC610Tcp_Init(const MC610Tcp_Config_T *config);
int MC610Tcp_Start(void);
void MC610Tcp_Stop(void);
void MC610Tcp_Process(void);
uint8_t MC610Tcp_IsConnected(void);
MC610Tcp_State_T MC610Tcp_GetState(void);
void MC610Tcp_GetStatus(MC610Tcp_Status_T *status);
void MC610Tcp_GetStats(MC610Tcp_Stats_T *stats);

/* Byte-stream receive API. TCP packet/chunk boundaries are intentionally hidden. */
uint16_t MC610Tcp_RxAvailable(void);
uint16_t MC610Tcp_Read(uint8_t *data, uint16_t capacity);
void MC610Tcp_FlushRx(void);

#endif /* MC610_TCP_H */
