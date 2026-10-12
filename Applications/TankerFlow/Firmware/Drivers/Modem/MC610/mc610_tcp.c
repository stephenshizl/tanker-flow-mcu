#include "mc610_tcp.h"

#include "modem_4g.h"
#include "platform_port.h"

#define MC610_TCP_COMMAND_MAX              (96U)
#define MC610_TCP_AT_RESPONSE_TIMEOUT_MS   (1000U)
#define MC610_TCP_MIPCALL_TIMEOUT_MS       (30000U)
#define MC610_TCP_MIPOPEN_TIMEOUT_MS       (5000U)
#define MC610_TCP_MIPCLOSE_TIMEOUT_MS      (3000U)
#define MC610_TCP_RX_CONFIG_TIMEOUT_MS      (1000U)

typedef enum
{
    MC610_TCP_OPERATION_NONE = 0,
    MC610_TCP_OPERATION_PDP_ACTIVATE,
    MC610_TCP_OPERATION_SOCKET_OPEN,
    MC610_TCP_OPERATION_SOCKET_CLOSE,
    MC610_TCP_OPERATION_PDP_DEACTIVATE
} MC610Tcp_Operation_T;

static MC610Tcp_Config_T g_config;
static MC610Tcp_Status_T g_status;
static MC610Tcp_Stats_T g_stats;
static MC610Tcp_Operation_T g_operation;
static Modem4G_NetworkEvent_T g_operation_event;
static uint8_t g_operation_event_valid;
static uint8_t g_transaction_started;
static uint8_t g_initialized;
static uint32_t g_retry_start_ms;
static uint8_t g_rx_buffer[MC610_TCP_RX_BUFFER_SIZE];
static uint16_t g_rx_head;
static uint16_t g_rx_tail;
static uint16_t g_rx_count;
static uint32_t g_rx_last_sequence;

