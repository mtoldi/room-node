#include "heating_actuator.h"

typedef struct
{
    heating_actuator_t base;
    bool on;
} sim_actuator_t;

static void sim_set(heating_actuator_t *self, bool on)
{
    ((sim_actuator_t *)self)->on = on;
}

static bool sim_is_on(const heating_actuator_t *self)
{
    return ((const sim_actuator_t *)self)->on;
}

static sim_actuator_t s_actuator = {
    .base = {.set = sim_set, .is_on = sim_is_on},
};

heating_actuator_t *sim_actuator_get(void)
{
    return &s_actuator.base;
}
