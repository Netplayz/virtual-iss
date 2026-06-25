/******************************************************************************
 * MDM_CORE.C - MDM Executive and Cyclic Scheduler
 *
 * ISS Command & Data Handling Multiplexer/Demultiplexer
 * Real-time executive with 10ms/100ms/1s/10s cyclic tasks.
 *
 * Target: Intel 80386EX @ 25MHz
 * OS:     VxWorks 5.3 / RTEMS 4.0
 * Bus:    MIL-STD-1553B
 *
 * Build:  gcc -O2 -Wall -Wextra -o mdm_core.o -c mdm_core.c
 ******************************************************************************/

#include "mdm.h"
#include <unistd.h>
#include <sys/time.h>

/******************************************************************************
 * GLOBALS - The MDM state machine, accessible to all modules and the bridge
 ******************************************************************************/

MDM_STATE g_mdm_state;

/******************************************************************************
 * FORWARD DECLARATIONS
 ******************************************************************************/

static void     scheduler_init(MDM_STATE *state);
static void     scheduler_dispatch(MDM_STATE *state);
static UINT64   time_get_us(void);
static void     time_delay_us(UINT32 us);
static void     watchdog_kick(void);
void     error_handler(MDM_STATE *state, UINT8 error_code);
static void     housekeeping_update(MDM_STATE *state);

/******************************************************************************
 * MDM_INIT - Initialize MDM state machine to known state
 *
 * Called once at power-on or after hard reset.
 * Sets all registers, clears queues, initializes buses.
 ******************************************************************************/

void
mdm_init(MDM_STATE *state, UINT8 mdm_id, UINT8 bus_addr)
{
    UINT8 i;

    if (state == NULL) {
        return;
    }

    /* Clear entire state structure */
    (void)memset(state, 0, sizeof(MDM_STATE));

    /* Set identification */
    state->mdm_id        = mdm_id;
    state->bus_address   = bus_addr;
    state->major_version = 4;
    state->minor_version = 2;
    state->patch_version = 1;
    state->mdm_mode      = MDM_MODE_BOOT;

    /* Clear bus queues */
    state->bus_rx_head   = 0;
    state->bus_rx_tail   = 0;
    state->bus_tx_head   = 0;
    state->bus_tx_tail   = 0;
    state->bus_error_count = 0;
    state->bus_retry_count = 0;

    /* Clear stored sequences */
    for (i = 0; i < STORED_SEQ_MAX; i++) {
        state->stored_sequences[i].active = 0;
        state->stored_sequences[i].sequence_id = 0;
        state->stored_sequences[i].cmd_count = 0;
    }

    /* Initialize timing */
    state->time_start = time_get_us();
    state->uptime_seconds = 0;
    state->uptime_milliseconds = 0;
    state->tick_count = 0;
    state->tlm_sequence = 0;
    state->cpu_load_pct = 0;

    /* Initialize buses */
    for (i = 0; i < 2; i++) {
        if (bus_init(i) != SUCCESS) {
            state->error_flags |= 0x01;
        }
    }

    /* Run self-test */
    if (mdm_self_test(state) != SUCCESS) {
        state->error_flags |= 0x02;
        state->mdm_mode = MDM_MODE_SAFE;
    } else {
        state->mdm_mode = MDM_MODE_OPERATIONAL;
    }

    /* Kick watchdog */
    watchdog_kick();

    /* Initialize scheduler */
    scheduler_init(state);
}

/******************************************************************************
 * MDM_MAIN_LOOP - Primary executive loop
 *
 * This function never returns under normal operation.
 * It runs the cyclic scheduler at the rate determined by the hardware timer.
 * Each iteration: process bus messages, dispatch tasks, output telemetry.
 ******************************************************************************/

