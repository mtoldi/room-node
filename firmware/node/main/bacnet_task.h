#pragma once

#include <stdint.h>

// BACnet/IP server (UDP 47808). Waits for an IP address, announces the
// device with I-Am and then serves Who-Is, ReadProperty(Multiple) and
// WriteProperty. Objects are listed in components/bacnet/include/rn_bacnet.h.
void bacnet_task_start(uint32_t min_switch_ms);
