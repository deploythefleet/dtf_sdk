# Deploy the Fleet C SDK

This C SDK integrates into existing projects to enable OTA (over-the-air) updates using the 
[Deploy the Fleet](https://deploythefleet.io) service.

# Prerequisites

- A [Deploy the Fleet](https://deploythefleet.io) account
- WiFi connectivity code in your project

> [!NOTE]
> The DTF SDK is purpose-built to handle OTA updates and does not have logic to manage your WiFi connection.
> Your existing firmware is responsible for connecting to WiFi and ensuring that connection is active
> before calling this SDK.

# Installation

## Add the SDK To Your Project

The easiest way to add the SDK to an existing project is as a git submodule. From the root of your project 
run the following command.

```sh
git submodule add https://github.com/deploythefleet/dtf_sdk.git ./components/dtf_sdk
```

# Usage

The SDK is extremely simple to use. Please ensure your device has an active connection to the internet 
before calling the SDK. Otherwise you will receive an error.

```cpp
const dtf_ota_cfg_t cfg = {
      .product_id = [YOUR DTF PRODUCT ID],
      .reboot_option = DTF_NO_REBOOT};

DTF_OtaResponse ret = dtf_get_firmware_update(&cfg);
```

Your product ID is available in the Deploy the Fleet dashboard.

## Options

The behavior of `dtf_get_firmware_update` is controlled by the `dtf_ota_cfg_g` struct. It it defined as:

```cpp
typedef struct _dtf_ota_config
{
    const char *product_id;
    const char *custom_version;
    DTF_RebootOption reboot_option;
}dtf_ota_cfg_t;
```
### Custom Version
By default, the SDK uses the built-in IDF-resolved version of your firmware. Please see the IDF docs for more 
information about how that version is derived. If you would like to override that version logic and provide 
your own custom version to be reported to Deploy the Fleet, you can do that with the `custom_version` setting 
in the configuration.

### Reboot Option
**Default:** Reboot on successful update

```cpp
typedef enum dtf_reboot_option
{
    DTF_NO_REBOOT = 0,
    DTF_REBOOT_ON_SUCCESS,
}DTF_RebootOption;
```