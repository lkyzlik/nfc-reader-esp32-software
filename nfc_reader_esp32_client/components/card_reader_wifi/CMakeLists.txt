idf_component_register (
  SRCS "card_reader_wifi.c"
  INCLUDE_DIRS "."
  EMBED_TXTFILES server_cert.pem
  REQUIRES nvs_flash esp-tls esp_http_client
)
