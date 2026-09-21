#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_core.h"

#define TEST_TX_MAX      (512U)
#define TEST_URC_MAX     (AT_CORE_LINE_MAX)
#define TEST_RESPONSE_MAX (256U)

typedef struct
{
    uint32_t now_ms;
    char tx[TEST_TX_MAX];
    uint16_t tx_length;
    uint8_t force_tx_error;
    uint32_t response_count;
    uint32_t urc_count;
    char last_response[TEST_RESPONSE_MAX];
    char last_urc[TEST_URC_MAX];
} TestContext_T;

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

static uint16_t Test_Tx(const uint8_t *data, uint16_t length, void *user)
{
    TestContext_T *ctx;
    uint16_t room;
    uint16_t copy_length;

    ctx = (TestContext_T *)user;
    if (ctx->force_tx_error != 0U)
    {
        return 0U;
    }

    room = (uint16_t)(TEST_TX_MAX - 1U - ctx->tx_length);
    copy_length = (length < room) ? length : room;
    if (copy_length > 0U)
    {
        (void)memcpy(&ctx->tx[ctx->tx_length], data, copy_length);
        ctx->tx_length = (uint16_t)(ctx->tx_length + copy_length);
        ctx->tx[ctx->tx_length] = '\0';
    }
    return copy_length;
}

static uint32_t Test_NowMs(void *user)
{
    return ((TestContext_T *)user)->now_ms;
}

static uint8_t Test_StartsWith(const char *line, uint16_t length, const char *prefix)
{
    uint16_t prefix_length;

    prefix_length = (uint16_t)strlen(prefix);
    return ((length >= prefix_length) && (memcmp(line, prefix, prefix_length) == 0)) ? 1U : 0U;
}

static uint8_t Test_IsUrc(const char *line, uint16_t length, void *user)
{
    (void)user;
    return ((Test_StartsWith(line, length, "+MIPSTAT") != 0U) ||
            (Test_StartsWith(line, length, "+MIPRTCP:") != 0U) ||
            (Test_StartsWith(line, length, "+CEREG:") != 0U) ||
            (Test_StartsWith(line, length, "+MIPCALL:") != 0U)) ? 1U : 0U;
}


static uint8_t Test_MatchMipcall(const char *line, uint16_t length, void *user)
{
    (void)user;
    return Test_StartsWith(line, length, "+MIPCALL:");
}

static AT_CoreStartResult_T Test_StartTransaction(AT_Core_T *core,
                                                  const char *command,
                                                  const char *prefix,
                                                  AT_CoreTransactionType_T type,
                                                  uint32_t response_timeout_ms,
                                                  uint32_t operation_timeout_ms,
                                                  AT_CoreMatchFn async_match)
{
    AT_CoreTransaction_T transaction;

    transaction.command = command;
    transaction.response_prefix = prefix;
    transaction.type = type;
    transaction.response_timeout_ms = response_timeout_ms;
    transaction.operation_timeout_ms = operation_timeout_ms;
    transaction.async_match = async_match;
    return AT_Core_StartTransaction(core, &transaction);
}

static void Test_CopyLine(char *dst, uint16_t capacity, const char *line, uint16_t length)
{
    uint16_t copy_length;

    copy_length = length;
    if (copy_length >= capacity)
    {
        copy_length = capacity - 1U;
    }
    if (copy_length > 0U)
    {
        (void)memcpy(dst, line, copy_length);
    }
    dst[copy_length] = '\0';
}

static void Test_OnResponse(const char *line, uint16_t length, void *user)
{
    TestContext_T *ctx;

    ctx = (TestContext_T *)user;
    ctx->response_count++;
    Test_CopyLine(ctx->last_response, sizeof(ctx->last_response), line, length);
}

static void Test_OnUrc(const char *line, uint16_t length, void *user)
{
    TestContext_T *ctx;

    ctx = (TestContext_T *)user;
    ctx->urc_count++;
    Test_CopyLine(ctx->last_urc, sizeof(ctx->last_urc), line, length);
}

