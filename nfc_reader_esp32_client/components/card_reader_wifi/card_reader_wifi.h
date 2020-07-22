#ifndef __WIFI_H__
#define __WIFI_H__

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MAX_HTTP_OUTPUT_BUFFER 2048
#define MAX_HTTP_URL_BUFFER 500

// Fill your data:
#define SERVER_ADDR "server_url"
#define WIFI_SSID "wifi_ssid"
#define WIFI_PASS "password"

#define WIFI_MAX_RETRY 5 // Max number of attempts to connect to WiFi on startup

typedef struct {
  uint32_t apiCode;
  char apiMessage[MAX_HTTP_OUTPUT_BUFFER-3];
} http_response_t;

static void wifi_eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
esp_err_t wifi_httpEventHandler(esp_http_client_event_t *evt);
void wifi_setup();
void wifi_printIP();
uint8_t wifi_httpsExchangeData(http_response_t *response, char* responseBuffer, char *queryString, char *readerKeyString);
uint8_t wifi_parseResponse(http_response_t *response, esp_http_client_handle_t client, char *buffer, esp_err_t error);
uint32_t wifi_parseApiCode(char *buffer);
void wifi_printResponse(http_response_t *response);

#endif
