#include "points.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAYLOAD 31

static const char *const MODE_NAMES[] = {"auto", "off", "on"};

const char *rc_mode_name(rc_mode_t mode)
{
    return (unsigned)mode < 3 ? MODE_NAMES[mode] : "unknown";
}

// Appends "key":value, or "key":null when the value is not valid
static int put(char *buf, size_t len, size_t *pos, const char *key, const char *fmt, double value, bool valid)
{
    int n = snprintf(buf + *pos, len - *pos, "%s\"%s\":", *pos > 1 ? "," : "", key);
    if (n < 0 || (size_t)n >= len - *pos)
        return -1;
    *pos += n;
    n = valid ? snprintf(buf + *pos, len - *pos, fmt, value) : snprintf(buf + *pos, len - *pos, "null");
    if (n < 0 || (size_t)n >= len - *pos)
        return -1;
    *pos += n;
    return 0;
}

int rc_points_to_json(const rc_points_t *p, char *buf, size_t len)
{
    size_t pos = 0;
    if (len < 2)
        return -1;
    buf[pos++] = '{';
    if (put(buf, len, &pos, "t_ref", "%.2f", p->t_ref, p->t_ref_valid) ||
        put(buf, len, &pos, "t_aux", "%.2f", p->t_aux, p->t_aux_valid) ||
        put(buf, len, &pos, "rh", "%.1f", p->rh, p->t_aux_valid) ||
        put(buf, len, &pos, "setpoint", "%.1f", p->setpoint, true))
        return -1;
    int n = snprintf(buf + pos, len - pos,
                     ",\"heating\":%s,\"priority\":%d,\"mode\":\"%s\",\"status\":%u,\"uptime\":%lu}",
                     p->heating ? "true" : "false", p->priority, rc_mode_name(p->mode), (unsigned)p->status,
                     (unsigned long)p->uptime_s);
    if (n < 0 || (size_t)n >= len - pos)
        return -1;
    return (int)(pos + n);
}

bool rc_points_changed(const rc_points_t *a, const rc_points_t *b)
{
    return a->heating != b->heating || a->priority != b->priority || a->mode != b->mode ||
           a->status != b->status || fabsf(a->setpoint - b->setpoint) >= 0.05f;
}

// Copies the payload without surrounding whitespace into a terminated buffer
static bool trimmed(const char *payload, size_t len, char *out)
{
    while (len && isspace((unsigned char)payload[0]))
        payload++, len--;
    while (len && isspace((unsigned char)payload[len - 1]))
        len--;
    if (len == 0 || len > MAX_PAYLOAD)
        return false;
    memcpy(out, payload, len);
    out[len] = '\0';
    return true;
}

rc_cmd_t rc_parse_setpoint(const char *payload, size_t len, float *celsius)
{
    char s[MAX_PAYLOAD + 1];
    if (!trimmed(payload, len, s))
        return RC_CMD_INVALID;
    if (strcmp(s, "null") == 0)
        return RC_CMD_RELINQUISH;

    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || !isfinite(v))
        return RC_CMD_INVALID;
    *celsius = v; // range is checked by rc_write_setpoint
    return RC_CMD_VALUE;
}

rc_cmd_t rc_parse_mode(const char *payload, size_t len, rc_mode_t *mode)
{
    char s[MAX_PAYLOAD + 1];
    if (!trimmed(payload, len, s))
        return RC_CMD_INVALID;
    for (int m = 0; m < 3; m++)
    {
        if (strcmp(s, MODE_NAMES[m]) == 0)
        {
            *mode = (rc_mode_t)m;
            return RC_CMD_VALUE;
        }
    }
    return RC_CMD_INVALID;
}
