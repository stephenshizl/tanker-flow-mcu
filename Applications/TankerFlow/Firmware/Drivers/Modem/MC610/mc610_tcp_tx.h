#ifndef MC610_TCP_TX_H
#define MC610_TCP_TX_H

#include <stdint.h>

#define MC610_TCP_TX_MAX_PAYLOAD  (2048U)

typedef enum
{
    MC610_TCP_TX_REQUEST_OK = 0,
    MC610_TCP_TX_REQUEST_INVALID_ARG = -1,
    MC610_TCP_TX_REQUEST_NOT_INITIALIZED = -2,
    MC610_TCP_TX_REQUEST_NOT_CONNECTED = -3,
    MC610_TCP_TX_REQUEST_BUSY = -4,
    MC610_TCP_TX_REQUEST_FLOW_CONTROLLED = -5,
    MC610_TCP_TX_REQUEST_START_FAILED = -6
} MC610TcpTx_RequestResult_T;

typedef enum
{
    MC610_TCP_TX_STATE_UNINITIALIZED = 0,
    MC610_TCP_TX_STATE_IDLE,
    MC610_TCP_TX_STATE_WAIT_PROMPT,
    MC610_TCP_TX_STATE_WRITE_PAYLOAD,
    MC610_TCP_TX_STATE_WAIT_CONFIRM,
    MC610_TCP_TX_STATE_RAW_WRITE_FAULT
} MC610TcpTx_State_T;

typedef enum
{
    MC610_TCP_TX_RESULT_NONE = 0,
    MC610_TCP_TX_RESULT_SUCCESS,
    MC610_TCP_TX_RESULT_PROMPT_ERROR,
    MC610_TCP_TX_RESULT_PROMPT_TIMEOUT,
    MC610_TCP_TX_RESULT_RAW_WRITE_ERROR,
    MC610_TCP_TX_RESULT_SEND_REJECTED,
    MC610_TCP_TX_RESULT_PUSH_FAILED,
    MC610_TCP_TX_RESULT_CONFIRM_TIMEOUT,
    MC610_TCP_TX_RESULT_DISCONNECTED
} MC610TcpTx_Result_T;

typedef struct
{
    MC610TcpTx_State_T state;
    MC610TcpTx_Result_T last_result;
    uint16_t payload_length;
    uint16_t payload_written;
    uint16_t free_size;
    uint8_t socket_id;
    uint8_t flow_controlled;
    uint8_t mipsend_seen;
    uint8_t mippush_seen;
    uint8_t raw_write_active;
} MC610TcpTx_Status_T;

typedef struct
{
    uint32_t send_requests;
    uint32_t send_successes;
    uint32_t prompt_failures;
    uint32_t raw_write_failures;
    uint32_t send_rejections;
    uint32_t push_failures;
    uint32_t confirm_timeouts;
    uint32_t flow_xoff_events;
    uint32_t flow_xon_events;
    uint32_t flow_control_rejections;
    uint32_t data_events;
    uint32_t ignored_data_events;
    uint32_t raw_bytes_written;
} MC610TcpTx_Stats_T;

/*
 * Bare-metal, non-blocking MC610 TCP transmit data plane.
 *
 * It uses the MC610 exact-length prompt form:
 *     AT+MIPSEND=<socket>,<length>
 *     >
 *     <raw payload bytes>
 *
 * The modem automatically pushes the exact-length payload.  Completion is
 * confirmed from +MIPPUSH and +MIPSEND events; an extra AT+MIPPUSH command is
 * therefore not issued for this mode.
 *
 * The payload is NOT copied to save MCU RAM.  The caller must keep the buffer
 * valid and unchanged until MC610TcpTx_IsBusy() becomes zero and the result is
 * consumed with MC610TcpTx_TakeResult().
 *
 * Foreground call order:
 *     Modem4G_Process();
 *     MC610Tcp_Process();
 *     MC610TcpTx_Process();
 */
void MC610TcpTx_Init(void);
MC610TcpTx_RequestResult_T MC610TcpTx_Send(const uint8_t *data, uint16_t length);
void MC610TcpTx_Process(void);
uint8_t MC610TcpTx_IsBusy(void);
uint8_t MC610TcpTx_IsFlowControlled(void);
MC610TcpTx_Result_T MC610TcpTx_PeekResult(void);
MC610TcpTx_Result_T MC610TcpTx_TakeResult(void);
void MC610TcpTx_GetStatus(MC610TcpTx_Status_T *status);
void MC610TcpTx_GetStats(MC610TcpTx_Stats_T *stats);
void MC610TcpTx_ResetStats(void);

#endif /* MC610_TCP_TX_H */
