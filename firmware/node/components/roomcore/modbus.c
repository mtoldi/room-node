#include "modbus.h"

#include <math.h>

#define FC_READ_HOLDING   0x03
#define FC_READ_INPUT     0x04
#define FC_WRITE_SINGLE   0x06
#define FC_WRITE_MULTIPLE 0x10

#define INPUT_COUNT       9
#define HOLDING_FIRST     100
#define HOLDING_COUNT     2
#define MAX_READ_QTY      125 // Modbus spec limit for FC03/04
#define MAX_WRITE_QTY     123 // Modbus spec limit for FC16

#define SETPOINT_RAW_MIN  ((int)(RC_SETPOINT_MIN * 100))
#define SETPOINT_RAW_MAX  ((int)(RC_SETPOINT_MAX * 100))

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v >> 8;
    p[1] = v & 0xFF;
}

static uint16_t scaled(float v, bool valid)
{
    if (!valid || !isfinite(v))
        return MB_NOT_AVAILABLE;
    long x = lroundf(v * 100.0f);
    if (x < -32767)
        x = -32767; // keep 0x8000 reserved for "not available"
    if (x > 32767)
        x = 32767;
    return (uint16_t)(int16_t)x;
}

static uint16_t input_register(const rc_points_t *p, int addr)
{
    switch (addr)
    {
    case 0: return scaled(p->t_ref, p->t_ref_valid);
    case 1: return scaled(p->t_aux, p->t_aux_valid);
    case 2: return p->t_aux_valid ? (uint16_t)lroundf(p->rh * 100.0f) : MB_NOT_AVAILABLE;
    case 3: return scaled(p->setpoint, true);
    case 4: return p->heating;
    case 5: return p->status;
    case 6: return (uint16_t)p->priority;
    case 7: return p->uptime_s >> 16;
    default: return p->uptime_s & 0xFFFF;
    }
}

static uint16_t holding_register(const rc_points_t *p, int addr)
{
    if (addr == HOLDING_FIRST)
        return (p->status & RC_STATUS_OVERRIDE) ? scaled(p->setpoint, true) : MB_NOT_AVAILABLE;
    return (uint16_t)p->mode;
}

static bool holding_value_valid(int addr, uint16_t v)
{
    if (addr == HOLDING_FIRST)
        return v == MB_NOT_AVAILABLE || ((int16_t)v >= SETPOINT_RAW_MIN && (int16_t)v <= SETPOINT_RAW_MAX);
    return v <= RC_MODE_ON;
}

static void apply_holding(const mb_map_t *map, int addr, uint16_t v)
{
    if (addr == HOLDING_FIRST)
    {
        if (v == MB_NOT_AVAILABLE)
            map->relinquish_setpoint(map->ctx);
        else
            map->write_setpoint(map->ctx, (int16_t)v / 100.0f);
    }
    else
        map->set_mode(map->ctx, (rc_mode_t)v);
}

static bool in_range(int first, int qty, int start, int count)
{
    return first >= start && first + qty <= start + count;
}

// Writes the PDU response into out; returns its length.
static int exception(uint8_t fc, uint8_t code, uint8_t *out)
{
    out[0] = fc | 0x80;
    out[1] = code;
    return 2;
}

static int read_registers(const mb_map_t *map, const uint8_t *pdu, size_t len, uint8_t *out)
{
    uint8_t fc = pdu[0];
    if (len != 5)
        return exception(fc, MB_EX_ILLEGAL_VALUE, out);
    int addr = get16(pdu + 1), qty = get16(pdu + 3);
    if (qty < 1 || qty > MAX_READ_QTY)
        return exception(fc, MB_EX_ILLEGAL_VALUE, out);

    bool input = fc == FC_READ_INPUT;
    if (input ? !in_range(addr, qty, 0, INPUT_COUNT) : !in_range(addr, qty, HOLDING_FIRST, HOLDING_COUNT))
        return exception(fc, MB_EX_ILLEGAL_ADDRESS, out);

    out[0] = fc;
    out[1] = (uint8_t)(qty * 2);
    for (int i = 0; i < qty; i++)
        put16(out + 2 + 2 * i, input ? input_register(map->points, addr + i) : holding_register(map->points, addr + i));
    return 2 + qty * 2;
}

