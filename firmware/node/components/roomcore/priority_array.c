#include "priority_array.h"

static bool valid(int priority)
{
    return priority >= 1 && priority <= PA_LEVELS;
}

void pa_init(priority_array_t *pa, float relinquish_default)
{
    for (int i = 0; i < PA_LEVELS; i++)
        pa->set[i] = false;
    pa->relinquish_default = relinquish_default;
}

void pa_write(priority_array_t *pa, int priority, float value)
{
    if (!valid(priority))
        return;
    pa->value[priority - 1] = value;
    pa->set[priority - 1] = true;
}

void pa_relinquish(priority_array_t *pa, int priority)
{
    if (valid(priority))
        pa->set[priority - 1] = false;
}

bool pa_is_set(const priority_array_t *pa, int priority)
{
    return valid(priority) && pa->set[priority - 1];
}

int pa_active_priority(const priority_array_t *pa)
{
    for (int i = 0; i < PA_LEVELS; i++)
        if (pa->set[i])
            return i + 1;
    return 0;
}

float pa_value(const priority_array_t *pa)
{
    int p = pa_active_priority(pa);
    return p ? pa->value[p - 1] : pa->relinquish_default;
}