static void Test_Init(AT_Core_T *core, TestContext_T *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
    AT_Core_Init(core, Test_Tx, Test_NowMs, Test_IsUrc, Test_OnResponse, Test_OnUrc, ctx);
}

static AT_CoreStartResult_T Test_Start(AT_Core_T *core,
                                       const char *command,
                                       const char *prefix,
                                       uint32_t timeout_ms)
{
    AT_CoreCommand_T request;

    request.command = command;
    request.response_prefix = prefix;
    request.success_token = "OK";
    request.timeout_ms = timeout_ms;
    return AT_Core_StartCommand(core, &request);
}

static void Test_BasicOkAndEcho(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT", 0, 1000U) == AT_CORE_START_OK);
    CHECK_TRUE(strcmp(ctx.tx, "AT\r\n") == 0);

    AT_Core_Feed(&core, (const uint8_t *)"AT\r\nO", 5U);
    AT_Core_Feed(&core, (const uint8_t *)"K\r\n", 3U);
    CHECK_TRUE(AT_Core_IsBusy(&core) == 0U);
    CHECK_TRUE(Test_Start(&core, "ATE0", 0, 1000U) == AT_CORE_START_BUSY);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
    CHECK_TRUE(core.stats.echo_lines == 1U);
    CHECK_TRUE(core.stats.commands_ok == 1U);
}

static void Test_ResponseAndUrcSeparation(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    const char stream[] = "+MIPSTAT: 1,1\r\n+CSQ: 15,99\r\nOK\r\n";

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT+CSQ", "+CSQ:", 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)stream, (uint16_t)(sizeof(stream) - 1U));

    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_urc, "+MIPSTAT: 1,1") == 0);
    CHECK_TRUE(ctx.response_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_response, "+CSQ: 15,99") == 0);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}

static void Test_ExpectedPrefixWinsOverUrcClassifier(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT+CEREG?", "+CEREG:", 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core,
                 (const uint8_t *)"+CEREG: 0,1\r\nOK\r\n",
                 (uint16_t)strlen("+CEREG: 0,1\r\nOK\r\n"));

    CHECK_TRUE(ctx.urc_count == 0U);
    CHECK_TRUE(ctx.response_count == 1U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}

static void Test_IdleLineIsUrc(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    AT_Core_Feed(&core, (const uint8_t *)"RDY\r\n", 5U);
    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_urc, "RDY") == 0);
}

static void Test_ErrorResults(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT+BAD", 0, 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)"ERROR\r\n", 7U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ERROR);

    CHECK_TRUE(Test_Start(&core, "AT+CPIN?", "+CPIN:", 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core,
                 (const uint8_t *)"+CME ERROR: 10\r\n",
                 (uint16_t)strlen("+CME ERROR: 10\r\n"));
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ERROR);

    CHECK_TRUE(Test_Start(&core, "ATD123", 0, 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)"BUSY\r\n", 6U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ERROR);
}

static void Test_TimeoutAndBusy(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT", 0, 500U) == AT_CORE_START_OK);
    CHECK_TRUE(Test_Start(&core, "ATE0", 0, 500U) == AT_CORE_START_BUSY);

    ctx.now_ms = 499U;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    ctx.now_ms = 500U;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_PeekResult(&core) == AT_CORE_RESULT_TIMEOUT);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_TIMEOUT);
}


static void Test_TimeoutAcrossTickWrap(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    ctx.now_ms = 0xFFFFFFF0UL;
    CHECK_TRUE(Test_Start(&core, "AT", 0, 32U) == AT_CORE_START_OK);
    ctx.now_ms = 0x0000000FUL;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    ctx.now_ms = 0x00000010UL;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_TIMEOUT);
}

static void Test_TxError(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    ctx.force_tx_error = 1U;
    CHECK_TRUE(Test_Start(&core, "AT", 0, 1000U) == AT_CORE_START_TX_ERROR);
    CHECK_TRUE(AT_Core_IsBusy(&core) == 0U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_TX_ERROR);
}

