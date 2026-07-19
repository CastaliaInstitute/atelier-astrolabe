#include "faculty175_usb_ncm.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "faculty175_screen_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "sdkconfig.h"

#if CONFIG_TINYUSB_NET_MODE_NCM
#include "tinyusb_net.h"
#include "tusb.h"
#endif

static const char *TAG = "faculty175_usb_ncm";

#define USB_NCM_SERVER_A 172
#define USB_NCM_SERVER_B 31
#define USB_NCM_SERVER_C 77
#define USB_NCM_SERVER_D 1
#define USB_NCM_CLIENT_D 2
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DNS_SERVER_PORT 53
#define DHCP_MAGIC_COOKIE 0x63825363UL
#define DHCP_OPT_PAD 0
#define DHCP_OPT_SUBNET_MASK 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_DNS 6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME 51
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_END 255
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

typedef struct __attribute__((packed)) {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} dhcp_msg_t;

static struct netif s_netif;
static struct udp_pcb *s_dhcp_pcb;
static struct udp_pcb *s_dns_pcb;
static bool s_ready;
static esp_ip4_addr_t s_server_ip;
static esp_ip4_addr_t s_client_ip;
static esp_ip4_addr_t s_netmask;
static uint8_t s_mac[6] = {0x02, 0x43, 0x41, 0x53, 0x54, 0x00};
static EventGroupHandle_t s_host_events;

#define USB_NCM_HOST_READY_BIT BIT0

#if CONFIG_TINYUSB_NET_MODE_NCM
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static uint8_t *dhcp_put_u8(uint8_t *opt, uint8_t code, uint8_t value)
{
    *opt++ = code;
    *opt++ = 1;
    *opt++ = value;
    return opt;
}

static uint8_t *dhcp_put_u32(uint8_t *opt, uint8_t code, uint32_t value)
{
    *opt++ = code;
    *opt++ = 4;
    memcpy(opt, &value, sizeof(value));
    return opt + sizeof(value);
}

static uint8_t dhcp_message_type(const dhcp_msg_t *msg, uint32_t *requested_ip)
{
    const uint8_t *opt = msg->options;
    const uint8_t *end = msg->options + sizeof(msg->options);
    uint8_t type = 0;
    if (requested_ip != NULL) {
        *requested_ip = 0;
    }
    while (opt < end && *opt != DHCP_OPT_END) {
        if (*opt == DHCP_OPT_PAD) {
            ++opt;
            continue;
        }
        if ((size_t)(end - opt) < 2u) {
            break;
        }
        const uint8_t code = opt[0];
        const uint8_t len = opt[1];
        opt += 2;
        if ((size_t)(end - opt) < len) {
            break;
        }
        if (code == DHCP_OPT_MSG_TYPE && len == 1) {
            type = opt[0];
        } else if (code == DHCP_OPT_REQUESTED_IP && len == 4 && requested_ip != NULL) {
            memcpy(requested_ip, opt, sizeof(*requested_ip));
        }
        opt += len;
    }
    return type;
}

static void dhcp_send_reply(const dhcp_msg_t *req, uint8_t reply_type)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(dhcp_msg_t), PBUF_RAM);
    if (p == NULL) {
        ESP_LOGW(TAG, "dhcp reply alloc failed");
        return;
    }

    dhcp_msg_t *reply = (dhcp_msg_t *)p->payload;
    memset(reply, 0, sizeof(*reply));
    reply->op = 2;
    reply->htype = req->htype;
    reply->hlen = req->hlen;
    reply->xid = req->xid;
    reply->secs = req->secs;
    reply->flags = req->flags;
    reply->yiaddr = s_client_ip.addr;
    reply->siaddr = s_server_ip.addr;
    memcpy(reply->chaddr, req->chaddr, sizeof(reply->chaddr));
    reply->magic = PP_HTONL(DHCP_MAGIC_COOKIE);

    uint8_t *opt = reply->options;
    opt = dhcp_put_u8(opt, DHCP_OPT_MSG_TYPE, reply_type);
    opt = dhcp_put_u32(opt, DHCP_OPT_SERVER_ID, s_server_ip.addr);
    opt = dhcp_put_u32(opt, DHCP_OPT_SUBNET_MASK, s_netmask.addr);
    opt = dhcp_put_u32(opt, DHCP_OPT_LEASE_TIME, PP_HTONL(3600));
    opt = dhcp_put_u32(opt, DHCP_OPT_ROUTER, s_server_ip.addr);
    opt = dhcp_put_u32(opt, DHCP_OPT_DNS, s_server_ip.addr);
    *opt++ = DHCP_OPT_END;
    p->len = p->tot_len = (u16_t)((uint8_t *)opt - (uint8_t *)reply);

    ip_addr_t broadcast;
    IP_ADDR4(&broadcast, 255, 255, 255, 255);
    const err_t err = udp_sendto_if(s_dhcp_pcb, p, &broadcast, DHCP_CLIENT_PORT, &s_netif);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "dhcp reply send failed: %d", (int)err);
    } else {
        ESP_LOGI(TAG, "dhcp %s -> " IPSTR, reply_type == DHCP_OFFER ? "offer" : "ack", IP2STR(&s_client_ip));
    }
    pbuf_free(p);
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;
    if (p == NULL) {
        return;
    }
    dhcp_msg_t req = {};
    if (p->tot_len >= offsetof(dhcp_msg_t, options) + 4u) {
        pbuf_copy_partial(p, &req, p->tot_len > sizeof(req) ? sizeof(req) : p->tot_len, 0);
        if (req.op == 1 && req.magic == PP_HTONL(DHCP_MAGIC_COOKIE)) {
            uint32_t requested_ip = 0;
            const uint8_t type = dhcp_message_type(&req, &requested_ip);
            if (type == DHCP_DISCOVER) {
                dhcp_send_reply(&req, DHCP_OFFER);
            } else if (type == DHCP_REQUEST) {
                dhcp_send_reply(&req, DHCP_ACK);
            }
        }
    }
    pbuf_free(p);
}

