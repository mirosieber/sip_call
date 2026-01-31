/*
   Copyright 2017 Christian Taedcke <hacking@taedcke.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sip_client/lwip_udp_client.h"
#include "sip_client/mbedtls_md5.h"
#include "sip_client/sip_client.h"

#include "button_handler.h"

#include <string.h>

static constexpr auto BELL_GPIO_PIN =
    static_cast<gpio_num_t>(CONFIG_BELL_INPUT_GPIO);
static constexpr auto RING_DURATION_TIMEOUT_MSEC = CONFIG_RING_DURATION;

#if CONFIG_POWER_SAVE_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MODEM
#elif CONFIG_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

/* FreeRTOS event group to signal when we are connected properly */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * one event
 * - are we connected to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static const char *TAG = "main";

using SipClientT = SipClient<LwipUdpClient, MbedtlsMd5>;

SipClientT client{CONFIG_SIP_USER, CONFIG_SIP_PASSWORD, CONFIG_SIP_SERVER_IP,
                  CONFIG_SIP_SERVER_PORT, CONFIG_LOCAL_IP};

// Log SIP configuration at startup
static void log_sip_config() {
  ESP_LOGI(TAG, "=== SIP Configuration ===");
  ESP_LOGI(TAG, "User: %s", CONFIG_SIP_USER);
  ESP_LOGI(TAG, "Password: %s",
           strlen(CONFIG_SIP_PASSWORD) > 0 ? "[SET]" : "[EMPTY]");
  ESP_LOGI(TAG, "Server: %s:%s", CONFIG_SIP_SERVER_IP, CONFIG_SIP_SERVER_PORT);
  ESP_LOGI(TAG, "========================");
}

static std::string ip_to_string(const ip4_addr_t *ip) {
  static constexpr size_t BUFFER_SIZE = 16;
  char buffer[BUFFER_SIZE];
  snprintf(buffer, BUFFER_SIZE, IPSTR, IP2STR(ip));
  return std::string(buffer);
}

// static std::string get_gw_ip_address(const system_event_sta_got_ip_t *got_ip)
// {
//     const ip4_addr_t *gateway = &got_ip->ip_info.gw;
//     return ip_to_string(gateway);
// }

// rozwiazanie ip bramy zamiast srv ~sikor
static std::string get_srv_ip_address() { return CONFIG_SIP_SERVER_IP; }

static std::string get_local_ip_address(const ip_event_got_ip_t *got_ip) {
  const esp_ip4_addr_t *local_addr = &got_ip->ip_info.ip;
  return ip_to_string((const ip4_addr_t *)local_addr);
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi STA_START, connecting...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *got_ip = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "WiFi GOT_IP, local=%s",
             get_local_ip_address(got_ip).c_str());
    /* tutaj problem z ip bramy zamiast srv ~sikor
    client.set_server_ip(get_gw_ip_address(got_ip));*/
    client.set_server_ip(get_srv_ip_address());
    client.set_my_ip(get_local_ip_address(got_ip));
    ESP_LOGI(TAG, "WiFi CONNECTED_BIT set");
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "WiFi DISCONNECTED, reconnecting...");
    /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate.
     */
    esp_wifi_connect();
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
  }
}

static void initialize_wifi(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config));
  strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD,
          sizeof(wifi_config.sta.password));
  wifi_config.sta.bssid_set = false;

  ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_LOGI(TAG, "Calling esp_wifi_start()...");
  esp_err_t start_err = esp_wifi_start();
  ESP_LOGI(TAG, "esp_wifi_start result: %s", esp_err_to_name(start_err));
  ESP_ERROR_CHECK(start_err);

  esp_wifi_set_ps(DEFAULT_PS_MODE);
}

ButtonInputHandler<SipClientT, BELL_GPIO_PIN, RING_DURATION_TIMEOUT_MSEC>
    button_input_handler(client);

static void sip_task(void *pvParameters) {
  for (;;) {
    // Wait for wifi connection
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true,
                        portMAX_DELAY);

    if (!client.is_initialized()) {
      bool result = client.init();
      ESP_LOGI(TAG, "SIP client initialized %ssuccessfully",
               result ? "" : "un");
      if (!result) {
        ESP_LOGI(TAG, "Waiting to try again...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        continue;
      }

      client.set_event_handler([](const SipClientEvent &event) {
        switch (event.event) {
        case SipClientEvent::Event::CALL_START:
          ESP_LOGI(TAG, "Call start");
          break;
        case SipClientEvent::Event::CALL_CANCELLED:
          ESP_LOGI(TAG, "Call cancelled, reason %d", (int)event.cancel_reason);
          button_input_handler.call_end();
          break;
        case SipClientEvent::Event::CALL_END:
          ESP_LOGI(TAG, "Call end");
          button_input_handler.call_end();
          vTaskDelay(500 / portTICK_PERIOD_MS);
          i2s_pause();
          break;
        case SipClientEvent::Event::BUTTON_PRESS:
          ESP_LOGI(TAG, "Got button press: %c for %d milliseconds",
                   event.button_signal, event.button_duration);
          break;
        }
      });
    }

    client.run();
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "app_main start");
  log_sip_config();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS init failed (%s), erasing...", esp_err_to_name(nvs_err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_err);
  ESP_LOGI(TAG, "NVS init ok");
  initialize_wifi();
  ESP_LOGI(TAG, "WiFi init ok");

  std::srand(esp_random());
  xTaskCreate(&sip_task, "sip_task", 4096, NULL, 5, NULL);

  // blocks forever
  button_input_handler.run();
}
