#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_core.h"
#include "mc610_tcp.h"
#include "modem_4g.h"
#include "platform_port.h"

typedef struct
{
    uint32_t now_ms;
    uint8_t modem_ready;
    uint8_t modem_busy;
    AT_CoreResult_T completed_result;
    AT_CoreTransaction_T last_transaction;
    char last_command[96];
    Modem4G_NetworkEvent_T events[8];
    uint8_t event_head;
    uint8_t event_tail;
    uint8_t event_count;
    Modem4G_TcpRxChunk_T rx_chunks[8];
    uint8_t rx_head;
    uint8_t rx_tail;
    uint8_t rx_count;
    uint32_t rx_sequence;
} TestCtx_T;

static TestCtx_T g_test;
static int g_failures;

#define CHECK_TRUE(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++; \
        } \
    } while (0)

static void Test_Reset(void)
{
    (void)memset(&g_test, 0, sizeof(g_test));
    g_test.completed_result = AT_CORE_RESULT_NONE;
}

static void Test_PushEvent(Modem4G_NetworkEventType_T type,
                           uint8_t socket_id,
                           uint8_t result)
{
    Modem4G_NetworkEvent_T *event;

    CHECK_TRUE(g_test.event_count < 8U);
    if (g_test.event_count >= 8U)
    {
        return;
    }
    event = &g_test.events[g_test.event_head];
    (void)memset(event, 0, sizeof(*event));
    event->type = type;
    event->socket_id = socket_id;
    event->result = result;
    g_test.event_head = (uint8_t)((g_test.event_head + 1U) % 8U);
    g_test.event_count++;
}

static void Test_PushRx(uint8_t socket_id, const uint8_t *data, uint16_t length)
{
    Modem4G_TcpRxChunk_T *chunk;

    CHECK_TRUE(length <= MODEM4G_TCP_RX_CHUNK_MAX);
    CHECK_TRUE(g_test.rx_count < 8U);
    if ((length > MODEM4G_TCP_RX_CHUNK_MAX) || (g_test.rx_count >= 8U))
    {
        return;
    }

    chunk = &g_test.rx_chunks[g_test.rx_head];
    (void)memset(chunk, 0, sizeof(*chunk));
    g_test.rx_sequence++;
    chunk->sequence = g_test.rx_sequence;
    chunk->socket_id = socket_id;
    chunk->length = length;
    if (length > 0U)
    {
        (void)memcpy(chunk->data, data, length);
    }
    g_test.rx_head = (uint8_t)((g_test.rx_head + 1U) % 8U);
    g_test.rx_count++;
}

static void Test_CompleteAsync(Modem4G_NetworkEventType_T type,
                               uint8_t socket_id,
                               uint8_t result)
{
    Test_PushEvent(type, socket_id, result);
    g_test.modem_busy = 0U;
    g_test.completed_result = AT_CORE_RESULT_ASYNC_EVENT;
    MC610Tcp_Process();
}

static void Test_CompleteSync(AT_CoreResult_T result)
{
    g_test.modem_busy = 0U;
    g_test.completed_result = result;
    MC610Tcp_Process();
}

uint32_t PlatformPort_GetMs(void)
{
    return g_test.now_ms;
}

uint8_t Modem4G_IsReady(void)
{
    return g_test.modem_ready;
}

AT_CoreStartResult_T Modem4G_StartTransaction(const AT_CoreTransaction_T *transaction)
{
    size_t length;

    if (g_test.modem_busy != 0U)
    {
        return AT_CORE_START_BUSY;
    }
    g_test.last_transaction = *transaction;
    length = strlen(transaction->command);
    CHECK_TRUE(length < sizeof(g_test.last_command));
    if (length >= sizeof(g_test.last_command))
    {
        return AT_CORE_START_INVALID;
    }
    (void)memcpy(g_test.last_command, transaction->command, length + 1U);
    g_test.modem_busy = 1U;
    g_test.completed_result = AT_CORE_RESULT_NONE;
    return AT_CORE_START_OK;
}

uint8_t Modem4G_IsBusy(void)
{
    return g_test.modem_busy;
}

AT_CoreResult_T Modem4G_TakeResult(void)
{
    AT_CoreResult_T result;

    result = g_test.completed_result;
    g_test.completed_result = AT_CORE_RESULT_NONE;
    return result;
}

