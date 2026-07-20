#include "faculty175_km.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "class/hid/hid.h"
#include "tusb.h"

#ifndef ASTROLABE_KM_ENABLED
#define ASTROLABE_KM_ENABLED 0
#endif

#if ASTROLABE_KM_ENABLED

static const char *TAG = "faculty175_km";

enum {
    KM_HID = 0,
    KM_REPORT_KEYBOARD = 1,
    KM_REPORT_MOUSE = 2,
};

typedef enum {
    KM_CMD_MOUSE,
    KM_CMD_KEY,
    KM_CMD_TEXT,
    KM_CMD_RELEASE,
    KM_CMD_INSTALL,
} km_command_kind_t;

typedef struct {
    km_command_kind_t kind;
    union {
        struct {
            int16_t dx;
            int16_t dy;
            int8_t wheel;
            uint8_t buttons;
        } mouse;
        struct {
            uint8_t keycode;
            uint8_t modifiers;
            bool down;
        } key;
        char text[160];
    } data;
} km_command_t;

static QueueHandle_t s_queue;
static char s_pairing_code[7] = "------";
static TickType_t s_install_armed_at;

static const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303a,
    .idProduct = 0x8003,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const char *s_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Castalia Institute",
    "Astrolabe WiFi KM",
    "astrolabe-wifi-km",
    "Astrolabe Console",
    "Astrolabe USBFLASH",
    "Boot Keyboard",
    "Boot Mouse",
};

static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(KM_REPORT_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(KM_REPORT_MOUSE)),
};

#define KM_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + TUD_HID_DESC_LEN)

enum {
    KM_ITF_CDC = 0,
    KM_ITF_CDC_DATA,
    KM_ITF_MSC,
    KM_ITF_HID,
    KM_ITF_TOTAL,
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, KM_ITF_TOTAL, 0, KM_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(KM_ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_MSC_DESCRIPTOR(KM_ITF_MSC, 5, 0x03, 0x83, 64),
    TUD_HID_DESCRIPTOR(KM_ITF_HID, 6, HID_ITF_PROTOCOL_NONE,
                       sizeof(s_hid_report_descriptor), 0x84,
                       CFG_TUD_HID_EP_BUFSIZE, 5),
};

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool faculty175_km_enabled(void)
{
    return true;
}

const tusb_desc_device_t *faculty175_km_device_descriptor(void)
{
    return &s_device_descriptor;
}

const uint8_t *faculty175_km_configuration_descriptor(void)
{
    return s_configuration_descriptor;
}

const char **faculty175_km_string_descriptors(void)
{
    return s_string_descriptors;
}

size_t faculty175_km_string_descriptor_count(void)
{
    return sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]);
}

const char *faculty175_km_pairing_code(void)
{
    return s_pairing_code;
}

bool faculty175_km_usb_ready(void)
{
    return tud_mounted();
}

