#include "rn_bacnet.h"

#include "bacnet/basic/npdu/h_npdu.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/datalink.h"
#include "rn_internal.h"

rn_bacnet_config_t rn_config;
const rn_bacnet_access_t *rn_access;

static uint8_t s_rx[MAX_MPDU];

bool rn_bacnet_start(const rn_bacnet_config_t *config, const rn_bacnet_access_t *access)
{
    rn_config = *config;
    rn_access = access;

    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);

    if (!bip_init(NULL))
        return false;
    Send_I_Am(&Handler_Transmit_Buffer[0]);
    return true;
}

void rn_bacnet_poll(unsigned timeout_ms)
{
    BACNET_ADDRESS src = {0};
    uint16_t len = bip_receive(&src, s_rx, sizeof(s_rx), timeout_ms);
    if (len > 0)
        npdu_handler(&src, s_rx, len);
}
