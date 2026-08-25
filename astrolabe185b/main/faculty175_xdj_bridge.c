#include "faculty175_xdj_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "usb/usb_types_ch9.h"

static const char *TAG = "faculty175_xdj";

#define XDJ_STREAM_PORT 8765
#define XDJ_QUEUE_DEPTH 64
#define XDJ_MAX_FRAME 512
#define XDJ_USB_IN_TRANSFERS 2
#define XDJ_HOST_EVENTS 8
#define XDJ_USB_MIDI_CLASS 0x01
#define XDJ_USB_MIDI_SUBCLASS 0x03
#define XDJ_USB_XFER_BULK 0x02
#define XDJ_EP_DIR_IN 0x80

typedef struct {
    uint8_t bytes[4];
} xdj_packet_t;

static usb_host_client_handle_t s_client;
static usb_device_handle_t s_device;
static usb_transfer_t *s_in[XDJ_USB_IN_TRANSFERS];
static usb_transfer_t *s_out;
static uint8_t s_interface;
static bool s_interface_claimed;
static bool s_endpoints_ready;
static bool s_out_busy;
static uint8_t s_in_endpoint;
static uint8_t s_out_endpoint;
static bool s_started;
static bool s_stop_requested;
static faculty175_xdj_usb_state_t s_usb_state = FACULTY175_XDJ_USB_DISCONNECTED;
static QueueHandle_t s_in_queue;
static QueueHandle_t s_out_queue;
static TaskHandle_t s_host_task;
static TaskHandle_t s_stream_task;

static void xdj_set_state(faculty175_xdj_usb_state_t state)
{
    s_usb_state = state;
    ESP_LOGI(TAG, "USB MIDI state: %s", faculty175_xdj_bridge_usb_state_name());
}

const char *faculty175_xdj_bridge_usb_state_name(void)
{
    switch (s_usb_state) {
        case FACULTY175_XDJ_USB_ENUMERATING: return "enumerating";
        case FACULTY175_XDJ_USB_READY: return "ready";
        case FACULTY175_XDJ_USB_ERROR: return "error";
        case FACULTY175_XDJ_USB_DISCONNECTED:
        default: return "disconnected";
    }
}

static void xdj_free_transfer(usb_transfer_t **transfer)
{
    if (transfer != NULL && *transfer != NULL) {
        (void)usb_host_transfer_free(*transfer);
        *transfer = NULL;
    }
}

static void xdj_release_device(void)
{
    if (s_device == NULL) {
        return;
    }
    if (s_in_endpoint != 0) {
        (void)usb_host_endpoint_halt(s_device, s_in_endpoint);
        (void)usb_host_endpoint_flush(s_device, s_in_endpoint);
    }
    if (s_out_endpoint != 0) {
        (void)usb_host_endpoint_halt(s_device, s_out_endpoint);
        (void)usb_host_endpoint_flush(s_device, s_out_endpoint);
    }
    for (size_t i = 0; i < XDJ_USB_IN_TRANSFERS; ++i) {
        xdj_free_transfer(&s_in[i]);
    }
    xdj_free_transfer(&s_out);
    if (s_interface_claimed) {
        (void)usb_host_interface_release(s_client, s_device, s_interface);
    }
    (void)usb_host_device_close(s_client, s_device);
    s_device = NULL;
    s_interface_claimed = false;
    s_endpoints_ready = false;
    s_out_busy = false;
    s_in_endpoint = 0;
    s_out_endpoint = 0;
    xQueueReset(s_in_queue);
    xQueueReset(s_out_queue);
}