static void Test_LongMiprtcpUrc(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    static char line[1120];
    uint16_t index;
    uint16_t prefix_length;

    Test_Init(&core, &ctx);
    (void)strcpy(line, "+MIPRTCP: 0,528,");
    prefix_length = (uint16_t)strlen(line);
    for (index = prefix_length; index < 1080U; index++)
    {
        line[index] = (char)(((index & 1U) != 0U) ? 'A' : '5');
    }
    line[1080] = '\r';
    line[1081] = '\n';
    line[1082] = '\0';

    AT_Core_Feed(&core, (const uint8_t *)line, 1082U);
    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strlen(ctx.last_urc) == 1080U);
    CHECK_TRUE(core.stats.line_overflow == 0U);
}

static void Test_OverflowRecovery(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    static uint8_t overlong[AT_CORE_LINE_MAX + 8U];
    uint16_t index;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT", 0, 1000U) == AT_CORE_START_OK);

    for (index = 0U; index < (uint16_t)(sizeof(overlong) - 1U); index++)
    {
        overlong[index] = (uint8_t)'X';
    }
    overlong[sizeof(overlong) - 1U] = (uint8_t)'\n';
    AT_Core_Feed(&core, overlong, (uint16_t)sizeof(overlong));
    AT_Core_Feed(&core, (const uint8_t *)"OK\r\n", 4U);

    CHECK_TRUE(core.stats.line_overflow == 1U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}



static void Test_LargeMultiLineResponseStreamsWithoutAggregateBuffer(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    char line[96];
    uint16_t index;
    uint16_t line_index;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT+CLAC", 0, 1000U) == AT_CORE_START_OK);

    /* Feed more than 1 KB total in deliberately small UART/DMA-like chunks. */
    for (line_index = 0U; line_index < 20U; line_index++)
    {
        int written;
        written = snprintf(line, sizeof(line), "+CMD%02u:", (unsigned int)line_index);
        CHECK_TRUE(written > 0);
        for (index = (uint16_t)written; index < 72U; index++)
        {
            line[index] = (char)('A' + (line_index % 26U));
        }
        line[72] = '\r';
        line[73] = '\n';
        line[74] = '\0';

        AT_Core_Feed(&core, (const uint8_t *)line, 7U);
        AT_Core_Feed(&core, (const uint8_t *)&line[7], 13U);
        AT_Core_Feed(&core, (const uint8_t *)&line[20], 54U);
    }

    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    CHECK_TRUE(ctx.response_count == 20U);
    CHECK_TRUE(core.stats.response_bytes == (20U * 72U));

    AT_Core_Feed(&core, (const uint8_t *)"OK\r\n", 4U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}

static void Test_AsyncOkIsAcceptanceNotOperationCompletion(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_Start(&core, "AT+MIPCALL=1", 0, 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)"OK\r\n", 4U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);

    /* The documented asynchronous execution result arrives later as a URC. */
    AT_Core_Feed(&core,
                 (const uint8_t *)"+MIPCALL:10.1.2.3\r\n",
                 (uint16_t)strlen("+MIPCALL:10.1.2.3\r\n"));
    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_urc, "+MIPCALL:10.1.2.3") == 0);
}

static void Test_PartialIdleUrcAcrossCommandStart(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    AT_Core_Feed(&core, (const uint8_t *)"+MIPST", 6U);
    CHECK_TRUE(Test_Start(&core, "AT", 0, 1000U) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)"AT: 1,0\r\nOK\r\n", (uint16_t)strlen("AT: 1,0\r\nOK\r\n"));

    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_urc, "+MIPSTAT: 1,0") == 0);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}

static void Test_CustomSuccessToken(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    AT_CoreCommand_T request;

    Test_Init(&core, &ctx);
    request.command = "AT+MIPSEND=0,10";
    request.response_prefix = 0;
    request.success_token = ">";
    request.timeout_ms = 1000U;
    CHECK_TRUE(AT_Core_StartCommand(&core, &request) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)">", 1U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}