void
mdm_main_loop(MDM_STATE *state)
{
    UINT64  loop_start;
    UINT64  loop_end;
    UINT64  loop_delta;
    UINT32  elapsed_ms;

    if (state == NULL) {
        return;
    }

    /**************************************************************************
     * MAIN EXECUTIVE LOOP
     *
     * Architecture:
     *   [1] Read 1553 bus messages
     *   [2] Process incoming commands
     *   [3] Execute cyclic tasks based on current tick
     *   [4] Output telemetry packets
     *   [5] Step stored command sequences
     *   [6] Kick watchdog
     *   [7] Sleep for remainder of 10ms frame
     **************************************************************************/

    while (1) {
        loop_start = time_get_us();

        /* [1] Service the 1553 bus */
        BUS_MESSAGE bus_msg;
        while (bus_receive_message(&bus_msg) == SUCCESS) {
            if (bus_msg.message_type == MSG_TYPE_BC_TO_RT) {
                /* Bus controller sent us a command */
                CMD_PACKET *cmd = (CMD_PACKET *)bus_msg.data_words;
                (void)cmd_process(state, cmd);
            } else if (bus_msg.message_type == MSG_TYPE_RT_TO_BC) {
                /* Response requested - queue telemetry on bus */
                (void)bus_send_message(&bus_msg);
            }
        }

        /* [2] Process incoming commands from stdin (bridge interface) */
        mdm_process_stdin();

        /* [3] Execute cyclic scheduler */
        scheduler_dispatch(state);

        /* [4] Send telemetry */
        mdm_output_tlm();

        /* [5] Step stored command sequences */
        (void)cmd_stored_sequence_step(state);

        /* [6] Update housekeeping */
        housekeeping_update(state);

        /* [7] Watchdog kick */
        watchdog_kick();

        /* [8] Maintain 10ms cadence */
        loop_end = time_get_us();
        loop_delta = loop_end - loop_start;
        if (loop_delta < TASK_10MS_TICK) {
            time_delay_us((UINT32)(TASK_10MS_TICK - loop_delta));
        }

        /* Update tick counter */
        state->tick_count++;
        elapsed_ms = (UINT32)((time_get_us() - state->time_start) / 1000);
        state->uptime_milliseconds = elapsed_ms % 1000;
        state->uptime_seconds = elapsed_ms / 1000;
    }
}

/******************************************************************************
 * SCHEDULER_INIT - Initialize cyclic task timing
 ******************************************************************************/

static void
scheduler_init(MDM_STATE *state)
{
    (void)state;
    /* Timing is handled by tick counting in main loop */
}

/******************************************************************************
 * SCHEDULER_DISPATCH - Dispatch cyclic tasks based on tick count
 *
 * 10ms tasks:   every tick
 * 100ms tasks:  every 10 ticks
 * 1s tasks:     every 100 ticks
 * 10s tasks:    every 1000 ticks
 ******************************************************************************/

static void
scheduler_dispatch(MDM_STATE *state)
{
    UINT64 tick;

    if (state == NULL) return;

    tick = state->tick_count;

    /* Always run 10ms tasks */
    cycle_10ms(state);

    /* 100ms tasks */
    if ((tick % 10) == 0) {
        cycle_100ms(state);
    }

    /* 1s tasks */
    if ((tick % 100) == 0) {
        cycle_1s(state);
    }

    /* 10s tasks */
    if ((tick % 1000) == 0) {
        cycle_10s(state);
    }
}

/******************************************************************************
 * CYCLE TASKS - Called at various rates by the scheduler
 ******************************************************************************/

void
cycle_10ms(MDM_STATE *state)
{
    /* Bus polling - check for new 1553 messages */
    /* Command validation - verify checksums on queued commands */
    /* Telemetry output - push high-rate data */
    (void)state;
}

void
cycle_100ms(MDM_STATE *state)
{
    /* Attitude data sampling */
    /* Power telemetry collection */
    /* Thermal sensor readback */
    /* Stored sequence advancement */
    (void)state;
}

void
cycle_1s(MDM_STATE *state)
{
    /* Health and status telemetry packet */
    /* Limit checking against red/yellow thresholds */
    /* CPU load calculation */
    /* Memory/stack usage monitoring */
    (void)state;

    /* Compute approximate CPU load */
    /* (stub: would measure idle time vs total time) */
    state->cpu_load_pct = 42;
    state->stack_remaining = 4096;
    state->heap_remaining = 16384;
}