static void MC610Tcp_Clear(void *dst, uint16_t length)
{
    uint8_t *bytes;
    uint16_t index;

    bytes = (uint8_t *)dst;
    for (index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static uint16_t MC610Tcp_StringLength(const char *text, uint16_t capacity)
{
    uint16_t length;

    if (text == 0)
    {
        return capacity;
    }
    for (length = 0U; length < capacity; length++)
    {
        if (text[length] == '\0')
        {
            return length;
        }
    }
    return capacity;
}

static uint8_t MC610Tcp_StringSafe(const char *text, uint16_t capacity, uint8_t allow_empty)
{
    uint16_t length;
    uint16_t index;

    length = MC610Tcp_StringLength(text, capacity);
    if ((length >= capacity) || ((length == 0U) && (allow_empty == 0U)))
    {
        return 0U;
    }
    for (index = 0U; index < length; index++)
    {
        if ((text[index] == '"') || (text[index] == '\r') || (text[index] == '\n'))
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t MC610Tcp_ConfigValid(const MC610Tcp_Config_T *config)
{
    if (config == 0)
    {
        return 0U;
    }
    if ((config->socket_id < 1U) || (config->socket_id > 6U) ||
        (config->remote_port == 0U) ||
        (config->auto_reconnect > 1U) ||
        ((config->auto_reconnect != 0U) && (config->reconnect_delay_ms == 0U)) ||
        (MC610Tcp_StringSafe(config->apn, MC610_TCP_APN_MAX, 1U) == 0U) ||
        (MC610Tcp_StringSafe(config->remote_host, MC610_TCP_HOST_MAX, 0U) == 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t MC610Tcp_AppendChar(char *buffer, uint16_t capacity, uint16_t *length, char value)
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

static uint8_t MC610Tcp_AppendText(char *buffer,
                                   uint16_t capacity,
                                   uint16_t *length,
                                   const char *text)
{
    uint16_t index;

    index = 0U;
    while (text[index] != '\0')
    {
        if (MC610Tcp_AppendChar(buffer, capacity, length, text[index]) == 0U)
        {
            return 0U;
        }
        index++;
    }
    return 1U;
}

static uint8_t MC610Tcp_AppendUint(char *buffer,
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
        if (MC610Tcp_AppendChar(buffer, capacity, length, digits[count]) == 0U)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t MC610Tcp_BuildMipCall(char *command, uint16_t capacity, uint8_t activate)
{
    uint16_t length;

    length = 0U;
    command[0] = '\0';
    if (MC610Tcp_AppendText(command, capacity, &length, "AT+MIPCALL=") == 0U)
    {
        return 0U;
    }
    if (MC610Tcp_AppendChar(command, capacity, &length, (activate != 0U) ? '1' : '0') == 0U)
    {
        return 0U;
    }
    if ((activate != 0U) && (g_config.apn[0] != '\0'))
    {
        if ((MC610Tcp_AppendText(command, capacity, &length, ",\"") == 0U) ||
            (MC610Tcp_AppendText(command, capacity, &length, g_config.apn) == 0U) ||
            (MC610Tcp_AppendChar(command, capacity, &length, '"') == 0U))
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t MC610Tcp_BuildMipOpen(char *command, uint16_t capacity)
{
    uint16_t length;

    length = 0U;
    command[0] = '\0';
    return (uint8_t)(
        (MC610Tcp_AppendText(command, capacity, &length, "AT+MIPOPEN=") != 0U) &&
        (MC610Tcp_AppendUint(command, capacity, &length, g_config.socket_id) != 0U) &&
        (MC610Tcp_AppendText(command, capacity, &length, ",\"") != 0U) &&
        (MC610Tcp_AppendText(command, capacity, &length, g_config.remote_host) != 0U) &&
        (MC610Tcp_AppendText(command, capacity, &length, "\",") != 0U) &&
        (MC610Tcp_AppendUint(command, capacity, &length, g_config.remote_port) != 0U) &&
        (MC610Tcp_AppendText(command, capacity, &length, ",0") != 0U));
}

static uint8_t MC610Tcp_BuildMipClose(char *command, uint16_t capacity)
{
    uint16_t length;

    length = 0U;
    command[0] = '\0';
    return (uint8_t)(
        (MC610Tcp_AppendText(command, capacity, &length, "AT+MIPCLOSE=") != 0U) &&
        (MC610Tcp_AppendUint(command, capacity, &length, g_config.socket_id) != 0U));
}

static uint8_t MC610Tcp_BuildMipDsets(char *command, uint16_t capacity)
{
    uint16_t length;

    length = 0U;
    command[0] = '\0';
    return (uint8_t)(
        (MC610Tcp_AppendText(command, capacity, &length, "AT+MIPDSETS=") != 0U) &&
        (MC610Tcp_AppendUint(command, capacity, &length, g_config.socket_id) != 0U) &&
        (MC610Tcp_AppendChar(command, capacity, &length, ',') != 0U) &&
        (MC610Tcp_AppendUint(command, capacity, &length, MC610_TCP_RX_CHUNK_SIZE) != 0U) &&
        (MC610Tcp_AppendText(command, capacity, &length, ",0") != 0U));
}

static uint8_t MC610Tcp_StartsWith(const char *line, uint16_t length, const char *prefix)
{
    uint16_t index;

    for (index = 0U; prefix[index] != '\0'; index++)
    {
        if ((index >= length) || (line[index] != prefix[index]))
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t MC610Tcp_MatchMipCall(const char *line, uint16_t length, void *user)
{
    (void)user;
    return MC610Tcp_StartsWith(line, length, "+MIPCALL:");
}

static uint8_t MC610Tcp_MatchMipOpen(const char *line, uint16_t length, void *user)
{
    (void)user;
    return (uint8_t)((MC610Tcp_StartsWith(line, length, "+MIPOPEN:") != 0U) ||
                     (MC610Tcp_StartsWith(line, length, "+MIPSTAT:") != 0U));
}

static uint8_t MC610Tcp_MatchMipClose(const char *line, uint16_t length, void *user)
{
    (void)user;
    return (uint8_t)((MC610Tcp_StartsWith(line, length, "+MIPCLOSE:") != 0U) ||
                     (MC610Tcp_StartsWith(line, length, "+MIPSTAT:") != 0U));
}

static void MC610Tcp_FlushRxInternal(void)
{
    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_rx_count = 0U;
    g_rx_last_sequence = 0U;
    g_status.rx_available = 0U;
    g_status.rx_overflow = 0U;
    Modem4G_FlushTcpRx();
}

static void MC610Tcp_DrainRxChunks(void)
{
    Modem4G_TcpRxChunk_T chunk;
    uint16_t index;
    uint16_t free_space;

    while (Modem4G_PopTcpRxChunk(&chunk) != 0U)
    {
        if ((g_rx_last_sequence != 0U) &&
            (chunk.sequence != (uint32_t)(g_rx_last_sequence + 1U)))
        {
            uint32_t gap;

            gap = (uint32_t)(chunk.sequence - g_rx_last_sequence - 1U);
            g_status.rx_overflow = 1U;
            g_stats.rx_sequence_gaps += gap;
        }
        g_rx_last_sequence = chunk.sequence;

        if ((chunk.socket_id != g_config.socket_id) ||
            ((g_status.state != MC610_TCP_STATE_RX_CHUNK_CONFIGURING) &&
             (g_status.state != MC610_TCP_STATE_CONNECTED)))
        {
            g_stats.ignored_rx_chunks++;
            continue;
        }

        free_space = (uint16_t)(MC610_TCP_RX_BUFFER_SIZE - g_rx_count);
        if (chunk.length > free_space)
        {
            g_status.rx_overflow = 1U;
            g_stats.rx_overflow_chunks++;
            g_stats.rx_dropped_bytes += chunk.length;
            continue;
        }

        for (index = 0U; index < chunk.length; index++)
        {
            g_rx_buffer[g_rx_head] = chunk.data[index];
            g_rx_head = (uint16_t)((g_rx_head + 1U) % MC610_TCP_RX_BUFFER_SIZE);
        }
        g_rx_count = (uint16_t)(g_rx_count + chunk.length);
        g_status.rx_available = g_rx_count;
        g_stats.rx_chunks++;
        g_stats.rx_bytes += chunk.length;
    }
}

static void MC610Tcp_SetState(MC610Tcp_State_T state)
{
    g_status.state = state;
    g_status.last_state_change_ms = PlatformPort_GetMs();
    g_transaction_started = 0U;
    g_operation = MC610_TCP_OPERATION_NONE;
    g_operation_event_valid = 0U;
}

static void MC610Tcp_SetError(MC610Tcp_Error_T error)
{
    g_status.last_error = error;
    g_stats.transaction_failures++;
}

static void MC610Tcp_ScheduleRetry(MC610Tcp_Error_T error)
{
    MC610Tcp_SetError(error);
    g_status.connected = 0U;
    MC610Tcp_FlushRxInternal();
    if (g_status.desired_running == 0U)
    {
        if (g_status.pdp_active != 0U)
        {
            MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
        }
        else
        {
            MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
        }
        return;
    }

    if (g_config.auto_reconnect != 0U)
    {
        g_retry_start_ms = PlatformPort_GetMs();
        g_status.reconnect_count++;
        g_stats.reconnect_scheduled++;
        MC610Tcp_SetState(MC610_TCP_STATE_RETRY_WAIT);
    }
    else
    {
        MC610Tcp_SetState(MC610_TCP_STATE_ERROR);
    }
}

static uint8_t MC610Tcp_AsyncEventMatchesOperation(const Modem4G_NetworkEvent_T *event)
{
    if (g_operation == MC610_TCP_OPERATION_PDP_ACTIVATE)
    {
        return (event->type == MODEM4G_NETWORK_EVENT_MIPCALL) ? 1U : 0U;
    }
    if (g_operation == MC610_TCP_OPERATION_PDP_DEACTIVATE)
    {
        return (event->type == MODEM4G_NETWORK_EVENT_MIPCALL) ? 1U : 0U;
    }
    if (g_operation == MC610_TCP_OPERATION_SOCKET_OPEN)
    {
        return (uint8_t)(((event->type == MODEM4G_NETWORK_EVENT_MIPOPEN) ||
                          (event->type == MODEM4G_NETWORK_EVENT_MIPSTAT)) &&
                         (event->socket_id == g_config.socket_id));
    }
    if (g_operation == MC610_TCP_OPERATION_SOCKET_CLOSE)
    {
        return (uint8_t)(((event->type == MODEM4G_NETWORK_EVENT_MIPCLOSE) ||
                          (event->type == MODEM4G_NETWORK_EVENT_MIPSTAT)) &&
                         (event->socket_id == g_config.socket_id));
    }
    return 0U;
}

static void MC610Tcp_HandleNetworkEvent(const Modem4G_NetworkEvent_T *event)
{
    g_stats.network_events++;

    if ((g_transaction_started != 0U) &&
        (MC610Tcp_AsyncEventMatchesOperation(event) != 0U))
    {
        g_operation_event = *event;
        g_operation_event_valid = 1U;
        return;
    }

    if ((event->type == MODEM4G_NETWORK_EVENT_MIPSTAT) &&
        (event->socket_id == g_config.socket_id) &&
        ((event->result == 1U) || (event->result == 2U)))
    {
        if (g_status.connected != 0U)
        {
            g_stats.disconnect_events++;
        }
        g_status.connected = 0U;
        if (event->result == 1U)
        {
            g_status.pdp_active = 0U;
        }
        if ((g_status.state == MC610_TCP_STATE_CONNECTED) ||
            (g_status.state == MC610_TCP_STATE_RX_CHUNK_CONFIGURING))
        {
            MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_SOCKET_CLOSED);
        }
        return;
    }

    if ((event->type == MODEM4G_NETWORK_EVENT_MIPCLOSE) &&
        (event->socket_id == g_config.socket_id))
    {
        g_status.connected = 0U;
        return;
    }

    if ((event->type == MODEM4G_NETWORK_EVENT_MIPCALL) && (event->result == 0U))
    {
        g_status.pdp_active = 0U;
        g_status.connected = 0U;
        return;
    }

    g_stats.ignored_network_events++;
}

static void MC610Tcp_DrainNetworkEvents(void)
{
    Modem4G_NetworkEvent_T event;

    while (Modem4G_PopNetworkEvent(&event) != 0U)
    {
        MC610Tcp_HandleNetworkEvent(&event);
    }
}

static uint8_t MC610Tcp_StartSync(const char *command)
{
    AT_CoreTransaction_T transaction;
    AT_CoreStartResult_T result;

    transaction.command = command;
    transaction.response_prefix = 0;
    transaction.type = AT_CORE_TRANSACTION_SYNC_OK;
    transaction.response_timeout_ms = MC610_TCP_RX_CONFIG_TIMEOUT_MS;
    transaction.operation_timeout_ms = 0U;
    transaction.async_match = 0;

    result = Modem4G_StartTransaction(&transaction);
    if (result == AT_CORE_START_BUSY)
    {
        return 0U;
    }
    if (result != AT_CORE_START_OK)
    {
        MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_TRANSACTION_START);
        return 0U;
    }

    g_transaction_started = 1U;
    g_operation = MC610_TCP_OPERATION_NONE;
    g_operation_event_valid = 0U;
    return 1U;
}

static uint8_t MC610Tcp_StartAsync(const char *command,
                                   MC610Tcp_Operation_T operation,
                                   uint32_t operation_timeout_ms,
                                   AT_CoreMatchFn match)
{
    AT_CoreTransaction_T transaction;
    AT_CoreStartResult_T result;

    transaction.command = command;
    transaction.response_prefix = 0;
    transaction.type = AT_CORE_TRANSACTION_ASYNC_OK;
    transaction.response_timeout_ms = MC610_TCP_AT_RESPONSE_TIMEOUT_MS;
    transaction.operation_timeout_ms = operation_timeout_ms;
    transaction.async_match = match;

    result = Modem4G_StartTransaction(&transaction);
    if (result == AT_CORE_START_BUSY)
    {
        return 0U;
    }
    if (result != AT_CORE_START_OK)
    {
        MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_TRANSACTION_START);
        return 0U;
    }

    g_transaction_started = 1U;
    g_operation = operation;
    g_operation_event_valid = 0U;
    return 1U;
}

static uint8_t MC610Tcp_TransactionComplete(AT_CoreResult_T *result)
{
    if ((g_transaction_started == 0U) || (Modem4G_IsBusy() != 0U))
    {
        return 0U;
    }
    *result = Modem4G_TakeResult();
    g_transaction_started = 0U;
    return 1U;
}

static void MC610Tcp_ProcessPdpActivate(void)
{
    char command[MC610_TCP_COMMAND_MAX];
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_BuildMipCall(command, sizeof(command), 1U) == 0U)
        {
            MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_COMMAND_BUILD);
            return;
        }
        if (MC610Tcp_StartAsync(command,
                                MC610_TCP_OPERATION_PDP_ACTIVATE,
                                MC610_TCP_MIPCALL_TIMEOUT_MS,
                                MC610Tcp_MatchMipCall) != 0U)
        {
            g_stats.pdp_activate_attempts++;
        }
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if ((result == AT_CORE_RESULT_ASYNC_EVENT) &&
        (g_operation_event_valid != 0U) &&
        (g_operation_event.type == MODEM4G_NETWORK_EVENT_MIPCALL) &&
        (g_operation_event.result != 0U))
    {
        g_status.pdp_active = 1U;
        g_status.last_error = MC610_TCP_ERROR_NONE;
        if (g_status.desired_running != 0U)
        {
            MC610Tcp_SetState(MC610_TCP_STATE_RX_FORMAT_CONFIGURING);
        }
        else
        {
            MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
        }
        return;
    }
    MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_PDP_ACTIVATE);
}

static void MC610Tcp_ProcessRxFormatConfig(void)
{
    static const char command[] = "AT+GTSET=\"IPRFMT\",0";
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_StartSync(command) != 0U)
        {
            g_stats.rx_format_config_attempts++;
        }
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if (result == AT_CORE_RESULT_OK)
    {
        MC610Tcp_FlushRxInternal();
        MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_OPENING);
        return;
    }
    MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_RX_FORMAT_CONFIG);
}

static void MC610Tcp_ProcessSocketOpen(void)
{
    char command[MC610_TCP_COMMAND_MAX];
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_BuildMipOpen(command, sizeof(command)) == 0U)
        {
            MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_COMMAND_BUILD);
            return;
        }
        if (MC610Tcp_StartAsync(command,
                                MC610_TCP_OPERATION_SOCKET_OPEN,
                                MC610_TCP_MIPOPEN_TIMEOUT_MS,
                                MC610Tcp_MatchMipOpen) != 0U)
        {
            g_stats.socket_open_attempts++;
        }
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if ((result == AT_CORE_RESULT_ASYNC_EVENT) &&
        (g_operation_event_valid != 0U) &&
        (g_operation_event.type == MODEM4G_NETWORK_EVENT_MIPOPEN) &&
        (g_operation_event.socket_id == g_config.socket_id) &&
        (g_operation_event.result == 1U))
    {
        g_status.connected = 0U;
        g_status.last_error = MC610_TCP_ERROR_NONE;
        if (g_status.desired_running != 0U)
        {
            MC610Tcp_SetState(MC610_TCP_STATE_RX_CHUNK_CONFIGURING);
        }
        else
        {
            MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_CLOSING);
        }
        return;
    }
    MC610Tcp_ScheduleRetry(MC610_TCP_ERROR_SOCKET_OPEN);
}