static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;
    if (p == NULL) {
        return;
    }

    uint8_t query[256];
    const u16_t query_len = p->tot_len > sizeof(query) ? sizeof(query) : p->tot_len;
    pbuf_copy_partial(p, query, query_len, 0);
    pbuf_free(p);

    if (query_len < sizeof(dns_header_t)) {
        return;
    }
    const dns_header_t *qhdr = (const dns_header_t *)query;
    if (PP_NTOHS(qhdr->qdcount) == 0) {
        return;
    }

    size_t off = sizeof(dns_header_t);
    while (off < query_len && query[off] != 0) {
        const uint8_t label_len = query[off++];
        if ((label_len & 0xc0u) != 0 || label_len == 0 || off + label_len > query_len) {
            return;
        }
        off += label_len;
    }
    if (off + 5u > query_len) {
        return;
    }
    off += 5u; /* root label, qtype, qclass */

    uint8_t reply[320];
    if (off + 16u > sizeof(reply)) {
        return;
    }
    memcpy(reply, query, off);
    dns_header_t *rhdr = (dns_header_t *)reply;
    rhdr->flags = PP_HTONS(0x8180); /* standard response, authoritative, recursion available */
    rhdr->ancount = PP_HTONS(1);
    rhdr->nscount = 0;
    rhdr->arcount = 0;

    uint8_t *out = reply + off;
    *out++ = 0xc0;
    *out++ = (uint8_t)sizeof(dns_header_t);
    *out++ = 0x00;
    *out++ = 0x01; /* A */
    *out++ = 0x00;
    *out++ = 0x01; /* IN */
    *out++ = 0x00;
    *out++ = 0x00;
    *out++ = 0x00;
    *out++ = 0x1e; /* 30s TTL */
    *out++ = 0x00;
    *out++ = 0x04;
    const uint32_t a = s_server_ip.addr;
    memcpy(out, &a, sizeof(a));
    out += sizeof(a);

    struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(out - reply), PBUF_RAM);
    if (resp == NULL) {
        ESP_LOGW(TAG, "dns reply alloc failed");
        return;
    }
    memcpy(resp->payload, reply, (size_t)(out - reply));
    const err_t err = udp_sendto_if(s_dns_pcb, resp, addr, port, &s_netif);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "dns reply send failed: %d", (int)err);
    }
    pbuf_free(resp);
}

static err_t usb_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (!tud_ready()) {
        return ERR_IF;
    }
    void *buf = malloc(p->tot_len);
    if (buf == NULL) {
        return ERR_MEM;
    }
    pbuf_copy_partial(p, buf, p->tot_len, 0);
    const esp_err_t err = tinyusb_net_send_sync(buf, p->tot_len, buf, pdMS_TO_TICKS(250));
    if (err != ESP_OK) {
        free(buf);
        return ERR_IF;
    }
    return ERR_OK;
}

static void usb_tx_free(void *buffer, void *ctx)
{
    (void)ctx;
    free(buffer);
}