void
cycle_10s(MDM_STATE *state)
{
    /* Periodic self-test */
    /* Bus statistics logging */
    /* Error counter reset */
    /* Watchdog confirmation */
    (void)state;

    if (state->bus_error_count > 100) {
        state->warning_flags |= 0x04;
    }
}

/******************************************************************************
 * MDM_PROCESS_STDIN - Read commands from stdin (bridge interface)
 *
 * The NATS bridge writes 1553 frames to our stdin.
 * We read them and queue them as bus messages.
 * Format: <length:2><data:length><crc:2>
 ******************************************************************************/

void
mdm_process_stdin(void)
{
    UINT8   buffer[sizeof(BUS_FRAME)];
    INT32   nread;
    UINT16  frame_len;

    /* Non-blocking read from stdin */
    /* Check if data available (simplified for simulation) */
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv) <= 0) {
        return;
    }

    /* Read frame length (2 bytes, network order) */
    nread = (INT32)read(STDIN_FILENO, buffer, 2);
    if (nread != 2) {
        return;
    }

    frame_len = (UINT16)(((UINT16)buffer[0] << 8) | (UINT16)buffer[1]);
    if (frame_len > sizeof(BUS_FRAME)) {
        return;
    }

    /* Read frame data */
    nread = (INT32)read(STDIN_FILENO, buffer, frame_len);
    if (nread != (INT32)frame_len) {
        return;
    }

    /* Parse as bus frame and queue */
    {
        BUS_FRAME *frame = (BUS_FRAME *)buffer;
        if (bus_validate_frame(frame) == SUCCESS) {
            BUS_MESSAGE msg;
            (void)memset(&msg, 0, sizeof(msg));
            msg.command_word = frame->command_word;
            msg.status_word  = frame->status_word;
            msg.word_count   = frame->data_word_count;
            msg.message_type = 
                (frame->command_word & CMD_TX_BIT) ?
                MSG_TYPE_RT_TO_BC : MSG_TYPE_BC_TO_RT;
            if (msg.word_count <= BUS_DATA_MAX) {
                (void)memcpy(msg.data_words, frame->data,
                            msg.word_count * sizeof(UINT16));
            }
            /* Queue the message */
            (void)bus_receive_message(&msg);
        }
    }
}

/******************************************************************************
 * MDM_OUTPUT_TLM - Write telemetry packets to stdout (bridge interface)
 *
 * Format: <length:2><data:length>
 ******************************************************************************/

void
mdm_output_tlm(void)
{
    /* Check if there are telemetry packets queued on the bus */
    BUS_MESSAGE msg;
    UINT8       output[sizeof(BUS_FRAME) + 2];
    UINT16      frame_len;

    while (bus_send_message(&msg) == SUCCESS) {
        /* Build a bus frame from the message */
        BUS_FRAME *frame = (BUS_FRAME *)&output[2];

        if (msg.word_count > BUS_DATA_MAX) {
            continue;
        }

        frame->sync_pattern   = 0xEB90;
        frame->command_word   = msg.command_word;
        frame->status_word    = msg.status_word;
        frame->data_word_count = msg.word_count;
        (void)memcpy(frame->data, msg.data_words,
                     msg.word_count * sizeof(UINT16));
        frame->crc = bus_calculate_crc((UINT16 *)frame,
                                       sizeof(BUS_FRAME) / 2);

        frame_len = sizeof(BUS_FRAME);
        output[0] = (UINT8)(frame_len >> 8);
        output[1] = (UINT8)(frame_len & 0xFF);

        /* Write to stdout */
        (void)fwrite(output, 1, frame_len + 2, stdout);
        (void)fflush(stdout);
    }
}

/******************************************************************************
 * MDM_SHUTDOWN - Graceful shutdown
 ******************************************************************************/

void
mdm_shutdown(MDM_STATE *state, UINT8 reason)
{
    if (state == NULL) return;

    state->mdm_mode = MDM_MODE_IDLE;
    state->error_flags |= reason;

    /* Flush any pending telemetry */
    mdm_output_tlm();

    /* Disable interrupts (simulated) */
    /* Power down sequence would go here */
}

