
#include "fingerprint.h"

// ======================================================
// UART DAN OBJEK SENSOR
// ======================================================

HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ======================================================
// INISIALISASI SENSOR FINGERPRINT
// ======================================================

bool inisialisasiFingerprint()
{
    Serial.println();
    Serial.println("======================================");
    Serial.println(" MEMERIKSA SENSOR FINGERPRINT");
    Serial.println("======================================");

    mySerial.begin(57600, SERIAL_8N1, RXD2, TXD2);
    delay(300);

    finger.begin(57600);
    delay(300);

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint TIDAK TERDETEKSI!");
        Serial.println("Periksa koneksi dan konfigurasi UART.");
        return false;
    }

    Serial.println("Sensor fingerprint TERDETEKSI!");

    if (finger.getParameters() == FINGERPRINT_OK)
    {
        Serial.print("Kapasitas sensor: ");
        Serial.println(finger.capacity);
    }

    Serial.println("Memeriksa kondisi sensor...");

    unsigned long mulai = millis();

    while (millis() - mulai < 3000)
    {
        uint8_t hasil = finger.getImage();

        if (hasil == FINGERPRINT_NOFINGER)
        {
            break;
        }

        delay(100);
    }

    Serial.println("Fingerprint SIAP.");
    return true;
}

// ======================================================
// CEK TEMPLATE BERDASARKAN ID
// ======================================================

bool cekFingerprintTersimpan(int id)
{
    if (id <= 0 || id > 300)
    {
        return false;
    }

    uint8_t hasil = finger.loadModel(id);
    return hasil == FINGERPRINT_OK;
}

// ======================================================
// HITUNG JUMLAH TEMPLATE
// ======================================================

int hitungFingerprint()
{
    uint8_t hasil = finger.getTemplateCount();

    if (hasil != FINGERPRINT_OK)
    {
        Serial.println("Gagal menghitung template fingerprint.");
        return -1;
    }

    return finger.templateCount;
}

// ======================================================
// CARI ID SENSOR KOSONG
// ======================================================

int cariIdKosong()
{
    if (!finger.verifyPassword())
    {
        Serial.println("Sensor tidak siap. Pencarian ID dibatalkan.");
        return -1;
    }

    for (int id = 1; id <= 300; id++)
    {
        if (!cekFingerprintTersimpan(id))
        {
            Serial.print("ID sensor kosong ditemukan: ");
            Serial.println(id);
            return id;
        }
    }

    Serial.println("Tidak ditemukan ID sensor yang kosong.");
    return -1;
}

// ======================================================
// TUNGGU JARI TERDETEKSI
// ======================================================

static bool tungguJariTerdeteksi(unsigned long timeoutMs)
{
    unsigned long mulai = millis();

    while (millis() - mulai < timeoutMs)
    {
        uint8_t hasil = finger.getImage();

        if (hasil == FINGERPRINT_OK)
        {
            return true;
        }

        if (hasil != FINGERPRINT_NOFINGER &&
            hasil != FINGERPRINT_PACKETRECIEVEERR)
        {
            Serial.print("Status sensor: ");
            Serial.println(hasil);
        }

        delay(100);
    }

    return false;
}

// ======================================================
// TUNGGU JARI DIANGKAT
// ======================================================

void tungguJariDiangkat()
{
    Serial.println("Angkat jari dari sensor...");

    unsigned long mulai = millis();

    while (millis() - mulai < 15000)
    {
        uint8_t hasil = finger.getImage();

        if (hasil == FINGERPRINT_NOFINGER)
        {
            Serial.println("Jari sudah diangkat.");
            delay(300);
            return;
        }

        delay(100);
    }

    Serial.println("Peringatan: waktu menunggu jari diangkat habis.");
}

// ======================================================
// AUTENTIKASI: BACA DAN CARI SIDIK JARI
// ======================================================

int cariFingerprint()
{
    Serial.println("Tempelkan jari pada sensor (timeout 10 detik).");

    unsigned long mulai = millis();
    uint8_t hasil = FINGERPRINT_NOFINGER;

    // Tunggu sampai jari terdeteksi
    while (millis() - mulai < 10000)
    {
        hasil = finger.getImage();

        if (hasil == FINGERPRINT_OK)
        {
            Serial.println("Gambar jari berhasil diambil.");
            break;
        }

        if (hasil != FINGERPRINT_NOFINGER &&
            hasil != FINGERPRINT_PACKETRECIEVEERR)
        {
            Serial.print("Gagal membaca sensor. Kode: ");
            Serial.println(hasil);
        }

        delay(100);
    }

    if (hasil != FINGERPRINT_OK)
    {
        Serial.println("Timeout: jari tidak terdeteksi.");
        return -1;
    }

    hasil = finger.image2Tz();

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Fingerprint gagal diproses. Kode: ");
        Serial.println(hasil);
        tungguJariDiangkat();
        return -1;
    }

    hasil = finger.fingerSearch();

    if (hasil == FINGERPRINT_OK)
    {
        Serial.println("Sidik jari DITEMUKAN.");

        Serial.print("ID Sensor: ");
        Serial.println(finger.fingerID);

        Serial.print("Kecocokan: ");
        Serial.println(finger.confidence);

        int id = finger.fingerID;
        tungguJariDiangkat();
        return id;
    }

    if (hasil == FINGERPRINT_NOTFOUND)
    {
        Serial.println("Sidik jari TIDAK DITEMUKAN.");
        tungguJariDiangkat();
        return -2; // Menandakan sidik jari tidak terdaftar
    }

    Serial.print("Pencarian fingerprint gagal. Kode: ");
    Serial.println(hasil);

    tungguJariDiangkat();
    return -1;
}

