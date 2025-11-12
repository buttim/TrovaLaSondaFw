#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <CRC.h>
#include "TrovaLaSondaFw.h"
#include "radio.h"
#include "rs41.h"
#include "rd41.h"

static bool processPacket(uint8_t buf[]);
static int processPartialPacket(uint8_t buf[]);

Sonde rd41 = {
  .name = "RD41",
  .bitRate = 4800,
  .afcBandWidth = 12500,
  .frequencyDeviation = 6300,
  .bandwidthHz = 6300,
  .packetLength = RD41_PACKET_LENGTH,
  .partialPacketLength = 0,
  .preambleLengthBytes = 0,
  .syncWordLen = 40,
  .flipBytes = false,
  .syncWord = { 0xA6, 0x5A, 0x99, 0x56, 0x95 /*, 0xA9, 0x55, 0x59*/ },
  .processPartialPacket = NULL,
  .processPacket = processPacket
};

void deManchester8n1(uint8_t *buf) {
    int i,j,n=0;
    uint8_t res=0, c;

    for (i=0,n=0; i<RD41_PACKET_LENGTH; i++) {
        for (j=0; j<8; j+=2,n=(n+1)%10) {
            c=buf[i]>>6;
            if (n==0) {
		res=0;
                if (c!=0b10)
                    Serial.printf("Errore start bit i=%d (c=%02X)\n",i,c);
            } else if (n==9) {
                if (c!=0b01)
                    Serial.printf("Errore stop bit i=%d\n",i);
                //~ Serial.printf("%d==>%02X\n",i*2/5,res);
                buf[i*2/5]=res;
            } else {
                res>>=1;
                if (c==0b01)
                    res|=0x80;
                else if (c!=0b10)
                    Serial.printf("Manchester errato\n");
            }
            buf[i]<<=2;
        }
    }
}

static bool processPacket(uint8_t buf[]) {
  double lat, lng;
  float alt;

  deManchester8n1(buf);
  // for (int i=0;i<RD41_PACKET_LENGTH/5*2;i++)
  //   Serial.printf("%02X ",buf[i]);
  // Serial.println();

  double x=((buf[41]<<24)+(buf[40]<<16)+(buf[39]<<8)+buf[38])/100.0,
    y=((buf[45]<<24)+(buf[44]<<16)+(buf[43]<<8)+buf[42])/100.0,
    z=((buf[49]<<24)+(buf[48]<<16)+(buf[47]<<8)+buf[46])/100.0;
  // Serial.printf("ECEF: %f,%f,%f\n",x,y,z);
  ecef2wgs84(x, y, z, lat, lng, alt);
  Serial.printf("lat=%f, lng=%f, alt=%f\n",lat,lng,alt);
  packet.lat=lat;
  packet.lng=lng;
  packet.alt=alt;
  strcpy(packet.serial,"---");

  return true;
}
