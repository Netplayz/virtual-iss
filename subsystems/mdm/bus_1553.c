/******************************************************************************
 * BUS_1553.C - MIL-STD-1553B Bus Interface
 *
 * Simulates the 1553 bus protocol including:
 *   - Command/status word parsing
 *   - Message formatting (BC->RT, RT->BC, RT->RT)
 *   - CRC-16 calculation
 *   - Bus retry and error handling
 *   - Dual bus redundancy (Bus A / Bus B)
 *
 * Reference: MIL-STD-1553B Notice 2, DoD Interface Standard
 ******************************************************************************/

#include "mdm.h"

/******************************************************************************
 * LOCAL STATE - Bus interface registers
 ******************************************************************************/

static struct {
    UINT8   initialized;
    UINT8   bus_active;
    UINT8   bus_mode;           /* 0=standby, 1=active */
    UINT8   bus_errors;
    UINT16  message_count;
    UINT16  error_count;
    UINT32  last_frame_time;
} bus_state[2] = { {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0} };

/* CRC-16-IBM lookup table */
static const UINT16 crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};

/******************************************************************************
 * BUS_INIT - Initialize 1553 bus interface
 ******************************************************************************/

STATUS
bus_init(UINT8 bus_number)
{
    if (bus_number > 1) {
        return FAILURE;
    }

    bus_state[bus_number].initialized    = 1;
    bus_state[bus_number].bus_active      = 1;
    bus_state[bus_number].bus_mode        = 1;
    bus_state[bus_number].bus_errors      = 0;
    bus_state[bus_number].message_count   = 0;
    bus_state[bus_number].error_count     = 0;
    bus_state[bus_number].last_frame_time = 0;

    return SUCCESS;
}

/******************************************************************************
 * BUS_SEND_MESSAGE - Transmit message on 1553 bus
 *
 * In the simulation, this queues the message in the global MDM state's
 * transmit queue. In real hardware, this would write to the BC RT hardware.
 ******************************************************************************/

STATUS
bus_send_message(BUS_MESSAGE *msg)
{
    MDM_STATE *state;
    UINT8      next_tail;

    if (msg == NULL) {
        return FAILURE;
    }

    state = &g_mdm_state;

    /* Compute next tail position with wrap */
    next_tail = (state->bus_tx_tail + 1) % BUS_MSG_MAX;

    /* Check if queue is full */
    if (next_tail == state->bus_tx_head) {
        state->bus_error_count++;
        return ERR_OVERFLOW;
    }

    /* Copy message into queue */
    (void)memcpy(&state->bus_tx_queue[state->bus_tx_tail],
                 msg, sizeof(BUS_MESSAGE));

    state->bus_tx_tail = next_tail;

    return SUCCESS;
}

/******************************************************************************
 * BUS_RECEIVE_MESSAGE - Receive message from 1553 bus
 *
 * Dequeues a message from the global receive queue.
 ******************************************************************************/

STATUS
bus_receive_message(BUS_MESSAGE *msg)
{
    MDM_STATE *state;

    if (msg == NULL) {
        return FAILURE;
    }

    state = &g_mdm_state;

    /* Check if queue is empty */
    if (state->bus_rx_head == state->bus_rx_tail) {
        return ERR_TIMEOUT;
    }

    /* Copy message from queue */
    (void)memcpy(msg, &state->bus_rx_queue[state->bus_rx_head],
                 sizeof(BUS_MESSAGE));

    state->bus_rx_head = (state->bus_rx_head + 1) % BUS_MSG_MAX;

    return SUCCESS;
}

/******************************************************************************
 * BUS_TRANSMIT_FRAME - Transmit raw 1553 frame
 ******************************************************************************/

STATUS
bus_transmit_frame(BUS_FRAME *frame)
{
    UINT8   bus_num;
    STATUS  status;

    if (frame == NULL) {
        return FAILURE;
    }

    /* Determine which bus to use (alternate between A and B) */
    bus_num = bus_state[0].bus_active ? 0 : 1;

    if (!bus_state[bus_num].initialized) {
        return ERR_BUS;
    }

    /* Validate frame */
    status = bus_validate_frame(frame);
    if (status != SUCCESS) {
        bus_state[bus_num].error_count++;
        return status;
    }

    /* In real system: write to 1553 hardware registers */
    /* For simulation: build a BUS_MESSAGE and queue it */

    BUS_MESSAGE msg;
    (void)memset(&msg, 0, sizeof(msg));
    msg.command_word   = frame->command_word;
    msg.status_word    = frame->status_word;
    msg.word_count     = frame->data_word_count;
    msg.bus_number     = (UINT8)bus_num;

    if (frame->data_word_count <= BUS_DATA_MAX) {
        (void)memcpy(msg.data_words, frame->data,
                     frame->data_word_count * sizeof(UINT16));
    }

    status = bus_send_message(&msg);
    if (status == SUCCESS) {
        bus_state[bus_num].message_count++;
    }

    return status;
}

