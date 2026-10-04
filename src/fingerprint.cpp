#include "fingerprint.h"

// ======================================================
// OBJECT FINGERPRINT
// ======================================================

HardwareSerial mySerial(2);

Adafruit_Fingerprint finger(&mySerial);

// ======================================================
// INISIALISASI FINGERPRINT
// ======================================================

bool inisialisasiFingerprint()
{
    Serial.println();
    Serial.println("======================================");
    Serial.println(" MEMERIKSA SENSOR FINGERPRINT");
    Serial.println("======================================");

    mySerial.begin(
        57600,
        SERIAL_8N1,
        RXD2,
        TXD2);

    finger.begin(57600);

    delay(500);

    if (finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint TERDETEKSI!");

        Serial.print("Kapasitas sensor: ");
        Serial.println(finger.capacity);

        return true;
    }

    Serial.println("Sensor fingerprint TIDAK TERDETEKSI!");
    Serial.println("Periksa kembali wiring TX/RX dan GND.");

    return false;
}

// ======================================================
// MENCARI FINGERPRINT
// ======================================================

int cariFingerprint()
{
    uint8_t p = finger.getImage();

    if (p != FINGERPRINT_OK)
    {
        return -1;
    }

    p = finger.image2Tz();

    if (p != FINGERPRINT_OK)
    {
        return -1;
    }

    p = finger.fingerSearch();

    if (p == FINGERPRINT_OK)
    {
        Serial.println();
        Serial.println("Sidik jari DITEMUKAN!");

        Serial.print("ID Sensor   : ");
        Serial.println(finger.fingerID);

        Serial.print("Kecocokan   : ");
        Serial.println(finger.confidence);

        return finger.fingerID;
    }

    if (p == FINGERPRINT_NOTFOUND)
    {
        Serial.println();
        Serial.println("Sidik jari TIDAK TERDAFTAR.");

        return 0;
    }

    return -1;
}

// ======================================================
// MENUNGGU JARI DIANGKAT
// ======================================================

void tungguJariDiangkat()
{
    Serial.println();
    Serial.println("Silakan angkat jari...");

    while (finger.getImage() != FINGERPRINT_NOFINGER)
    {
        delay(100);
    }

    Serial.println("Jari sudah diangkat.");
    Serial.println();
    Serial.println("Silakan tempelkan jari...");
}