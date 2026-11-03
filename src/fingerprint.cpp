#include "fingerprint.h"

// ======================================================
// UART DAN OBJEK SENSOR
// ======================================================

HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

int maxIdSensor = 127;

static HookTunggu hookTunggu = nullptr;
static bool jariPerluDiangkat = false;

void setHookTunggu(HookTunggu hook)
{
    hookTunggu = hook;
}

static inline void jalankanHookTunggu()
{
    if (hookTunggu != nullptr)
    {
        hookTunggu();
    }
}

// ======================================================
// INISIALISASI SENSOR
// ======================================================

bool inisialisasiFingerprint()
{
    Serial.println();
    Serial.println("======================================");
    Serial.println(" MEMERIKSA SENSOR FINGERPRINT");
    Serial.println("======================================");

    jariPerluDiangkat = false;

    mySerial.begin(57600, SERIAL_8N1, RXD2, TXD2);
    delay(500);

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint TIDAK TERDETEKSI!");
        Serial.println("Periksa koneksi UART dan konfigurasi sensor.");
        return false;
    }

    Serial.println("Sensor fingerprint TERDETEKSI!");

    if (finger.getParameters() == FINGERPRINT_OK)
    {
        Serial.print("Kapasitas sensor: ");
        Serial.println(finger.capacity);

        if (finger.capacity >= 1 && finger.capacity <= 1000)
        {
            maxIdSensor = finger.capacity;
        }
    }

    Serial.print("Batas ID sensor: ");
    Serial.println(maxIdSensor);

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
// PERIKSA KONEKSI SENSOR
// ======================================================

bool periksaSensor()
{
    if (finger.verifyPassword())
    {
        return true;
    }

    delay(200);
    return finger.verifyPassword();
}

// ======================================================
// STATUS SLOT SENSOR
// 1 = terisi
// 0 = kosong
// -1 = error / tidak dapat dipastikan
// ======================================================

int statusSlot(int id)
{
    if (id <= 0 || id > maxIdSensor)
    {
        return -1;
    }

    uint8_t hasil = finger.loadModel(id);

    if (hasil == FINGERPRINT_OK)
    {
        return 1;
    }

    if (hasil == FINGERPRINT_PACKETRECIEVEERR ||
        hasil == FINGERPRINT_TIMEOUT ||
        hasil == FINGERPRINT_BADPACKET ||
        hasil == FINGERPRINT_BADLOCATION)
    {
        return -1;
    }

    return 0;
}

// ======================================================
// CEK TEMPLATE
// ======================================================

bool cekFingerprintTersimpan(int id)
{
    if (id <= 0 || id > maxIdSensor)
    {
        return false;
    }

    return statusSlot(id) != 0;
}

// ======================================================
// HITUNG TEMPLATE
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