static void xdj_usb_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer == NULL || transfer->context == NULL || transfer->device_handle != s_device) {
        return;
    }
    const bool is_in = (transfer->bEndpointAddress & XDJ_EP_DIR_IN) != 0;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (is_in) {
            for (size_t offset = 0; offset + sizeof(xdj_packet_t) <= transfer->actual_num_bytes; offset += 4) {
                xdj_packet_t packet;
                memcpy(packet.bytes, transfer->data_buffer + offset, sizeof(packet.bytes));
                if ((packet.bytes[0] & 0x0f) != 0) {
                    (void)xQueueSendFromISR(s_in_queue, &packet, NULL);
                }
            }
            (void)usb_host_transfer_submit(transfer);
        } else {
            s_out_busy = false;
        }
    } else if (transfer->status != USB_TRANSFER_STATUS_CANCELED && is_in) {
        (void)usb_host_transfer_submit(transfer);
    }
}

static void xdj_submit_out_if_idle(void)
{
    if (!s_endpoints_ready || s_out == NULL || s_out_busy) {
        return;
    }
    xdj_packet_t packet;
    if (xQueueReceive(s_out_queue, &packet, 0) != pdPASS) {
        return;
    }
    memcpy(s_out->data_buffer, packet.bytes, sizeof(packet.bytes));
    s_out->num_bytes = sizeof(packet.bytes);
    s_out_busy = true;
    if (usb_host_transfer_submit(s_out) != ESP_OK) {
        s_out_busy = false;
        s_endpoints_ready = false;
    }
}

static esp_err_t xdj_setup_in_endpoint(const usb_ep_desc_t *endpoint)
{
    for (size_t i = 0; i < XDJ_USB_IN_TRANSFERS; ++i) {
        if (s_in[i] != NULL) {
            continue;
        }
        esp_err_t err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &s_in[i]);
        if (err != ESP_OK) {
            return err;
        }
        s_in[i]->device_handle = s_device;
        s_in[i]->bEndpointAddress = endpoint->bEndpointAddress;
        s_in[i]->callback = xdj_usb_transfer_cb;
        s_in[i]->context = s_in[i];
        s_in[i]->num_bytes = endpoint->wMaxPacketSize;
        s_in_endpoint = endpoint->bEndpointAddress;
        return usb_host_transfer_submit(s_in[i]);
    }
    return ESP_OK;
}

static esp_err_t xdj_setup_out_endpoint(const usb_ep_desc_t *endpoint)
{
    if (s_out != NULL) {
        return ESP_OK;
    }
    esp_err_t err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &s_out);
    if (err != ESP_OK) {
        return err;
    }
    s_out->device_handle = s_device;
    s_out->bEndpointAddress = endpoint->bEndpointAddress;
    s_out->callback = xdj_usb_transfer_cb;
    s_out->context = s_out;
    s_out->num_bytes = 0;
    s_out_endpoint = endpoint->bEndpointAddress;
    return ESP_OK;
}

