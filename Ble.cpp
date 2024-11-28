#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLE2901.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include "Ble.h"
#include "sx126x.h"
#include "TrovaLaSondaFw.h"
#include "radio.h"

#define SERVICE_UUID "79ee1705-f663-4674-8774-55042fc215f5"
#define OTA_SERVICE_UUID "0410c8a6-2c9c-4d6a-9f0e-4bc0ff7e0f7e"
#define LAT_UUID "fc62efe0-eb5d-4cb0-93d3-01d4fb083e18"
#define LON_UUID "c8666b42-954a-420f-b235-6baaba740840"
#define ALT_UUID "1bfdccfe-80f4-46d0-844f-ad8410001989"
#define VEL_UUID "9cb28ac2-fb89-4714-954b-e9292dedce60"
#define SERIAL_UUID "539fd1f8-f427-4ddc-99d2-80f51616baab"
#define BATT_UUID "4578ee77-f50f-4584-b59c-46264c56d949"
#define RSSI_UUID "e482dfeb-774f-4f8b-8eea-87a752326fbd"
#define TYPEFREQ_UUID "66bf4d7f-2b21-468d-8dce-b241c7447cc6"
#define BK_UUID "b4da41fe-3194-42e7-8bbb-2e11d3ff6f6d"
#define MUTE_UUID "a8b47819-eb1a-4b5c-8873-6258ddfe8055"
#define OTA_UUID "63fa4cbe-3a81-463f-aa84-049dea77a209"
#define VERSION_UUID "2bc3ed96-a00a-4c9a-84af-7e1283835d71"

BLEServer *pServer = NULL;
BLECharacteristic *pLatChar, *pLonChar, *pAltChar, *pVelChar, *pSerialChar, *pBurstKillChar,
  *pBattChar, *pRSSIChar, *pTypeFreqChar, *pMuteChar, *pVersionChr, *pRxChr;
BLE2901 *descriptor_2901 = NULL;
bool wasConnected = false;
esp_ota_handle_t handleOta;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) {
    connected = true;
    //pServer->updateConnParams(param->connect.remote_bda, 6, 6, 0, 10);
    BLEAddress a(param->connect.remote_bda);
    Serial.printf("connected to %s\n",a.toString().c_str());
  };

  void onDisconnect(BLEServer *pServer) {
    connected = false;
    Serial.println("disconnected");
  }

  void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) {
    Serial.printf("onMtuChanged\n");
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    if (pCharacteristic->getUUID().equals(BLEUUID(OTA_UUID))) {
      int nLen=pCharacteristic->getLength();
      if (!otaRunning) {
        if (nLen!=8) return;
        uint8_t *p=pCharacteristic->getData();
        if (p[0]!=0x53 || p[1]!=0x48
         || p[2]!=0 || p[3]!=0)
          Serial.printf("Numero magico non corrisponde! (%02X %02X %02X %02X)\n",p[0],p[1],p[2],p[3]);
        otaLength=p[4]+256*(p[5]+256*(p[6]+256*p[7]));
        otaProgress=0;
        Serial.printf("Lunghezza nuovo firmware : %d bytes\n",otaLength);
        otaRunning=true;
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(running);
        esp_err_t err=esp_ota_begin(next,otaLength,&handleOta);
        if (err!=ESP_OK) {
          Serial.printf("Errore esp_ota_begin %d\n",err);
          otaErr=err;
          return;
        }
        return;
      }
      else {
        if (nLen==0) {
          ESP.restart();
          return;
        }
        esp_err_t err=esp_ota_write(handleOta,pCharacteristic->getData(),nLen);
        otaProgress+=nLen;
        if (err!=ESP_OK) {
          Serial.printf("Errore esp_ota_write %s\n",esp_err_to_name(err));
          otaErr=err;
        }
        else {
          Serial.printf("Ota progress: (%d) %d%%\n",nLen,(100*otaProgress)/otaLength);

          if (otaProgress==otaLength) {
            esp_ota_end(handleOta);
            const esp_partition_t *running=esp_ota_get_running_partition(),
              *next = esp_ota_get_next_update_partition(running);

            esp_ota_set_boot_partition(next);
            delay(1000);
            ESP.restart();
          }
        }
      }
    }
    else if (pCharacteristic == pTypeFreqChar) {
      uint8_t *pVal=pCharacteristic->getData();
      currentSonde=pVal[0];
      freq=pVal[1]+256*(pVal[2]+256*(pVal[3]+256*pVal[4]));
      
      Serial.printf("TypeFreq: %d %d\n", currentSonde, freq);
      savePrefs();
      initRadio();
    }
    else if (pCharacteristic == pMuteChar) {
      Serial.printf("Mute: %d\n", mute = *(int *)pCharacteristic->getData());
      if (!mute) bip(80,440);
    }
  }
};