int cariIdKosong(int mulaiDari)
{
    if (!finger.verifyPassword())
    {
        Serial.println("Sensor tidak siap. Pencarian ID dibatalkan.");
        return -1;
    }

    if (mulaiDari < 1 || mulaiDari > maxIdSensor)
        return -1;

    for (int id = mulaiDari; id <= maxIdSensor; id++)
    {
        int status = statusSlot(id);

        if (status == -1)
        {
            delay(50);
            status = statusSlot(id);
        }

        if (status == -1)
        {
            Serial.print("Error komunikasi saat memeriksa ID ");
            Serial.println(id);
            Serial.println("Pencarian ID dibatalkan.");
            return -1;
        }

        if (status == 0)
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

    uint8_t statusTerakhir = 255;

    while (millis() - mulai < timeoutMs)
    {
        uint8_t hasil = finger.getImage();

        if (hasil == FINGERPRINT_OK)
        {
            Serial.println("Gambar sidik jari berhasil dibaca.");
            return true;
        }

        // Cetak perubahan kode saja agar Serial Monitor
        // tidak dipenuhi pesan yang sama berulang kali.
        if (hasil != FINGERPRINT_NOFINGER &&
            hasil != statusTerakhir)
        {
            Serial.print("[FINGERPRINT] Kode getImage: ");
            Serial.println(hasil);

            statusTerakhir = hasil;
        }

        jalankanHookTunggu();
        delay(100);
    }

    Serial.println("[FINGERPRINT] Waktu tunggu deteksi jari habis.");
    return false;
}

// ======================================================
// TUNGGU JARI DIANGKAT
// ======================================================

bool tungguJariDiangkat()
{
    Serial.println("Angkat jari dari sensor...");

    unsigned long mulai = millis();

    while (millis() - mulai < 15000)
    {
        uint8_t hasil = finger.getImage();

        if (hasil == FINGERPRINT_NOFINGER)
        {
            Serial.println("Jari sudah diangkat.");

            jariPerluDiangkat = false;

            delay(300);
            return true;
        }

        jalankanHookTunggu();
        delay(100);
    }

    Serial.println("Peringatan: waktu menunggu jari diangkat habis.");

    // Jangan mengubah status jari secara paksa.
    // Pada autentikasi, flag tetap menunggu sampai sensor
    // benar-benar melaporkan NOFINGER.
    return false;
}

// ======================================================
// AUTENTIKASI FINGERPRINT
//
// Return:
// > 0 = ID sensor ditemukan
// -1  = timeout / error
// -2  = sidik jari tidak terdaftar
// ======================================================

int cariFingerprint()
{
    static bool sudahMenunggu = false;

    if (jariPerluDiangkat)
    {
        uint8_t cek = finger.getImage();

        if (cek == FINGERPRINT_NOFINGER)
        {
            jariPerluDiangkat = false;
            Serial.println("Jari sudah diangkat.");
        }
        else
        {
            jalankanHookTunggu();
            delay(100);
            return -1;
        }
    }

    if (!sudahMenunggu)
    {
        Serial.println("Menunggu sidik jari...");
        sudahMenunggu = true;
    }

    unsigned long mulai = millis();
    uint8_t hasil = FINGERPRINT_NOFINGER;

    while (millis() - mulai < 1500)
    {
        hasil = finger.getImage();

        if (hasil == FINGERPRINT_OK)
        {
            Serial.println("Gambar jari berhasil diambil.");
            sudahMenunggu = false;
            break;
        }

        jalankanHookTunggu();
        delay(100);
    }

    if (hasil != FINGERPRINT_OK)
    {
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

        jariPerluDiangkat = true;

        return finger.fingerID;
    }

    if (hasil == FINGERPRINT_NOTFOUND)
    {
        Serial.println("Sidik jari TIDAK DITEMUKAN.");

        tungguJariDiangkat();
        return -2;
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

    if (id <= 0 || id > maxIdSensor)
    {
        Serial.println("ID sensor tidak valid.");
        return false;
    }

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor fingerprint tidak siap.");
        return false;
    }

    int status = statusSlot(id);

    if (status == 0)
    {
        Serial.println("ID sudah kosong. Tidak ada template yang dihapus.");
        return true;
    }

    if (status == -1)
    {
        Serial.println("Status ID tidak dapat dipastikan. Penghapusan dibatalkan.");
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

    if (!tungguJariDiangkat())
    {
        Serial.println("Reset dibatalkan karena jari belum terangkat.");
        return false;
    }

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
//
// templateTersimpan:
// false = belum ada template yang berhasil disimpan
// true  = storeModel berhasil, meskipun verifikasi berikutnya gagal
// ======================================================

bool daftarSatuFingerprint(
    int id,
    const String &namaJari,
    unsigned long timeoutMs,
    bool *templateTersimpan)
{
    // Nilai awal wajib false.
    if (templateTersimpan != nullptr)
    {
        *templateTersimpan = false;
    }

    Serial.println();
    Serial.println("--------------------------------------");
    Serial.println(" PENDAFTARAN JARI");
    Serial.println("--------------------------------------");

    Serial.print("Jari      : ");
    Serial.println(namaJari);

    Serial.print("ID Sensor : ");
    Serial.println(id);

    if (id <= 0 || id > maxIdSensor)
    {
        Serial.println("ID sensor tidak valid.");
        return false;
    }

    if (!finger.verifyPassword())
    {
        Serial.println("Sensor tidak siap.");
        return false;
    }

    // Pastikan slot benar-benar kosong.
    int status = statusSlot(id);

    if (status == 1)
    {
        Serial.println("ID sensor sudah digunakan.");
        return false;
    }

    if (status == -1)
    {
        Serial.println("Status slot tidak dapat dipastikan.");
        Serial.println("Pendaftaran dibatalkan.");
        return false;
    }

    // Pastikan tidak ada jari yang tertinggal.
    if (!tungguJariDiangkat())
    {
        Serial.println("Jari belum terangkat. Pendaftaran dibatalkan.");
        return false;
    }

    // --------------------------------------------------
    // SCAN PERTAMA
    // --------------------------------------------------

    Serial.print("Tempelkan ");
    Serial.print(namaJari);
    Serial.println(" pada sensor.");

    if (!tungguJariTerdeteksi(timeoutMs))
    {
        Serial.println("TIMEOUT: scan pertama gagal.");
        return false;
    }

    uint8_t hasil = finger.image2Tz(1);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Scan pertama gagal diproses. Kode: ");
        Serial.println(hasil);

        tungguJariDiangkat();
        return false;
    }

    Serial.println("Scan pertama berhasil.");

    // --------------------------------------------------
    // CEK DUPLIKAT
    // --------------------------------------------------

    hasil = finger.fingerSearch();

    if (hasil == FINGERPRINT_OK)
    {
        Serial.print("Jari sudah terdaftar di ID sensor ");
        Serial.println(finger.fingerID);

        tungguJariDiangkat();
        return false;
    }

    if (hasil != FINGERPRINT_NOTFOUND)
    {
        Serial.print("Pemeriksaan duplikat gagal. Kode: ");
        Serial.println(hasil);

        tungguJariDiangkat();
        return false;
    }

    // --------------------------------------------------
    // ANGKAT JARI SEBELUM SCAN KEDUA
    // --------------------------------------------------

    if (!tungguJariDiangkat())
    {
        Serial.println("Jari tidak berhasil diangkat.");
        Serial.println("Pendaftaran dibatalkan.");
        return false;
    }

    // --------------------------------------------------
    // SCAN KEDUA
    // --------------------------------------------------

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
        Serial.print("Scan kedua gagal diproses. Kode: ");
        Serial.println(hasil);

        tungguJariDiangkat();
        return false;
    }

    Serial.println("Scan kedua berhasil.");

    // Wajib berhasil mengangkat jari sebelum membuat model.
    if (!tungguJariDiangkat())
    {
        Serial.println("Jari belum terangkat.");
        Serial.println("Pembuatan template dibatalkan.");
        return false;
    }

    // --------------------------------------------------
    // BUAT TEMPLATE
    // --------------------------------------------------

    Serial.println("Membuat template fingerprint...");

    hasil = finger.createModel();

    if (hasil == FINGERPRINT_ENROLLMISMATCH)
    {
        Serial.println("Kedua scan tidak cocok.");
        Serial.println("Gunakan jari yang sama untuk kedua scan.");
        return false;
    }

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Model gagal dibuat. Kode: ");
        Serial.println(hasil);
        return false;
    }

    // --------------------------------------------------
    // SIMPAN TEMPLATE
    // --------------------------------------------------

    Serial.println("Menyimpan fingerprint ke sensor...");

    hasil = finger.storeModel(id);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Gagal menyimpan fingerprint. Kode: ");
        Serial.println(hasil);
        return false;
    }

    // PENTING:
    // Mulai titik ini, template benar-benar sudah disimpan.
    // Main.cpp boleh melakukan rollback jika verifikasi gagal.
    if (templateTersimpan != nullptr)
    {
        *templateTersimpan = true;
    }

    // --------------------------------------------------
    // VERIFIKASI
    // --------------------------------------------------

    Serial.println("Memverifikasi template...");

    hasil = finger.loadModel(id);

    if (hasil != FINGERPRINT_OK)
    {
        Serial.print("Verifikasi template gagal. Kode: ");
        Serial.println(hasil);

        Serial.println("Template sudah disimpan, tetapi belum terverifikasi.");
        return false;
    }

    Serial.println("Fingerprint berhasil disimpan dan diverifikasi.");

    Serial.print("ID Sensor: ");
    Serial.println(id);

    return true;
}