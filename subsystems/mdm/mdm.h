/******************************************************************************
 * MDM.H - Multiplexer/Demultiplexer Flight Software Header
 *
 * International Space Station Command & Data Handling MDM
 * MIL-STD-1750A / Intel 80386EX Target
 *
 * (c) NASA Johnson Space Center, 1993
 * All Rights Reserved
 ******************************************************************************/

#ifndef MDM_H
#define MDM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>

/******************************************************************************
 * TYPES - Standard MIL-STD-1750A type definitions
 ******************************************************************************/

typedef unsigned char      UINT8;
typedef signed char        INT8;
typedef unsigned short     UINT16;
typedef signed short       INT16;
typedef unsigned long      UINT32;
typedef signed long        INT32;
typedef float              FLOAT32;
typedef double             FLOAT64;
typedef unsigned long long UINT64;
typedef long long          INT64;

typedef UINT8              STATUS;
typedef UINT16             ADDRESS;
typedef UINT32             WORD32;
typedef UINT16             WORD16;
typedef UINT8              BYTE;

/******************************************************************************
 * STATUS CODES
 ******************************************************************************/

#define SUCCESS             ((STATUS)0x00)
#define FAILURE             ((STATUS)0x01)
#define ERR_BUS             ((STATUS)0x02)
#define ERR_CMD             ((STATUS)0x03)
#define ERR_CRC             ((STATUS)0x04)
#define ERR_TIMEOUT         ((STATUS)0x05)
#define ERR_NOMEM           ((STATUS)0x06)
#define ERR_BUSY            ((STATUS)0x07)
#define ERR_CHECKSUM        ((STATUS)0x08)
#define ERR_OVERFLOW        ((STATUS)0x09)
#define ERR_UNDEFINED       ((STATUS)0xFF)

/******************************************************************************
 * MIL-STD-1553 CONSTANTS
 ******************************************************************************/

#define RT_ADDR_MIN          ((UINT8)0x01)
#define RT_ADDR_MAX          ((UINT8)0x1E)
#define BC_ADDR              ((UINT8)0x00)
#define BUS_MSG_MAX          ((UINT16)32)
#define BUS_WORD_SIZE        ((UINT16)16)
#define BUS_DATA_MAX         ((UINT16)32)
#define BUS_RETRY_MAX        ((UINT8)3)
#define BUS_TIMEOUT_US       ((UINT32)1000)

#define MSG_TYPE_BC_TO_RT    ((UINT8)0x01)
#define MSG_TYPE_RT_TO_BC    ((UINT8)0x02)
#define MSG_TYPE_RT_TO_RT    ((UINT8)0x03)
#define MSG_TYPE_BC_CMD      ((UINT8)0x04)

/* 1553 command word bits */
#define CMD_TX_BIT           ((UINT16)0x0400)
#define CMD_RT_ADDR_SHIFT    ((UINT16)11)
#define CMD_SUBADDR_SHIFT    ((UINT16)5)
#define CMD_WORD_COUNT_MASK  ((UINT16)0x001F)

/******************************************************************************
 * PACKED STRUCTURE SUPPORT - for 1553 bus message layout
 ******************************************************************************/

#ifdef __GNUC__
#define PACKED              __attribute__((packed))
#else
#define PACKED
#pragma pack(push, 1)
#endif

/******************************************************************************
 * 1553 BUS MESSAGE STRUCTURES
 ******************************************************************************/

typedef struct PACKED {
    UINT16  command_word;
    UINT16  status_word;
    UINT16  data_words[BUS_DATA_MAX];
    UINT16  word_count;
    UINT8   retry_count;
    UINT8   message_type;
    UINT8   bus_number;
    UINT8   flags;
} PACKED BUS_MESSAGE;

#define BUS_MSG_SIZE        sizeof(BUS_MESSAGE)

typedef struct PACKED {
    UINT16  sync_pattern;        /* 0xEB90 */
    UINT16  command_word;
    UINT16  status_word;
    UINT16  data_word_count;
    UINT16  data[BUS_DATA_MAX];
    UINT16  crc;
} PACKED BUS_FRAME;

#define BUS_FRAME_HEADER    6   /* sync + cmd + status + count = 4 words */
#define BUS_FRAME_OVERHEAD  7   /* header + crc */

/******************************************************************************
 * TELEMETRY PACKET STRUCTURE - CCSDS-style
 ******************************************************************************/

