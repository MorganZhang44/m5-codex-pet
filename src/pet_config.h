#pragma once

#if __has_include("pet_config.local.h")
#include "pet_config.local.h"
#endif

// Runtime NVS provisioning is the default; these are optional migration/fallbacks.
#ifndef PET_WIFI_SSID
#define PET_WIFI_SSID ""
#endif
#ifndef PET_WIFI_PASSWORD
#define PET_WIFI_PASSWORD ""
#endif
#ifndef PET_STATUS_HOST
#define PET_STATUS_HOST ""
#endif
#ifndef PET_STATUS_PORT
#define PET_STATUS_PORT 8788
#endif
