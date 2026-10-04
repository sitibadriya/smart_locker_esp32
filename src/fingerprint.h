#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

#define RXD2 16
#define TXD2 17

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;

bool inisialisasiFingerprint();
int cariFingerprint();
void tungguJariDiangkat();

#endif