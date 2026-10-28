#pragma once

// Modbus TCP server protocol layer: MBAP framing, PDU decoding, the register
// map and exception responses. No sockets here; the firmware feeds complete
// frames in and sends the responses back, the module tests do the same.
//
// Register map (values scaled by 100, 0x8000 = not available):
//   Input    0  regulated temperature    int16  °C x100
//            1  SHTC3 temperature        int16  °C x100
//            2  relative humidity        uint16 % x100
//            3  active setpoint          int16  °C x100
//            4  heating                  0 / 1
//            5  status bits              see RC_STATUS_*
//            6  active heating priority  1..16, 0 = relinquish default
//            7  uptime high word         uint32 seconds
//            8  uptime low word
//   Holding 100 setpoint override (prio 8) int16 °C x100, 1600..2600, 0x8000 = relinquish
//          101 mode                      0 auto, 1 off, 2 on
// Function codes: 03, 04, 06, 16.

#include <stddef.h>
#include <stdint.h>

#include "points.h"

#define MB_TCP_HEADER_LEN 7   // MBAP: transaction, protocol, length, unit
#define MB_TCP_MAX_ADU    260 // 7 + 253 byte PDU
#define MB_NOT_AVAILABLE  0x8000

#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_ADDRESS  0x02
#define MB_EX_ILLEGAL_VALUE    0x03
#define MB_EX_GATEWAY_TARGET   0x0B

typedef struct
{
    const rc_points_t *points; // snapshot used for reads
    // Writes go to the controller; called only after the whole request is valid
    rc_result_t (*write_setpoint)(void *ctx, float celsius);
    void (*relinquish_setpoint)(void *ctx);
    rc_result_t (*set_mode)(void *ctx, rc_mode_t mode);
    void *ctx;
} mb_map_t;

// From the 7-byte MBAP header: total frame length, or -1 if the header is
// not Modbus (protocol id != 0) or announces an impossible length. The
// caller then drops the connection.
int mb_tcp_frame_length(const uint8_t *header);

// Handles one complete frame. Returns the response length written to resp,
// 0 if no response is to be sent, or -1 if the frame is malformed.
// Unit IDs 1 and 255 address this node; others get exception 0x0B.
int mb_tcp_handle(const mb_map_t *map, const uint8_t *req, size_t len, uint8_t *resp, size_t resp_size);
