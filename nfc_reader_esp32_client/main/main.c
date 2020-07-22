#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
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

#include "pn532.h"

#include "card_reader_gpio.h"
#include "card_reader_wifi.h"
#include "card_reader_nfc.h"

static const char* TAG = "main";

#define ALIVE_MSG_INTERVAL_S 10
#define READER_KEY_LEN 32

/**
* Embeding binary and text files
*/
extern const char rkey_seed_txt_start[] asm("_binary_rkey_seed_txt_start");
extern const char rkey_seed_txt_end[]   asm("_binary_rkey_seed_txt_end");

/**
* Semaphores which protect usage of resources
*/
SemaphoreHandle_t httpSemaphore = NULL;
SemaphoreHandle_t indLedSemaphore = NULL;

/**
* Global variables of the Main component
*/
static pn532_t nfc; // PN532 module struct

uint8_t keyA[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // Key A to acess data on card
uint8_t rid[] = { 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78 }; // Reader ID
uint8_t rkey[READER_KEY_LEN]; // Reader Key

char responseBuffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

/**
*  @brief Task reading card data, sending it to a remote server, processing response and indicating it to a user
*/
void cardReadTask(void *pvParameter) {
  ESP_LOGI(TAG, "Card Read task runs!");
  // Infinite loop
  while (1) {
    // Wait for card and log data
    log_data_t logData;
    if(nfc_logCard(&nfc, &logData, rid, keyA)) {
      ESP_LOGE(TAG, "Loging card failed");
    }
    else {
      // Convert log data and Reader Key to REST API string
      char queryStr[MAX_HTTP_URL_BUFFER];
      nfc_logDataToApiString(&logData, queryStr);
      char rkeyStr[READER_KEY_LEN*2+7];
      nfc_arrayToApiString(NULL, "rkey", rkey, READER_KEY_LEN, rkeyStr);

      // Check if HTTP resource is avalible
      if(xSemaphoreTake(httpSemaphore, portMAX_DELAY) == pdTRUE) {
        // Send data to server and get response
        http_response_t resp;
        uint8_t err = wifi_httpsExchangeData(&resp, responseBuffer, queryStr, rkeyStr);
        xSemaphoreGive(httpSemaphore); // Free HTTP resource
        // Check errors
        if(err) {
          ESP_LOGE(TAG, "Log data message response failed");

          // Double red flash
          gpio_setIndicatorLed(LED_RED);
          vTaskDelay(200 / portTICK_PERIOD_MS); // Flash lasts 0.2s
          gpio_setIndicatorLed(LED_OFF);
          vTaskDelay(100 / portTICK_PERIOD_MS);
          gpio_setIndicatorLed(LED_RED);
          vTaskDelay(200 / portTICK_PERIOD_MS); // Flash lasts 0.2s
          gpio_setIndicatorLed(LED_OFF);
        }
        else {
          // Print response
          wifi_printResponse(&resp);
          // Check if Indicator LED resource is avalible
          if(xSemaphoreTake(indLedSemaphore, portMAX_DELAY) == pdTRUE) {
            // Indicate if the access was granted to a user
            if(resp.apiCode == 100) {
              gpio_setIndicatorLed(LED_GREEN);
              ESP_LOGI(TAG, "ACCESS GRANTED");
            }
            else {
              gpio_setIndicatorLed(LED_RED);
              ESP_LOGI(TAG, "ACCESS DENIED");
            }
            vTaskDelay(500 / portTICK_PERIOD_MS); // Flash lasts 0.5s
            gpio_setIndicatorLed(LED_OFF);

            xSemaphoreGive(indLedSemaphore); // Free Indicator LED reasource
          }
        }
      }
      else {
        ESP_LOGE(TAG, "HTTP reasource occupied. Couldn't send log data message");
      }

    }
  }

}

/**
*  @brief Task sending regular messages about the reader status to a backend server
*/
void aliveTask(void *pvParameter) {
  ESP_LOGI(TAG, "Alive task runs!");
  // Infinite loop
  while (1) {
    // Wait 10 s
    vTaskDelay((ALIVE_MSG_INTERVAL_S*1000) / portTICK_PERIOD_MS);

    // Convert reader ID and key to REST API string
    char rkeyStr[READER_KEY_LEN*2+7];
    nfc_arrayToApiString(NULL, "rkey", rkey, READER_KEY_LEN, rkeyStr);
    char queryStr[READER_ID_LEN*2+7];
    nfc_arrayToApiString(NULL, "rid", rid, READER_ID_LEN, queryStr);

    // Check if HTTP resource is avalible
    if(xSemaphoreTake(httpSemaphore, portMAX_DELAY) == pdTRUE) {
      // Send data to server and get response
      http_response_t resp;
        uint8_t err = wifi_httpsExchangeData(&resp, responseBuffer, queryStr, rkeyStr);
      xSemaphoreGive(httpSemaphore); // Free HTTP resource
      // Check errors
      if(err) {
        ESP_LOGE(TAG, "Alive message response failed");
      }
      else {
        // Print response
        wifi_printResponse(&resp);
        // Check response
        if(resp.apiCode != 200) {
          ESP_LOGE(TAG, "Reader not registered");
        }
      }
    }
    else {
      ESP_LOGE(TAG, "HTTP reasource occupied. Couldn't send alive message");
    }
  }

}

/**
*  @brief Task checking battery and power status and indicating to a user when the level of charge is critical
*/
void batteryWarningTask(void *pvParameter) {
  ESP_LOGI(TAG, "Battery Management task runs!");

  // Infinite loop
  while (1) {
    // Check if battery is critical
    if(!(gpio_isSourcePowered()) && gpio_isBatteryCritical()) {
      if(xSemaphoreTake(indLedSemaphore, portMAX_DELAY) == pdTRUE) {
        // Indicate warnining
        ESP_LOGI(TAG, "BATTERY CRITICAL");
        gpio_setIndicatorLed(LED_ORANGE);
        while(1) {
          // Check if battery is OK again
          if(gpio_isSourcePowered()) {
            // Stop warning
            ESP_LOGI(TAG, "BATTERY OK");
            gpio_setIndicatorLed(LED_OFF);
            xSemaphoreGive(indLedSemaphore);
            break;
          }
          vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 s
        }
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 s
  }

}

/**
* @brief Generate hash from Reader ID and rkey_seed.txt using HMAC SHA-256
*
* @param  readerId      Reader ID array
* @param  seed          Pointer to the start of the embeded text file with random seed
* @param  destination   32-byte array to store the Reader Key to
*
* @return Error code
*/
uint8_t generateReaderKey(uint8_t *readerId, const char *seed, uint8_t *destination) {
  size_t seedLen = strlen(seed)-1;

  // Generate hash
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  if(mbedtls_md_setup(&context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1)) {
    ESP_LOGE(TAG, "Setup failed");
    return 2;
  }
  if(mbedtls_md_hmac_starts(&context, (const unsigned char *) readerId, READER_ID_LEN)) {
    ESP_LOGE(TAG, "Key parameter varification failed");
    return 3;
  }
  if(mbedtls_md_hmac_update(&context, (const unsigned char *) seed, seedLen)) {
    ESP_LOGE(TAG, "Paylod parameter varification failed");
    return 4;
  }
  mbedtls_md_hmac_finish(&context, destination);
  mbedtls_md_free(&context);

  return 0;
}

/**
* @brief Print Reader ID, seed, and Reader Key using ESP_LOGI
*
* @param  readerId      Reader ID array
* @param  seed          Pointer to the start of the embeded text file with random seed
* @param  readerKey     Reader Key array
*/
void printReaderKeyInfo(uint8_t *readerId, const char *seed, uint8_t *readerKey) {
  ESP_LOGI(TAG, "---");
  ESP_LOGI(TAG, "Reader ID:");
  ESP_LOG_BUFFER_HEXDUMP(TAG, readerId, READER_ID_LEN, ESP_LOG_INFO);
  ESP_LOGI(TAG, "Seed:\n%s", rkey_seed_txt_start);
  ESP_LOGI(TAG, "Reader key:");
  ESP_LOG_BUFFER_HEXDUMP(TAG, readerKey, READER_KEY_LEN, ESP_LOG_INFO);
  ESP_LOGI(TAG, "---");
}

/**
* Main function
*/
void app_main() {
  // Setups
  gpio_setup();
  wifi_setup();
  nfc_setup(&nfc);

  // Generate Reader Key from Reader ID and seed
  generateReaderKey(rid, rkey_seed_txt_start, rkey);
  printReaderKeyInfo(rid, rkey_seed_txt_start, rkey);

  // Set semaphores
  vSemaphoreCreateBinary(httpSemaphore);
  vSemaphoreCreateBinary(indLedSemaphore);

  // Start tasks
  xTaskCreate(&cardReadTask, "card_read_task", 8192, NULL, 5, NULL);
  xTaskCreate(&aliveTask, "alive_task", 10*1024, NULL, 5, NULL);
  xTaskCreate(&batteryWarningTask, "battery_warning_task", 4096, NULL, 5, NULL);
}