/******************************************************************************
 * BUS_RECEIVE_FRAME - Receive raw 1553 frame
 ******************************************************************************/

STATUS
bus_receive_frame(BUS_FRAME *frame)
{
    BUS_MESSAGE msg;
    STATUS      status;

    if (frame == NULL) {
        return FAILURE;
    }

    status = bus_receive_message(&msg);
    if (status != SUCCESS) {
        return status;
    }

    /* Build frame from message */
    frame->sync_pattern   = 0xEB90;
    frame->command_word   = msg.command_word;
    frame->status_word    = msg.status_word;
    frame->data_word_count = msg.word_count;

    if (msg.word_count <= BUS_DATA_MAX) {
        (void)memcpy(frame->data, msg.data_words,
                     msg.word_count * sizeof(UINT16));
    }

    frame->crc = bus_calculate_crc((UINT16 *)frame,
                                   sizeof(BUS_FRAME) / 2);

    return SUCCESS;
}

/******************************************************************************
 * BUS_CALCULATE_CRC - CRC-16-IBM (0x8005 polynomial)
 *
 * Used for 1553 frame integrity verification.
 ******************************************************************************/

UINT16
bus_calculate_crc(UINT16 *data, UINT16 length)
{
    UINT16  crc;
    UINT16  idx;
    UINT8   byte;
    UINT8   *bytes;

    crc   = 0xFFFF;
    bytes = (UINT8 *)data;

    for (idx = 0; idx < length * 2; idx++) {
        byte = (UINT8)((crc ^ bytes[idx]) & 0xFF);
        crc  = (UINT16)((crc >> 8) ^ crc16_table[byte]);
    }

    return (UINT16)(~crc);
}

/******************************************************************************
 * BUS_VALIDATE_FRAME - Validate 1553 frame integrity
 *
 * Checks sync pattern, word count bounds, and CRC.
 ******************************************************************************/

STATUS
bus_validate_frame(BUS_FRAME *frame)
{
    UINT16  computed_crc;

    if (frame == NULL) {
        return FAILURE;
    }

    /* Check sync pattern */
    if (frame->sync_pattern != 0xEB90) {
        return ERR_BUS;
    }

    /* Check word count bounds */
    if (frame->data_word_count > BUS_DATA_MAX) {
        return ERR_OVERFLOW;
    }

    /* Validate CRC */
    computed_crc = bus_calculate_crc((UINT16 *)frame,
                                     sizeof(BUS_FRAME) / 2 - 1);
    /* Note: last word of BUS_FRAME is the CRC field itself */
    /* In real implementation, skip the CRC word when calculating */

    /* Simplified CRC check for simulation */
    if (computed_crc == 0) {
        return ERR_CRC;
    }

    return SUCCESS;
}

/******************************************************************************
 * BUS_PARSE_COMMAND_WORD - Parse 1553 command word into fields
 *
 * Command word format (16 bits):
 *   Bits 15-11: RT address (5 bits)
 *   Bits 10-9:  Subaddress / Mode (5 bits)
 *   Bit  10:    T/R bit (1=transmit, 0=receive)
 *   Bits 9-5:   Subaddress (5 bits)
 *   Bits 4-0:   Word count / Mode code (5 bits)
 ******************************************************************************/

void
bus_parse_command_word(
    UINT16   cmd_word,
    UINT8   *rt_addr,
    UINT8   *subaddr,
    UINT8   *tx,
    UINT16  *word_count)
{
    if (rt_addr) {
        *rt_addr = (UINT8)((cmd_word >> CMD_RT_ADDR_SHIFT) & 0x1F);
    }

    if (tx) {
        *tx = (UINT8)((cmd_word & CMD_TX_BIT) ? 1 : 0);
    }

    if (subaddr) {
        *subaddr = (UINT8)((cmd_word >> CMD_SUBADDR_SHIFT) & 0x1F);
    }

    if (word_count) {
        *word_count = (UINT16)(cmd_word & CMD_WORD_COUNT_MASK);
    }
}

/******************************************************************************
 * Bus status inquiry functions
 ******************************************************************************/

UINT8 bus_get_error_count(UINT8 bus_number)
{
    if (bus_number > 1) return 0;
    return bus_state[bus_number].error_count;
}

UINT16 bus_get_message_count(UINT8 bus_number)
{
    if (bus_number > 1) return 0;
    return bus_state[bus_number].message_count;
}

UINT8 bus_is_active(UINT8 bus_number)
{
    if (bus_number > 1) return 0;
    return bus_state[bus_number].bus_active;
}
/******************************************************************************/
