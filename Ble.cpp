#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>
#include <arduino_base64.hpp>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include "TrovaLaSondaFw.h"
#include "Ble.h"
#include "radio.h"

#define SERVICE_UUID "177fba78-7843-40a6-801b-a4cd8d7f5c11"
#define PACKET_UUID "4dee4a71-2e7e-4018-9656-b60f1e562047"
#define BATT_UUID "4578ee77-f50f-4584-b59c-46264c56d949"
#define RSSI_UUID "e482dfeb-774f-4f8b-8eea-87a752326fbd"
#define TYPEFREQ_UUID "66bf4d7f-2b21-468d-8dce-b241c7447cc6"
#define MUTE_UUID "a8b47819-eb1a-4b5c-8873-6258ddfe8055"
#define VERSION_UUID "2bc3ed96-a00a-4c9a-84af-7e1283835d71"

#define OTA_SERVICE_UUID "0410c8a6-2c9c-4d6a-9f0e-4bc0ff7e0f7e"
#define OTA_TX_UUID "63fa4cbe-3a81-463f-aa84-049dea77a209"
#define OTA_RX_UUID "4f0227ff-dca1-4484-99f9-155cba7f3d86"

esp_ota_handle_t handleOta;
NimBLEAddress connected_addr;
NimBLEServer* pServer = nullptr;
NimBLECharacteristic *pPacketChar = nullptr,
                     *pBattChar = nullptr,
                     *pRSSIChar = nullptr,
                     *pTypeFreqChar = nullptr,
                     *pMuteChar = nullptr,
                     *pVersionChar = nullptr,
                     *pOtaTxChar = nullptr,
                     *pOtaRxChar = nullptr;

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    connected = true;
    connected_addr = connInfo.getAddress();
    Serial.printf("Client connected! Address: %s\n", connected_addr.toString().c_str());
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    connected = false;
    Serial.printf("Client disconnected. Reason: %d\n", reason);
    connected_addr = NimBLEAddress();
  }

  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
    Serial.printf("MTU negotiated successfully! New Size: %d bytes for client handle: %08X\n", MTU, connInfo.getConnHandle());
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.print("Read event on property: ");
    Serial.println(pCharacteristic->getUUID().toString().c_str());
  }

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.println("onWrite");
    bool restart = false;
    if (pCharacteristic->getUUID().equals(BLEUUID(OTA_TX_UUID))) {
      NimBLEAttValue value = pCharacteristic->getValue();
      int nLen = value.length();
      if (!otaRunning) {
        if (nLen != 8) return;
        pServer->updateConnParams(connected_addr, 9, 9, 0, 50);
        const uint8_t* p = value.data();
        if (p[0] != 0x53 || p[1] != 0x48 || p[2] != 0 || p[3] != 0)
          Serial.printf("Numero magico non corrisponde! (%02X %02X %02X %02X)\n", p[0], p[1], p[2], p[3]);
        otaLength = p[4] + 256 * (p[5] + 256 * (p[6] + 256 * p[7]));
        otaProgress = 0;
        Serial.printf("Lunghezza nuovo firmware : %d bytes\n", otaLength);
        otaRunning = true;
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(running);
        esp_err_t err = esp_ota_begin(next, otaLength, &handleOta);
        if (err != ESP_OK) {
          Serial.printf("Errore esp_ota_begin %d\n", err);
          otaErr = err;
        }
      } else {
        if (nLen == 0) {
          restart = true;
        } else {
          esp_err_t err = esp_ota_write(handleOta, value.data(), nLen);
          otaProgress += nLen;
          if (err != ESP_OK) {
            Serial.printf("Errore esp_ota_write %s\n", esp_err_to_name(err));
            otaErr = err;
          } else {
            Serial.printf("Ota progress: (%d) %d%%\n", nLen, (100 * otaProgress) / otaLength);

            if (otaProgress == otaLength) {
              esp_ota_end(handleOta);
              const esp_partition_t *running = esp_ota_get_running_partition(),
                                    *next = esp_ota_get_next_update_partition(running);

              esp_ota_set_boot_partition(next);
              restart = true;
            }
          }
        }
      }

      pOtaRxChar->setValue(otaErr);
      pOtaRxChar->notify();

      if (restart) {
        delay(1000);
        ESP.restart();
      }
    } else if (pCharacteristic->getUUID().toString() == TYPEFREQ_UUID) {
      NimBLEAttValue value = pCharacteristic->getValue();
      const uint8_t* pVal = value.data();
      currentSonde = pVal[0];
      freq = pVal[1] + 256 * (pVal[2] + 256 * (pVal[3] + 256 * pVal[4]));

      Serial.printf("TypeFreq: %d %d\n", currentSonde, freq);
      savePrefs();
      initRadio();
    } else if (pCharacteristic->getUUID().toString() == MUTE_UUID) {
      mute = pCharacteristic->getValue<uint8_t>();
      Serial.printf("Mute: %d\n", mute);
      if (!mute) bip(80, 440);
    }
  }
};

CharacteristicCallbacks* pCharCallbacks = new CharacteristicCallbacks();