static void MC610Tcp_ProcessRxChunkConfig(void)
{
    char command[MC610_TCP_COMMAND_MAX];
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_BuildMipDsets(command, sizeof(command)) == 0U)
        {
            MC610Tcp_SetError(MC610_TCP_ERROR_COMMAND_BUILD);
            MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_CLOSING);
            return;
        }
        if (MC610Tcp_StartSync(command) != 0U)
        {
            g_stats.rx_chunk_config_attempts++;
        }
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if (result == AT_CORE_RESULT_OK)
    {
        g_status.connected = 1U;
        g_status.last_error = MC610_TCP_ERROR_NONE;
        g_stats.connect_successes++;
        MC610Tcp_SetState(MC610_TCP_STATE_CONNECTED);
        return;
    }

    MC610Tcp_SetError(MC610_TCP_ERROR_RX_CHUNK_CONFIG);
    g_status.connected = 0U;
    MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_CLOSING);
}

static void MC610Tcp_ProcessSocketClose(void)
{
    char command[MC610_TCP_COMMAND_MAX];
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_BuildMipClose(command, sizeof(command)) == 0U)
        {
            MC610Tcp_SetError(MC610_TCP_ERROR_COMMAND_BUILD);
            MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
            return;
        }
        (void)MC610Tcp_StartAsync(command,
                                  MC610_TCP_OPERATION_SOCKET_CLOSE,
                                  MC610_TCP_MIPCLOSE_TIMEOUT_MS,
                                  MC610Tcp_MatchMipClose);
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if (result != AT_CORE_RESULT_ASYNC_EVENT)
    {
        MC610Tcp_SetError(MC610_TCP_ERROR_SOCKET_CLOSED);
    }
    g_status.connected = 0U;
    MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
}

