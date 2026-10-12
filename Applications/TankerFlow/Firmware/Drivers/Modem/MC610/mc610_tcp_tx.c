#include "mc610_tcp_tx.h"

#include "mc610_tcp.h"
#include "modem_4g.h"
#include "platform_port.h"

#define MC610_TCP_TX_COMMAND_MAX          (40U)
#define MC610_TCP_TX_PROMPT_TIMEOUT_MS    (1000U)
#define MC610_TCP_TX_CONFIRM_TIMEOUT_MS   (3000U)
#define MC610_TCP_TX_RAW_CHUNK_SIZE       (64U)

typedef struct
{
    const uint8_t *payload;
    uint32_t confirm_start_ms;
    MC610TcpTx_Status_T status;
    MC610TcpTx_Stats_T stats;
    uint8_t initialized;
} MC610TcpTx_Context_T;

static MC610TcpTx_Context_T g_tx;

static void MC610TcpTx_Clear(void *dst, uint16_t length)
{
    uint8_t *bytes;
    uint16_t index;

    bytes = (uint8_t *)dst;
    for (index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static uint8_t MC610TcpTx_AppendChar(char *buffer,
                                     uint16_t capacity,
                                     uint16_t *length,
                                     char value)
{
    if ((*length + 1U) >= capacity)
    {
        return 0U;
    }
    buffer[*length] = value;
    (*length)++;
    buffer[*length] = '\0';
    return 1U;
}

static uint8_t MC610TcpTx_AppendText(char *buffer,
                                     uint16_t capacity,
                                     uint16_t *length,
                                     const char *text)
{
    uint16_t index;

    index = 0U;
    while (text[index] != '\0')
    {
        if (MC610TcpTx_AppendChar(buffer, capacity, length, text[index]) == 0U)
        {
            return 0U;
        }
        index++;
    }
    return 1U;
}

static uint8_t MC610TcpTx_AppendUint(char *buffer,
                                     uint16_t capacity,
                                     uint16_t *length,
                                     uint32_t value)
{
    char digits[10];
    uint8_t count;

    count = 0U;
    do
    {
        digits[count] = (char)('0' + (value % 10U));
        count++;
        value /= 10U;
    } while ((value != 0U) && (count < (uint8_t)sizeof(digits)));

    while (count > 0U)
    {
        count--;
        if (MC610TcpTx_AppendChar(buffer, capacity, length, digits[count]) == 0U)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t MC610TcpTx_BuildSendCommand(char *command,
                                            uint16_t capacity,
                                            uint8_t socket_id,
                                            uint16_t payload_length)
{
    uint16_t length;

    length = 0U;
    command[0] = '\0';
    return (uint8_t)(
        (MC610TcpTx_AppendText(command, capacity, &length, "AT+MIPSEND=") != 0U) &&
        (MC610TcpTx_AppendUint(command, capacity, &length, socket_id) != 0U) &&
        (MC610TcpTx_AppendChar(command, capacity, &length, ',') != 0U) &&
        (MC610TcpTx_AppendUint(command, capacity, &length, payload_length) != 0U));
}

static uint8_t MC610TcpTx_IsElapsed(uint32_t start_ms, uint32_t period_ms)
{
    return ((uint32_t)(PlatformPort_GetMs() - start_ms) >= period_ms) ? 1U : 0U;
}

static void MC610TcpTx_ReleaseRawWrite(void)
{
    if (g_tx.status.raw_write_active != 0U)
    {
        Modem4G_EndRawWrite();
        g_tx.status.raw_write_active = 0U;
    }
}

static void MC610TcpTx_Finish(MC610TcpTx_Result_T result)
{
    MC610TcpTx_ReleaseRawWrite();
    g_tx.status.last_result = result;
    g_tx.status.state = MC610_TCP_TX_STATE_IDLE;
    g_tx.payload = 0;

    if (result == MC610_TCP_TX_RESULT_SUCCESS)
    {
        g_tx.stats.send_successes++;
    }
}

static void MC610TcpTx_HandleDataEvent(const Modem4G_DataEvent_T *event)
{
    g_tx.stats.data_events++;

    if (event->type == MODEM4G_DATA_EVENT_MIPXOFF)
    {
        if (event->socket_id == g_tx.status.socket_id)
        {
            g_tx.status.flow_controlled = 1U;
            g_tx.stats.flow_xoff_events++;
        }
        else
        {
            g_tx.stats.ignored_data_events++;
        }
        return;
    }

    if (event->type == MODEM4G_DATA_EVENT_MIPXON)
    {
        if (event->socket_id == g_tx.status.socket_id)
        {
            g_tx.status.flow_controlled = 0U;
            g_tx.stats.flow_xon_events++;
        }
        else
        {
            g_tx.stats.ignored_data_events++;
        }
        return;
    }

    if ((event->socket_id != g_tx.status.socket_id) ||
        ((g_tx.status.state != MC610_TCP_TX_STATE_WRITE_PAYLOAD) &&
         (g_tx.status.state != MC610_TCP_TX_STATE_WAIT_CONFIRM)))
    {
        g_tx.stats.ignored_data_events++;
        return;
    }

    if (event->type == MODEM4G_DATA_EVENT_MIPSEND)
    {
        g_tx.status.mipsend_seen = 1U;
        g_tx.status.free_size = (event->value <= 0xFFFFU) ?
                                (uint16_t)event->value : 0xFFFFU;
        if (event->status != 0U)
        {
            g_tx.stats.send_rejections++;
            MC610TcpTx_Finish(MC610_TCP_TX_RESULT_SEND_REJECTED);
        }
        return;
    }

    if (event->type == MODEM4G_DATA_EVENT_MIPPUSH)
    {
        g_tx.status.mippush_seen = 1U;
        if (event->status != 0U)
        {
            g_tx.stats.push_failures++;
            MC610TcpTx_Finish(MC610_TCP_TX_RESULT_PUSH_FAILED);
        }
        return;
    }

    g_tx.stats.ignored_data_events++;
}

static void MC610TcpTx_DrainDataEvents(void)
{
    Modem4G_DataEvent_T event;

    while (Modem4G_PopDataEvent(&event) != 0U)
    {
        MC610TcpTx_HandleDataEvent(&event);
    }
}

void MC610TcpTx_Init(void)
{
    MC610TcpTx_Clear(&g_tx, (uint16_t)sizeof(g_tx));
    g_tx.status.state = MC610_TCP_TX_STATE_IDLE;
    g_tx.status.last_result = MC610_TCP_TX_RESULT_NONE;
    g_tx.initialized = 1U;
}

MC610TcpTx_RequestResult_T MC610TcpTx_Send(const uint8_t *data, uint16_t length)
{
    MC610Tcp_Status_T tcp_status;
    AT_CoreTransaction_T transaction;
    AT_CoreStartResult_T start_result;
    char command[MC610_TCP_TX_COMMAND_MAX];

    if (g_tx.initialized == 0U)
    {
        return MC610_TCP_TX_REQUEST_NOT_INITIALIZED;
    }
    if ((data == 0) || (length == 0U) || (length > MC610_TCP_TX_MAX_PAYLOAD))
    {
        return MC610_TCP_TX_REQUEST_INVALID_ARG;
    }
    if (g_tx.status.state != MC610_TCP_TX_STATE_IDLE)
    {
        return MC610_TCP_TX_REQUEST_BUSY;
    }

    MC610Tcp_GetStatus(&tcp_status);
    if ((tcp_status.connected == 0U) || (MC610Tcp_IsConnected() == 0U))
    {
        return MC610_TCP_TX_REQUEST_NOT_CONNECTED;
    }

    g_tx.status.socket_id = tcp_status.socket_id;
    MC610TcpTx_DrainDataEvents();
    if (g_tx.status.flow_controlled != 0U)
    {
        g_tx.stats.flow_control_rejections++;
        return MC610_TCP_TX_REQUEST_FLOW_CONTROLLED;
    }

    if (MC610TcpTx_BuildSendCommand(command,
                                    sizeof(command),
                                    tcp_status.socket_id,
                                    length) == 0U)
    {
        return MC610_TCP_TX_REQUEST_INVALID_ARG;
    }

    transaction.command = command;
    transaction.response_prefix = 0;
    transaction.type = AT_CORE_TRANSACTION_PROMPT;
    transaction.response_timeout_ms = MC610_TCP_TX_PROMPT_TIMEOUT_MS;
    transaction.operation_timeout_ms = 0U;
    transaction.async_match = 0;

    start_result = Modem4G_StartTransaction(&transaction);
    if (start_result == AT_CORE_START_BUSY)
    {
        return MC610_TCP_TX_REQUEST_BUSY;
    }
    if (start_result != AT_CORE_START_OK)
    {
        return MC610_TCP_TX_REQUEST_START_FAILED;
    }

    g_tx.payload = data;
    g_tx.status.payload_length = length;
    g_tx.status.payload_written = 0U;
    g_tx.status.free_size = 0U;
    g_tx.status.mipsend_seen = 0U;
    g_tx.status.mippush_seen = 0U;
    g_tx.status.raw_write_active = 0U;
    g_tx.status.last_result = MC610_TCP_TX_RESULT_NONE;
    g_tx.status.state = MC610_TCP_TX_STATE_WAIT_PROMPT;
    g_tx.stats.send_requests++;
    return MC610_TCP_TX_REQUEST_OK;
}

void MC610TcpTx_Process(void)
{
    AT_CoreResult_T at_result;
    MC610Tcp_Status_T tcp_status;
    uint16_t remaining;
    uint16_t chunk;
    uint16_t written;

    if (g_tx.initialized == 0U)
    {
        return;
    }

    MC610Tcp_GetStatus(&tcp_status);
    if ((tcp_status.socket_id >= 1U) && (tcp_status.socket_id <= 6U))
    {
        g_tx.status.socket_id = tcp_status.socket_id;
    }
    if (tcp_status.connected == 0U)
    {
        /* XOFF is scoped to the old socket lifetime.  Do not carry a stale
         * flow-control latch across a disconnect/reconnect cycle. */
        g_tx.status.flow_controlled = 0U;
    }
    MC610TcpTx_DrainDataEvents();

    switch (g_tx.status.state)
    {
        case MC610_TCP_TX_STATE_IDLE:
            break;

        case MC610_TCP_TX_STATE_WAIT_PROMPT:
            if (Modem4G_IsBusy() != 0U)
            {
                break;
            }
            at_result = Modem4G_TakeResult();
            if (at_result == AT_CORE_RESULT_PROMPT)
            {
                if (Modem4G_BeginRawWrite() == 0U)
                {
                    g_tx.stats.raw_write_failures++;
                    MC610TcpTx_Finish(MC610_TCP_TX_RESULT_RAW_WRITE_ERROR);
                    break;
                }
                g_tx.status.raw_write_active = 1U;
                g_tx.status.state = MC610_TCP_TX_STATE_WRITE_PAYLOAD;
            }
            else if (at_result == AT_CORE_RESULT_TIMEOUT)
            {
                g_tx.stats.prompt_failures++;
                MC610TcpTx_Finish(MC610_TCP_TX_RESULT_PROMPT_TIMEOUT);
            }
            else
            {
                g_tx.stats.prompt_failures++;
                MC610TcpTx_Finish(MC610_TCP_TX_RESULT_PROMPT_ERROR);
            }
            break;

        case MC610_TCP_TX_STATE_WRITE_PAYLOAD:
            remaining = (uint16_t)(g_tx.status.payload_length -
                                   g_tx.status.payload_written);
            chunk = (remaining > MC610_TCP_TX_RAW_CHUNK_SIZE) ?
                    MC610_TCP_TX_RAW_CHUNK_SIZE : remaining;
            written = Modem4G_WriteRaw(&g_tx.payload[g_tx.status.payload_written], chunk);
            if (written == 0U)
            {
                /* The exact-length prompt is now incomplete.  Keep the raw-write
                 * guard asserted; only a modem reset/re-init can safely recover
                 * without risking AT command bytes being consumed as payload. */
                g_tx.stats.raw_write_failures++;
                g_tx.status.last_result = MC610_TCP_TX_RESULT_RAW_WRITE_ERROR;
                g_tx.status.state = MC610_TCP_TX_STATE_RAW_WRITE_FAULT;
                break;
            }
            g_tx.status.payload_written = (uint16_t)(g_tx.status.payload_written + written);
            g_tx.stats.raw_bytes_written += written;
            if (g_tx.status.payload_written >= g_tx.status.payload_length)
            {
                g_tx.confirm_start_ms = PlatformPort_GetMs();
                g_tx.status.state = MC610_TCP_TX_STATE_WAIT_CONFIRM;
            }
            break;

        case MC610_TCP_TX_STATE_WAIT_CONFIRM:
            if (g_tx.status.state != MC610_TCP_TX_STATE_WAIT_CONFIRM)
            {
                break;
            }
            if ((g_tx.status.mipsend_seen != 0U) &&
                (g_tx.status.mippush_seen != 0U))
            {
                if (MC610Tcp_IsConnected() != 0U)
                {
                    MC610TcpTx_Finish(MC610_TCP_TX_RESULT_SUCCESS);
                }
                else
                {
                    MC610TcpTx_Finish(MC610_TCP_TX_RESULT_DISCONNECTED);
                }
            }
            else if (MC610TcpTx_IsElapsed(g_tx.confirm_start_ms,
                                          MC610_TCP_TX_CONFIRM_TIMEOUT_MS) != 0U)
            {
                g_tx.stats.confirm_timeouts++;
                MC610TcpTx_Finish(MC610_TCP_TX_RESULT_CONFIRM_TIMEOUT);
            }
            else
            {
            }
            break;

        case MC610_TCP_TX_STATE_RAW_WRITE_FAULT:
            /* Intentionally keep Modem4G raw-write lock asserted.  The caller
             * must reset/reinitialize the modem before sending more AT commands. */
            break;

        case MC610_TCP_TX_STATE_UNINITIALIZED:
        default:
            break;
    }
}

uint8_t MC610TcpTx_IsBusy(void)
{
    if (g_tx.initialized == 0U)
    {
        return 0U;
    }
    return (g_tx.status.state == MC610_TCP_TX_STATE_IDLE) ? 0U : 1U;
}

uint8_t MC610TcpTx_IsFlowControlled(void)
{
    return g_tx.status.flow_controlled;
}

MC610TcpTx_Result_T MC610TcpTx_PeekResult(void)
{
    return g_tx.status.last_result;
}

MC610TcpTx_Result_T MC610TcpTx_TakeResult(void)
{
    MC610TcpTx_Result_T result;

    result = g_tx.status.last_result;
    g_tx.status.last_result = MC610_TCP_TX_RESULT_NONE;
    return result;
}

void MC610TcpTx_GetStatus(MC610TcpTx_Status_T *status)
{
    if (status == 0)
    {
        return;
    }
    *status = g_tx.status;
}

void MC610TcpTx_GetStats(MC610TcpTx_Stats_T *stats)
{
    if (stats == 0)
    {
        return;
    }
    *stats = g_tx.stats;
}

void MC610TcpTx_ResetStats(void)
{
    MC610TcpTx_Clear(&g_tx.stats, (uint16_t)sizeof(g_tx.stats));
}