MyCallbacks myCallbacks;
MyServerCallbacks myServerCallbacks;

void createCharacteristic(BLEService *pService, const char *desc, const char *uuid, BLECharacteristic **ppChar, bool writable = false) {
  uint32_t flags = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE;
  if (writable) flags |= BLECharacteristic::PROPERTY_WRITE;
  *ppChar = pService->createCharacteristic(uuid, flags);

  // Creates BLE Descriptor 0x2902: Client Characteristic Configuration Descriptor (CCCD)
  (*ppChar)->addDescriptor(new BLE2902());
  // Adds also the Characteristic User Description - 0x2901 descriptor
  descriptor_2901 = new BLE2901();
  descriptor_2901->setDescription(desc);
  if (!writable)
    descriptor_2901->setAccessPermissions(ESP_GATT_PERM_READ);  // enforce read only - default is Read|Write
  (*ppChar)->addDescriptor(descriptor_2901);
  if (writable)
    (*ppChar)->setCallbacks(&myCallbacks);
}

void BLEInit() {
  char s[32];
  uint8_t add[8];
  esp_efuse_mac_get_default(add);
  snprintf(s, sizeof s, "TrovaLaSonda%02X%02X%02X%02X%02X%02X", add[0], add[1], add[2], add[3], add[4], add[5]);
  //snprintf(s, sizeof s, "TLS%02X%02X%02X%02X%02X%02X%02X%02X", add[0], add[1], add[2], add[3], add[4], add[5], add[6], add[7]);
  Serial.printf("Nome: %s\n", s);
  BLEDevice::init(s);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&myServerCallbacks);

  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID), 90);

  createCharacteristic(pService, "Latitude", LAT_UUID, &pLatChar);
  createCharacteristic(pService, "Longitude", LON_UUID, &pLonChar);
  createCharacteristic(pService, "Altitude", ALT_UUID, &pAltChar);
  createCharacteristic(pService, "Velocity", VEL_UUID, &pVelChar);
  createCharacteristic(pService, "Serial", SERIAL_UUID, &pSerialChar);
  createCharacteristic(pService, "Battery", BATT_UUID, &pBattChar);
  createCharacteristic(pService, "RSSI", RSSI_UUID, &pRSSIChar);
  createCharacteristic(pService, "BurstKill", BK_UUID, &pBurstKillChar);
  createCharacteristic(pService, "TypeFreq", TYPEFREQ_UUID, &pTypeFreqChar, true);
  createCharacteristic(pService, "Mute", MUTE_UUID, &pMuteChar, true);
  createCharacteristic(pService, "Version", VERSION_UUID, &pVersionChr);

  uint8_t buffer[5];
  buffer[0]=currentSonde;
  buffer[1]=freq;
  buffer[2]=freq>>8;
  buffer[3]=freq>>16;
  buffer[4]=freq>>24;
  pTypeFreqChar->setValue(buffer,sizeof buffer);

  pMuteChar->setValue(mute);
  pVersionChr->setValue(version);
  pService->start();

  pService = pServer->createService(BLEUUID(OTA_SERVICE_UUID), 90);
  createCharacteristic(pService, "RX", OTA_UUID, &pRxChr, true);
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
}

void BLENotifyLat() {
  if (!connected) return;
  pLatChar->setValue(lat);
  pLatChar->notify();
}

void BLENotifyLon() {
  if (!connected) return;
  pLonChar->setValue(lng);
  pLonChar->notify();
}

void BLENotifyAlt() {
  if (!connected) return;
  pAltChar->setValue(alt);
  pAltChar->notify();
}

void BLENotifyVel() {
  if (!connected) return;
  pVelChar->setValue(vel);
  pVelChar->notify();
}

void BLENotifyBatt() {
  if (!connected) return;
  pBattChar->setValue(batt);
  pBattChar->notify();
}

void BLENotifyRSSI() {
  if (!connected) return;
  pRSSIChar->setValue(rssi);
  pRSSIChar->notify();
}

void BLENotifySerial() {
  if (!connected) return;
  pSerialChar->setValue(serial);
  pSerialChar->notify();
}

void BLENotifyBurstKill() {
  if (!connected) return;
  uint8_t buffer[]={ bkStatus, bkTime, bkTime>>8, bkTime>>16, bkTime>>24 };
  pBurstKillChar->setValue(buffer,sizeof buffer);
  pBurstKillChar->notify();
}

void BLELoop() {
  static bool startAdvertising=false;
  if (startAdvertising) 
    pServer->startAdvertising();
  
  if (connected && !wasConnected)
    wasConnected = connected;
  if (!connected && wasConnected)  {
    startAdvertising=true;
    wasConnected = connected;
  }
}