/******************************************************************************
 * MDM_RESET - Software reset (does not re-init hardware)
 ******************************************************************************/

void
mdm_reset(MDM_STATE *state)
{
    if (state == NULL) return;

    mdm_shutdown(state, 0);
    mdm_init(state, state->mdm_id, state->bus_address);
}

/******************************************************************************
 * MDM_SELF_TEST - Power-on self-test
 *
 * Tests: CPU registers, memory, bus interface, CRC logic
 * Returns: SUCCESS if all tests pass
 ******************************************************************************/

STATUS
mdm_self_test(MDM_STATE *state)
{
    UINT8   test_pattern;
    UINT16  crc_test[4];
    UINT16  crc_result;
    STATUS  status;

    status = SUCCESS;
    (void)state;

    /* Test 1: CPU register sanity */
    {
        volatile UINT32 reg_test = 0xA5A5A5A5UL;
        if (reg_test != 0xA5A5A5A5UL) {
            status = FAILURE;
        }
        reg_test = 0x5A5A5A5AUL;
        if (reg_test != 0x5A5A5A5AUL) {
            status = FAILURE;
        }
    }

    /* Test 2: CRC calculation */
    {
        crc_test[0] = 0x1234;
        crc_test[1] = 0x5678;
        crc_test[2] = 0x9ABC;
        crc_test[3] = 0xDEF0;
        crc_result = bus_calculate_crc(crc_test, 4);
        if (crc_result == 0) {
            status = FAILURE;
        }
    }

    /* Test 3: Memory test - write/read pattern */
    {
        volatile UINT8  mem_test[64];
        for (test_pattern = 0; test_pattern < 64; test_pattern++) {
            mem_test[test_pattern] = test_pattern ^ 0xAA;
        }
        for (test_pattern = 0; test_pattern < 64; test_pattern++) {
            if (mem_test[test_pattern] != (test_pattern ^ 0xAA)) {
                status = FAILURE;
                break;
            }
        }
    }

    /* Test 4: Word count and bit operations */
    {
        UINT16 bit_test = 0x0001;
        UINT8  bit_count;
        for (bit_count = 0; bit_count < 16; bit_count++) {
            if (!(bit_test & (1 << bit_count))) {
                status = FAILURE;
                break;
            }
            bit_test = (UINT16)(bit_test << 1);
        }
    }

    return status;
}

/******************************************************************************
 * WATCHDOG_KICK - Reset watchdog timer
 ******************************************************************************/

static void
watchdog_kick(void)
{
    /* In real hardware this would write to the watchdog timer register */
    /* Example: outportb(0x64, 0xFE); */
}

/******************************************************************************
 * HOUSEKEEPING_UPDATE - Update internal metrics
 ******************************************************************************/

static void
housekeeping_update(MDM_STATE *state)
{
    /* Update status flags based on current mode and error state */
    if (state->mdm_mode == MDM_MODE_SAFE) {
        state->status_flags |= 0x80;
    } else {
        state->status_flags &= 0x7F;
    }

    /* Clear transient error flags */
    state->error_flags &= 0xF0;
}

/******************************************************************************
 * ERROR_HANDLER - Non-recoverable error handling
 ******************************************************************************/

void
error_handler(MDM_STATE *state, UINT8 error_code)
{
    state->error_flags |= error_code;

    /* If critical, transition to safe mode */
    if (error_code & 0xF0) {
        state->mdm_mode = MDM_MODE_SAFE;
    }

    /* Log error (would write to NVRAM in real system) */
}

/******************************************************************************
 * TIME FUNCTIONS - Microsecond timing using POSIX clock
 ******************************************************************************/

static UINT64
time_get_us(void)
{
    struct timespec ts;

    /* Use monotonic clock for reliable interval timing */
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((UINT64)ts.tv_sec * 1000000ULL) +
           ((UINT64)ts.tv_nsec / 1000ULL);
}

static void
time_delay_us(UINT32 us)
{
    struct timespec ts;

    ts.tv_sec  = (time_t)(us / 1000000);
    ts.tv_nsec = (long)((us % 1000000) * 1000);

    (void)nanosleep(&ts, NULL);
}
/******************************************************************************/
