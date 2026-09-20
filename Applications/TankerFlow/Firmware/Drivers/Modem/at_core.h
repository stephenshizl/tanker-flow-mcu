#ifndef AT_CORE_H
#define AT_CORE_H

#include <stdint.h>

#define AT_CORE_LINE_MAX          (1200U)
#define AT_CORE_COMMAND_MAX       (96U)
#define AT_CORE_PREFIX_MAX        (48U)
#define AT_CORE_CAPTURE_MAX       (192U)

typedef enum
{
    AT_CORE_RESULT_NONE = 0,
    AT_CORE_RESULT_OK,
    AT_CORE_RESULT_ERROR,
    AT_CORE_RESULT_TIMEOUT,
    AT_CORE_RESULT_TX_ERROR
} AT_CoreResult_T;

typedef enum
{
    AT_CORE_START_OK = 0,
    AT_CORE_START_BUSY,
    AT_CORE_START_INVALID,
    AT_CORE_START_TX_ERROR
} AT_CoreStartResult_T;

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
    uint32_t urc_lines;
    uint32_t echo_lines;
    uint32_t commands_started;
    uint32_t commands_ok;
    uint32_t commands_error;
    uint32_t commands_timeout;
    uint32_t tx_errors;
    uint32_t line_overflow;
} AT_CoreStats_T;

typedef uint16_t (*AT_CoreTxFn)(const uint8_t *data, uint16_t length, void *user);
typedef uint32_t (*AT_CoreNowMsFn)(void *user);
typedef uint8_t (*AT_CoreIsUrcFn)(const char *line, uint16_t length, void *user);
typedef void (*AT_CoreLineFn)(const char *line, uint16_t length, void *user);

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
    char success_token[AT_CORE_PREFIX_MAX];
    char response_capture[AT_CORE_CAPTURE_MAX];

    uint32_t command_start_ms;
    uint32_t command_timeout_ms;
    uint8_t busy;
    AT_CoreResult_T completed_result;
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
AT_CoreStartResult_T AT_Core_StartCommand(AT_Core_T *core, const AT_CoreCommand_T *command);
void AT_Core_Feed(AT_Core_T *core, const uint8_t *data, uint16_t length);
void AT_Core_Process(AT_Core_T *core);
uint8_t AT_Core_IsBusy(const AT_Core_T *core);
AT_CoreResult_T AT_Core_PeekResult(const AT_Core_T *core);
AT_CoreResult_T AT_Core_TakeResult(AT_Core_T *core, char *response, uint16_t response_capacity);
const AT_CoreStats_T *AT_Core_GetStats(const AT_Core_T *core);

#endif /* AT_CORE_H */
