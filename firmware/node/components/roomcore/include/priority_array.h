#pragma once

#include <stdbool.h>

// BACnet-style command priority array (ASHRAE 135, clause 19.2).
// Each of the 16 levels is either empty or holds a value; the highest
// filled level (1 = highest) wins, and with all levels empty the
// relinquish default applies.
#define PA_LEVELS 16

typedef struct
{
    float value[PA_LEVELS];
    bool set[PA_LEVELS];
    float relinquish_default;
} priority_array_t;

void pa_init(priority_array_t *pa, float relinquish_default);

// Priorities outside 1..16 are ignored.
void pa_write(priority_array_t *pa, int priority, float value);
void pa_relinquish(priority_array_t *pa, int priority);
bool pa_is_set(const priority_array_t *pa, int priority);

float pa_value(const priority_array_t *pa);

// Level that currently determines the value, 0 when the relinquish default applies.
int pa_active_priority(const priority_array_t *pa);