typedef struct PACKED {
    unsigned int apid             : 11;
    unsigned int sequence_count   : 14;
    unsigned int packet_length    : 16;
    unsigned int version          : 3;
    unsigned int type             : 1;
    unsigned int secondary_header : 1;
    unsigned int sequence_flags   : 2;
} PACKED TLM_PRIMARY_HEADER;

#define TLM_HEADER_SIZE     sizeof(TLM_PRIMARY_HEADER)

typedef struct PACKED {
    UINT32  timestamp_secs;
    UINT16  timestamp_subsecs;
    UINT8   subsystem_id;
    UINT8   packet_type;
} PACKED TLM_SECONDARY_HEADER;

typedef struct PACKED {
    TLM_PRIMARY_HEADER    primary;
    TLM_SECONDARY_HEADER  secondary;
    UINT8                 data[256];
    UINT16                crc;
} PACKED TLM_PACKET;

#define TLM_PACKET_MAX      sizeof(TLM_PACKET)
#define TLM_DATA_MAX        256

/******************************************************************************
 * COMMAND PACKET STRUCTURE
 ******************************************************************************/

typedef struct PACKED {
    UINT32  cmd_id;
    UINT8   cmd_code;
    UINT8   cmd_source;
    UINT8   cmd_priority;
    UINT8   cmd_checksum;
    UINT8   cmd_params[64];
} PACKED CMD_PACKET;

#define CMD_ID_INVALID       ((UINT32)0xFFFFFFFF)

/* Command codes */
#define CMD_NOP              ((UINT8)0x00)
#define CMD_RESET            ((UINT8)0x01)
#define CMD_SET_MODE         ((UINT8)0x02)
#define CMD_SET_PARAM        ((UINT8)0x03)
#define CMD_EXEC_SEQUENCE    ((UINT8)0x04)
#define CMD_ABORT_SEQUENCE   ((UINT8)0x05)
#define CMD_SET_TIME         ((UINT8)0x06)
#define CMD_REPORT_STATUS    ((UINT8)0x07)
#define CMD_LOAD_MEMORY      ((UINT8)0x08)
#define CMD_DUMP_MEMORY      ((UINT8)0x09)
#define CMD_SELF_TEST        ((UINT8)0x0A)
#define CMD_POWER_CYCLE      ((UINT8)0x0B)
#define CMD_LOAD_STORED      ((UINT8)0x10)
#define CMD_EXEC_STORED      ((UINT8)0x11)
#define CMD_CLEAR_STORED     ((UINT8)0x12)

/******************************************************************************
 * MDM EXECUTIVE CONSTANTS
 ******************************************************************************/

#define TASK_10MS_TICK       ((UINT32)10000)
#define TASK_100MS_TICK      ((UINT32)100000)
#define TASK_1S_TICK         ((UINT32)1000000)
#define TASK_10S_TICK        ((UINT32)10000000)

#define TASK_SLOT_MAX        ((UINT8)32)
#define CYCLE_TASK_COUNT     ((UINT8)16)
#define AP_TASK_COUNT        ((UINT8)16)

#define MDM_MODE_IDLE        ((UINT8)0x00)
#define MDM_MODE_OPERATIONAL ((UINT8)0x01)
#define MDM_MODE_SAFE        ((UINT8)0x02)
#define MDM_MODE_DIAGNOSTIC  ((UINT8)0x03)
#define MDM_MODE_BOOT        ((UINT8)0x04)

#define STORED_SEQ_MAX       ((UINT8)16)
#define STORED_CMD_MAX       ((UINT8)32)

/******************************************************************************
 * STORED COMMAND SEQUENCE
 ******************************************************************************/

typedef struct {
    unsigned int active      : 1;
    unsigned int sequence_id : 4;
    unsigned int cmd_count   : 6;
    unsigned int current_step : 6;
    UINT8      cmd_codes[STORED_CMD_MAX];
    UINT8      cmd_params[STORED_CMD_MAX][8];
    UINT32     step_delay_us[STORED_CMD_MAX];
    UINT32     time_started;
} PACKED STORED_SEQUENCE;

/******************************************************************************
 * MDM STATE
 ******************************************************************************/

