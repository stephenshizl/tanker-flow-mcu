#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_core.h"
#include "mc610_tcp.h"
#include "mc610_tcp_tx.h"
#include "modem_4g.h"
#include "platform_port.h"

#define TEST_RAW_MAX  (4096U)

typedef struct
{
    uint32_t now_ms;
    uint8_t connected;
    uint8_t socket_id;
    uint8_t modem_busy;
    uint8_t raw_active;
    AT_CoreResult_T completed_result;
    AT_CoreTransaction_T last_transaction;
    char last_command[96];
    uint8_t raw[TEST_RAW_MAX];
    uint16_t raw_length;
    Modem4G_DataEvent_T events[16];
    uint8_t event_head;
    uint8_t event_tail;
    uint8_t event_count;
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
    g_test.connected = 1U;
    g_test.socket_id = 1U;
    g_test.completed_result = AT_CORE_RESULT_NONE;
    MC610TcpTx_Init();
}

static void Test_PushDataEvent(Modem4G_DataEventType_T type,
                               uint8_t socket_id,
                               uint8_t status,
                               uint32_t value)
{
    Modem4G_DataEvent_T *event;

    CHECK_TRUE(g_test.event_count < 16U);
    if (g_test.event_count >= 16U)
    {
        return;
    }
    event = &g_test.events[g_test.event_head];
    (void)memset(event, 0, sizeof(*event));
    event->type = type;
    event->socket_id = socket_id;
    event->status = status;
    event->value = value;
    g_test.event_head = (uint8_t)((g_test.event_head + 1U) % 16U);
    g_test.event_count++;
}

static void Test_CompletePrompt(AT_CoreResult_T result)
{
    g_test.modem_busy = 0U;
    g_test.completed_result = result;
    MC610TcpTx_Process();
}

uint32_t PlatformPort_GetMs(void)
{
    return g_test.now_ms;
}

void MC610Tcp_GetStatus(MC610Tcp_Status_T *status)
{
    (void)memset(status, 0, sizeof(*status));
    status->connected = g_test.connected;
    status->socket_id = g_test.socket_id;
}

uint8_t MC610Tcp_IsConnected(void)
{
    return g_test.connected;
}

AT_CoreStartResult_T Modem4G_StartTransaction(const AT_CoreTransaction_T *transaction)
{
    size_t length;

    if ((g_test.modem_busy != 0U) || (g_test.raw_active != 0U))
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

uint8_t Modem4G_PopDataEvent(Modem4G_DataEvent_T *event)
{
    if ((event == 0) || (g_test.event_count == 0U))
    {
        return 0U;
    }
    *event = g_test.events[g_test.event_tail];
    g_test.event_tail = (uint8_t)((g_test.event_tail + 1U) % 16U);
    g_test.event_count--;
    return 1U;
}

uint8_t Modem4G_BeginRawWrite(void)
{
    if ((g_test.raw_active != 0U) || (g_test.modem_busy != 0U))
    {
        return 0U;
    }
    g_test.raw_active = 1U;
    return 1U;
}

uint16_t Modem4G_WriteRaw(const uint8_t *data, uint16_t length)
{
    uint16_t room;
    uint16_t copy_length;

    if (g_test.raw_active == 0U)
    {
        return 0U;
    }
    room = (uint16_t)(TEST_RAW_MAX - g_test.raw_length);
    copy_length = (length < room) ? length : room;
    if (copy_length > 0U)
    {
        (void)memcpy(&g_test.raw[g_test.raw_length], data, copy_length);
        g_test.raw_length = (uint16_t)(g_test.raw_length + copy_length);
    }
    return copy_length;
}

void Modem4G_EndRawWrite(void)
{
    g_test.raw_active = 0U;
}

uint8_t Modem4G_IsRawWriteActive(void)
{
    return g_test.raw_active;
}

static void Test_ExactLengthPromptSend(void)
{
    uint8_t payload[100];
    uint16_t index;
    MC610TcpTx_Status_T status;
    MC610TcpTx_Stats_T stats;

    Test_Reset();
    for (index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t)index;
    }

    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
    CHECK_TRUE(strcmp(g_test.last_command, "AT+MIPSEND=1,100") == 0);
    CHECK_TRUE(g_test.last_transaction.type == AT_CORE_TRANSACTION_PROMPT);
    CHECK_TRUE(g_test.last_transaction.response_timeout_ms == 1000U);

    Test_CompletePrompt(AT_CORE_RESULT_PROMPT);
    CHECK_TRUE(g_test.raw_active != 0U);

    MC610TcpTx_Process();
    CHECK_TRUE(g_test.raw_length == 64U);
    MC610TcpTx_Process();
    CHECK_TRUE(g_test.raw_length == sizeof(payload));
    CHECK_TRUE(memcmp(g_test.raw, payload, sizeof(payload)) == 0);

    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPPUSH, 1U, 0U, sizeof(payload));
    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPSEND, 1U, 0U, 1948U);
    MC610TcpTx_Process();

    CHECK_TRUE(MC610TcpTx_IsBusy() == 0U);
    CHECK_TRUE(g_test.raw_active == 0U);
    CHECK_TRUE(MC610TcpTx_TakeResult() == MC610_TCP_TX_RESULT_SUCCESS);
    MC610TcpTx_GetStatus(&status);
    CHECK_TRUE(status.free_size == 1948U);
    MC610TcpTx_GetStats(&stats);
    CHECK_TRUE(stats.send_requests == 1U);
    CHECK_TRUE(stats.send_successes == 1U);
    CHECK_TRUE(stats.raw_bytes_written == sizeof(payload));
}

