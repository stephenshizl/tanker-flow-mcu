#ifndef FLOW_METER_BOARD_CONFIG_H
#define FLOW_METER_BOARD_CONFIG_H

/*
 * Phase-3E-F safe bring-up profile.
 *
 * The current software sources do not prove the active polarity of the two
 * RS485 control pins, DIR_FLOW, or DET_INSERT. Keep hardware activation off
 * until the schematic/bench has confirmed those levels. Setting ENABLE to 1
 * while any required level remains UNCONFIRMED is rejected by FlowMeterApp.
 */
#define FLOW_METER_BOARD_LEVEL_UNCONFIRMED       (0xFFU)
#define FLOW_METER_BOARD_BRINGUP_ENABLE          (0U)

#define FLOW_METER_BOARD_RS485_TX_ENABLE_LEVEL   FLOW_METER_BOARD_LEVEL_UNCONFIRMED
#define FLOW_METER_BOARD_RS485_TX_DISABLE_LEVEL  FLOW_METER_BOARD_LEVEL_UNCONFIRMED
#define FLOW_METER_BOARD_RS485_RX_ENABLE_LEVEL   FLOW_METER_BOARD_LEVEL_UNCONFIRMED
#define FLOW_METER_BOARD_RS485_RX_DISABLE_LEVEL  FLOW_METER_BOARD_LEVEL_UNCONFIRMED
#define FLOW_METER_BOARD_INFLOW_LEVEL            FLOW_METER_BOARD_LEVEL_UNCONFIRMED
#define FLOW_METER_BOARD_INSERT_PRESENT_LEVEL    FLOW_METER_BOARD_LEVEL_UNCONFIRMED

/* ZELZ protocol/application policy defaults; adjust during real-board bring-up. */
#define FLOW_METER_BOARD_SLAVE_ADDRESS            (1U)
#define FLOW_METER_BOARD_POWER_SETTLE_MS          (1000U)
#define FLOW_METER_BOARD_POLL_INTERVAL_MS         (1000U)
#define FLOW_METER_BOARD_RESPONSE_TIMEOUT_MS      (300U)
#define FLOW_METER_BOARD_MAX_RETRIES              (2U)

#endif /* FLOW_METER_BOARD_CONFIG_H */
