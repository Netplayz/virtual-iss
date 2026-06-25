/******************************************************************************
 * TLM_PACK.C - Telemetry Packet Assembly and Output
 *
 * Builds CCSDS-style telemetry packets from subsystem state.
 * Three packet types:
 *   - State telemetry (high-rate, 10ms)
 *   - 1553 bus status (medium-rate, 100ms)
 *   - Health and status (low-rate, 1s)
 *
 * Packets are queued on the 1553 bus for transmission to the
 * bridge, which forwards them to the NATS message bus.
 ******************************************************************************/

#include "mdm.h"

/******************************************************************************
 * TLM_BUILD_PACKET - Assemble a CCSDS-style telemetry packet
 *
 * Builds primary and secondary headers, copies data payload,
 * and computes the packet CRC.
 *
 * Returns: SUCCESS or error code
 ******************************************************************************/

STATUS
tlm_build_packet(
    MDM_STATE   *state,
    TLM_PACKET  *packet,
    UINT8        subsystem_id,
    UINT8        packet_type,
    UINT8       *data,
    UINT16       data_length)
{
    if (state == NULL || packet == NULL) {
        return FAILURE;
    }

    if (data_length > TLM_DATA_MAX) {
        return ERR_OVERFLOW;
    }

    /* Clear packet */
    (void)memset(packet, 0, sizeof(TLM_PACKET));

    /**************************************************************************
     * PRIMARY HEADER (CCSDS-style, 6 bytes)
     *
     * Bit layout:
     *   31-21: APID (11 bits)
     *   20-7:  Sequence count (14 bits)
     *   6-3:   Packet length (16 bits, upper nibble)
     *   2-0:   Version (3 bits)
     *
     *   Word 2:
     *   15-13: Version (3 bits, cont.)
     *   12:    Type (1 bit)
     *   11:    Secondary header flag (1 bit)
     *   10-9:  Sequence flags (2 bits)
     *   8-0:   Packet length (16 bits, lower bits)
     **************************************************************************/

    packet->primary.apid             = (UINT32)(0x100 | subsystem_id);
    packet->primary.sequence_count   = (UINT32)state->tlm_sequence++;
    packet->primary.packet_length    = (UINT32)(data_length + 8); /* hdrs + data + crc */
    packet->primary.version          = 0;
    packet->primary.type             = 0; /* telemetry */
    packet->primary.secondary_header = 1;
    packet->primary.sequence_flags   = 3; /* standalone packet */

    /**************************************************************************
     * SECONDARY HEADER (6 bytes)
     *
     * Includes timestamp and subsystem identification
     * for ground processing and time correlation.
     **************************************************************************/

    packet->secondary.timestamp_secs    = state->uptime_seconds;
    packet->secondary.timestamp_subsecs = state->uptime_milliseconds;
    packet->secondary.subsystem_id      = subsystem_id;
    packet->secondary.packet_type       = packet_type;

    /**************************************************************************
     * DATA PAYLOAD
     **************************************************************************/

    if (data != NULL && data_length > 0) {
        (void)memcpy(packet->data, data, data_length);
    }

    /**************************************************************************
     * COMPUTE CRC
     **************************************************************************/

    packet->crc = tlm_calculate_crc((UINT8 *)packet,
                                    TLM_HEADER_SIZE + 6 + data_length);

    return SUCCESS;
}

/******************************************************************************
 * TLM_SEND_PACKET - Queue telemetry packet for transmission
 *
 * Wraps the TLM_PACKET in a BUS_MESSAGE and queues it
 * on the 1553 bus transmit queue.
 ******************************************************************************/

STATUS
tlm_send_packet(MDM_STATE *state, TLM_PACKET *packet)
{
    BUS_MESSAGE msg;

    if (state == NULL || packet == NULL) {
        return FAILURE;
    }

    (void)memset(&msg, 0, sizeof(msg));

    /* Build bus message from TLM packet */
    msg.command_word = (UINT16)(((UINT16)state->bus_address << CMD_RT_ADDR_SHIFT) |
                                (0x01 << CMD_SUBADDR_SHIFT) | /* subaddress = 1 = telemetry */
                                0x10); /* 16 words */

    msg.status_word = 0x0000; /* will be set by bus interface */
    msg.word_count  = sizeof(TLM_PACKET) / sizeof(UINT16);
    msg.message_type = MSG_TYPE_RT_TO_BC;
    msg.bus_number  = 0;

    if (msg.word_count > BUS_DATA_MAX) {
        msg.word_count = BUS_DATA_MAX;
    }

    (void)memcpy(msg.data_words, packet,
                 msg.word_count * sizeof(UINT16));

    /* Queue on bus */
    state->tlm_packets_sent++;
    state->tlm_bytes_sent += msg.word_count * sizeof(UINT16);

    return bus_send_message(&msg);
}

/******************************************************************************
 * TLM_CALCULATE_CRC - CRC-16 for telemetry packet integrity
 *
 * Uses the same CRC-16-IBM polynomial as the 1553 bus,
 * operating on byte data instead of word data.
 ******************************************************************************/

