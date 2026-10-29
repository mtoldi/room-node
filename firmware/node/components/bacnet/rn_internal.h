#pragma once

#include "rn_bacnet.h"

// Shared between rn_bacnet.c (task glue) and rn_objects.c (object model)
extern rn_bacnet_config_t rn_config;
extern const rn_bacnet_access_t *rn_access;