static err_t usb_netif_init(struct netif *netif)
{
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output = etharp_output;
    netif->linkoutput = usb_linkoutput;
    netif->mtu = 1500;
    netif->hwaddr_len = sizeof(s_mac);
    memcpy(netif->hwaddr, s_mac, sizeof(s_mac));
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

static esp_err_t usb_rx(void *buffer, uint16_t len, void *ctx)
{
    (void)ctx;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (pbuf_take(p, buffer, len) != ERR_OK) {
        pbuf_free(p);
        return ESP_FAIL;
    }
    const err_t err = s_netif.input(p, &s_netif);
    if (err != ERR_OK) {
        pbuf_free(p);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void usb_net_init_cb(void *ctx)
{
    (void)ctx;
    /* This callback is the NCM success boundary: USB enumeration completed
     * and the host selected the NCM data interface. */
    if (s_host_events != NULL) {
        xEventGroupSetBits(s_host_events, USB_NCM_HOST_READY_BIT);
    }
    ESP_LOGI(TAG, "host initialized NCM data interface");
}
#endif

esp_err_t faculty175_usb_ncm_init(void)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "esp_netif init");
    }
    ESP_RETURN_ON_ERROR(esp_read_mac(s_mac, ESP_MAC_ETH), TAG, "eth mac");
    s_mac[0] |= 0x02u;
    s_mac[0] &= 0xfeu;
    IP4_ADDR(&s_server_ip, USB_NCM_SERVER_A, USB_NCM_SERVER_B, USB_NCM_SERVER_C, USB_NCM_SERVER_D);
    IP4_ADDR(&s_client_ip, USB_NCM_SERVER_A, USB_NCM_SERVER_B, USB_NCM_SERVER_C, USB_NCM_CLIENT_D);
    IP4_ADDR(&s_netmask, 255, 255, 255, 0);

    ip4_addr_t ipaddr = {.addr = s_server_ip.addr};
    ip4_addr_t netmask = {.addr = s_netmask.addr};
    ip4_addr_t gw = {.addr = s_server_ip.addr};
    ESP_RETURN_ON_FALSE(netif_add(&s_netif, &ipaddr, &netmask, &gw, NULL, usb_netif_init, tcpip_input) != NULL,
                        ESP_FAIL,
                        TAG,
                        "netif add");
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);

    s_dhcp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    ESP_RETURN_ON_FALSE(s_dhcp_pcb != NULL, ESP_ERR_NO_MEM, TAG, "dhcp pcb");
    ESP_RETURN_ON_FALSE(udp_bind(s_dhcp_pcb, IP_ANY_TYPE, DHCP_SERVER_PORT) == ERR_OK,
                        ESP_FAIL,
                        TAG,
                        "dhcp bind");
    udp_recv(s_dhcp_pcb, dhcp_recv, NULL);

    s_dns_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    ESP_RETURN_ON_FALSE(s_dns_pcb != NULL, ESP_ERR_NO_MEM, TAG, "dns pcb");
    ESP_RETURN_ON_FALSE(udp_bind(s_dns_pcb, IP_ANY_TYPE, DNS_SERVER_PORT) == ERR_OK,
                        ESP_FAIL,
                        TAG,
                        "dns bind");
    udp_recv(s_dns_pcb, dns_recv, NULL);

    tinyusb_net_config_t net_cfg = {
        .on_recv_callback = usb_rx,
        .free_tx_buffer = usb_tx_free,
        .on_init_callback = usb_net_init_cb,
        .user_context = NULL,
    };
    memcpy(net_cfg.mac_addr, s_mac, sizeof(s_mac));
    s_host_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_host_events != NULL, ESP_ERR_NO_MEM, TAG, "host events");
    ESP_RETURN_ON_ERROR(tinyusb_net_init(TINYUSB_USBDEV_0, &net_cfg), TAG, "tinyusb ncm init");
    tud_network_link_state(0, true);
    ESP_LOGI(TAG,
             "USB NCM network prepared; DHCP/DNS captive endpoint http://" IPSTR "/settings",
             IP2STR(&s_server_ip));
    return ESP_OK;
#endif
}

esp_err_t faculty175_usb_ncm_wait_for_host(TickType_t timeout)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    (void)timeout;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_ready) {
        return ESP_OK;
    }
    if (s_host_events == NULL ||
        (xEventGroupWaitBits(s_host_events, USB_NCM_HOST_READY_BIT, pdFALSE, pdTRUE, timeout) &
         USB_NCM_HOST_READY_BIT) == 0) {
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
        ESP_RETURN_ON_ERROR(faculty175_screen_http_start(&s_server_ip), TAG, "start usb http before host-ready");
        s_ready = true;
        ESP_LOGW(TAG,
                 "USB NCM host-ready callback timed out; keeping PWA endpoint http://" IPSTR "/ available",
                 IP2STR(&s_server_ip));
        return ESP_OK;
#else
        return ESP_ERR_TIMEOUT;
#endif
    }
    ESP_RETURN_ON_ERROR(faculty175_screen_http_start(&s_server_ip), TAG, "start usb http");
    s_ready = true;
    ESP_LOGI(TAG,
             "USB NCM ready; device=http://" IPSTR "/ client=" IPSTR,
             IP2STR(&s_server_ip),
             IP2STR(&s_client_ip));
    return ESP_OK;
#endif
}

bool faculty175_usb_ncm_ready(void)
{
    return s_ready;
}

const esp_ip4_addr_t *faculty175_usb_ncm_ip(void)
{
    return s_ready ? &s_server_ip : NULL;
}
