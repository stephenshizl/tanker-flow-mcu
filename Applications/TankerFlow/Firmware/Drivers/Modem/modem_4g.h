#ifndef MODEM_4G_H
#define MODEM_4G_H

#include <stdint.h>

#include "at_core.h"

typedef struct
{
    uint32_t urc_count;
    uint32_t miprtcp_count;
    uint32_t mipstat_count;
    uint32_t network_urc_count;
} Modem4G_Stats_T;

void Modem4G_Init(void);
void Modem4G_Process(void);
AT_CoreStartResult_T Modem4G_StartCommand(const char *command,
                                           const char *response_prefix,
                                           const char *success_token,
                                           uint32_t timeout_ms);
uint8_t Modem4G_IsBusy(void);
AT_CoreResult_T Modem4G_PeekResult(void);
AT_CoreResult_T Modem4G_TakeResult(char *response, uint16_t response_capacity);
const AT_CoreStats_T *Modem4G_GetAtStats(void);
void Modem4G_GetStats(Modem4G_Stats_T *stats);
uint16_t Modem4G_GetLastUrc(char *dst, uint16_t capacity);

#endif /* MODEM_4G_H */