static esp_err_t xdj_parse_config(const usb_config_desc_t *config)
{
    const uint8_t *cursor = config->val;
    const uint8_t *end = ((const uint8_t *)config) + config->wTotalLength;
    uint8_t current_interface = 0xff;
    bool midi_interface_seen = false;

    while (cursor + 2 <= end && cursor[0] >= 2 && cursor + cursor[0] <= end) {
        if (cursor[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE && cursor[0] >= sizeof(usb_intf_desc_t)) {
            const usb_intf_desc_t *interface = (const usb_intf_desc_t *)cursor;
            current_interface = interface->bInterfaceNumber;
            if (!midi_interface_seen && interface->bInterfaceClass == XDJ_USB_MIDI_CLASS &&
                interface->bInterfaceSubClass == XDJ_USB_MIDI_SUBCLASS) {
                esp_err_t err = usb_host_interface_claim(s_client, s_device,
                                                          interface->bInterfaceNumber,
                                                          interface->bAlternateSetting);
                if (err != ESP_OK) {
                    return err;
                }
                s_interface = interface->bInterfaceNumber;
                s_interface_claimed = true;
                midi_interface_seen = true;
            }
        } else if (cursor[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && midi_interface_seen &&
                   current_interface == s_interface && cursor[0] >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)cursor;
            if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != XDJ_USB_XFER_BULK) {
                cursor += cursor[0];
                continue;
            }
            esp_err_t err = (endpoint->bEndpointAddress & XDJ_EP_DIR_IN) != 0
                                ? xdj_setup_in_endpoint(endpoint)
                                : xdj_setup_out_endpoint(endpoint);
            if (err != ESP_OK) {
                return err;
            }
        }
        cursor += cursor[0];
    }

    s_endpoints_ready = midi_interface_seen && s_out != NULL && s_in[0] != NULL;
    return s_endpoints_ready ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void xdj_client_event_cb(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event == NULL) {
        return;
    }
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (s_device != NULL) {
            return;
        }
        xdj_set_state(FACULTY175_XDJ_USB_ENUMERATING);
        esp_err_t err = usb_host_device_open(s_client, event->new_dev.address, &s_device);
        if (err == ESP_OK) {
            const usb_config_desc_t *config = NULL;
            err = usb_host_get_active_config_descriptor(s_device, &config);
            if (err == ESP_OK) {
                err = xdj_parse_config(config);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "XDJ MIDI enumeration failed: %s", esp_err_to_name(err));
            xdj_release_device();
            xdj_set_state(FACULTY175_XDJ_USB_ERROR);
        } else {
            xdj_set_state(FACULTY175_XDJ_USB_READY);
        }
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl == s_device) {
        xdj_release_device();
        xdj_set_state(FACULTY175_XDJ_USB_DISCONNECTED);
    }
}

static void xdj_host_task(void *arg)
{
    (void)arg;
    while (!s_stop_requested) {
        (void)usb_host_lib_handle_events(pdMS_TO_TICKS(20), NULL);
        if (s_client != NULL) {
            (void)usb_host_client_handle_events(s_client, pdMS_TO_TICKS(20));
        }
    }
    s_host_task = NULL;
    vTaskDelete(NULL);
}

static int xdj_make_server(void)
{
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        return -1;
    }
    int yes = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(XDJ_STREAM_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(server, 1) != 0) {
        close(server);
        return -1;
    }
    int flags = fcntl(server, F_GETFL, 0);
    (void)fcntl(server, F_SETFL, flags | O_NONBLOCK);
    return server;
}

static bool xdj_send_frame(int client, const xdj_packet_t *packet)
{
    uint8_t frame[8] = {0, 6, 0, 0, packet->bytes[0], packet->bytes[1], packet->bytes[2], packet->bytes[3]};
    size_t sent = 0;
    while (sent < sizeof(frame)) {
        ssize_t count = send(client, frame + sent, sizeof(frame) - sent, 0);
        if (count <= 0) {
            return false;
        }
        sent += (size_t)count;
    }
    return true;
}

