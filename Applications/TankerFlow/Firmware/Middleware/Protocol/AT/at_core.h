#ifndef AT_CORE_H
#define AT_CORE_H

#include <stdint.h>

/*
 * Portable, line-oriented AT transaction core.
 *
 * Design constraints for small MCUs:
 * - one transaction in flight;
 * - no malloc and no aggregate command-response buffer;
 * - response/URC lines are streamed immediately to callbacks;
 * - synchronous, prompt and asynchronous command transactions are distinct;
 * - unsolicited URCs are accepted even when no command is active.
 *
 * AT_CORE_LINE_MAX is a per-line limit, not a whole-response limit.  A command
 * may therefore return arbitrarily many normal lines without consuming a
 * matching amount of RAM.  Very long single-line/raw TCP payloads are a
 * separate data-plane problem and are intentionally not buffered here.
 */
#define AT_CORE_LINE_MAX          (1200U)
#define AT_CORE_COMMAND_MAX       (96U)
#define AT_CORE_PREFIX_MAX        (48U)

typedef enum
{
    AT_CORE_RESULT_NONE = 0,
    AT_CORE_RESULT_OK,
    AT_CORE_RESULT_ERROR,
    AT_CORE_RESULT_TIMEOUT,
    AT_CORE_RESULT_TX_ERROR,
    AT_CORE_RESULT_PROMPT,
    AT_CORE_RESULT_ASYNC_EVENT
} AT_CoreResult_T;

typedef enum
{
    AT_CORE_FINAL_NONE = 0,
    AT_CORE_FINAL_OK,
    AT_CORE_FINAL_CONNECT,
    AT_CORE_FINAL_ERROR,
    AT_CORE_FINAL_CME_ERROR,
    AT_CORE_FINAL_CMS_ERROR,
    AT_CORE_FINAL_NO_CARRIER,
    AT_CORE_FINAL_NO_DIALTONE,
    AT_CORE_FINAL_BUSY,
    AT_CORE_FINAL_NO_ANSWER,
    AT_CORE_FINAL_PROMPT,
    AT_CORE_FINAL_CUSTOM
} AT_CoreFinalCode_T;

typedef enum
{
    AT_CORE_PHASE_IDLE = 0,
    AT_CORE_PHASE_WAIT_RESPONSE,
    AT_CORE_PHASE_WAIT_ASYNC
} AT_CorePhase_T;

/*
 * Transaction types used by the new API.
 *
 * SYNC_OK:
 *   response line(s) -> OK / error final result.
 *
 * SYNC_CONNECT:
 *   response line(s) -> CONNECT / error final result.  Intended for commands
 *   which enter an online data mode.  The raw online data plane itself is not
 *   handled by this control-plane parser.
 *
 * PROMPT:
 *   command -> '>' prompt / error.  The caller may then send its payload.
 *
 * ASYNC_OK:
 *   command -> OK (accepted) -> asynchronous result event / timeout.
 *   The asynchronous event is identified by async_match().  Its business
 *   success/failure semantics are left to the modem driver.
 */
typedef enum
{
    AT_CORE_TRANSACTION_SYNC_OK = 0,
    AT_CORE_TRANSACTION_SYNC_CONNECT,
    AT_CORE_TRANSACTION_PROMPT,
    AT_CORE_TRANSACTION_ASYNC_OK
} AT_CoreTransactionType_T;

typedef enum
{
    AT_CORE_START_OK = 0,
    AT_CORE_START_BUSY,
    AT_CORE_START_INVALID,
    AT_CORE_START_TX_ERROR
} AT_CoreStartResult_T;

typedef uint16_t (*AT_CoreTxFn)(const uint8_t *data, uint16_t length, void *user);
typedef uint32_t (*AT_CoreNowMsFn)(void *user);
typedef uint8_t (*AT_CoreIsUrcFn)(const char *line, uint16_t length, void *user);
typedef uint8_t (*AT_CoreMatchFn)(const char *line, uint16_t length, void *user);
typedef void (*AT_CoreLineFn)(const char *line, uint16_t length, void *user);

