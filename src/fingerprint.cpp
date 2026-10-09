#include "fingerprint.h"

// ======================================================
// UART
// ======================================================

HardwareSerial mySerial(2);

Adafruit_Fingerprint finger =
    Adafruit_Fingerprint(&mySerial);

// ======================================================
// INISIALISASI FINGERPRINT
// ======================================================

bool inisialisasiFingerprint()
{
    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " MEMERIKSA SENSOR FINGERPRINT");

    Serial.println(
        "======================================");

    mySerial.begin(
        57600,
        SERIAL_8N1,
        RXD2,
        TXD2);

    delay(300);

    finger.begin(57600);

    delay(300);

    if (!finger.verifyPassword())
    {
        Serial.println(
            "Sensor fingerprint TIDAK TERDETEKSI!");

        Serial.println(
            "Periksa koneksi TX/RX/GND/VCC.");

        return false;
    }

    Serial.println(
        "Sensor fingerprint TERDETEKSI!");

    if (
        finger.getParameters() ==
        FINGERPRINT_OK)
    {
        Serial.print(
            "Kapasitas sensor: ");

        Serial.println(
            finger.capacity);
    }

    // --------------------------------------------------
    // PASTIKAN TIDAK ADA JARI DI SENSOR
    // --------------------------------------------------

    Serial.println(
        "Memeriksa kondisi sensor...");

    unsigned long mulai =
        millis();

    while (
        millis() - mulai < 3000)
    {
        uint8_t hasil =
            finger.getImage();

        if (
            hasil ==
            FINGERPRINT_NOFINGER)
        {
            break;
        }

        delay(100);
    }

    Serial.println(
        "Fingerprint SIAP.");

    return true;
}

// ======================================================
// CEK APAKAH ID SUDAH TERISI
// ======================================================

bool cekFingerprintTersimpan(int id)
{
    if (
        id <= 0 ||
        id > 300)
    {
        return false;
    }

    uint8_t hasil =
        finger.loadModel(id);

    if (
        hasil == FINGERPRINT_OK)
    {
        return true;
    }

    return false;
}

// ======================================================
// HITUNG FINGERPRINT
// ======================================================

int hitungFingerprint()
{
    uint8_t hasil =
        finger.getTemplateCount();

    if (
        hasil != FINGERPRINT_OK)
    {
        return -1;
    }

    return finger.templateCount;
}

// ======================================================
// CARI ID SENSOR KOSONG
// ======================================================

int cariIdKosong()
{
    for (
        int id = 1;
        id <= 300;
        id++)
    {
        if (
            !cekFingerprintTersimpan(id))
        {
            return id;
        }
    }

    return -1;
}

// ======================================================
// TUNGGU JARI TERDETEKSI
// ======================================================

static bool tungguJariTerdeteksi(
    unsigned long timeoutMs)
{
    unsigned long mulai =
        millis();

    while (
        millis() - mulai < timeoutMs)
    {
        uint8_t hasil =
            finger.getImage();

        if (
            hasil == FINGERPRINT_OK)
        {
            return true;
        }

        if (
            hasil != FINGERPRINT_NOFINGER &&
            hasil != FINGERPRINT_PACKETRECIEVEERR)
        {
            Serial.print(
                "Status sensor: ");

            Serial.println(
                hasil);
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
    Serial.println();
    Serial.println(
        "Angkat jari dari sensor...");

    unsigned long mulai =
        millis();

    while (
        millis() - mulai < 15000)
    {
        uint8_t hasil =
            finger.getImage();

        if (
            hasil == FINGERPRINT_NOFINGER)
        {
            Serial.println(
                "Jari sudah diangkat.");

            delay(300);

            return;
        }

        delay(100);
    }

    Serial.println(
        "Peringatan: waktu menunggu jari "
        "diangkat habis.");
}

// ======================================================
// HAPUS SEMUA DATABASE FINGERPRINT
// ======================================================

bool hapusSemuaFingerprint()
{
    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " MEMBERSIHKAN DATABASE FINGERPRINT");

    Serial.println(
        "======================================");

    if (
        !finger.verifyPassword())
    {
        Serial.println(
            "Sensor fingerprint tidak siap.");

        return false;
    }

    // Pastikan jari tidak menempel
    tungguJariDiangkat();

    uint8_t hasil =
        finger.emptyDatabase();

    if (
        hasil == FINGERPRINT_OK)
    {
        Serial.println(
            "Database fingerprint berhasil "
            "dikosongkan.");

        delay(500);

        return true;
    }

    Serial.print(
        "Gagal menghapus database fingerprint.");

    Serial.print(
        " Kode: ");

    Serial.println(
        hasil);

    return false;
}

// ======================================================
// CARI FINGERPRINT
// ======================================================

int cariFingerprint()
{
    // --------------------------------------------------
    // AMBIL GAMBAR
    // --------------------------------------------------

    uint8_t hasil =
        finger.getImage();

    if (
        hasil == FINGERPRINT_NOFINGER)
    {
        return -1;
    }

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.print(
            "Gagal mengambil gambar. Kode: ");

        Serial.println(
            hasil);

        return -1;
    }

    // --------------------------------------------------
    // KONVERSI GAMBAR
    // --------------------------------------------------

    hasil =
        finger.image2Tz();

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.println(
            "Fingerprint tidak dapat diproses.");

        tungguJariDiangkat();

        return -1;
    }

    // --------------------------------------------------
    // CARI KECocokan
    // --------------------------------------------------

    hasil =
        finger.fingerSearch();

    if (
        hasil == FINGERPRINT_OK)
    {
        Serial.println();
        Serial.println(
            "Sidik jari DITEMUKAN.");

        Serial.print(
            "ID Sensor : ");

        Serial.println(
            finger.fingerID);

        Serial.print(
            "Kecocokan : ");

        Serial.println(
            finger.confidence);

        int id =
            finger.fingerID;

        tungguJariDiangkat();

        return id;
    }

    if (
        hasil == FINGERPRINT_NOTFOUND)
    {
        Serial.println();
        Serial.println(
            "Sidik jari TIDAK DITEMUKAN.");

        tungguJariDiangkat();

        return -1;
    }

    Serial.println();
    Serial.print(
        "Pencarian fingerprint gagal. Kode: ");

    Serial.println(
        hasil);

    tungguJariDiangkat();

    return -1;
}

