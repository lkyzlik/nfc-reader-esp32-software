#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "pn532.h"

#include "card_reader_nfc.h"

#define NFC_DEBUG_EN

#ifdef NFC_DEBUG_EN
#define NFC_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define NFC_DEBUG(fmt, ...)
#endif

static const char* TAG = "card_reader_nfc";

/**
* @brief  Configure and start communication with PN532 module
*
* @param  obj       Pointer to PN532 device descriptor struct
*/
void nfc_setup(pn532_t *obj) {
  // Configure pins
  pn532_spi_init(obj, PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  pn532_begin(obj);

  // Check connection to PN532 and get firmware version
  uint32_t versiondata = pn532_getFirmwareVersion(obj);
  if (!versiondata)
  {
      ESP_LOGI(TAG, "Didn't find PN53x board");
      while (1)
      {
          vTaskDelay(1000 / portTICK_RATE_MS);
      }
  }
  ESP_LOGI(TAG, "Found chip PN5 %x", (versiondata >> 24) & 0xFF);
  ESP_LOGI(TAG, "Firmware ver. %d.%d", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);

  // Configure board to read RFID tags
  pn532_SAMConfig(obj);

  ESP_LOGI(TAG, "NFC module set up!");
}

/**
* @brief  Read ID of ISO14443A card and save it in log_data_t struct.
*
* @param  obj       Pointer to PN532 device descriptor struct
* @param  logData   Pointer to struct holding log data
*
* @return Length of card ID or -1 for faliure
*/
uint32_t nfc_readCardId(pn532_t *obj, log_data_t *logData) {
  if(pn532_readPassiveTargetID(obj, PN532_MIFARE_ISO14443A, logData->cid, &(logData->cidLen), 0)) {
    NFC_DEBUG("Found an ISO14443A card\n");
    NFC_DEBUG("Card ID Length: %d bytes\n", logData->cidLen);
    NFC_DEBUG("Card ID Value:");
    for(int i = 0; i < CARD_ID_LEN; ++i)
    NFC_DEBUG(" %02hhx", logData->cid[i]);
    NFC_DEBUG("\n");

    return logData->cidLen;
  }
  else {
    NFC_DEBUG("Card timeout\n");
    return -1;
  }

}

/**
* @brief  Save Reader ID to log_data_t struct.
*
* @param  logData    Pointer to struct holding card data
* @param  id         Reader ID array
*/
void nfc_setReaderId(log_data_t *logData, uint8_t *id) {
  for(int i = 0; i < READER_ID_LEN; ++i)
    logData->rid[i] = id[i];

  NFC_DEBUG("Reader ID set to");
  for(int i = 0; i < READER_ID_LEN; ++i)
    NFC_DEBUG(" %02hhx", logData->rid[i]);
  NFC_DEBUG("\n");
}

/**
* @brief  Authenticate and read one 16-byte block from ISO14443A card.
*
* @param  obj         Pointer to PN532 device descriptor struct
* @param  logData     Pointer to struct holding log data
* @param  keyA        Key A used for card authentication
* @param  block       Number of block to read
* @param  blockData   Array to sotre the block data to
*
* @return Error code (0 = success, 1 = authentication failed, 2 = reading failed)
*/
uint8_t nfc_authReadBlock(pn532_t *obj, log_data_t *logData, uint8_t *keyA, uint32_t block, uint8_t *blockData) {
  // Authentication
	if(!(pn532_mifareclassic_AuthenticateBlock(obj, logData->cid, logData->cidLen, block, 0, keyA))) {
    ESP_LOGE(TAG, "Authentication of block %d failed", block);
		return 1;
  }
	// Reading block data
	if(!(pn532_mifareclassic_ReadDataBlock(obj, block, blockData))) {
    ESP_LOGE(TAG, "Reading block %d failed", block);
		return 2;
  }
	return 0;
}

/**
* @brief  Authenticate and read blocks and store data to log_data_t struct
*
* @param  obj         Pointer to PN532 device descriptor struct
* @param  logData     Pointer to struct holding log data
* @param  keyA        Key A used for card authentication
* @param  firstBlock  Number of the first block to be read
*
* @return Error code (0 = success, 1 = failed)
*/
uint8_t nfc_authReadData(pn532_t *obj, log_data_t *logData, uint8_t *keyA, uint32_t firstBlock) {
  uint8_t data[CARD_DATA_LEN];

  // Read blocks
  for(int b = firstBlock; b < (firstBlock + CARD_DATA_LEN / 16); ++b) {
    uint8_t block_data[CARD_DATA_LEN];
    if(!nfc_authReadBlock(obj, logData, keyA, b, block_data)) {
      for(int i = 0; i < 16; ++i) {
        data[i + (b-firstBlock)*16] = block_data[i];
      }
    }
    else {
      ESP_LOGE(TAG, "Reading block %d failed", b);
      return 1;
    }
  }

  // Print debug info
  NFC_DEBUG("Data:");
  for(int i = 0; i < CARD_DATA_LEN; ++i) {
    logData->data[i] = data[i];

    if(i%16 == 0) NFC_DEBUG("\n");
    NFC_DEBUG("%02hhx ", logData->data[i]);
  }
  NFC_DEBUG("\n");

  return 0;
}

/**
* @brief  Set all variables in log_data_t struct to 0
*
* @param  logData     Pointer to struct holding log data
*/
void nfc_initLogData(log_data_t *logData) {
  logData-> cidLen = 0;
  for(int i = 0; i < READER_ID_LEN ; ++i) logData->rid[i] = 0x00;
  for(int i = 0; i < CARD_ID_LEN ; ++i) logData->cid[i] = 0x00;
  for(int i = 0; i < CARD_DATA_LEN ; ++i) logData->data[i] = 0x00;
  NFC_DEBUG("Log data reset\n");
}

/**
* @brief  Print all variables in log_data_t struct using ESP_LOGI
*
* @param  logData     Pointer to struct holding log data
*/
void nfc_printLogData(log_data_t *logData) {
  ESP_LOGI(TAG, "Card data");
  ESP_LOGI(TAG, "---");
  ESP_LOGI(TAG, "Reader ID:");
  esp_log_buffer_hexdump_internal(TAG, logData->rid, READER_ID_LEN, ESP_LOG_INFO);
  ESP_LOGI(TAG, "Card ID Length: %d", logData->cidLen);
  ESP_LOGI(TAG, "Card ID:");
  esp_log_buffer_hexdump_internal(TAG, logData->cid, CARD_ID_LEN, ESP_LOG_INFO);
  ESP_LOGI(TAG, "Data:");
  esp_log_buffer_hexdump_internal(TAG, logData->data, CARD_DATA_LEN, ESP_LOG_INFO);
  ESP_LOGI(TAG, "---");
}

/**
* @brief  Convert log_data_t to REST API string
*
* @param  logData      Pointer to struct holding log data
* @param  destination  Pointer to the output string location
*
* @return Output string
*/
char *nfc_logDataToApiString(log_data_t *logData, char *destination) {
  nfc_arrayToApiString(NULL, "rid", logData->rid, READER_ID_LEN, destination);
  nfc_arrayToApiString(destination, "cid", logData->cid, CARD_ID_LEN, destination);
  return nfc_arrayToApiString(destination, "data", logData->data, CARD_DATA_LEN, destination);
}

/**
* @brief  Convert array to REST API string in the fomat prefix&key=0x[array in hex]
*
* @param  prefix       String to be prepended in frot of the output (if NULL no prefix will be prepended)
* @param  key          Name/key of the filed
* @param  array        Array of values to be converted
* @param  arrayLen     Length of the array
* @param  destination  Pointer to the output string location
*
* @return Output string
*/
char *nfc_arrayToApiString(char *prefix, char *key, uint8_t *array, size_t arrayLen, char *destination) {
  int n = 0;
  if(prefix != NULL) { // If NULL prefix is ignored
    n += sprintf(&destination[n], prefix);
    n += sprintf(&destination[n], "&");
  }
  n += sprintf(&destination[n], key);
  n += sprintf(&destination[n], "=0x");
  for(int i = 0; i < arrayLen; ++i) {
		n += sprintf(&destination[n],"%02hhx", array[i]);
	}
  NFC_DEBUG("API string: %s\n", destination);
  return destination;
}

/**
* @brief  Wait for the ISO14443A card and log its info to log_data_t struct
*
* @param  obj         Pointer to PN532 device descriptor struct
* @param  logData     Pointer to struct holding log data
* @param  readerId    Reader ID array
* @param  keyA        Key A used for card authentication
*
* @return Error code (0 = success, 1 = reading ID failed, 2 = reading data failed)
*/
uint8_t nfc_logCard(pn532_t *obj, log_data_t *logData, uint8_t *readerId, uint8_t *keyA) {
  nfc_initLogData(logData);
  nfc_setReaderId(logData, readerId);
  if(nfc_readCardId(obj, logData) == -1) {
    ESP_LOGE(TAG, "Reading Card ID failed");
    return 1;
  }
  if(nfc_authReadData(obj,logData, keyA, CARD_DATA_FIRST_BLOCK)) {
    ESP_LOGE(TAG, "Reading Card Data failed");
    return 2;
  }
  return 0;
}