static void Test_XoffBlocksNextSendUntilXon(void)
{
    static const uint8_t payload[] = { 1U, 2U, 3U };

    Test_Reset();
    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPXOFF, 1U, 0U, 0U);
    MC610TcpTx_Process();
    CHECK_TRUE(MC610TcpTx_IsFlowControlled() != 0U);
    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) ==
               MC610_TCP_TX_REQUEST_FLOW_CONTROLLED);

    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPXON, 1U, 0U, 0U);
    MC610TcpTx_Process();
    CHECK_TRUE(MC610TcpTx_IsFlowControlled() == 0U);
    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
}

static void Test_SendRejectedAndPromptTimeout(void)
{
    static const uint8_t payload[] = { 0xAAU, 0x55U };

    Test_Reset();
    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
    Test_CompletePrompt(AT_CORE_RESULT_TIMEOUT);
    CHECK_TRUE(MC610TcpTx_TakeResult() == MC610_TCP_TX_RESULT_PROMPT_TIMEOUT);

    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
    Test_CompletePrompt(AT_CORE_RESULT_PROMPT);
    MC610TcpTx_Process();
    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPSEND, 1U, 1U, 2048U);
    MC610TcpTx_Process();
    CHECK_TRUE(MC610TcpTx_IsBusy() == 0U);
    CHECK_TRUE(g_test.raw_active == 0U);
    CHECK_TRUE(MC610TcpTx_TakeResult() == MC610_TCP_TX_RESULT_SEND_REJECTED);
}

static void Test_ConfirmTimeoutAndDisconnectedResult(void)
{
    static const uint8_t payload[] = { 9U, 8U, 7U, 6U };

    Test_Reset();
    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
    Test_CompletePrompt(AT_CORE_RESULT_PROMPT);
    MC610TcpTx_Process();
    g_test.now_ms += 3000U;
    MC610TcpTx_Process();
    CHECK_TRUE(MC610TcpTx_TakeResult() == MC610_TCP_TX_RESULT_CONFIRM_TIMEOUT);
    CHECK_TRUE(g_test.raw_active == 0U);

    Test_Reset();
    CHECK_TRUE(MC610TcpTx_Send(payload, sizeof(payload)) == MC610_TCP_TX_REQUEST_OK);
    Test_CompletePrompt(AT_CORE_RESULT_PROMPT);
    MC610TcpTx_Process();
    g_test.connected = 0U;
    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPPUSH, 1U, 0U, sizeof(payload));
    Test_PushDataEvent(MODEM4G_DATA_EVENT_MIPSEND, 1U, 0U, 2044U);
    MC610TcpTx_Process();
    CHECK_TRUE(MC610TcpTx_TakeResult() == MC610_TCP_TX_RESULT_DISCONNECTED);
}

static void Test_InvalidRequests(void)
{
    static const uint8_t payload[] = { 1U };

    Test_Reset();
    CHECK_TRUE(MC610TcpTx_Send(0, 1U) == MC610_TCP_TX_REQUEST_INVALID_ARG);
    CHECK_TRUE(MC610TcpTx_Send(payload, 0U) == MC610_TCP_TX_REQUEST_INVALID_ARG);
    g_test.connected = 0U;
    CHECK_TRUE(MC610TcpTx_Send(payload, 1U) == MC610_TCP_TX_REQUEST_NOT_CONNECTED);
}

int main(void)
{
    Test_ExactLengthPromptSend();
    Test_XoffBlocksNextSendUntilXon();
    Test_SendRejectedAndPromptTimeout();
    Test_ConfirmTimeoutAndDisconnectedResult();
    Test_InvalidRequests();

    if (g_failures != 0)
    {
        printf("MC610 TCP TX host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("MC610 TCP TX host tests: PASS\n");
    return 0;
}