static void Test_DetailedFinalCodes(void)
{
    typedef struct
    {
        const char *text;
        AT_CoreFinalCode_T code;
    } FinalCase_T;

    static const FinalCase_T cases[] =
    {
        { "ERROR\r\n", AT_CORE_FINAL_ERROR },
        { "+CME ERROR: 10\r\n", AT_CORE_FINAL_CME_ERROR },
        { "+CMS ERROR: 500\r\n", AT_CORE_FINAL_CMS_ERROR },
        { "NO CARRIER\r\n", AT_CORE_FINAL_NO_CARRIER },
        { "NO DIALTONE\r\n", AT_CORE_FINAL_NO_DIALTONE },
        { "BUSY\r\n", AT_CORE_FINAL_BUSY },
        { "NO ANSWER\r\n", AT_CORE_FINAL_NO_ANSWER }
    };
    AT_Core_T core;
    TestContext_T ctx;
    uint16_t index;

    Test_Init(&core, &ctx);
    for (index = 0U; index < (uint16_t)(sizeof(cases) / sizeof(cases[0])); index++)
    {
        CHECK_TRUE(Test_StartTransaction(&core,
                                         "ATD123",
                                         0,
                                         AT_CORE_TRANSACTION_SYNC_OK,
                                         1000U,
                                         0U,
                                         0) == AT_CORE_START_OK);
        AT_Core_Feed(&core,
                     (const uint8_t *)cases[index].text,
                     (uint16_t)strlen(cases[index].text));
        CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ERROR);
        CHECK_TRUE(AT_Core_GetLastFinalCode(&core) == cases[index].code);
    }
}

static void Test_ConnectIsAValidNonOkSuccess(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "ATD*99#",
                                     0,
                                     AT_CORE_TRANSACTION_SYNC_CONNECT,
                                     1000U,
                                     0U,
                                     0) == AT_CORE_START_OK);
    AT_Core_Feed(&core,
                 (const uint8_t *)"CONNECT 115200\r\n",
                 (uint16_t)strlen("CONNECT 115200\r\n"));
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
    CHECK_TRUE(AT_Core_GetLastFinalCode(&core) == AT_CORE_FINAL_CONNECT);
}

static void Test_PromptIsAFirstClassCompletion(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "AT+MIPSEND=1,32",
                                     0,
                                     AT_CORE_TRANSACTION_PROMPT,
                                     1000U,
                                     0U,
                                     0) == AT_CORE_START_OK);
    AT_Core_Feed(&core, (const uint8_t *)">", 1U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_PROMPT);
    CHECK_TRUE(AT_Core_GetLastFinalCode(&core) == AT_CORE_FINAL_PROMPT);
    CHECK_TRUE(core.stats.prompts == 1U);
}

static void Test_AsyncOkDoesNotCompleteTransaction(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "AT+MIPCALL=1",
                                     0,
                                     AT_CORE_TRANSACTION_ASYNC_OK,
                                     1000U,
                                     30000U,
                                     Test_MatchMipcall) == AT_CORE_START_OK);

    AT_Core_Feed(&core, (const uint8_t *)"OK\r\n", 4U);
    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    CHECK_TRUE(AT_Core_GetPhase(&core) == AT_CORE_PHASE_WAIT_ASYNC);
    CHECK_TRUE(AT_Core_PeekResult(&core) == AT_CORE_RESULT_NONE);
    CHECK_TRUE(core.stats.async_accepted == 1U);

    /* Unrelated unsolicited reports remain URCs while the operation is pending. */
    AT_Core_Feed(&core,
                 (const uint8_t *)"+CEREG: 1\r\n",
                 (uint16_t)strlen("+CEREG: 1\r\n"));
    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    CHECK_TRUE(ctx.urc_count == 1U);

    AT_Core_Feed(&core,
                 (const uint8_t *)"+MIPCALL:10.1.2.3\r\n",
                 (uint16_t)strlen("+MIPCALL:10.1.2.3\r\n"));
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ASYNC_EVENT);
    CHECK_TRUE(AT_Core_GetLastFinalCode(&core) == AT_CORE_FINAL_OK);
    CHECK_TRUE(ctx.urc_count == 2U);
    CHECK_TRUE(core.stats.async_completed == 1U);
}