static int write_single(const mb_map_t *map, const uint8_t *pdu, size_t len, uint8_t *out)
{
    if (len != 5)
        return exception(FC_WRITE_SINGLE, MB_EX_ILLEGAL_VALUE, out);
    int addr = get16(pdu + 1);
    uint16_t value = get16(pdu + 3);
    if (!in_range(addr, 1, HOLDING_FIRST, HOLDING_COUNT))
        return exception(FC_WRITE_SINGLE, MB_EX_ILLEGAL_ADDRESS, out);
    if (!holding_value_valid(addr, value))
        return exception(FC_WRITE_SINGLE, MB_EX_ILLEGAL_VALUE, out);

    apply_holding(map, addr, value);
    for (int i = 0; i < 5; i++) // the response echoes the request
        out[i] = pdu[i];
    return 5;
}

static int write_multiple(const mb_map_t *map, const uint8_t *pdu, size_t len, uint8_t *out)
{
    if (len < 6)
        return exception(FC_WRITE_MULTIPLE, MB_EX_ILLEGAL_VALUE, out);
    int addr = get16(pdu + 1), qty = get16(pdu + 3), bytes = pdu[5];
    if (qty < 1 || qty > MAX_WRITE_QTY || bytes != qty * 2 || len != (size_t)(6 + bytes))
        return exception(FC_WRITE_MULTIPLE, MB_EX_ILLEGAL_VALUE, out);
    if (!in_range(addr, qty, HOLDING_FIRST, HOLDING_COUNT))
        return exception(FC_WRITE_MULTIPLE, MB_EX_ILLEGAL_ADDRESS, out);

    // All or nothing: validate every value before applying any
    for (int i = 0; i < qty; i++)
        if (!holding_value_valid(addr + i, get16(pdu + 6 + 2 * i)))
            return exception(FC_WRITE_MULTIPLE, MB_EX_ILLEGAL_VALUE, out);
    for (int i = 0; i < qty; i++)
        apply_holding(map, addr + i, get16(pdu + 6 + 2 * i));

    out[0] = FC_WRITE_MULTIPLE;
    put16(out + 1, (uint16_t)addr);
    put16(out + 3, (uint16_t)qty);
    return 5;
}

static int handle_pdu(const mb_map_t *map, const uint8_t *pdu, size_t len, uint8_t *out)
{
    switch (pdu[0])
    {
    case FC_READ_HOLDING:
    case FC_READ_INPUT:
        return read_registers(map, pdu, len, out);
    case FC_WRITE_SINGLE:
        return write_single(map, pdu, len, out);
    case FC_WRITE_MULTIPLE:
        return write_multiple(map, pdu, len, out);
    default:
        return exception(pdu[0], MB_EX_ILLEGAL_FUNCTION, out);
    }
}

int mb_tcp_frame_length(const uint8_t *header)
{
    int protocol = get16(header + 2), length = get16(header + 4);
    // length counts the unit id plus a PDU of 1..253 bytes
    if (protocol != 0 || length < 2 || length > MB_TCP_MAX_ADU - 6)
        return -1;
    return 6 + length;
}

int mb_tcp_handle(const mb_map_t *map, const uint8_t *req, size_t len, uint8_t *resp, size_t resp_size)
{
    if (len < MB_TCP_HEADER_LEN + 1 || mb_tcp_frame_length(req) != (int)len || resp_size < MB_TCP_MAX_ADU)
        return -1;

    uint8_t unit = req[6];
    const uint8_t *pdu = req + MB_TCP_HEADER_LEN;
    size_t pdu_len = len - MB_TCP_HEADER_LEN;
    uint8_t *out = resp + MB_TCP_HEADER_LEN;

    int out_len = (unit == 1 || unit == 255) ? handle_pdu(map, pdu, pdu_len, out)
                                             : exception(pdu[0], MB_EX_GATEWAY_TARGET, out);

    for (int i = 0; i < 4; i++) // transaction and protocol id are echoed
        resp[i] = req[i];
    put16(resp + 4, (uint16_t)(out_len + 1));
    resp[6] = unit;
    return MB_TCP_HEADER_LEN + out_len;
}