static void xdj_stream_task(void *arg)
{
    (void)arg;
    const int server = xdj_make_server();
    if (server < 0) {
        ESP_LOGE(TAG, "cannot listen on TCP %u", XDJ_STREAM_PORT);
        s_stream_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    int client = -1;
    uint8_t input[XDJ_MAX_FRAME];
    size_t buffered = 0;
    while (!s_stop_requested) {
        if (client < 0) {
            client = accept(server, NULL, NULL);
            if (client >= 0) {
                int flags = fcntl(client, F_GETFL, 0);
                (void)fcntl(client, F_SETFL, flags | O_NONBLOCK);
            }
        }
        if (client >= 0) {
            ssize_t received = recv(client, input + buffered, sizeof(input) - buffered, MSG_DONTWAIT);
            if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(client);
                client = -1;
                buffered = 0;
            } else if (received > 0) {
                buffered += (size_t)received;
                size_t offset = 0;
                while (buffered - offset >= 2) {
                    const size_t payload_len = ((size_t)input[offset] << 8) | input[offset + 1];
                    const size_t frame_len = payload_len + 2;
                    if (payload_len < 6 || payload_len > XDJ_MAX_FRAME - 2) {
                        close(client);
                        client = -1;
                        buffered = 0;
                        break;
                    }
                    if (buffered - offset < frame_len) {
                        break;
                    }
                    if (input[offset + 2] == 1 && input[offset + 3] == 0 &&
                        ((payload_len - 2) % sizeof(xdj_packet_t)) == 0) {
                        for (size_t packet_offset = offset + 4;
                             packet_offset + sizeof(xdj_packet_t) <= offset + frame_len;
                             packet_offset += sizeof(xdj_packet_t)) {
                            xdj_packet_t packet;
                            memcpy(packet.bytes, input + packet_offset, sizeof(packet.bytes));
                            (void)xQueueSend(s_out_queue, &packet, 0);
                        }
                    }
                    offset += frame_len;
                }
                if (client >= 0 && offset > 0) {
                    memmove(input, input + offset, buffered - offset);
                    buffered -= offset;
                }
            }
            xdj_submit_out_if_idle();
            xdj_packet_t packet;
            while (xQueueReceive(s_in_queue, &packet, 0) == pdPASS && !xdj_send_frame(client, &packet)) {
                close(client);
                client = -1;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (client >= 0) {
        close(client);
    }
    close(server);
    s_stream_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t faculty175_xdj_bridge_start(void)
{
#if !ASTROLABE185B_XDJ_BUILD
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_started) {
        return ESP_OK;
    }
    s_in_queue = xQueueCreate(XDJ_QUEUE_DEPTH, sizeof(xdj_packet_t));
    s_out_queue = xQueueCreate(XDJ_QUEUE_DEPTH, sizeof(xdj_packet_t));
    if (s_in_queue == NULL || s_out_queue == NULL) {
        if (s_in_queue != NULL) {
            vQueueDelete(s_in_queue);
            s_in_queue = NULL;
        }
        if (s_out_queue != NULL) {
            vQueueDelete(s_out_queue);
            s_out_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }
    const usb_host_config_t host_config = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = XDJ_HOST_EVENTS,
        .async = {.client_event_callback = xdj_client_event_cb, .callback_arg = NULL},
    };
    err = usb_host_client_register(&client_config, &s_client);
    if (err != ESP_OK) {
        vQueueDelete(s_in_queue);
        vQueueDelete(s_out_queue);
        s_in_queue = NULL;
        s_out_queue = NULL;
        return err;
    }
    s_stop_requested = false;
    s_started = true;
    xTaskCreate(xdj_host_task, "xdj_usb_host", 4096, NULL, 8, &s_host_task);
    xTaskCreate(xdj_stream_task, "xdj_stream", 4096, NULL, 5, &s_stream_task);
    return ESP_OK;
#endif
}

esp_err_t faculty175_xdj_bridge_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }
    s_stop_requested = true;
    if (s_client != NULL) {
        (void)usb_host_client_unblock(s_client);
    }
    (void)usb_host_lib_unblock();
    while (s_host_task != NULL || s_stream_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_device != NULL) {
        xdj_release_device();
    }
    if (s_client != NULL) {
        (void)usb_host_client_deregister(s_client);
        s_client = NULL;
    }
    (void)usb_host_uninstall();
    if (s_in_queue != NULL) {
        vQueueDelete(s_in_queue);
        s_in_queue = NULL;
    }
    if (s_out_queue != NULL) {
        vQueueDelete(s_out_queue);
        s_out_queue = NULL;
    }
    s_started = false;
    s_usb_state = FACULTY175_XDJ_USB_DISCONNECTED;
    return ESP_OK;
}

bool faculty175_xdj_bridge_running(void)
{
    return s_started && !s_stop_requested;
}

faculty175_xdj_usb_state_t faculty175_xdj_bridge_usb_state(void)
{
    return s_usb_state;
}

uint16_t faculty175_xdj_bridge_stream_port(void)
{
    return XDJ_STREAM_PORT;
}