static void Test_AsyncOperationHasIndependentTimeout(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "AT+MIPCALL=1",
                                     0,
                                     AT_CORE_TRANSACTION_ASYNC_OK,
                                     1000U,
                                     30000U,
                                     Test_MatchMipcall) == AT_CORE_START_OK);
    ctx.now_ms = 100U;
    AT_Core_Feed(&core, (const uint8_t *)"OK\r\n", 4U);

    ctx.now_ms = 30099U;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_IsBusy(&core) != 0U);
    ctx.now_ms = 30100U;
    AT_Core_Process(&core);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_TIMEOUT);
    CHECK_TRUE(AT_Core_GetLastFinalCode(&core) == AT_CORE_FINAL_OK);
}

static void Test_AsyncEventBeforeOkIsHandled(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "AT+MIPCALL=1",
                                     0,
                                     AT_CORE_TRANSACTION_ASYNC_OK,
                                     1000U,
                                     30000U,
                                     Test_MatchMipcall) == AT_CORE_START_OK);
    AT_Core_Feed(&core,
                 (const uint8_t *)"+MIPCALL:10.1.2.3\r\nOK\r\n",
                 (uint16_t)strlen("+MIPCALL:10.1.2.3\r\nOK\r\n"));
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_ASYNC_EVENT);
    CHECK_TRUE(ctx.urc_count == 1U);
}

static void Test_RingIsUnsolicitedDuringCommand(void)
{
    AT_Core_T core;
    TestContext_T ctx;

    Test_Init(&core, &ctx);
    CHECK_TRUE(Test_StartTransaction(&core,
                                     "AT+CSQ",
                                     "+CSQ:",
                                     AT_CORE_TRANSACTION_SYNC_OK,
                                     1000U,
                                     0U,
                                     0) == AT_CORE_START_OK);
    AT_Core_Feed(&core,
                 (const uint8_t *)"RING\r\n+CSQ: 18,99\r\nOK\r\n",
                 (uint16_t)strlen("RING\r\n+CSQ: 18,99\r\nOK\r\n"));
    CHECK_TRUE(ctx.urc_count == 1U);
    CHECK_TRUE(strcmp(ctx.last_urc, "RING") == 0);
    CHECK_TRUE(ctx.response_count == 1U);
    CHECK_TRUE(AT_Core_TakeResult(&core) == AT_CORE_RESULT_OK);
}

static void Test_RawWriteUsesTransportWithoutFraming(void)
{
    AT_Core_T core;
    TestContext_T ctx;
    static const uint8_t payload[] = { '1', '2', '3', '4' };

    Test_Init(&core, &ctx);
    CHECK_TRUE(AT_Core_WriteRaw(&core, payload, (uint16_t)sizeof(payload)) == sizeof(payload));
    CHECK_TRUE(ctx.tx_length == sizeof(payload));
    CHECK_TRUE(memcmp(ctx.tx, payload, sizeof(payload)) == 0);
}

int main(void)
{
    Test_BasicOkAndEcho();
    Test_ResponseAndUrcSeparation();
    Test_ExpectedPrefixWinsOverUrcClassifier();
    Test_IdleLineIsUrc();
    Test_ErrorResults();
    Test_TimeoutAndBusy();
    Test_TimeoutAcrossTickWrap();
    Test_TxError();
    Test_LongMiprtcpUrc();
    Test_OverflowRecovery();
    Test_LargeMultiLineResponseStreamsWithoutAggregateBuffer();
    Test_AsyncOkIsAcceptanceNotOperationCompletion();
    Test_PartialIdleUrcAcrossCommandStart();
    Test_CustomSuccessToken();
    Test_DetailedFinalCodes();
    Test_ConnectIsAValidNonOkSuccess();
    Test_PromptIsAFirstClassCompletion();
    Test_AsyncOkDoesNotCompleteTransaction();
    Test_AsyncOperationHasIndependentTimeout();
    Test_AsyncEventBeforeOkIsHandled();
    Test_RingIsUnsolicitedDuringCommand();
    Test_RawWriteUsesTransportWithoutFraming();

    if (g_failures != 0)
    {
        printf("AT core host tests: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("AT core host tests: PASS\n");
    return 0;
}