static void MC610Tcp_ProcessPdpDeactivate(void)
{
    char command[MC610_TCP_COMMAND_MAX];
    AT_CoreResult_T result;

    if (g_transaction_started == 0U)
    {
        if (MC610Tcp_BuildMipCall(command, sizeof(command), 0U) == 0U)
        {
            MC610Tcp_SetError(MC610_TCP_ERROR_COMMAND_BUILD);
            MC610Tcp_SetState(MC610_TCP_STATE_ERROR);
            return;
        }
        (void)MC610Tcp_StartAsync(command,
                                  MC610_TCP_OPERATION_PDP_DEACTIVATE,
                                  MC610_TCP_MIPCALL_TIMEOUT_MS,
                                  MC610Tcp_MatchMipCall);
        return;
    }

    if (MC610Tcp_TransactionComplete(&result) == 0U)
    {
        return;
    }
    if (!((result == AT_CORE_RESULT_ASYNC_EVENT) &&
          (g_operation_event_valid != 0U) &&
          (g_operation_event.type == MODEM4G_NETWORK_EVENT_MIPCALL) &&
          (g_operation_event.result == 0U)))
    {
        MC610Tcp_SetError(MC610_TCP_ERROR_PDP_DEACTIVATE);
    }
    g_status.pdp_active = 0U;
    g_status.connected = 0U;
    MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
}

