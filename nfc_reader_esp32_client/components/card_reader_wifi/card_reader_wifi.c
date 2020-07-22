#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "card_reader_wifi.h"

#define WIFI_DEBUG_EN

#ifdef WIFI_DEBUG_EN
#define WIFI_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define WIFI_DEBUG(fmt, ...)
#endif

static const char* TAG = "card_reader_wifi";

/**
* Embeding binary and text files
*/
extern const char server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const char server_cert_pem_end[]   asm("_binary_server_cert_pem_end");

/**
* Global vars for WiFi component
*/
static EventGroupHandle_t wifi_event_group;
static int retry_num = 0;

/**
* @brief  Manage WiFi events
*/
static void wifi_eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    // Action on STA start
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    // Action on disconect
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Retry connecting till WIFI_MAX_RETRY attempts
        if (retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG, "Retring to connect");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connecting to AP failed");
    // Action on getting IP
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
* @brief  Manage HTTP events
*/
esp_err_t wifi_httpEventHandler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            WIFI_DEBUG("HTTP_EVENT_ERROR\n");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            WIFI_DEBUG("HTTP_EVENT_ON_CONNECTED\n");
            break;
        case HTTP_EVENT_HEADER_SENT:
            WIFI_DEBUG("HTTP_EVENT_HEADER_SENT\n");
            break;
        case HTTP_EVENT_ON_HEADER:
            WIFI_DEBUG("HTTP_EVENT_ON_HEADER, key=%s, value=%s\n", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            WIFI_DEBUG("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memset(evt->user_data, '\0', MAX_HTTP_OUTPUT_BUFFER);
                    memcpy(evt->user_data, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer, evt->data, evt->data_len);
                }
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            WIFI_DEBUG("HTTP_EVENT_ON_FINISH\n");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer
                free(output_buffer);
                output_buffer = NULL;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            WIFI_DEBUG("HTTP_EVENT_DISCONNECTED\n");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

/**
* @brief  Configure wifi and connect to network
*/
void wifi_setup() {
  // Disable the default wifi logging
	esp_log_level_set("wifi", ESP_LOG_NONE);

  // Initialise flash
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialise Netif
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  esp_netif_create_default_wifi_sta();

  // Initialise WiFi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register events
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_eventHandler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_eventHandler, NULL, &instance_got_ip));

  // Configure WiFi connection
  wifi_config_t wifi_config = {
      .sta = {
          .ssid = WIFI_SSID,
          .password = WIFI_PASS
      },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );

  ESP_LOGI(TAG, "Connecting to AP...");
  ESP_ERROR_CHECK(esp_wifi_start());

  // Wait until either the connection is established or connection failed
  EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
          pdFALSE,
          pdFALSE,
          portMAX_DELAY);

  // Print what happend
  if (bits & WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "Connected to AP with SSID: %s", WIFI_SSID);
  } else if (bits & WIFI_FAIL_BIT) {
      ESP_LOGI(TAG, "Failed to connect to AP with SSID: %s", WIFI_SSID);
  } else {
      ESP_LOGE(TAG, "Unexpected event");
  }

  // Unregister events
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(wifi_event_group);

  ESP_LOGI(TAG, "WiFi module set up!");
}

/**
* @brief  Prnt current IP, subnet mask and gateway
*/
void wifi_printIP() {
  tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	ESP_LOGI(TAG,"IP Address:  %s", ip4addr_ntoa(&ip_info.ip));
	ESP_LOGI(TAG,"Subnet mask: %s", ip4addr_ntoa(&ip_info.netmask));
	ESP_LOGI(TAG,"Gateway:     %s", ip4addr_ntoa(&ip_info.gw));
}

