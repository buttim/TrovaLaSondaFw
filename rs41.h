#ifndef __RS41_h__
#define __RS41_h__
#define RS41_PACKET_LENGTH	312
#define RS41AUX_PACKET_LENGTH	510
extern Sonde rs41;
void ecef2wgs84(double x, double y, double z, double &lat, double &lon, float &alt);
#endif