typedef struct {
    /* Core state */
    UINT8      mdm_id;
    UINT8      mdm_mode;
    UINT8      bus_address;
    UINT8      bus_count;
    UINT8      major_version;
    UINT8      minor_version;
    UINT8      patch_version;

    /* Timing */
    UINT32     uptime_seconds;
    UINT32     uptime_milliseconds;
    UINT32     tick_count;
    UINT64     time_start;

    /* 1553 bus */
    BUS_MESSAGE bus_rx_queue[BUS_MSG_MAX];
    BUS_MESSAGE bus_tx_queue[BUS_MSG_MAX];
    UINT8       bus_rx_head;
    UINT8       bus_rx_tail;
    UINT8       bus_tx_head;
    UINT8       bus_tx_tail;
    UINT8       bus_error_count;
    UINT8       bus_retry_count;

    /* Commands */
    UINT32     cmd_received;
    UINT32     cmd_processed;
    UINT32     cmd_rejected;
    UINT32     cmd_last_id;

    /* Telemetry */
    UINT32     tlm_packets_sent;
    UINT32     tlm_bytes_sent;
    UINT16     tlm_sequence;

    /* Stored sequences */
    STORED_SEQUENCE stored_sequences[STORED_SEQ_MAX];

    /* Status */
    UINT8      status_flags;
    UINT8      error_flags;
    UINT8      warning_flags;
    UINT8      cpu_load_pct;
    UINT16     stack_remaining;
    UINT16     heap_remaining;
} MDM_STATE;

/******************************************************************************
 * FUNCTION DECLARATIONS
 ******************************************************************************/

/* mdm_core.c */
void     mdm_init(MDM_STATE *state, UINT8 mdm_id, UINT8 bus_addr);
void     mdm_main_loop(MDM_STATE *state);
void     mdm_shutdown(MDM_STATE *state, UINT8 reason);
void     mdm_reset(MDM_STATE *state);
STATUS   mdm_self_test(MDM_STATE *state);

/* bus_1553.c */
STATUS   bus_init(UINT8 bus_number);
STATUS   bus_send_message(BUS_MESSAGE *msg);
STATUS   bus_receive_message(BUS_MESSAGE *msg);
STATUS   bus_transmit_frame(BUS_FRAME *frame);
STATUS   bus_receive_frame(BUS_FRAME *frame);
UINT16   bus_calculate_crc(UINT16 *data, UINT16 length);
STATUS   bus_validate_frame(BUS_FRAME *frame);
void     bus_parse_command_word(UINT16 cmd_word, UINT8 *rt_addr,
                                UINT8 *subaddr, UINT8 *tx, UINT16 *word_cnt);
UINT8    bus_get_error_count(UINT8 bus_number);
UINT16   bus_get_message_count(UINT8 bus_number);
UINT8    bus_is_active(UINT8 bus_number);

/* cmd_handler.c */
STATUS   cmd_process(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_validate(CMD_PACKET *cmd);
STATUS   cmd_execute_nop(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_execute_reset(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_execute_set_mode(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_execute_set_param(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_execute_stored(MDM_STATE *state, CMD_PACKET *cmd);
STATUS   cmd_stored_sequence_step(MDM_STATE *state);

/* tlm_pack.c */
STATUS   tlm_build_packet(MDM_STATE *state, TLM_PACKET *packet,
                          UINT8 subsystem_id, UINT8 packet_type,
                          UINT8 *data, UINT16 data_length);
STATUS   tlm_send_packet(MDM_STATE *state, TLM_PACKET *packet);
UINT16   tlm_calculate_crc(UINT8 *data, UINT16 length);
void     tlm_format_state(MDM_STATE *state, UINT8 *buffer, UINT16 *length);
void     tlm_format_1553_status(MDM_STATE *state, UINT8 *buffer, UINT16 *length);
void     tlm_format_health(MDM_STATE *state, UINT8 *buffer, UINT16 *length);

/* cycle tasks */
void     cycle_10ms(MDM_STATE *state);
void     cycle_100ms(MDM_STATE *state);
void     cycle_1s(MDM_STATE *state);
void     cycle_10s(MDM_STATE *state);

/******************************************************************************
 * EXTERNAL INTERFACE - for NATS bridge
 ******************************************************************************/

extern MDM_STATE g_mdm_state;

#ifdef __cplusplus
extern "C" {
#endif

void     mdm_process_stdin(void);
void     mdm_output_tlm(void);

#ifdef __cplusplus
}
#endif

#endif /* MDM_H */
/******************************************************************************/
