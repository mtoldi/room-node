// BACnet/IP datalink (Annex J) on lwIP BSD sockets for ESP-IDF.
// bacnet-stack's own ports use Arduino (esp32) or the lwIP raw API (lwip),
// which is not thread safe outside the tcpip task; sockets are.

#include <string.h>

#include "bacnet/bacdef.h"
#include "bacnet/basic/bbmd/h_bbmd.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/bvlc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

static const char *TAG = "bip";

static int s_sock = -1;
static BACNET_IP_ADDRESS s_addr;      // our unicast address
static BACNET_IP_ADDRESS s_broadcast; // directed subnet broadcast
static uint16_t s_port = 0xBAC0; // 47808, host byte order

static void to_sockaddr(const BACNET_IP_ADDRESS *a, struct sockaddr_in *sin)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(a->port);
    memcpy(&sin->sin_addr.s_addr, a->address, 4); // both in network byte order
}

static void from_sockaddr(const struct sockaddr_in *sin, BACNET_IP_ADDRESS *a)
{
    memcpy(a->address, &sin->sin_addr.s_addr, 4);
    a->port = ntohs(sin->sin_port);
}

bool bip_set_addr(const BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(&s_addr, addr);
}

bool bip_get_addr(BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(addr, &s_addr);
}

bool bip_set_broadcast_addr(const BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(&s_broadcast, addr);
}

bool bip_get_broadcast_addr(BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(addr, &s_broadcast);
}

void bip_set_port(uint16_t port)
{
    s_port = port;
}

uint16_t bip_get_port(void)
{
    return s_port;
}

void bip_get_my_address(BACNET_ADDRESS *my_address)
{
    bvlc_ip_address_to_bacnet_local(my_address, &s_addr);
}

void bip_get_broadcast_address(BACNET_ADDRESS *dest)
{
    bvlc_ip_address_to_bacnet_local(dest, &s_broadcast);
    dest->net = BACNET_BROADCAST_NETWORK;
}

bool bip_valid(void)
{
    return s_sock >= 0;
}

int bip_send_mpdu(const BACNET_IP_ADDRESS *dest, const uint8_t *mtu, uint16_t mtu_len)
{
    struct sockaddr_in sin;
    to_sockaddr(dest, &sin);
    int n = sendto(s_sock, mtu, mtu_len, 0, (struct sockaddr *)&sin, sizeof(sin));
    return n < 0 ? 0 : n;
}

int bip_send_pdu(BACNET_ADDRESS *dest, BACNET_NPDU_DATA *npdu_data, uint8_t *pdu, unsigned pdu_len)
{
    return bvlc_send_pdu(dest, npdu_data, pdu, pdu_len);
}

uint16_t bip_receive(BACNET_ADDRESS *src, uint8_t *pdu, uint16_t max_pdu, unsigned timeout_ms)
{
    if (s_sock < 0)
        return 0;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s_sock, &fds);
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    if (select(s_sock + 1, &fds, NULL, NULL, &tv) <= 0)
        return 0;

    struct sockaddr_in sin;
    socklen_t sin_len = sizeof(sin);
    int len = recvfrom(s_sock, pdu, max_pdu, 0, (struct sockaddr *)&sin, &sin_len);
    if (len <= 0)
        return 0;

    BACNET_IP_ADDRESS from;
    from_sockaddr(&sin, &from);
    if (!bvlc_address_different(&from, &s_addr))
        return 0; // our own broadcast coming back

    // The BVLC layer strips its header and tells where the NPDU starts
    int offset = bvlc_handler(&from, src, pdu, (uint16_t)len);
    if (offset <= 0 || offset >= len)
        return 0;
    memmove(pdu, pdu + offset, len - offset);
    return (uint16_t)(len - offset);
}

bool bip_init(const char *ifname)
{
    (void)ifname;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0)
    {
        ESP_LOGE(TAG, "no IP address yet");
        return false;
    }
    uint32_t broadcast = ip.ip.addr | ~ip.netmask.addr;
    memcpy(s_addr.address, &ip.ip.addr, 4);
    memcpy(s_broadcast.address, &broadcast, 4);
    s_addr.port = s_broadcast.port = s_port;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct sockaddr_in any = {
        .sin_family = AF_INET,
        .sin_port = htons(s_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (s_sock < 0 || bind(s_sock, (struct sockaddr *)&any, sizeof(any)) != 0)
    {
        ESP_LOGE(TAG, "cannot bind UDP port %u (errno %d)", s_port, errno);
        if (s_sock >= 0)
            close(s_sock);
        s_sock = -1;
        return false;
    }
    ESP_LOGI(TAG, "BACnet/IP on " IPSTR ":%u, broadcast " IPSTR, IP2STR(&ip.ip), s_port,
             IP2STR((esp_ip4_addr_t *)&broadcast));
    return true;
}

void bip_cleanup(void)
{
    if (s_sock >= 0)
        close(s_sock);
    s_sock = -1;
}