// ======================================================
// HAPUS SATU TEMPLATE
// ======================================================

bool hapusFingerprint(int id)
{
    Serial.println();
    Serial.println("=== MENGHAPUS FINGERPRINT ===");

    if (id <= 0 || id > 300)
    {
        Serial.println("ID sensor tidak valid.");
        return false;
    }

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint tidak siap.");
        return false;
    }

    uint8_t hasil = finger.deleteModel(id);

    if (hasil == FINGERPRINT_OK)
    {
        Serial.print("Fingerprint ID ");
        Serial.print(id);
        Serial.println(" berhasil dihapus.");
        return true;
    }

    Serial.print("Gagal menghapus fingerprint. Kode: ");
    Serial.println(hasil);
    return false;
}

// ======================================================
// HAPUS SELURUH DATABASE SENSOR
// PERHATIAN: MENGHAPUS SEMUA TEMPLATE
// ======================================================

bool hapusSemuaFingerprint()
{
    Serial.println();
    Serial.println("=== RESET DATABASE FINGERPRINT ===");

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint tidak siap.");
        return false;
    }

    tungguJariDiangkat();

    uint8_t hasil = finger.emptyDatabase();

    if (hasil == FINGERPRINT_OK)
    {
        Serial.println("Database fingerprint berhasil dikosongkan.");
        delay(500);
        return true;
    }

    Serial.print("Gagal mengosongkan database. Kode: ");
    Serial.println(hasil);
    return false;
}

// ======================================================
// PENDAFTARAN SATU FINGERPRINT
// SETIAP JARI DIPINDAI DUA KALI
// ======================================================

bool daftarSatuFingerprint(
    int id,
    String namaJari,
    unsigned long timeoutMs)
{
    Serial.println();
    Serial.println("--------------------------------------");
    Serial.println(" PENDAFTARAN JARI");
    Serial.println("--------------------------------------");

    Serial.print("Jari      : ");
    Serial.println(namaJari);

    Serial.print("ID Sensor : ");
    Serial.println(id);

    if (id <= 0 || id > 300)
    {
        Serial.println("ID sensor tidak valid.");
        return false;
    }

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor tidak siap.");
        return false;
    }

    if (cekFingerprintTersimpan(id))
    {
        Serial.println("ID sensor sudah digunakan.");
        return false;
    }

    tungguJariDiangkat();

    // Scan pertama
    Serial.print("Tempelkan ");
    Serial.print(namaJari);
    Serial.println(" pada sensor.");

    if (!tungguJariTerdeteksi(timeoutMs))
    {
        Serial.println("TIMEOUT: jari tidak terdeteksi.");
        return false;
    }

    uint8_t hasil = finger.image2Tz(1);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Scan pertama gagal. Kode: ");
        Serial.println(hasil);
        tungguJariDiangkat();
        return false;
    }

    Serial.println("Scan pertama berhasil.");
    tungguJariDiangkat();

    // Scan kedua
    Serial.print("Tempelkan kembali ");
    Serial.print(namaJari);
    Serial.println(" yang sama.");

    if (!tungguJariTerdeteksi(timeoutMs))
    {
        Serial.println("TIMEOUT: scan kedua gagal.");
        return false;
    }

    hasil = finger.image2Tz(2);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Scan kedua gagal. Kode: ");
        Serial.println(hasil);
        tungguJariDiangkat();
        return false;
    }

    Serial.println("Scan kedua berhasil.");
    tungguJariDiangkat();

    // Buat template dari dua scan
    Serial.println("Membuat template fingerprint...");

    hasil = finger.createModel();

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Model gagal dibuat. Kode: ");
        Serial.println(hasil);
        return false;
    }

    // Simpan template
    Serial.println("Menyimpan fingerprint ke sensor...");

    hasil = finger.storeModel(id);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Gagal menyimpan fingerprint. Kode: ");
        Serial.println(hasil);
        return false;
    }

    // Verifikasi
    Serial.println("Memverifikasi template...");

    hasil = finger.loadModel(id);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Verifikasi template gagal. Kode: ");
        Serial.println(hasil);
        Serial.println("Pendaftaran belum dinyatakan berhasil.");
        return false;
    }

    Serial.println("Fingerprint berhasil disimpan dan diverifikasi.");
    Serial.print("ID Sensor: ");
    Serial.println(id);

    return true;
}