int MC610Tcp_Init(const MC610Tcp_Config_T *config)
{
    if (MC610Tcp_ConfigValid(config) == 0U)
    {
        MC610Tcp_Clear(&g_status, (uint16_t)sizeof(g_status));
        g_status.state = MC610_TCP_STATE_ERROR;
        g_status.last_error = MC610_TCP_ERROR_INVALID_CONFIG;
        g_initialized = 0U;
        return MC610_TCP_ERR_INVALID_ARG;
    }

    g_config = *config;
    MC610Tcp_Clear(&g_status, (uint16_t)sizeof(g_status));
    MC610Tcp_Clear(&g_stats, (uint16_t)sizeof(g_stats));
    g_status.state = MC610_TCP_STATE_STOPPED;
    g_status.last_error = MC610_TCP_ERROR_NONE;
    g_status.socket_id = g_config.socket_id;
    g_status.last_state_change_ms = PlatformPort_GetMs();
    g_operation = MC610_TCP_OPERATION_NONE;
    g_operation_event_valid = 0U;
    g_transaction_started = 0U;
    g_retry_start_ms = g_status.last_state_change_ms;
    MC610Tcp_FlushRxInternal();
    g_initialized = 1U;
    return MC610_TCP_OK;
}

int MC610Tcp_Start(void)
{
    if (g_initialized == 0U)
    {
        return MC610_TCP_ERR_NOT_INITIALIZED;
    }

    g_status.desired_running = 1U;
    if (g_status.state == MC610_TCP_STATE_CONNECTED)
    {
        return MC610_TCP_OK;
    }
    if ((g_status.state == MC610_TCP_STATE_PDP_ACTIVATING) ||
        (g_status.state == MC610_TCP_STATE_RX_FORMAT_CONFIGURING) ||
        (g_status.state == MC610_TCP_STATE_SOCKET_OPENING) ||
        (g_status.state == MC610_TCP_STATE_RX_CHUNK_CONFIGURING) ||
        (g_status.state == MC610_TCP_STATE_SOCKET_CLOSING) ||
        (g_status.state == MC610_TCP_STATE_PDP_DEACTIVATING))
    {
        return MC610_TCP_ERR_BUSY;
    }

    g_status.last_error = MC610_TCP_ERROR_NONE;
    MC610Tcp_SetState(MC610_TCP_STATE_WAIT_MODEM);
    return MC610_TCP_OK;
}

