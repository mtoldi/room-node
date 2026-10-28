#pragma once

#include <stdbool.h>

// Connects as a station using the credentials from Kconfig and keeps the
// connection up, retrying with exponential backoff (1 s up to 60 s).
// Returns false when no SSID is configured; the node then runs offline.
bool wifi_start(void);