// ======================================================
// DAFTAR SATU FINGERPRINT
// ======================================================

bool daftarSatuFingerprint(
    int id,
    String namaJari,
    unsigned long timeoutMs)
{
    Serial.println();
    Serial.println(
        "--------------------------------------");

    Serial.println(
        " PENDAFTARAN JARI");

    Serial.println(
        "--------------------------------------");

    Serial.print(
        "Jari       : ");

    Serial.println(
        namaJari);

    Serial.print(
        "ID Sensor  : ");

    Serial.println(
        id);

    // --------------------------------------------------
    // CEK ID
    // --------------------------------------------------

    if (
        id <= 0 ||
        id > 300)
    {
        Serial.println(
            "ID sensor tidak valid.");

        return false;
    }

    // --------------------------------------------------
    // PASTIKAN ID KOSONG
    // --------------------------------------------------

    if (
        cekFingerprintTersimpan(id))
    {
        Serial.println(
            "ID sensor sudah digunakan.");

        return false;
    }

    // --------------------------------------------------
    // PASTIKAN SENSOR SIAP
    // --------------------------------------------------

    if (
        !finger.verifyPassword())
    {
        Serial.println(
            "Sensor tidak siap.");

        return false;
    }

    // --------------------------------------------------
    // PASTIKAN JARI TIDAK MENEMPEL
    // --------------------------------------------------

    tungguJariDiangkat();

    // ==================================================
    // SCAN PERTAMA
    // ==================================================

    Serial.println();
    Serial.print(
        "Tempelkan ");

    Serial.print(
        namaJari);

    Serial.println(
        " pada sensor.");

    if (
        !tungguJariTerdeteksi(
            timeoutMs))
    {
        Serial.println();
        Serial.println(
            "TIMEOUT: jari tidak terdeteksi.");

        return false;
    }

    Serial.println(
        "Jari terdeteksi.");

    // --------------------------------------------------
    // KONVERSI SCAN PERTAMA
    // --------------------------------------------------

    uint8_t hasil =
        finger.image2Tz(1);

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.print(
            "Scan pertama gagal. Kode: ");

        Serial.println(
            hasil);

        tungguJariDiangkat();

        return false;
    }

    Serial.println(
        "Scan pertama berhasil.");

    // --------------------------------------------------
    // ANGKAT JARI
    // --------------------------------------------------

    tungguJariDiangkat();

    // ==================================================
    // SCAN KEDUA
    // ==================================================

    Serial.println();
    Serial.print(
        "Tempelkan kembali ");

    Serial.print(
        namaJari);

    Serial.println(
        " yang sama.");

    if (
        !tungguJariTerdeteksi(
            timeoutMs))
    {
        Serial.println();
        Serial.println(
            "TIMEOUT: scan kedua gagal.");

        return false;
    }

    Serial.println(
        "Jari terdeteksi kembali.");

    // --------------------------------------------------
    // KONVERSI SCAN KEDUA
    // --------------------------------------------------

    hasil =
        finger.image2Tz(2);

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.print(
            "Scan kedua gagal. Kode: ");

        Serial.println(
            hasil);

        tungguJariDiangkat();

        return false;
    }

    Serial.println(
        "Scan kedua berhasil.");

    // --------------------------------------------------
    // ANGKAT JARI
    // --------------------------------------------------

    tungguJariDiangkat();

    // ==================================================
    // BUAT MODEL
    // ==================================================

    Serial.println();
    Serial.println(
        "Membuat template fingerprint...");

    hasil =
        finger.createModel();

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.print(
            "Fingerprint tidak cocok "
            "atau model gagal dibuat.");

        Serial.print(
            " Kode: ");

        Serial.println(
            hasil);

        return false;
    }

    Serial.println(
        "Template fingerprint berhasil dibuat.");

    // ==================================================
    // SIMPAN MODEL
    // ==================================================

    Serial.println();
    Serial.println(
        "Menyimpan fingerprint ke sensor...");

    hasil =
        finger.storeModel(id);

    if (
        hasil != FINGERPRINT_OK)
    {
        Serial.println();
        Serial.print(
            "Gagal menyimpan fingerprint.");

        Serial.print(
            " Kode: ");

        Serial.println(
            hasil);

        return false;
    }

    Serial.println(
        "Fingerprint berhasil disimpan.");

    Serial.print(
        "ID Sensor: ");

    Serial.println(
        id);

    return true;
}