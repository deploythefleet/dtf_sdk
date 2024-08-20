# Getting Started With the DTF SDK

This is the simplest possible example of how to get started with the Deploy the Fleet SDK.

It uses the IDF boilerplate code to connect to WiFi and then checks Deploy the Fleet for updates.

## How to Use The Example

 1. Clone the DTF SDK into your _components_ folder of your ESP-IDF project
 1. Replace the **main** folder of your project with the main folder of this example
 1. Set `WIFI_SSID` and `WIFI_PASSWORD` in **wifi_connect.c** in your **main** folder
 1. Set `DTF_PRODUCT_ID` in **main.c** to the GUID in your Deploy the Fleet update URL. _Do not use the entire URL_
 1. Build and flash the project.

> [!NOTE]
> The example has simple IDF boilerplate code to connect to WiFi which is not production ready. It only serves to make sure the device
> is connected to the internet. Your project should have its own, production-ready, connectivity layer built in.

## Example Output
```
Current firmware version: 1.0.0
Connecting to WiFi
I (631) wifi:wifi driver task: 3ffc0868, prio:23, stack:6656, core=0
I (641) wifi:wifi firmware version: 0caa81945
I (641) wifi:wifi certification version: v7.0
I (641) wifi:config NVS flash: enabled
I (641) wifi:config nano formating: disabled
I (651) wifi:Init data frame dynamic rx buffer num: 32
I (651) wifi:Init static rx mgmt buffer num: 5
I (661) wifi:Init management short buffer num: 32
I (661) wifi:Init dynamic tx buffer num: 32
I (671) wifi:Init static rx buffer size: 1600
I (671) wifi:Init static rx buffer num: 10
I (671) wifi:Init dynamic rx buffer num: 32
I (681) wifi_init: rx ba win: 6
I (681) wifi_init: accept mbox: 6
I (691) wifi_init: tcpip mbox: 32
I (691) wifi_init: udp mbox: 6
I (691) wifi_init: tcp mbox: 6
I (701) wifi_init: tcp tx win: 5760
I (701) wifi_init: tcp rx win: 5760
I (701) wifi_init: tcp mss: 1440
I (711) wifi_init: WiFi IRAM OP enabled
I (711) wifi_init: WiFi RX IRAM OP enabled
I (721) phy_init: phy_version 4830,54550f7,Jun 20 2024,14:22:08
I (801) wifi:mode : sta (ac:0b:fb:6c:f4:2c)
I (801) wifi:enable tsf
I (801) wifi station: wifi_init_sta finished.
I (831) wifi:new:<6,0>, old:<1,0>, ap:<255,255>, sta:<6,0>, prof:1, snd_ch_cfg:0x0
I (831) wifi:state: init -> auth (0xb0)
I (2201) wifi:state: auth -> assoc (0x0)
I (2211) wifi:state: assoc -> run (0x10)
I (2241) wifi:connected with ******, aid = 1, channel 6, BW20, bssid = fa:e2:c6:d2:6a:82
I (2241) wifi:security: WPA3-SAE, phy: bgn, rssi: -59
I (2241) wifi:pm start, type: 1

I (2241) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (2251) wifi:AP's beacon interval = 102400 us, DTIM period = 1
I (2271) wifi:<ba-add>idx:0 (ifx:0, fa:e2:c6:d2:6a:82), tid:6, ssn:2, winSize:64
I (3261) esp_netif_handlers: sta ip: 192.168.3.94, mask: 255.255.255.0, gw: 192.168.3.1
I (3261) wifi station: got ip:192.168.3.94
I (3261) wifi station: connected to ap SSID:****** password:*******
Checking for updates from Deploy the Fleet
I (3321) wifi:<ba-add>idx:1 (ifx:0, fa:e2:c6:d2:6a:82), tid:0, ssn:0, winSize:64
I (3571) esp-x509-crt-bundle: Certificate validated
I (9841) esp_https_ota: Starting OTA...
I (9841) esp_https_ota: Writing to partition subtype 17 at offset 0x1c0000
E (9841) esp_https_ota: Complete headers were not received
W (9851) ota_provider: OTA Error: -1
I (9851) ota_provider: No updates available
I (9861) main_task: Returned from app_main()
```