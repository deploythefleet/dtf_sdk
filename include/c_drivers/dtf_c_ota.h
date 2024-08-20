#ifndef DTF_C_OTA_PROVIDER_H
#define DTF_C_OTA_PROVIDER_H

typedef enum dtf_ota_response
{
    DTFOTA_NewFirmwareFlashed = 0,
    DTFOTA_InvalidFirmwareImage,
    DTFOTA_NotEnoughMemory,
    DTFOTA_FirmwareWriteFailed,
    DTFOTA_NoUpdatesAvailable,
    DTFOTA_UnknownError
}DTF_OtaResponse;

typedef enum dtf_reboot_option
{
    DTF_NO_REBOOT = 0,
    DTF_REBOOT_ON_SUCCESS,
}DTF_RebootOption;

typedef struct _dtf_ota_config
{
    const char *product_id;
    const char *custom_version;
    DTF_RebootOption reboot_option;
}dtf_ota_cfg_t;

DTF_OtaResponse dtf_get_firmware_update(const dtf_ota_cfg_t *cfg);
const char* dtf_get_active_fw_version();

#endif //DTF_C_OTA_PROVIDER_H