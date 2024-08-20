#include <stdio.h>
#include <esp_event.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dtf_c_ota.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_connect.h"

#define DTF_PRODUCT_ID "a9d40ef1-a48e-41db-b3c6-51a0c163297f"

void app_main(void)
{
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  printf("Current firmware version: %s\n", dtf_get_active_fw_version());
  printf("Connecting to WiFi\n");

  connect_to_wifi();

  printf("Checking for updates from Deploy the Fleet\n");
  const dtf_ota_cfg_t cfg = {
      .product_id = DTF_PRODUCT_ID,
      .reboot_option = DTF_REBOOT_ON_SUCCESS};

  if (is_wifi_connected())
  {
    dtf_get_firmware_update(&cfg);
  }
}