uint8_t Modem4G_PopNetworkEvent(Modem4G_NetworkEvent_T *event)
{
    if ((event == 0) || (g_test.event_count == 0U))
    {
        return 0U;
    }
    *event = g_test.events[g_test.event_tail];
    g_test.event_tail = (uint8_t)((g_test.event_tail + 1U) % 8U);
    g_test.event_count--;
    return 1U;
}

uint8_t Modem4G_PopTcpRxChunk(Modem4G_TcpRxChunk_T *chunk)
{
    if ((chunk == 0) || (g_test.rx_count == 0U))
    {
        return 0U;
    }
    *chunk = g_test.rx_chunks[g_test.rx_tail];
    g_test.rx_tail = (uint8_t)((g_test.rx_tail + 1U) % 8U);
    g_test.rx_count--;
    return 1U;
}

void Modem4G_FlushTcpRx(void)
{
    g_test.rx_head = 0U;
    g_test.rx_tail = 0U;
    g_test.rx_count = 0U;
}

static MC610Tcp_Config_T Test_Config(void)
{
    MC610Tcp_Config_T config;

    (void)memset(&config, 0, sizeof(config));
    (void)strcpy(config.apn, "CMNET");
    (void)strcpy(config.remote_host, "server.example.com");
    config.remote_port = 3417U;
    config.socket_id = 1U;
    config.auto_reconnect = 1U;
    config.reconnect_delay_ms = 1000U;
    return config;
}

static void Test_BringConnected(void)
{
    MC610Tcp_Config_T config;

    config = Test_Config();
    g_test.modem_ready = 1U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);

    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_PDP_ACTIVATING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCALL=1,\"CMNET\"") == 0);
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);

    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RX_FORMAT_CONFIGURING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+GTSET=\"IPRFMT\",0") == 0);
    CHECK_TRUE(g_test.last_transaction.type == AT_CORE_TRANSACTION_SYNC_OK);
    Test_CompleteSync(AT_CORE_RESULT_OK);

    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_OPENING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command,
                      "AT+MIPOPEN=1,\"server.example.com\",3417,0") == 0);
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);

    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RX_CHUNK_CONFIGURING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPDSETS=1,256,0") == 0);
    CHECK_TRUE(g_test.last_transaction.type == AT_CORE_TRANSACTION_SYNC_OK);
    Test_CompleteSync(AT_CORE_RESULT_OK);

    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_CONNECTED);
    CHECK_TRUE(MC610Tcp_IsConnected() != 0U);
}

static void Test_ConnectSequence(void)
{
    MC610Tcp_Config_T config;
    MC610Tcp_Status_T status;
    MC610Tcp_Stats_T stats;

    Test_Reset();
    config = Test_Config();
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_WAIT_MODEM);

    MC610Tcp_Process();
    CHECK_TRUE(g_test.last_command[0] == '\0');

    g_test.modem_ready = 1U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_PDP_ACTIVATING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCALL=1,\"CMNET\"") == 0);
    CHECK_TRUE(g_test.last_transaction.type == AT_CORE_TRANSACTION_ASYNC_OK);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 30000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RX_FORMAT_CONFIGURING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+GTSET=\"IPRFMT\",0") == 0);
    Test_CompleteSync(AT_CORE_RESULT_OK);

    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_OPENING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command,
                      "AT+MIPOPEN=1,\"server.example.com\",3417,0") == 0);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 5000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);
    CHECK_TRUE(MC610Tcp_IsConnected() == 0U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RX_CHUNK_CONFIGURING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPDSETS=1,256,0") == 0);
    Test_CompleteSync(AT_CORE_RESULT_OK);

    CHECK_TRUE(MC610Tcp_IsConnected() != 0U);
    MC610Tcp_GetStatus(&status);
    CHECK_TRUE(status.pdp_active != 0U);
    CHECK_TRUE(status.connected != 0U);
    MC610Tcp_GetStats(&stats);
    CHECK_TRUE(stats.rx_format_config_attempts == 1U);
    CHECK_TRUE(stats.rx_chunk_config_attempts == 1U);
    CHECK_TRUE(stats.connect_successes == 1U);
}

