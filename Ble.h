#ifndef __BLE_H__
#define __BLE_H__

#define CONFIG_BLUEDROID_ENABLED

void BLELoop();
void BLEInit();

void BLENotifyPacket();
void BLENotifyBatt();
void BLENotifyRSSI();
#endif