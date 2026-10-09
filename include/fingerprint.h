#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// ======================================================
// UART FINGERPRINT
// ======================================================

#define RXD2 16
#define TXD2 17

// ======================================================
// OBJEK FINGERPRINT
// ======================================================

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;

// ======================================================
// INISIALISASI
// ======================================================

bool inisialisasiFingerprint();

// ======================================================
// CEK TEMPLATE
// ======================================================

bool cekFingerprintTersimpan(int id);

// ======================================================
// HITUNG TEMPLATE
// ======================================================

int hitungFingerprint();

// ======================================================
// CARI ID SENSOR KOSONG
// ======================================================

int cariIdKosong();

// ======================================================
// CARI FINGERPRINT SAAT AUTENTIKASI
// ======================================================

int cariFingerprint();

// ======================================================
// ENROLL SATU FINGERPRINT
// ======================================================

bool daftarSatuFingerprint(
    int id,
    String namaJari,
    unsigned long timeoutMs = 30000);

// ======================================================
// MENUNGGU JARI DIANGKAT
// ======================================================

void tungguJariDiangkat();

// ======================================================
// HAPUS SEMUA TEMPLATE FINGERPRINT
// ======================================================

bool hapusSemuaFingerprint();

#endif