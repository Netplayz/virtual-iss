/******************************************************************************
 * CMD_HANDLER.C - Command Processing and Stored Sequence Execution
 *
 * Processes incoming 1553 command packets:
 *   - Validates checksums and addressing
 *   - Dispatches to command-specific handlers
 *   - Manages stored command sequences
 *   - Returns status responses
 *
 * Command flow:
 *   bus_parse -> cmd_validate -> cmd_process -> cmd_execute_* -> response
 ******************************************************************************/

#include "mdm.h"

/******************************************************************************
 * FORWARD DECLARATIONS
 ******************************************************************************/

static STATUS cmd_dispatch(MDM_STATE *state, CMD_PACKET *cmd);

/******************************************************************************
 * CMD_PROCESS - Top-level command processing entry point
 *
 * Validates the command and dispatches to the appropriate handler.
 * Increments counters and logs results.
 ******************************************************************************/

STATUS
cmd_process(MDM_STATE *state, CMD_PACKET *cmd)
{
    STATUS status;

    if (state == NULL || cmd == NULL) {
        return FAILURE;
    }

    state->cmd_received++;

    /* Validate command */
    status = cmd_validate(cmd);
    if (status != SUCCESS) {
        state->cmd_rejected++;
        return status;
    }

    /* Dispatch to handler */
    status = cmd_dispatch(state, cmd);
    if (status == SUCCESS) {
        state->cmd_processed++;
        state->cmd_last_id = cmd->cmd_id;
    } else {
        state->cmd_rejected++;
    }

    return status;
}

/******************************************************************************
 * CMD_DISPATCH - Command dispatch table
 *
 * Routes command codes to their respective handler functions.
 * Unknown command codes are rejected.
 ******************************************************************************/

static STATUS
cmd_dispatch(MDM_STATE *state, CMD_PACKET *cmd)
{
    switch (cmd->cmd_code) {

    case CMD_NOP:
        return cmd_execute_nop(state, cmd);

    case CMD_RESET:
        return cmd_execute_reset(state, cmd);

    case CMD_SET_MODE:
        return cmd_execute_set_mode(state, cmd);

    case CMD_SET_PARAM:
        return cmd_execute_set_param(state, cmd);

    case CMD_EXEC_SEQUENCE:
        return cmd_execute_stored(state, cmd);

    case CMD_ABORT_SEQUENCE:
        /* Abort all running sequences */
        {
            UINT8 i;
            for (i = 0; i < STORED_SEQ_MAX; i++) {
                state->stored_sequences[i].active = 0;
            }
        }
        return SUCCESS;

    case CMD_SET_TIME:
        /* Set spacecraft time - stub */
        return SUCCESS;

    case CMD_REPORT_STATUS:
        /* Status will be reported in next telemetry packet */
        state->status_flags |= 0x01;
        return SUCCESS;

    case CMD_SELF_TEST:
        return mdm_self_test(state);

    default:
        /* Unknown command code */
        state->cmd_rejected++;
        return ERR_UNDEFINED;
    }
}

/******************************************************************************
 * CMD_VALIDATE - Command packet validation
 *
 * Checks: addressing, checksum, parameter bounds
 ******************************************************************************/

STATUS
cmd_validate(CMD_PACKET *cmd)
{
    UINT8   checksum;
    UINT16  i;

    if (cmd == NULL) {
        return FAILURE;
    }

    /* Command ID must be valid */
    if (cmd->cmd_id == CMD_ID_INVALID) {
        return ERR_CMD;
    }

    /* Validate checksum */
    checksum = 0;
    {
        UINT8 *bytes = (UINT8 *)cmd;
        /* Skip checksum byte itself */
        for (i = 0; i < sizeof(CMD_PACKET); i++) {
            if (i != 6) { /* offset of cmd_checksum */
                checksum += bytes[i];
            }
        }
    }
    checksum = (UINT8)(~checksum + 1); /* Two's complement */

    if (checksum != cmd->cmd_checksum) {
        return ERR_CHECKSUM;
    }

    return SUCCESS;
}

/******************************************************************************
 * CMD_EXECUTE_NOP - No operation (used for bus timing verification)
 ******************************************************************************/

STATUS
cmd_execute_nop(MDM_STATE *state, CMD_PACKET *cmd)
{
    (void)state;
    (void)cmd;

    /* Consume exactly NOP-level CPU time for bus timing calibration */
    volatile UINT32 delay;
    for (delay = 0; delay < 100; delay++) {
        /* Busy wait for bus timing sync */
    }

    return SUCCESS;
}

/******************************************************************************
 * CMD_EXECUTE_RESET - Software reset
 ******************************************************************************/

STATUS
cmd_execute_reset(MDM_STATE *state, CMD_PACKET *cmd)
{
    UINT8 reset_type;

    if (cmd->cmd_params[0] == 0) {
        /* Warm reset - preserve state */
        state->mdm_mode = MDM_MODE_BOOT;
        state->mdm_mode = MDM_MODE_OPERATIONAL;
        state->tick_count = 0;
    } else {
        /* Cold reset - full re-initialization */
        mdm_reset(state);
    }

    reset_type = cmd->cmd_params[0];
    (void)reset_type;

    return SUCCESS;
}

/******************************************************************************
 * CMD_EXECUTE_SET_MODE - Change MDM operating mode
 ******************************************************************************/