/*
 * New transaction descriptor.  The structure is consumed synchronously by
 * AT_Core_StartTransaction(); pointers do not need to remain valid afterwards.
 */
typedef struct
{
    const char *command;
    const char *response_prefix;
    AT_CoreTransactionType_T type;
    uint32_t response_timeout_ms;
    uint32_t operation_timeout_ms; /* ASYNC_OK only. */
    AT_CoreMatchFn async_match;    /* ASYNC_OK only. */
} AT_CoreTransaction_T;

/*
 * Legacy descriptor retained for Phase-3D modem compatibility.  It maps to a
 * synchronous transaction.  A success_token of ">" still works; new code
 * should use AT_Core_StartTransaction() so PROMPT is reported explicitly.
 */
typedef struct
{
    const char *command;
    const char *response_prefix;
    const char *success_token;
    uint32_t timeout_ms;
} AT_CoreCommand_T;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t lines;
    uint32_t response_lines;
    uint32_t response_bytes;
    uint32_t urc_lines;
    uint32_t echo_lines;
    uint32_t commands_started;
    uint32_t commands_ok;
    uint32_t commands_error;
    uint32_t commands_timeout;
    uint32_t tx_errors;
    uint32_t prompts;
    uint32_t async_accepted;
    uint32_t async_completed;
    uint32_t line_overflow;
} AT_CoreStats_T;

typedef struct
{
    AT_CoreTxFn tx;
    AT_CoreNowMsFn now_ms;
    AT_CoreIsUrcFn is_urc;
    AT_CoreLineFn on_response;
    AT_CoreLineFn on_urc;
    void *user;

    char line[AT_CORE_LINE_MAX];
    uint16_t line_length;
    uint8_t dropping_line;
    uint8_t line_started_busy;

    char command[AT_CORE_COMMAND_MAX];
    char response_prefix[AT_CORE_PREFIX_MAX];
    char custom_success_token[AT_CORE_PREFIX_MAX];

    uint32_t phase_start_ms;
    uint32_t response_timeout_ms;
    uint32_t operation_timeout_ms;
    AT_CoreMatchFn async_match;
    AT_CoreTransactionType_T transaction_type;
    AT_CorePhase_T phase;
    uint8_t busy;
    uint8_t async_event_seen;
    uint8_t legacy_custom_result;
    AT_CoreResult_T completed_result;
    AT_CoreFinalCode_T last_final_code;
    AT_CoreStats_T stats;
} AT_Core_T;

void AT_Core_Init(AT_Core_T *core,
                  AT_CoreTxFn tx,
                  AT_CoreNowMsFn now_ms,
                  AT_CoreIsUrcFn is_urc,
                  AT_CoreLineFn on_response,
                  AT_CoreLineFn on_urc,
                  void *user);
void AT_Core_Reset(AT_Core_T *core);

AT_CoreStartResult_T AT_Core_StartTransaction(AT_Core_T *core,
                                              const AT_CoreTransaction_T *transaction);
AT_CoreStartResult_T AT_Core_StartCommand(AT_Core_T *core, const AT_CoreCommand_T *command);

/* Raw write helper for a future PROMPT/data stage; no AT framing is added. */
uint16_t AT_Core_WriteRaw(AT_Core_T *core, const uint8_t *data, uint16_t length);

void AT_Core_Feed(AT_Core_T *core, const uint8_t *data, uint16_t length);
void AT_Core_Process(AT_Core_T *core);
uint8_t AT_Core_IsBusy(const AT_Core_T *core);
AT_CorePhase_T AT_Core_GetPhase(const AT_Core_T *core);
AT_CoreFinalCode_T AT_Core_GetLastFinalCode(const AT_Core_T *core);
AT_CoreResult_T AT_Core_PeekResult(const AT_Core_T *core);
AT_CoreResult_T AT_Core_TakeResult(AT_Core_T *core);
const AT_CoreStats_T *AT_Core_GetStats(const AT_Core_T *core);

#endif /* AT_CORE_H */