UINT16
tlm_calculate_crc(UINT8 *data, UINT16 length)
{
    UINT16  crc;
    UINT16  idx;
    UINT8   byte;

    /* CRC-16-IBM lookup table (same as bus_1553.c) */
    static const UINT16 tlm_crc16_table[256] = {
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

    crc = 0xFFFF;

    for (idx = 0; idx < length; idx++) {
        byte = (UINT8)((crc ^ data[idx]) & 0xFF);
        crc  = (UINT16)((crc >> 8) ^ tlm_crc16_table[byte]);
    }

    return (UINT16)(~crc);
}

/******************************************************************************
 * TLM_FORMAT_STATE - Build MDM state telemetry packet payload
 *
 * Contains: mode, uptime, tick count, command counters, error flags
 * Packet type: 0x01 (MDM state)
 ******************************************************************************/

void
tlm_format_state(MDM_STATE *state, UINT8 *buffer, UINT16 *length)
{
    UINT16 idx;

    idx = 0;

    /* Packet type identifier */
    buffer[idx++] = 0x01;

    /* MDM identification */
    buffer[idx++] = state->mdm_id;
    buffer[idx++] = state->bus_address;

    /* Operating mode */
    buffer[idx++] = state->mdm_mode;

    /* Timing */
    buffer[idx++] = (UINT8)((state->uptime_seconds >> 24) & 0xFF);
    buffer[idx++] = (UINT8)((state->uptime_seconds >> 16) & 0xFF);
    buffer[idx++] = (UINT8)((state->uptime_seconds >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(state->uptime_seconds & 0xFF);
    buffer[idx++] = (UINT8)((state->tick_count >> 24) & 0xFF);
    buffer[idx++] = (UINT8)((state->tick_count >> 16) & 0xFF);
    buffer[idx++] = (UINT8)((state->tick_count >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(state->tick_count & 0xFF);

    /* Command counters */
    buffer[idx++] = (UINT8)((state->cmd_received >> 24) & 0xFF);
    buffer[idx++] = (UINT8)((state->cmd_received >> 16) & 0xFF);
    buffer[idx++] = (UINT8)((state->cmd_received >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(state->cmd_received & 0xFF);

    /* Error and status flags */
    buffer[idx++] = state->status_flags;
    buffer[idx++] = state->error_flags;
    buffer[idx++] = state->warning_flags;

    /* CPU and memory */
    buffer[idx++] = state->cpu_load_pct;
    buffer[idx++] = (UINT8)((state->stack_remaining >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(state->stack_remaining & 0xFF);

    *length = idx;
}

/******************************************************************************
 * TLM_FORMAT_1553_STATUS - Build 1553 bus status telemetry payload
 *
 * Contains: per-bus error counts, message counts, active bus
 * Packet type: 0x02 (1553 bus status)
 ******************************************************************************/

void
tlm_format_1553_status(MDM_STATE *state, UINT8 *buffer, UINT16 *length)
{
    UINT16 idx;

    idx = 0;

    /* Packet type identifier */
    buffer[idx++] = 0x02;

    /* Bus A status */
    buffer[idx++] = bus_is_active(0);
    buffer[idx++] = bus_get_error_count(0);
    buffer[idx++] = (UINT8)((bus_get_message_count(0) >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(bus_get_message_count(0) & 0xFF);

    /* Bus B status */
    buffer[idx++] = bus_is_active(1);
    buffer[idx++] = bus_get_error_count(1);
    buffer[idx++] = (UINT8)((bus_get_message_count(1) >> 8) & 0xFF);
    buffer[idx++] = (UINT8)(bus_get_message_count(1) & 0xFF);

    /* Queue depths */
    buffer[idx++] = (UINT8)((state->bus_rx_head - state->bus_rx_tail) % BUS_MSG_MAX);
    buffer[idx++] = (UINT8)((state->bus_tx_head - state->bus_tx_tail) % BUS_MSG_MAX);

    *length = idx;
}

/******************************************************************************
 * TLM_FORMAT_HEALTH - Build health and status telemetry payload
 *
 * Contains: self-test results, temperature, voltage, stored sequence status
 * Packet type: 0x03 (health and status)
 ******************************************************************************/

void
tlm_format_health(MDM_STATE *state, UINT8 *buffer, UINT16 *length)
{
    UINT16 idx;
    UINT8  i;

    idx = 0;

    /* Packet type identifier */
    buffer[idx++] = 0x03;

    /* Firmware version */
    buffer[idx++] = state->major_version;
    buffer[idx++] = state->minor_version;
    buffer[idx++] = state->patch_version;

    /* Stored sequence status */
    {
        UINT8 active_count = 0;
        for (i = 0; i < STORED_SEQ_MAX; i++) {
            if (state->stored_sequences[i].active) {
                active_count++;
            }
        }
        buffer[idx++] = active_count;
    }

    /* In a real system, these would come from ADC readings */
    buffer[idx++] = 0x55; /* internal temp +25C (placeholder) */
    buffer[idx++] = 0x4B; /* +5V rail (placeholder) */
    buffer[idx++] = 0x32; /* +3.3V rail (placeholder) */

    /* Spare bytes for future expansion */
    buffer[idx++] = 0x00;
    buffer[idx++] = 0x00;
    buffer[idx++] = 0x00;
    buffer[idx++] = 0x00;

    *length = idx;
}
/******************************************************************************/
