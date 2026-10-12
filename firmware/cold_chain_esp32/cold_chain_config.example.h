#pragma once

// Copy to cold_chain_config.h and fill in locally. Do not commit real credentials.
#define COLDCHAIN_WIFI_SSID ""
#define COLDCHAIN_WIFI_PASSWORD ""

#define COLDCHAIN_DEVICE_ID "VAULT_FRIDGE_01"
#define COLDCHAIN_MQTT_HOST ""
#define COLDCHAIN_MQTT_PORT 8883
#define COLDCHAIN_MQTT_USERNAME ""
#define COLDCHAIN_MQTT_PASSWORD ""

#define COLDCHAIN_HTTPS_HOST ""
#define COLDCHAIN_HTTPS_PORT 8443
#define COLDCHAIN_API_KEY ""
#define COLDCHAIN_NTP_SERVER "pool.ntp.org"

// Paste the public CA certificate or explicitly trusted self-signed server
// certificate as PEM text. Never place a private key in this file.
#define COLDCHAIN_MQTT_CA_CERT R"PEM()PEM"
#define COLDCHAIN_HTTPS_CA_CERT R"PEM()PEM"