void createCharacteristic(NimBLEService* pService, const char* desc, const char* uuid, NimBLECharacteristic** ppChar, bool writable = false, bool notify = true) {
  // 1. Establish core bitmask (Always readable by default)
  uint32_t properties = NIMBLE_PROPERTY::READ;

  if (writable)
    properties |= NIMBLE_PROPERTY::WRITE;
  if (notify)
    properties |= NIMBLE_PROPERTY::NOTIFY;

  // 2. Build out characteristic on the chosen service instance
  *ppChar = pService->createCharacteristic(uuid, properties);

  // 3. Set the metadata User Description string if supplied
  if (desc != nullptr && strlen(desc) > 0) {
    // Create the standard 0x2901 User Description descriptor
    NimBLEDescriptor* pUserDesc = (*ppChar)->createDescriptor(
      "2901",
      NIMBLE_PROPERTY::READ);
    pUserDesc->setValue(desc);
  }
}

void BLENotifyPacket() {
  if (!connected || otaRunning) return;
  pPacketChar->setValue((uint8_t*)&packet, sizeof packet);
  pPacketChar->notify();
}

void BLENotifyBatt() {
  if (!connected || otaRunning) return;
  pBattChar->setValue(batt);
  pBattChar->notify();
}

void BLENotifyRSSI() {
  if (!connected || otaRunning) return;
  pRSSIChar->setValue(rssi);
  pRSSIChar->notify();
}

void BLEInit() {
  char s[32], b64[13];
  uint8_t add[8];
  esp_efuse_mac_get_default(add);
  base64::encode(add, 8, b64);
  snprintf(s, sizeof s, "TrovaLaSonda%.11s", b64);  //removed trailing b64 padding
  //snprintf(s, sizeof s, "TrovaLaSonda%02X%02X%02X%02X%02X%02X", add[0], add[1], add[2], add[3], add[4], add[5]);
  Serial.printf("MAC address: %02X%02X%02X%02X%02X%02X%02X%02X\n", add[0], add[1], add[2], add[3], add[4], add[5], add[6], add[7]);
  Serial.printf("Nome: %s\n", s);

  delay(1000);
  Serial.println("Initializing Multi-Service NimBLE Stack...");

  NimBLEDevice::init(s);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  pServer->advertiseOnDisconnect(true);  // Auto-restarts advertising loop when client disconnects

  NimBLEService* pMainService = pServer->createService(SERVICE_UUID);

  createCharacteristic(pMainService, "Packet", PACKET_UUID, &pPacketChar);
  createCharacteristic(pMainService, "Battery", BATT_UUID, &pBattChar);
  createCharacteristic(pMainService, "RSSI", RSSI_UUID, &pRSSIChar);
  createCharacteristic(pMainService, "TypeFreq", TYPEFREQ_UUID, &pTypeFreqChar, true, false);
  createCharacteristic(pMainService, "Mute", MUTE_UUID, &pMuteChar, true, false);
  createCharacteristic(pMainService, "Version", VERSION_UUID, &pVersionChar, false, false);

  const uint8_t buffer[] = { currentSonde, freq, freq >> 8, freq >> 16, freq >> 24 };
  pTypeFreqChar->setValue(buffer, sizeof buffer);
  pMuteChar->setValue(mute);
  pVersionChar->setValue(String(platform) + "/" + String(version));
  pPacketChar->setValue("");
  pBattChar->setValue(0);

  pTypeFreqChar->setCallbacks(pCharCallbacks);
  pMuteChar->setCallbacks(pCharCallbacks);

  NimBLEService* pOtaService = pServer->createService(OTA_SERVICE_UUID);

  createCharacteristic(pOtaService, "OTA_TX", OTA_TX_UUID, &pOtaTxChar, false, true);  // Stream out to phone
  createCharacteristic(pOtaService, "OTA_RX", OTA_RX_UUID, &pOtaRxChar, true, false);  // Receive from phone

  pOtaRxChar->setCallbacks(pCharCallbacks);

  // advertising initialization (optimized layout for long names + dual 128-bit UUIDs)
  NimBLEAdvertising* pAdvertising = pServer->getAdvertising();

  NimBLEAdvertisementData advData;
  NimBLEAdvertisementData scanData;

  // =========================================================================
  // PACKET 1: Main Broadcast Packet (Max 31 Bytes)
  // =========================================================================
  // - Base Discovery Flags: 3 Bytes
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);

  // - Main Service UUID: 18 Bytes
  advData.addServiceUUID(SERVICE_UUID);

  // Total Packet 1 size = 21 Bytes. (Fits comfortably under the 31-byte limit)

  // =========================================================================
  // PACKET 2: Scan Response Packet (Max 31 Bytes)
  // =========================================================================
  // - Your 23-byte Name String: Takes 25 bytes total with headers.
  // NOTE: We MUST remove the OTA_SERVICE_UUID from here. If it stays here,
  // the packet hits 43 bytes and fails silently.
  scanData.setName(s);

  // Total Packet 2 size = 25 Bytes. (Fits perfectly under the 31-byte limit!)

  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanData);

  pAdvertising->enableScanResponse(true);
  pAdvertising->setPreferredParams(0x0, 0x0);

  pAdvertising->start();
  Serial.println("System online. Advertisement data structured below 31-byte thresholds!");
}

void BLELoop() {
  vTaskDelay(pdMS_TO_TICKS(1));
}
