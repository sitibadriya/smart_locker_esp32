#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// ======================================================
// KONFIGURASI UART FINGERPRINT
// ======================================================

#define RXD2 16
#define TXD2 17

// ======================================================
// DEKLARASI OBJEK FINGERPRINT
// ======================================================

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;

// ======================================================
// INISIALISASI SENSOR
// ======================================================

bool inisialisasiFingerprint();

// ======================================================
// PENGELOLAAN ID FINGERPRINT
// ======================================================

// Memeriksa apakah ID sensor sudah digunakan.
bool cekFingerprintTersimpan(int id);

// Menghitung jumlah template fingerprint pada sensor.
// Mengembalikan -1 jika terjadi kegagalan.
int hitungFingerprint();

// Mencari ID sensor yang belum digunakan.
// Mengembalikan -1 jika tidak ditemukan.
int cariIdKosong();

// ======================================================
// AUTENTIKASI FINGERPRINT
// ======================================================

// Mencari sidik jari yang cocok pada sensor.
// Mengembalikan ID sensor jika ditemukan,
// atau -1 jika tidak ditemukan.
int cariFingerprint();

// ======================================================
// PENDAFTARAN FINGERPRINT
// ======================================================

// Mendaftarkan satu sidik jari dengan dua kali scan.
// Parameter timeoutMs memiliki nilai default 30000 ms.
bool daftarSatuFingerprint(
    int id,
    String namaJari,
    unsigned long timeoutMs = 30000);

// Menunggu sampai jari diangkat dari sensor.
void tungguJariDiangkat();

// ======================================================
// PENGHAPUSAN TEMPLATE FINGERPRINT
// ======================================================

// Menghapus satu template berdasarkan ID sensor.
bool hapusFingerprint(int id);

// Menghapus seluruh template fingerprint.
// Gunakan hanya ketika reset database memang diperlukan.
bool hapusSemuaFingerprint();

#endif