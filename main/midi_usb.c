// USB host side: finds the MIDI Streaming interface of whatever is plugged into the
// Type-A port (a class-compliant piano, or a USB-to-DIN MIDI cable) and reads its IN endpoint.
// ESP-IDF has no MIDI host class driver, so this talks to the USB Host Library directly.
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "app.h"
#include "midi_parse.h"

static const char *TAG = "usb";

#define USB_CLASS_AUDIO_       0x01
#define USB_SUBCLASS_MIDI_     0x03
#define MAX_XFER_ERRORS        20

static struct {
    usb_host_client_handle_t client;
    usb_device_handle_t dev;
    usb_transfer_t *xfer;
    uint8_t intf;
    bool claimed;
    volatile bool inflight;
    volatile bool gone;
    volatile uint8_t pending_addr;      // device waiting to be inspected, 0 = none
    volatile bool connected;
    int errors;
    char name[48];
} s;

static midi_parser_t s_parser;

static void on_msg(void *ctx, int64_t t_us, const uint8_t *msg, size_t len)
{
    recorder_submit(SRC_USB, t_us, msg, len);
}

static void submit(usb_transfer_t *t)
{
    if (usb_host_transfer_submit(t) == ESP_OK) s.inflight = true;
    else ESP_LOGW(TAG, "transfer submit failed");
}

// Runs in the client task (from usb_host_client_handle_events).
static void xfer_cb(usb_transfer_t *t)
{
    s.inflight = false;
    switch (t->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
        s.errors = 0;
        if (t->actual_num_bytes >= 4)
            midi_parser_feed_usb(&s_parser, esp_timer_get_time(), t->data_buffer, (size_t)t->actual_num_bytes);
        submit(t);
        break;
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
        break;                          // unplugged: the DEV_GONE event cleans up
    default:
        if (t->status == USB_TRANSFER_STATUS_STALL)
            usb_host_endpoint_clear(s.dev, t->bEndpointAddress);
        if (++s.errors < MAX_XFER_ERRORS) submit(t);
        else ESP_LOGE(TAG, "too many transfer errors (status %d), giving up until replug", t->status);
        break;
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!s.dev && !s.pending_addr) s.pending_addr = msg->new_dev.address;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (msg->dev_gone.dev_hdl == s.dev) s.gone = true;
    }
}

static void read_name(usb_device_handle_t dev)
{
    strcpy(s.name, "USB MIDI device");
    usb_device_info_t info;
    if (usb_host_device_info(dev, &info) != ESP_OK || !info.str_desc_product) return;
    const usb_str_desc_t *sd = info.str_desc_product;
    int n = (sd->bLength - 2) / 2, j = 0;
    for (int i = 0; i < n && j < (int)sizeof s.name - 1; i++) {
        uint16_t c = sd->wData[i];
        s.name[j++] = (c >= 32 && c < 127 && c != '"' && c != '\\') ? (char)c : '?';
    }
    if (j) s.name[j] = 0;
}

static void open_device(uint8_t addr)
{
    usb_device_handle_t dev;
    if (usb_host_device_open(s.client, addr, &dev) != ESP_OK) return;

    const usb_config_desc_t *cfg;
    const usb_ep_desc_t *ep_in = NULL;
    int intf_num = -1, alt = 0;
    if (usb_host_get_active_config_descriptor(dev, &cfg) == ESP_OK) {
        int offset = 0;
        bool in_midi = false;
        const usb_standard_desc_t *d = (const usb_standard_desc_t *)cfg;
        while ((d = usb_parse_next_descriptor(d, cfg->wTotalLength, &offset)) != NULL) {
            if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                const usb_intf_desc_t *i = (const usb_intf_desc_t *)d;
                in_midi = i->bInterfaceClass == USB_CLASS_AUDIO_ && i->bInterfaceSubClass == USB_SUBCLASS_MIDI_;
                if (in_midi) { intf_num = i->bInterfaceNumber; alt = i->bAlternateSetting; }
            } else if (in_midi && d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                const usb_ep_desc_t *e = (const usb_ep_desc_t *)d;
                uint8_t type = e->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
                bool is_in = e->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK;
                if (is_in && (type == USB_BM_ATTRIBUTES_XFER_BULK || type == USB_BM_ATTRIBUTES_XFER_INT)) {
                    ep_in = e;
                    break;
                }
            }
        }
    }
    if (!ep_in) {
        ESP_LOGW(TAG, "device %d has no MIDI IN endpoint (not class-compliant?), ignoring", addr);
        usb_host_device_close(s.client, dev);
        return;
    }
    if (usb_host_interface_claim(s.client, dev, (uint8_t)intf_num, (uint8_t)alt) != ESP_OK) {
        ESP_LOGE(TAG, "cannot claim interface %d", intf_num);
        usb_host_device_close(s.client, dev);
        return;
    }
    int mps = USB_EP_DESC_GET_MPS(ep_in);
    if (mps < 8) mps = 8;
    usb_transfer_t *t;
    if (usb_host_transfer_alloc((size_t)mps, 0, &t) != ESP_OK) {
        usb_host_interface_release(s.client, dev, (uint8_t)intf_num);
        usb_host_device_close(s.client, dev);
        return;
    }
    t->device_handle = dev;
    t->bEndpointAddress = ep_in->bEndpointAddress;
    t->callback = xfer_cb;
    t->context = NULL;
    t->num_bytes = mps;                 // IN transfers must be a multiple of the packet size
    t->timeout_ms = 0;

    s.dev = dev;
    s.intf = (uint8_t)intf_num;
    s.claimed = true;
    s.xfer = t;
    s.errors = 0;
    s.gone = false;
    midi_parser_init(&s_parser, on_msg, NULL);
    read_name(dev);
    s.connected = true;
    ESP_LOGI(TAG, "MIDI device connected: %s (interface %d, endpoint 0x%02x, mps %d)",
             s.name, intf_num, ep_in->bEndpointAddress, mps);
    submit(t);
}

static void close_device(void)
{
    s.connected = false;
    if (s.claimed) usb_host_interface_release(s.client, s.dev, s.intf);
    if (s.xfer) usb_host_transfer_free(s.xfer);
    usb_host_device_close(s.client, s.dev);
    s.dev = NULL;
    s.xfer = NULL;
    s.claimed = false;
    s.gone = false;
    ESP_LOGI(TAG, "MIDI device removed");
}

static void client_task(void *arg)
{
    const usb_host_client_config_t ccfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = { .client_event_callback = client_event_cb, .callback_arg = NULL },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&ccfg, &s.client));
    for (;;) {
        usb_host_client_handle_events(s.client, pdMS_TO_TICKS(200));
        if (s.gone && s.dev && !s.inflight) close_device();
        if (s.pending_addr && !s.dev) {
            uint8_t addr = s.pending_addr;
            s.pending_addr = 0;
            open_device(addr);
        }
    }
}

static void host_lib_task(void *arg)
{
    const usb_host_config_t cfg = {
        .skip_phy_setup = false,        // internal high-speed PHY wired to the Type-A port
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    xTaskCreate(client_task, "usb_midi", 4096, NULL, 9, NULL);
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

void midi_usb_start(void)
{
    xTaskCreate(host_lib_task, "usb_host", 4096, NULL, 10, NULL);
}

bool midi_usb_connected(char *name, size_t name_len)
{
    if (name && name_len) strlcpy(name, s.connected ? s.name : "", name_len);
    return s.connected;
}