static bool wait_ready(uint8_t instance, TickType_t timeout)
{
    const TickType_t started = xTaskGetTickCount();
    while (!tud_hid_n_ready(instance)) {
        if ((xTaskGetTickCount() - started) >= timeout) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

static void send_keyboard(uint8_t modifiers, const uint8_t keys[6])
{
    if (wait_ready(KM_HID, pdMS_TO_TICKS(100))) {
        (void)tud_hid_n_keyboard_report(KM_HID, KM_REPORT_KEYBOARD, modifiers, keys);
    }
}

static void send_mouse(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel)
{
    while (dx != 0 || dy != 0 || wheel != 0) {
        const int8_t part_x = dx > 127 ? 127 : (dx < -127 ? -127 : (int8_t)dx);
        const int8_t part_y = dy > 127 ? 127 : (dy < -127 ? -127 : (int8_t)dy);
        const int8_t part_w = wheel;
        if (!wait_ready(KM_HID, pdMS_TO_TICKS(100))) {
            return;
        }
        (void)tud_hid_n_mouse_report(KM_HID, KM_REPORT_MOUSE, buttons, part_x, part_y, part_w, 0);
        dx -= part_x;
        dy -= part_y;
        wheel -= part_w;
    }
    if (dx == 0 && dy == 0 && wheel == 0 && wait_ready(KM_HID, pdMS_TO_TICKS(100))) {
        (void)tud_hid_n_mouse_report(KM_HID, KM_REPORT_MOUSE, buttons, 0, 0, 0, 0);
    }
}

static bool ascii_key(char ch, uint8_t *keycode, uint8_t *modifier)
{
    *modifier = 0;
    if (ch >= 'a' && ch <= 'z') {
        *keycode = HID_KEY_A + (uint8_t)(ch - 'a');
        return true;
    }
    if (ch >= 'A' && ch <= 'Z') {
        *keycode = HID_KEY_A + (uint8_t)(ch - 'A');
        *modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
        return true;
    }
    if (ch >= '1' && ch <= '9') {
        *keycode = HID_KEY_1 + (uint8_t)(ch - '1');
        return true;
    }
    if (ch == '0') {
        *keycode = HID_KEY_0;
        return true;
    }
    switch (ch) {
        case ' ': *keycode = HID_KEY_SPACE; return true;
        case '\n': *keycode = HID_KEY_ENTER; return true;
        case '\t': *keycode = HID_KEY_TAB; return true;
        case '-': *keycode = HID_KEY_MINUS; return true;
        case '_': *keycode = HID_KEY_MINUS; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '=': *keycode = HID_KEY_EQUAL; return true;
        case '+': *keycode = HID_KEY_EQUAL; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '[': *keycode = HID_KEY_BRACKET_LEFT; return true;
        case '{': *keycode = HID_KEY_BRACKET_LEFT; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case ']': *keycode = HID_KEY_BRACKET_RIGHT; return true;
        case '}': *keycode = HID_KEY_BRACKET_RIGHT; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '\\': *keycode = HID_KEY_BACKSLASH; return true;
        case '|': *keycode = HID_KEY_BACKSLASH; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case ';': *keycode = HID_KEY_SEMICOLON; return true;
        case ':': *keycode = HID_KEY_SEMICOLON; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '\'': *keycode = HID_KEY_APOSTROPHE; return true;
        case '"': *keycode = HID_KEY_APOSTROPHE; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '`': *keycode = HID_KEY_GRAVE; return true;
        case '~': *keycode = HID_KEY_GRAVE; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case ',': *keycode = HID_KEY_COMMA; return true;
        case '<': *keycode = HID_KEY_COMMA; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '.': *keycode = HID_KEY_PERIOD; return true;
        case '>': *keycode = HID_KEY_PERIOD; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '/': *keycode = HID_KEY_SLASH; return true;
        case '?': *keycode = HID_KEY_SLASH; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '!': *keycode = HID_KEY_1; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '@': *keycode = HID_KEY_2; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '#': *keycode = HID_KEY_3; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '$': *keycode = HID_KEY_4; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '%': *keycode = HID_KEY_5; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '^': *keycode = HID_KEY_6; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '&': *keycode = HID_KEY_7; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '*': *keycode = HID_KEY_8; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case '(': *keycode = HID_KEY_9; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        case ')': *keycode = HID_KEY_0; *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true;
        default: return false;
    }
}

static void release_reports(uint8_t keys[6], uint8_t *mouse_buttons)
{
    memset(keys, 0, 6);
    *mouse_buttons = 0;
    send_keyboard(0, keys);
    send_mouse(0, 0, 0, 0);
}

static void type_ascii_text(const char *text)
{
    for (size_t i = 0; text[i] != '\0'; ++i) {
        uint8_t code = 0;
        uint8_t mod = 0;
        if (!ascii_key(text[i], &code, &mod)) {
            continue;
        }
        uint8_t one_key[6] = {code};
        send_keyboard(mod, one_key);
        send_keyboard(0, NULL);
    }
}

static void km_worker(void *arg)
{
    (void)arg;
    uint8_t keys[6] = {};
    uint8_t modifiers = 0;
    uint8_t mouse_buttons = 0;
    TickType_t last_input = xTaskGetTickCount();

    while (true) {
        km_command_t cmd = {};
        if (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(250)) != pdTRUE) {
            if ((modifiers != 0 || mouse_buttons != 0 || keys[0] != 0) &&
                (xTaskGetTickCount() - last_input) >= pdMS_TO_TICKS(1500)) {
                release_reports(keys, &mouse_buttons);
                modifiers = 0;
                ESP_LOGW(TAG, "input watchdog released held controls");
            }
            continue;
        }
        last_input = xTaskGetTickCount();

        if (cmd.kind == KM_CMD_RELEASE) {
            release_reports(keys, &mouse_buttons);
            modifiers = 0;
        } else if (cmd.kind == KM_CMD_MOUSE) {
            mouse_buttons = cmd.data.mouse.buttons;
            send_mouse(mouse_buttons, cmd.data.mouse.dx, cmd.data.mouse.dy, cmd.data.mouse.wheel);
        } else if (cmd.kind == KM_CMD_KEY) {
            modifiers = cmd.data.key.modifiers;
            const uint8_t code = cmd.data.key.keycode;
            if (code != 0) {
                if (cmd.data.key.down) {
                    bool present = false;
                    for (size_t i = 0; i < 6; ++i) {
                        present = present || keys[i] == code;
                    }
                    if (!present) {
                        for (size_t i = 0; i < 6; ++i) {
                            if (keys[i] == 0) {
                                keys[i] = code;
                                break;
                            }
                        }
                    }
                } else {
                    for (size_t i = 0; i < 6; ++i) {
                        if (keys[i] == code) {
                            keys[i] = 0;
                        }
                    }
                }
            }
            send_keyboard(modifiers, keys);
        } else if (cmd.kind == KM_CMD_TEXT) {
            release_reports(keys, &mouse_buttons);
            modifiers = 0;
            type_ascii_text(cmd.data.text);
        } else if (cmd.kind == KM_CMD_INSTALL) {
            release_reports(keys, &mouse_buttons);
            modifiers = 0;
            const uint8_t terminal_key[6] = {HID_KEY_T};
            send_keyboard(KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTALT, terminal_key);
            send_keyboard(0, NULL);
            vTaskDelay(pdMS_TO_TICKS(1500));
            type_ascii_text("p=$(find /media/$USER -name astrolabe-install.sh -print -quit);bash \"$p\"");
            const uint8_t enter_key[6] = {HID_KEY_ENTER};
            send_keyboard(0, enter_key);
            send_keyboard(0, NULL);
        }
    }
}

esp_err_t faculty175_km_start(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    snprintf(s_pairing_code, sizeof(s_pairing_code), "%06lu",
             (unsigned long)(esp_random() % 1000000u));
    s_queue = xQueueCreate(32, sizeof(km_command_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(km_worker, "ast_km", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WiFi KM pairing code: %s", s_pairing_code);
    printf("Astrolabe WiFi KM pairing code: %s\n", s_pairing_code);
    return ESP_OK;
}

static esp_err_t queue_command(const km_command_t *cmd)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_queue, cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t faculty175_km_mouse(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons)
{
    const km_command_t cmd = {
        .kind = KM_CMD_MOUSE,
        .data.mouse = {.dx = dx, .dy = dy, .wheel = wheel, .buttons = (uint8_t)(buttons & 0x07)},
    };
    return queue_command(&cmd);
}

esp_err_t faculty175_km_key(uint8_t keycode, bool down, uint8_t modifiers)
{
    const km_command_t cmd = {
        .kind = KM_CMD_KEY,
        .data.key = {.keycode = keycode, .modifiers = (uint8_t)(modifiers & 0x0f), .down = down},
    };
    return queue_command(&cmd);
}

esp_err_t faculty175_km_type_text(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    km_command_t cmd = {.kind = KM_CMD_TEXT};
    strlcpy(cmd.data.text, text, sizeof(cmd.data.text));
    return queue_command(&cmd);
}

esp_err_t faculty175_km_release_all(void)
{
    const km_command_t cmd = {.kind = KM_CMD_RELEASE};
    return queue_command(&cmd);
}

bool faculty175_km_install_armed(void)
{
    return s_install_armed_at != 0 &&
           (xTaskGetTickCount() - s_install_armed_at) < pdMS_TO_TICKS(10000);
}

esp_err_t faculty175_km_install_pi_agent(void)
{
    if (!faculty175_km_install_armed()) {
        s_install_armed_at = xTaskGetTickCount();
        return ESP_ERR_NOT_FINISHED;
    }
    s_install_armed_at = 0;
    const km_command_t cmd = {.kind = KM_CMD_INSTALL};
    return queue_command(&cmd);
}

typedef struct {
    const char *name;
    uint8_t code;
} dom_key_t;

uint8_t faculty175_km_keycode_from_dom(const char *code)
{
    if (code == NULL) {
        return 0;
    }
    if (strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z' && code[4] == '\0') {
        return HID_KEY_A + (uint8_t)(code[3] - 'A');
    }
    if (strncmp(code, "Digit", 5) == 0 && code[5] >= '1' && code[5] <= '9' && code[6] == '\0') {
        return HID_KEY_1 + (uint8_t)(code[5] - '1');
    }
    if (strcmp(code, "Digit0") == 0) {
        return HID_KEY_0;
    }
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9' && code[2] == '\0') {
        return HID_KEY_F1 + (uint8_t)(code[1] - '1');
    }
    static const dom_key_t keys[] = {
        {"Enter", HID_KEY_ENTER}, {"Escape", HID_KEY_ESCAPE}, {"Backspace", HID_KEY_BACKSPACE},
        {"Tab", HID_KEY_TAB}, {"Space", HID_KEY_SPACE}, {"Minus", HID_KEY_MINUS},
        {"Equal", HID_KEY_EQUAL}, {"BracketLeft", HID_KEY_BRACKET_LEFT},
        {"BracketRight", HID_KEY_BRACKET_RIGHT}, {"Backslash", HID_KEY_BACKSLASH},
        {"Semicolon", HID_KEY_SEMICOLON}, {"Quote", HID_KEY_APOSTROPHE},
        {"Backquote", HID_KEY_GRAVE}, {"Comma", HID_KEY_COMMA}, {"Period", HID_KEY_PERIOD},
        {"Slash", HID_KEY_SLASH}, {"CapsLock", HID_KEY_CAPS_LOCK}, {"PrintScreen", HID_KEY_PRINT_SCREEN},
        {"ScrollLock", HID_KEY_SCROLL_LOCK}, {"Pause", HID_KEY_PAUSE}, {"Insert", HID_KEY_INSERT},
        {"Home", HID_KEY_HOME}, {"PageUp", HID_KEY_PAGE_UP}, {"Delete", HID_KEY_DELETE},
        {"End", HID_KEY_END}, {"PageDown", HID_KEY_PAGE_DOWN}, {"ArrowRight", HID_KEY_ARROW_RIGHT},
        {"ArrowLeft", HID_KEY_ARROW_LEFT}, {"ArrowDown", HID_KEY_ARROW_DOWN}, {"ArrowUp", HID_KEY_ARROW_UP},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (strcmp(code, keys[i].name) == 0) {
            return keys[i].code;
        }
    }
    return 0;
}

#else

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return NULL;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool faculty175_km_enabled(void) { return false; }
const tusb_desc_device_t *faculty175_km_device_descriptor(void) { return NULL; }
const uint8_t *faculty175_km_configuration_descriptor(void) { return NULL; }
const char **faculty175_km_string_descriptors(void) { return NULL; }
size_t faculty175_km_string_descriptor_count(void) { return 0; }
esp_err_t faculty175_km_start(void) { return ESP_ERR_NOT_SUPPORTED; }
const char *faculty175_km_pairing_code(void) { return "------"; }
bool faculty175_km_usb_ready(void) { return false; }
esp_err_t faculty175_km_mouse(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons)
{ (void)dx; (void)dy; (void)wheel; (void)buttons; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t faculty175_km_key(uint8_t keycode, bool down, uint8_t modifiers)
{ (void)keycode; (void)down; (void)modifiers; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t faculty175_km_type_text(const char *text)
{ (void)text; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t faculty175_km_release_all(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t faculty175_km_install_pi_agent(void) { return ESP_ERR_NOT_SUPPORTED; }
bool faculty175_km_install_armed(void) { return false; }
uint8_t faculty175_km_keycode_from_dom(const char *code) { (void)code; return 0; }

#endif