void MC610Tcp_Stop(void)
{
    if (g_initialized == 0U)
    {
        return;
    }
    g_status.desired_running = 0U;
}

void MC610Tcp_Process(void)
{
    if (g_initialized == 0U)
    {
        return;
    }

    MC610Tcp_DrainNetworkEvents();
    MC610Tcp_DrainRxChunks();

    if ((g_status.state != MC610_TCP_STATE_STOPPED) &&
        (g_status.state != MC610_TCP_STATE_ERROR) &&
        (Modem4G_IsReady() == 0U))
    {
        g_status.connected = 0U;
        g_status.pdp_active = 0U;
        g_transaction_started = 0U;
        g_operation = MC610_TCP_OPERATION_NONE;
        if (g_status.desired_running != 0U)
        {
            MC610Tcp_SetState(MC610_TCP_STATE_WAIT_MODEM);
        }
        else
        {
            MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
        }
        return;
    }

    switch (g_status.state)
    {
        case MC610_TCP_STATE_STOPPED:
            if (g_status.desired_running != 0U)
            {
                MC610Tcp_SetState(MC610_TCP_STATE_WAIT_MODEM);
            }
            break;

        case MC610_TCP_STATE_WAIT_MODEM:
            if (g_status.desired_running == 0U)
            {
                MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
            }
            else if (Modem4G_IsReady() != 0U)
            {
                MC610Tcp_SetState(MC610_TCP_STATE_PDP_ACTIVATING);
            }
            else
            {
            }
            break;

        case MC610_TCP_STATE_PDP_ACTIVATING:
            MC610Tcp_ProcessPdpActivate();
            break;

        case MC610_TCP_STATE_RX_FORMAT_CONFIGURING:
            if ((g_status.desired_running == 0U) && (g_transaction_started == 0U))
            {
                MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
            }
            else
            {
                MC610Tcp_ProcessRxFormatConfig();
            }
            break;

        case MC610_TCP_STATE_SOCKET_OPENING:
            if ((g_status.desired_running == 0U) && (g_status.pdp_active != 0U) &&
                (g_transaction_started == 0U))
            {
                MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
            }
            else
            {
                MC610Tcp_ProcessSocketOpen();
            }
            break;

        case MC610_TCP_STATE_RX_CHUNK_CONFIGURING:
            if ((g_status.desired_running == 0U) && (g_transaction_started == 0U))
            {
                MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_CLOSING);
            }
            else
            {
                MC610Tcp_ProcessRxChunkConfig();
            }
            break;

        case MC610_TCP_STATE_CONNECTED:
            if (g_status.desired_running == 0U)
            {
                MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_CLOSING);
            }
            break;

        case MC610_TCP_STATE_SOCKET_CLOSING:
            MC610Tcp_ProcessSocketClose();
            break;

        case MC610_TCP_STATE_PDP_DEACTIVATING:
            MC610Tcp_ProcessPdpDeactivate();
            break;

        case MC610_TCP_STATE_RETRY_WAIT:
            if (g_status.desired_running == 0U)
            {
                if (g_status.pdp_active != 0U)
                {
                    MC610Tcp_SetState(MC610_TCP_STATE_PDP_DEACTIVATING);
                }
                else
                {
                    MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
                }
            }
            else if ((uint32_t)(PlatformPort_GetMs() - g_retry_start_ms) >=
                     g_config.reconnect_delay_ms)
            {
                if (g_status.pdp_active != 0U)
                {
                    MC610Tcp_SetState(MC610_TCP_STATE_SOCKET_OPENING);
                }
                else
                {
                    MC610Tcp_SetState(MC610_TCP_STATE_WAIT_MODEM);
                }
            }
            else
            {
            }
            break;

        case MC610_TCP_STATE_ERROR:
            if (g_status.desired_running == 0U)
            {
                MC610Tcp_SetState(MC610_TCP_STATE_STOPPED);
            }
            break;

        case MC610_TCP_STATE_UNINITIALIZED:
        default:
            break;
    }
}

