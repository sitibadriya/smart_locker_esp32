
#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

#ifndef RXD2
#define RXD2 16
#endif

#ifndef TXD2
#define TXD2 17
#endif

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;
extern int maxIdSensor;

typedef void (*HookTunggu)();

void setHookTunggu(HookTunggu hook);

bool inisialisasiFingerprint();
bool periksaSensor();

int statusSlot(int id);
bool cekFingerprintTersimpan(int id);
int hitungFingerprint();

// Mulai pencarian dari ID tertentu.
// Default 1 agar L01 tetap seperti sebelumnya.
int cariIdKosong(int mulaiDari = 1);

int cariFingerprint();

bool daftarSatuFingerprint(
    int id,
    const String &namaJari,
    unsigned long timeoutMs = 30000,
    bool *templateTersimpan = nullptr);

bool tungguJariDiangkat();

bool hapusFingerprint(int id);
bool hapusSemuaFingerprint();

#endif
