#ifndef DTF_C_OTA_PROVIDER_H
#define DTF_C_OTA_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif
typedef enum dtf_ota_response
{
    DTFOTA_UnknownError = -1,
    DTFOTA_NewFirmwareFlashed = 0,
    DTFOTA_InvalidFirmwareImage = 1,
    DTFOTA_NotEnoughMemory = 2,
    DTFOTA_FirmwareWriteFailed = 3,
    DTFOTA_NoUpdatesAvailable = 4,
    DTFOTA_CertValidationFailed = 5,
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

#ifdef __cplusplus
}
#endif

#endif //DTF_C_OTA_PROVIDER_H