STATUS
cmd_execute_set_mode(MDM_STATE *state, CMD_PACKET *cmd)
{
    UINT8 requested_mode;

    requested_mode = cmd->cmd_params[0];

    switch (requested_mode) {

    case MDM_MODE_IDLE:
    case MDM_MODE_OPERATIONAL:
    case MDM_MODE_SAFE:
    case MDM_MODE_DIAGNOSTIC:
        state->mdm_mode = requested_mode;
        return SUCCESS;

    default:
        return ERR_CMD;
    }
}

/******************************************************************************
 * CMD_EXECUTE_SET_PARAM - Set a configuration parameter
 *
 * Parameter format:
 *   param[0] = parameter ID
 *   param[1-7] = parameter value(s)
 ******************************************************************************/

STATUS
cmd_execute_set_param(MDM_STATE *state, CMD_PACKET *cmd)
{
    UINT8 param_id;
    UINT8 param_value;

    param_id    = cmd->cmd_params[0];
    param_value = cmd->cmd_params[1];

    (void)state;

    switch (param_id) {

    case 0x01: /* Bus retry count */
        if (param_value <= BUS_RETRY_MAX) {
            state->bus_retry_count = param_value;
        }
        break;

    case 0x02: /* Telemetry rate divisor */
        if (param_value >= 1 && param_value <= 100) {
            /* tlm_rate_divisor = param_value; */ /* would set global */
        }
        break;

    case 0x03: /* Default timeout */
        /* timeout = param_value; */ /* would set global */
        break;

    default:
        return ERR_CMD;
    }

    return SUCCESS;
}

/******************************************************************************
 * CMD_EXECUTE_STORED - Load and execute a stored command sequence
 *
 * Sequences are pre-loaded and triggered by ID.
 * Each sequence contains up to STORED_CMD_MAX commands with step delays.
 ******************************************************************************/

STATUS
cmd_execute_stored(MDM_STATE *state, CMD_PACKET *cmd)
{
    UINT8  seq_id;
    UINT8  seq_slot;
    UINT8  i;
    UINT8  found;

    seq_id = cmd->cmd_params[0];

    /* Find existing sequence or allocate new slot */
    found = 0;
    seq_slot = 0;

    for (i = 0; i < STORED_SEQ_MAX; i++) {
        if (state->stored_sequences[i].sequence_id == seq_id &&
            state->stored_sequences[i].active) {
            /* Sequence already loaded, trigger execution */
            seq_slot = i;
            found = 1;
            break;
        }
        if (!state->stored_sequences[i].active && !found) {
            seq_slot = i;
            found = 2; /* mark as available slot */
        }
    }

    if (found == 0) {
        /* All slots full */
        return ERR_BUSY;
    }

    if (found == 2) {
        /* Load new sequence */
        STORED_SEQUENCE *seq = &state->stored_sequences[seq_slot];

        seq->active      = 1;
        seq->sequence_id = seq_id;
        seq->cmd_count   = cmd->cmd_params[1];
        seq->current_step = 0;

        if (seq->cmd_count > STORED_CMD_MAX) {
            seq->cmd_count = STORED_CMD_MAX;
        }

        /* Parse command list from parameters */
        for (i = 0; i < seq->cmd_count && i < STORED_CMD_MAX; i++) {
            UINT8 idx = 2 + (i * 10);
            if ((UINT16)(idx + 9) < sizeof(cmd->cmd_params)) {
                seq->cmd_codes[i]    = cmd->cmd_params[idx];
                seq->step_delay_us[i] =
                    ((UINT32)cmd->cmd_params[idx + 1] << 24) |
                    ((UINT32)cmd->cmd_params[idx + 2] << 16) |
                    ((UINT32)cmd->cmd_params[idx + 3] << 8)  |
                    (UINT32)cmd->cmd_params[idx + 4];
                (void)memcpy(seq->cmd_params[i],
                             &cmd->cmd_params[idx + 5], 8);
            }
        }

        seq->time_started = state->uptime_seconds;
    }

    return SUCCESS;
}

/******************************************************************************
 * CMD_STORED_SEQUENCE_STEP - Advance stored command sequences
 *
 * Called each 100ms tick by the scheduler.
 * Steps each active sequence and executes commands when delays expire.
 ******************************************************************************/

STATUS
cmd_stored_sequence_step(MDM_STATE *state)
{
    UINT8  i;

    if (state == NULL) {
        return FAILURE;
    }

    for (i = 0; i < STORED_SEQ_MAX; i++) {
        STORED_SEQUENCE *seq = &state->stored_sequences[i];

        if (!seq->active) {
            continue;
        }

        /* Check if current step's delay has expired */
        if (seq->current_step < seq->cmd_count) {
            UINT32 elapsed = state->uptime_seconds - seq->time_started;

            if (elapsed >= seq->step_delay_us[seq->current_step] / 1000000) {
                /* Execute current step */
                CMD_PACKET step_cmd;
                (void)memset(&step_cmd, 0, sizeof(step_cmd));

                step_cmd.cmd_id   = (UINT32)(seq->sequence_id << 16) |
                                    (UINT32)seq->current_step;
                step_cmd.cmd_code = seq->cmd_codes[seq->current_step];
                (void)memcpy(step_cmd.cmd_params,
                             seq->cmd_params[seq->current_step], 8);

                (void)cmd_process(state, &step_cmd);

                seq->current_step++;
            }
        }

        /* Check if sequence is complete */
        if (seq->current_step >= seq->cmd_count) {
            seq->active = 0;
        }
    }

    return SUCCESS;
}
/******************************************************************************/