/**
* @brief   Send HTTP(S) GET, wait for response and record it to http_response_t struct
*
* @param   response          Pointer to a struct to store server response to
* @param   responseBuffer    Buffer array to store raw response to
* @param   queryString       Query string of the GET request URL (if NULL no query will be send)
* @param   readerKeyString   Content of the Reader Key cookie (if NULL no cookie will be send)
*
* @return  Error code (0 = success, 1 = request error, 2 = response error)
*/
uint8_t wifi_httpsExchangeData(http_response_t *response, char* responseBuffer, char *queryString, char *readerKeyString) {
  // Configure GET request
  char urlStr[MAX_HTTP_URL_BUFFER] = SERVER_ADDR;
  if(queryString == NULL || queryString[0] == '\0') {
    ESP_LOGW(TAG, "No query string");
  }
  else {
    strcat(urlStr, queryString); // Conctenate server URL and API request string
  }
  esp_http_client_config_t config = {
      .url = urlStr,
      .cert_pem = server_cert_pem_start,
      .event_handler = wifi_httpEventHandler,
      .user_data = responseBuffer, // Pass address of buffer to get response
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  // Add Reder Key cookie
  if(readerKeyString == NULL || readerKeyString[0] == '\0') {
    ESP_LOGW(TAG, "No Reader Key cookie");
  }
  else {
    esp_http_client_set_header(client, "Set-Cookie", readerKeyString);
  }
  // Perform request
  esp_err_t err = esp_http_client_perform(client);
  // Parse response
  uint8_t ret = wifi_parseResponse(response, client, responseBuffer, err);
  // Cleanup
  esp_http_client_cleanup(client);

  return ret;
}

/**
* @brief   Read raw response and parse it in http_response_t struct
*
* @param   response   Pointer to a struct to store parsed response to
* @param   client     Client struct used to get raw response
* @param   buffer     Buffer array with raw response
* @param   error      Error value from performing the request
*
* @return  Error code (0 = success, 1 = fail)
*/
uint8_t wifi_parseResponse(http_response_t *response, esp_http_client_handle_t client, char *buffer, esp_err_t error) {
  if (error == ESP_OK) {
    if(esp_http_client_get_status_code(client) != 200) {
      ESP_LOGE(TAG, "HTTP response error. Status code: %d", esp_http_client_get_status_code(client));
      return 2;
    }
    else {
      // Parse response
      response->apiCode = wifi_parseApiCode(buffer);
      if(response->apiCode != -1) {
        strcpy(response->apiMessage, &buffer[5]); // Copy bit after API Code
        response->apiMessage[strlen(response->apiMessage)-1] = '\0'; // Trim last char
      } else {
        strcpy(response->apiMessage, "Ureadable response");
      }

      WIFI_DEBUG("---\n");
      WIFI_DEBUG("HTTP GET request successful\n");
      WIFI_DEBUG("Buffer: %s \n", buffer);
      WIFI_DEBUG("API Code: %d\n", response->apiCode);
      WIFI_DEBUG("API Message: %s\n", response->apiMessage);
      WIFI_DEBUG("---\n");

      return 0;
    }
  } else {
      ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(error));
      return 1;
  }
}

/**
* @brief   Read API Code from response buffer
*
* @param   buffer     Buffer array with raw response
*
* @return  API Code or -1 if reading failed
*/
uint32_t wifi_parseApiCode(char *buffer) {
  // Check if string is long enough
  if(strlen(buffer) < 6) {
    WIFI_DEBUG("Response too short, len=%d\n", strlen(buffer));
    return -1;
  }
  // Convert
  char *end;
  long ret = strtol(&buffer[1], &end, 10);
  if(*end == ' ') return ret;
  else {
    WIFI_DEBUG("Numer is not followed by space, end=%c", *end);
    return -1;
  }
}

/**
* @brief   Print content of http_response_t struct using ESP_LOGI
*
* @param   response   Pointer to a struct
*/
void wifi_printResponse(http_response_t *response) {
  ESP_LOGI(TAG, "API Code: %d, API Message: %s", response->apiCode, response->apiMessage);
}
