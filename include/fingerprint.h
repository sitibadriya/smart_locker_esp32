#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// ======================================================
// KONFIGURASI UART FINGERPRINT
// Nilai bawaan; tidak menimpa jika sudah didefinisikan (mis. di config.h)
// ======================================================

#ifndef RXD2
#define RXD2 16
#endif

#ifndef TXD2
#define TXD2 17
#endif

// ======================================================
// DEKLARASI OBJEK FINGERPRINT
// ======================================================

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;

// ID sensor tertinggi yang valid. Diisi dari kapasitas sensor
// saat inisialisasiFingerprint() berhasil (nilai awal 127).
extern int maxIdSensor;

// ======================================================
// INISIALISASI SENSOR
// ======================================================

bool inisialisasiFingerprint();

// ======================================================
// PENGELOLAAN ID FINGERPRINT
// ======================================================

// Status slot sensor:
//   1  = slot terisi template
//   0  = slot kosong
//  -1  = gagal membaca (error komunikasi atau ID tidak valid)
int statusSlot(int id);

// Memeriksa apakah ID sensor sudah digunakan.
// Mengembalikan true jika terisi ATAU jika status tidak bisa dipastikan
// (error komunikasi dianggap terisi agar template tidak tertimpa).
bool cekFingerprintTersimpan(int id);

// Menghitung jumlah template fingerprint pada sensor.
// Mengembalikan -1 jika terjadi kegagalan.
int hitungFingerprint();

// Mencari ID sensor yang belum digunakan.
// Mengembalikan -1 jika tidak ditemukan atau terjadi error komunikasi.
int cariIdKosong();

// ======================================================
// AUTENTIKASI FINGERPRINT
// ======================================================

// Membaca dan mencari sidik jari pada sensor.
// Mengembalikan:
//   ID sensor (> 0) jika sidik jari cocok
//   -1 jika timeout atau terjadi error pembacaan
//   -2 jika sidik jari terbaca tetapi tidak terdaftar
int cariFingerprint();

// ======================================================
// PENDAFTARAN FINGERPRINT
// ======================================================

// Mendaftarkan satu sidik jari dengan dua kali scan.
// Parameter timeoutMs memiliki nilai default 30000 ms.
bool daftarSatuFingerprint(
    int id,
    const String &namaJari,
    unsigned long timeoutMs = 30000);

// Menunggu sampai jari diangkat dari sensor.
// Mengembalikan false jika waktu tunggu (15 detik) habis.
bool tungguJariDiangkat();

// ======================================================
// PENGHAPUSAN TEMPLATE FINGERPRINT
// ======================================================

// Menghapus satu template berdasarkan ID sensor.
bool hapusFingerprint(int id);

// Menghapus seluruh template fingerprint.
// Gunakan hanya ketika reset database memang diperlukan.
bool hapusSemuaFingerprint();

#endif