uint8_t MC610Tcp_IsConnected(void)
{
    return (uint8_t)((g_status.state == MC610_TCP_STATE_CONNECTED) &&
                     (g_status.connected != 0U));
}

MC610Tcp_State_T MC610Tcp_GetState(void)
{
    return g_status.state;
}

void MC610Tcp_GetStatus(MC610Tcp_Status_T *status)
{
    if (status != 0)
    {
        *status = g_status;
    }
}

void MC610Tcp_GetStats(MC610Tcp_Stats_T *stats)
{
    if (stats != 0)
    {
        *stats = g_stats;
    }
}

uint16_t MC610Tcp_RxAvailable(void)
{
    return g_rx_count;
}

uint16_t MC610Tcp_Read(uint8_t *data, uint16_t capacity)
{
    uint16_t count;

    if ((data == 0) || (capacity == 0U))
    {
        return 0U;
    }

    count = 0U;
    while ((count < capacity) && (g_rx_count > 0U))
    {
        data[count] = g_rx_buffer[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1U) % MC610_TCP_RX_BUFFER_SIZE);
        g_rx_count--;
        count++;
    }
    g_status.rx_available = g_rx_count;
    return count;
}

void MC610Tcp_FlushRx(void)
{
    MC610Tcp_FlushRxInternal();
}
