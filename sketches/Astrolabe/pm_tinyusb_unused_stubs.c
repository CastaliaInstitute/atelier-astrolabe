#include <stdbool.h>
#include <stdint.h>

#include "class/hid/hid.h"

__attribute__((weak)) uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return NULL;
}

__attribute__((weak)) uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                                     hid_report_type_t report_type,
                                                     uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

__attribute__((weak)) void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                                 hid_report_type_t report_type,
                                                 uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

__attribute__((weak)) int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                                void *buffer, uint32_t bufsize)
{
    (void)lun;
    (void)lba;
    (void)offset;
    (void)buffer;
    (void)bufsize;
    return -1;
}

__attribute__((weak)) int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                                 uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    (void)lba;
    (void)offset;
    (void)buffer;
    (void)bufsize;
    return -1;
}

__attribute__((weak)) bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return false;
}

__attribute__((weak)) void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                                               uint16_t *block_size)
{
    (void)lun;
    if (block_count) {
        *block_count = 0;
    }
    if (block_size) {
        *block_size = 0;
    }
}

__attribute__((weak)) int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                                              void *buffer, uint16_t bufsize)
{
    (void)lun;
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;
    return -1;
}

__attribute__((weak)) void tud_dfu_runtime_reboot_to_dfu_cb(void)
{
}

__attribute__((weak)) uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
    (void)alt;
    (void)state;
    return 0;
}

__attribute__((weak)) void tud_dfu_download_cb(uint8_t alt, uint16_t block_num,
                                               uint8_t const *data, uint16_t length)
{
    (void)alt;
    (void)block_num;
    (void)data;
    (void)length;
}

__attribute__((weak)) void tud_dfu_manifest_cb(uint8_t alt)
{
    (void)alt;
}

__attribute__((weak)) bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    (void)src;
    (void)size;
    return false;
}

__attribute__((weak)) void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                                      const uint8_t *report, uint16_t len)
{
    (void)dev_addr;
    (void)idx;
    (void)report;
    (void)len;
}