static void Test_RxByteStreamAndOverflow(void)
{
    static const uint8_t hello[] = { 'H', 'e', 'l', 'l', 'o' };
    uint8_t chunk[200];
    uint8_t output[16];
    uint16_t index;
    MC610Tcp_Status_T status;
    MC610Tcp_Stats_T stats;

    Test_Reset();
    Test_BringConnected();

    Test_PushRx(1U, hello, sizeof(hello));
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_RxAvailable() == sizeof(hello));
    MC610Tcp_GetStatus(&status);
    CHECK_TRUE(status.rx_available == sizeof(hello));
    CHECK_TRUE(MC610Tcp_Read(output, 2U) == 2U);
    CHECK_TRUE(output[0] == 'H');
    CHECK_TRUE(output[1] == 'e');
    CHECK_TRUE(MC610Tcp_Read(output, sizeof(output)) == 3U);
    CHECK_TRUE(memcmp(output, "llo", 3U) == 0);
    CHECK_TRUE(MC610Tcp_RxAvailable() == 0U);

    for (index = 0U; index < sizeof(chunk); index++)
    {
        chunk[index] = (uint8_t)index;
    }
    Test_PushRx(1U, chunk, sizeof(chunk));
    Test_PushRx(1U, chunk, sizeof(chunk));
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_RxAvailable() == 400U);

    Test_PushRx(1U, chunk, sizeof(chunk));
    MC610Tcp_Process();
    MC610Tcp_GetStatus(&status);
    CHECK_TRUE(status.rx_overflow != 0U);
    CHECK_TRUE(status.rx_available == 400U);
    MC610Tcp_GetStats(&stats);
    CHECK_TRUE(stats.rx_overflow_chunks == 1U);
    CHECK_TRUE(stats.rx_dropped_bytes == sizeof(chunk));

    MC610Tcp_FlushRx();
    CHECK_TRUE(MC610Tcp_RxAvailable() == 0U);
    MC610Tcp_GetStatus(&status);
    CHECK_TRUE(status.rx_overflow == 0U);
}

static void Test_RemoteCloseSchedulesReconnect(void)
{
    Test_Reset();
    Test_BringConnected();

    Test_PushEvent(MODEM4G_NETWORK_EVENT_MIPSTAT, 1U, 2U);
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RETRY_WAIT);
    CHECK_TRUE(MC610Tcp_IsConnected() == 0U);
    CHECK_TRUE(MC610Tcp_RxAvailable() == 0U);

    g_test.now_ms += 999U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_RETRY_WAIT);
    g_test.now_ms += 1U;
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_OPENING);
}

static void Test_StopClosesSocketAndPdp(void)
{
    Test_Reset();
    Test_BringConnected();

    MC610Tcp_Stop();
    MC610Tcp_Process();
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_CLOSING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCLOSE=1") == 0);
    CHECK_TRUE(g_test.last_transaction.operation_timeout_ms == 3000U);

    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCLOSE, 1U, 0U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_PDP_DEACTIVATING);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPCALL=0") == 0);
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 0U);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_STOPPED);
}

static void Test_RxConfigFailureClosesSocket(void)
{
    MC610Tcp_Config_T config;

    Test_Reset();
    config = Test_Config();
    g_test.modem_ready = 1U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_OK);
    CHECK_TRUE(MC610Tcp_Start() == MC610_TCP_OK);
    MC610Tcp_Process();
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPCALL, 0U, 1U);
    MC610Tcp_Process();
    Test_CompleteSync(AT_CORE_RESULT_OK);
    MC610Tcp_Process();
    Test_CompleteAsync(MODEM4G_NETWORK_EVENT_MIPOPEN, 1U, 1U);
    MC610Tcp_Process();
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPDSETS=1,256,0") == 0);
    Test_CompleteSync(AT_CORE_RESULT_ERROR);
    CHECK_TRUE(MC610Tcp_GetState() == MC610_TCP_STATE_SOCKET_CLOSING);
    CHECK_TRUE(MC610Tcp_IsConnected() == 0U);
}

static void Test_InvalidConfigRejected(void)
{
    MC610Tcp_Config_T config;

    Test_Reset();
    config = Test_Config();
    config.remote_port = 0U;
    CHECK_TRUE(MC610Tcp_Init(&config) == MC610_TCP_ERR_INVALID_ARG);
}

int main(void)
{
    Test_ConnectSequence();
    Test_RxByteStreamAndOverflow();
    Test_RemoteCloseSchedulesReconnect();
    Test_StopClosesSocketAndPdp();
    Test_RxConfigFailureClosesSocket();
    Test_InvalidConfigRejected();

    if (g_failures != 0)
    {
        printf("MC610 TCP host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("MC610 TCP host tests: PASS\n");
    return 0;
}
