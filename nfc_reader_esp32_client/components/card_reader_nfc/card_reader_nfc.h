#ifndef __NFC_H__
#define __NFC_H__

#define PN532_SCK 26
#define PN532_MOSI 33
#define PN532_SS 32
#define PN532_MISO 25

#define READER_ID_LEN 8
#define CARD_ID_LEN 8
#define CARD_DATA_LEN 32
#define CARD_DATA_FIRST_BLOCK 4

typedef struct {
  uint8_t cidLen; // Length of Card ID
  uint8_t rid[READER_ID_LEN]; // Reader ID
  uint8_t cid[CARD_ID_LEN]; // Card ID
  uint8_t data[CARD_DATA_LEN]; // 2 blocks of data
} log_data_t;

void nfc_setup(pn532_t *obj);
uint32_t nfc_readCardId(pn532_t *obj, log_data_t *logData);
void nfc_setReaderId(log_data_t *logData, uint8_t *id);
uint8_t nfc_authReadBlock(pn532_t *obj, log_data_t *logData, uint8_t *keyA, uint32_t block, uint8_t *block_data);
uint8_t nfc_authReadData(pn532_t *obj, log_data_t *logData, uint8_t *keyA, uint32_t firstBlock);
void nfc_initLogData(log_data_t *logData);
void nfc_printLogData(log_data_t *logData);
char *nfc_logDataToApiString(log_data_t *logData, char *destination);
char *nfc_arrayToApiString(char *prefix, char *key, uint8_t *array, size_t arrayLen, char *destination);
uint8_t nfc_logCard(pn532_t *obj, log_data_t *logData, uint8_t *readerId, uint8_t *keyA);
uint8_t nfc_generateReaderKey(uint8_t *readerId, uint8_t *destination);

#endif
