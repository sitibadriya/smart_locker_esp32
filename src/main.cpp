#define ENABLE_USER_AUTH
#define ENABLE_FIRESTORE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_system.h>

#include "fingerprint.h"
#include "secrets.h"

// ======================================================
// IDENTITAS PERANGKAT
// ======================================================

#ifndef ID_PERANGKAT
#define ID_PERANGKAT "esp32-locker-01"
#endif

#ifndef ID_LOCKER
#define ID_LOCKER "L01"
#endif

// ======================================================
// PIN PERANGKAT KERAS
// ======================================================

#define RELAY_PIN 18
#define SENSOR_PINTU_PIN 4

// ======================================================
// KONFIGURASI
// ======================================================

#ifndef AKTIFKAN_SENSOR_PINTU
#define AKTIFKAN_SENSOR_PINTU true
#endif

#ifndef SENSOR_PINTU_TERPASANG
#define SENSOR_PINTU_TERPASANG false
#endif

#ifndef DURASI_BUKA_MS
#define DURASI_BUKA_MS 10000
#endif

#ifndef BATAS_MAKS_SOLENOID_MS
#define BATAS_MAKS_SOLENOID_MS 0
#endif

#ifndef PINTU_TERTUTUP_LEVEL
#define PINTU_TERTUTUP_LEVEL LOW
#endif

#ifndef DEBOUNCE_PINTU_MS
#define DEBOUNCE_PINTU_MS 300
#endif

#ifndef KIRIM_STATUS_PINTU
#define KIRIM_STATUS_PINTU true
#endif

#ifndef DEBUG_SENSOR_PINTU
#define DEBUG_SENSOR_PINTU false
#endif

#ifndef INTERVAL_CEK_SENSOR_MS
#define INTERVAL_CEK_SENSOR_MS 30000
#endif

// ======================================================
// INTERVAL WIFI
// ======================================================

const unsigned long WIFI_TIMEOUT_MS = 20000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

// ======================================================
// RELAY
// ======================================================

bool lockerTerbuka = false;

void relayOn()
{
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    lockerTerbuka = true;
}

void relayOff()
{
    pinMode(RELAY_PIN, INPUT);
    lockerTerbuka = false;
}

// ======================================================
// STATUS SIKLUS PINTU
// ======================================================

enum StatusSiklusPintu
{
    LOCKER_TERKUNCI,
    MENUNGGU_PINTU_DIBUKA,
    MENUNGGU_PINTU_DITUTUP
};

StatusSiklusPintu statusSiklusPintu = LOCKER_TERKUNCI;

// True hanya setelah akses sah membuka solenoid; mencegah alarm pintu palsu.
bool aksesPintuTerotorisasi = false;

unsigned long waktuMulaiSiklus = 0;
unsigned long waktuMulaiMenungguTutup = 0;

void periksaSensorPintu();
void perbaruiSiklusLocker();

// ======================================================
// FIREBASE
// ======================================================

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;
WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client);
Firestore::Documents Docs;

// ======================================================
// STATUS SISTEM
// ======================================================

bool firebaseStarted = false;
bool firebaseReady = false;
bool fingerprintReady = false;
bool prosesEnrollmentAktif = false;

unsigned long waktuCekEnrollment = 0;
const unsigned long INTERVAL_CEK_ENROLLMENT = 6000;

unsigned long waktuCekLocker = 0;
const unsigned long INTERVAL_CEK_LOCKER = 6000;

unsigned long waktuReconnectWiFi = 0;
unsigned long waktuRetryFingerprint = 0;
unsigned long waktuCekSensor = 0;

// ======================================================
// DATA ENROLLMENT
// ======================================================

const String namaJari[5] = {
    "JEMPOL",
    "TELUNJUK",
    "TENGAH",
    "MANIS",
    "KELINGKING"};

// Satu firmware untuk L01 dan L02
String labelFingerprint[5];

void inisialisasiLabelFingerprint()
{
    int offset = (String(ID_LOCKER) == "L02") ? 5 : 0;

    for (int i = 0; i < 5; i++)
    {
        int nomor = offset + i + 1;
        String angka = (nomor < 10 ? "0" : "") + String(nomor);
        labelFingerprint[i] = "FP" + angka;
    }
}

// ======================================================
// CACHE MAPPING
// ======================================================

struct EntriMapping
{
    bool ada = false;
    int idSensor = -1;
    String idFingerprint;
    String idSantri;
    String namaSantri;
    String idLocker;
    bool statusAktif = false;
};

EntriMapping cacheMapping[5];

String cacheBatchId = "";

bool cacheSiap = false;

unsigned long waktuCobaCache = 0;

const unsigned long JEDA_ULANG_CACHE_MS = 10000;

void kosongkanCache()
{
    for (int i = 0; i < 5; i++)
    {
        cacheMapping[i] = EntriMapping();
    }

    cacheBatchId = "";
    cacheSiap = true;
    waktuCobaCache = 0;
}

// ======================================================
// WIFI
// ======================================================

bool hubungkanWiFi(unsigned long timeoutMs = WIFI_TIMEOUT_MS)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    Serial.println();
    Serial.print("[WIFI] Menghubungkan ke: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Memulai koneksi baru.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long mulai = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - mulai < timeoutMs)
    {
        delay(500);
        Serial.print(".");

        periksaSensorPintu();
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("[WIFI] Berhasil terhubung.");

        Serial.print("[WIFI] IP Address: ");
        Serial.println(WiFi.localIP());

        Serial.print("[WIFI] RSSI: ");
        Serial.println(WiFi.RSSI());

        return true;
    }

    Serial.print("[WIFI] Gagal terhubung. Kode status: ");
    Serial.println(WiFi.status());

    return false;
}

// ======================================================
// WAKTU UTC
// ======================================================

String dapatkanWaktuUTC()
{
    time_t now = time(nullptr);

    if (now < 1700000000)
        return "";

    struct tm timeinfo;

    gmtime_r(&now, &timeinfo);

    char buffer[30];

    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%dT%H:%M:%SZ",
        &timeinfo);

    return String(buffer);
}

// ======================================================
// BANTUAN FIRESTORE
// ======================================================

String ambilString(JsonObject fields, const char *namaField)
{
    return fields[namaField]["stringValue"] | "";
}

int ambilInteger(
    JsonObject fields,
    const char *namaField,
    int nilaiDefault = -1)
{
    JsonVariant nilai = fields[namaField];

    if (!nilai["integerValue"].isNull())
    {
        const char *teks = nilai["integerValue"];

        if (teks != nullptr)
            return atoi(teks);
    }

    if (nilai.is<int>())
        return nilai.as<int>();

    if (nilai.is<long>())
        return (int)nilai.as<long>();

    return nilaiDefault;
}

bool ambilBoolean(
    JsonObject fields,
    const char *namaField,
    bool nilaiDefault = false)
{
    return fields[namaField]["booleanValue"] | nilaiDefault;
}

bool bacaDokumen(String documentPath, String &hasil)
{
    if (!firebaseReady || WiFi.status() != WL_CONNECTED)
        return false;

    hasil = Docs.get(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        GetDocumentOptions());

    // Log Firestore ringkas; JSON lengkap tidak dicetak agar Serial Monitor rapi.
    if (aClient.lastError().code() != 0)
    {
        Serial.printf("[FIRESTORE][ERROR] GET %s | code=%d | %s\n",
                      documentPath.c_str(),
                      aClient.lastError().code(),
                      aClient.lastError().message().c_str());
    }
    // GET sukses sengaja tidak dicetak agar Serial Monitor fokus pada event.

    if (aClient.lastError().code() != 0)
        return false;

    return hasil.length() > 0;
}

// ======================================================
// MUAT CACHE MAPPING
// ======================================================

bool muatCacheMapping(const String &batchId)
{
    if (batchId.length() == 0)
    {
        kosongkanCache();

        Serial.println(
            "[CACHE] Tidak ada pemilik aktif. Cache dikosongkan.");

        return true;
    }

    EntriMapping baru[5];

    int jumlah = 0;

    for (int i = 0; i < 5; i++)
    {
        String documentPath =
            "fingerprint-mapping/" +
            String(ID_LOCKER) + "_" +
            batchId + "_" +
            labelFingerprint[i];

        String hasil;

        if (!bacaDokumen(documentPath, hasil))
            continue;

        JsonDocument doc;

        if (deserializeJson(doc, hasil))
            continue;

        JsonObject fields = doc["fields"];

        if (fields.isNull())
            continue;

        baru[i].ada = true;

        baru[i].idSensor =
            ambilInteger(fields, "idSensor", -1);

        baru[i].idFingerprint =
            ambilString(fields, "idFingerprint");

        baru[i].idSantri =
            ambilString(fields, "idSantri");

        baru[i].namaSantri =
            ambilString(fields, "namaSantri");

        baru[i].idLocker =
            ambilString(fields, "idLocker");

        baru[i].statusAktif =
            ambilBoolean(fields, "statusAktif", false);

        // Batch hanya valid jika setiap mapping aktif dan sesuai locker/batch.
        if (!baru[i].statusAktif ||
            baru[i].idLocker != String(ID_LOCKER) ||
            baru[i].idFingerprint != labelFingerprint[i] ||
            baru[i].idSantri.length() == 0 ||
            baru[i].idSensor <= 0)
        {
            Serial.println("[CACHE] Mapping tidak aktif/tidak valid. Cache dibatalkan.");
            kosongkanCache();
            return false;
        }

        jumlah++;
    }

    if (jumlah != 5)
    {
        Serial.print("[CACHE] Gagal memuat mapping (");
        Serial.print(jumlah);
        Serial.println("/5). Akan dicoba lagi.");

        return false;
    }

    for (int i = 0; i < 5; i++)
    {
        cacheMapping[i] = baru[i];
    }

    cacheBatchId = batchId;
    cacheSiap = true;
    waktuCobaCache = 0;

    Serial.print("[CACHE] Mapping batch ");
    Serial.print(batchId);
    Serial.println(" dimuat ke RAM.");

    return true;
}

// ======================================================
// HOOK SAAT MENUNGGU FINGERPRINT
// ======================================================

void layaniFirebase()
{
    periksaSensorPintu();
    perbaruiSiklusLocker();

    if (firebaseStarted &&
        WiFi.status() == WL_CONNECTED)
    {
        app.loop();
        Docs.loop();

        firebaseReady = app.ready();
    }
}

// ======================================================
// UPDATE STATUS ENROLLMENT
// ======================================================

bool updateStatusEnrollmentCommand(
    String status,
    String pesan = "")
{
    if (!firebaseReady)
        return false;

    String documentPath =
        "enrollment-commands/" + String(ID_PERANGKAT);

    for (int percobaan = 0; percobaan < 3; percobaan++)
    {
        Document<Values::Value> dokumen;

        dokumen.add(
            "status",
            Values::Value(Values::StringValue(status)));

        dokumen.add(
            "errorMessage",
            Values::Value(Values::StringValue(pesan)));

        PatchDocumentOptions patchOptions(
            DocumentMask("status,errorMessage"),
            DocumentMask(),
            Precondition());

        Docs.patch(
            aClient,
            Firestore::Parent(FIREBASE_PROJECT_ID),
            documentPath,
            patchOptions,
            dokumen);

        if (aClient.lastError().code() == 0)
            return true;

        layaniFirebase();
        delay(300);
    }

    return false;
}

// ======================================================
// SIMPAN MAPPING BATCH
// ======================================================

bool simpanMappingBatch(
    String batchId,
    int idSensor,
    String idFingerprint,
    String idSantri,
    String namaSantri,
    String namaJariSekarang,
    int urutanJari)
{
    String documentId =
        String(ID_LOCKER) + "_" +
        batchId + "_" +
        idFingerprint;

    String documentPath =
        "fingerprint-mapping/" + documentId;

    Document<Values::Value> dokumen;

    dokumen.add(
        "idFingerprint",
        Values::Value(Values::StringValue(idFingerprint)));

    dokumen.add(
        "idSensor",
        Values::Value(Values::IntegerValue(idSensor)));

    dokumen.add(
        "idSantri",
        Values::Value(Values::StringValue(idSantri)));

    dokumen.add(
        "namaSantri",
        Values::Value(Values::StringValue(namaSantri)));

    dokumen.add(
        "idLocker",
        Values::Value(Values::StringValue(ID_LOCKER)));

    dokumen.add(
        "idPerangkat",
        Values::Value(Values::StringValue(ID_PERANGKAT)));

    dokumen.add(
        "jari",
        Values::Value(Values::StringValue(namaJariSekarang)));

    dokumen.add(
        "urutanJari",
        Values::Value(Values::IntegerValue(urutanJari)));

    dokumen.add(
        "batchId",
        Values::Value(Values::StringValue(batchId)));

    dokumen.add(
        "statusAktif",
        Values::Value(Values::BooleanValue(true)));

    String waktu = dapatkanWaktuUTC();

    if (waktu.length() > 0)
    {
        dokumen.add(
            "createdAt",
            Values::Value(Values::TimestampValue(waktu)));
    }

    Docs.createDocument(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        DocumentMask(),
        dokumen);

    return aClient.lastError().code() == 0;
}

// ======================================================
// HAPUS DOKUMEN FIRESTORE (BENAR-BENAR DIHAPUS)
// ======================================================
//
// Method FirebaseClient untuk menghapus dokumen adalah Docs.deleteDoc()
// (bukan deleteDocument). Menghapus dokumen yang sudah tidak ada tetap
// dianggap sukses oleh Firestore, jadi aman dipanggil ulang saat retry.
// Pastikan Firestore Rules mengizinkan "delete" pada fingerprint-mapping
// untuk akun USER_EMAIL milik ESP32.

bool hapusDokumenFirestore(const String &documentPath)
{
    for (int percobaan = 0; percobaan < 3; percobaan++)
    {
        Docs.deleteDoc(
            aClient,
            Firestore::Parent(FIREBASE_PROJECT_ID),
            documentPath,
            Precondition());

        int kode = aClient.lastError().code();

        if (kode == 0)
            return true;

        Serial.printf("[FIRESTORE][ERROR] DELETE %s | code=%d | %s\n",
                      documentPath.c_str(),
                      kode,
                      aClient.lastError().message().c_str());

        layaniFirebase();
        delay(300);
    }

    return false;
}

// ======================================================
// HAPUS MAPPING SATU BATCH
// ======================================================
// Menghapus kelima dokumen fingerprint-mapping milik satu batch.

bool hapusMappingBatch(const String &batchId)
{
    if (batchId.length() == 0)
        return true;

    bool semuaBerhasil = true;

    for (int i = 0; i < 5; i++)
    {
        String mappingPath =
            "fingerprint-mapping/" + String(ID_LOCKER) + "_" +
            batchId + "_" + labelFingerprint[i];

        if (hapusDokumenFirestore(mappingPath))
        {
            Serial.print("[HAPUS] Mapping dihapus: ");
            Serial.println(mappingPath);
        }
        else
        {
            semuaBerhasil = false;

            Serial.print("[HAPUS][ERROR] Gagal menghapus: ");
            Serial.println(mappingPath);
        }
    }

    return semuaBerhasil;
}

// ======================================================
// KOSONGKAN CONFIG PEMILIK
// ======================================================

bool kosongkanConfigPemilik(const String &configPath)
{
    Document<Values::Value> dokumen;

    dokumen.add(
        "activeBatchId",
        Values::Value(Values::StringValue("")));

    dokumen.add(
        "activeSantriId",
        Values::Value(Values::StringValue("")));

    dokumen.add(
        "resetCommand",
        Values::Value(Values::StringValue("IDLE")));

    String waktu = dapatkanWaktuUTC();

    if (waktu.length() > 0)
    {
        dokumen.add(
            "updatedAt",
            Values::Value(Values::TimestampValue(waktu)));
    }

    PatchDocumentOptions patchOptions(
        DocumentMask(
            "activeBatchId,activeSantriId,resetCommand,updatedAt"),
        DocumentMask(),
        Precondition());

    for (int percobaan = 0; percobaan < 3; percobaan++)
    {
        Docs.patch(
            aClient,
            Firestore::Parent(FIREBASE_PROJECT_ID),
            configPath,
            patchOptions,
            dokumen);

        if (aClient.lastError().code() == 0)
            return true;

        layaniFirebase();
        delay(300);
    }

    return false;
}

// ======================================================
// HAPUS PEMILIK LAMA (RESET / GANTI PEMILIK)
// ======================================================
//
// Urutan sengaja dibuat "akses dicabut dulu":
//   1. Cache RAM dikosongkan  -> jari lama langsung ditolak.
//   2. Seluruh template di sensor dihapus.
//   3. Dokumen fingerprint-mapping milik batch lama DIHAPUS dari Firestore.
//   4. locker-config dikosongkan (activeBatchId/activeSantriId).
//
// Jika langkah 2-4 gagal, fungsi mengembalikan false dan akses lama tetap
// sudah tercabut. Pemanggil boleh mencoba lagi (semua langkah idempoten).
//
// batchDiketahui : activeBatchId yang sudah dibaca pemanggil (boleh kosong,
//                  maka dibaca ulang dari locker-config).
// adaPemilikLama : (opsional) diisi true jika ada batch lama yang dihapus.

bool hapusPemilikLama(
    const String &configPath,
    const String &batchDiketahui,
    bool *adaPemilikLama = nullptr)
{
    if (adaPemilikLama != nullptr)
        *adaPemilikLama = false;

    // Kumpulkan semua batch yang harus dihapus (maks. 2 kandidat berbeda).
    String batchHapus[2];
    int jumlahBatch = 0;

    auto tambahBatch = [&](const String &b)
    {
        if (b.length() == 0)
            return;

        for (int i = 0; i < jumlahBatch; i++)
        {
            if (batchHapus[i] == b)
                return;
        }

        if (jumlahBatch < 2)
            batchHapus[jumlahBatch++] = b;
    };

    tambahBatch(batchDiketahui);
    tambahBatch(cacheBatchId); // harus diambil SEBELUM cache dikosongkan

    // Jika pemanggil belum tahu batch aktif, baca dari locker-config.
    if (batchDiketahui.length() == 0)
    {
        for (int percobaan = 0; percobaan < 2; percobaan++)
        {
            String hasilConfig;

            if (bacaDokumen(configPath, hasilConfig))
            {
                JsonDocument docConfig;

                if (!deserializeJson(docConfig, hasilConfig))
                {
                    JsonObject fieldsConfig = docConfig["fields"];

                    if (!fieldsConfig.isNull())
                        tambahBatch(ambilString(fieldsConfig, "activeBatchId"));
                }

                break;
            }

            layaniFirebase();
            delay(300);
        }
    }

    if (adaPemilikLama != nullptr)
        *adaPemilikLama = (jumlahBatch > 0);

    // 1. Cabut akses: jari lama langsung ditolak.
    kosongkanCache();

    // 2. Hapus seluruh template di sensor.
    if (!hapusSemuaFingerprint())
    {
        Serial.println(
            "[HAPUS PEMILIK] Gagal mengosongkan sensor.");

        return false;
    }

    // 3. Hapus mapping lama dari Firestore.
    for (int i = 0; i < jumlahBatch; i++)
    {
        Serial.print("[HAPUS PEMILIK] Menghapus mapping batch ");
        Serial.println(batchHapus[i]);

        if (!hapusMappingBatch(batchHapus[i]))
        {
            Serial.println(
                "[HAPUS PEMILIK] Sebagian mapping gagal dihapus.");

            return false;
        }
    }

    // 4. Kosongkan locker-config.
    if (!kosongkanConfigPemilik(configPath))
    {
        Serial.println(
            "[HAPUS PEMILIK] Gagal mengosongkan locker-config.");

        return false;
    }

    Serial.println(
        "[HAPUS PEMILIK] Data pemilik lama bersih (sensor + Firestore).");

    return true;
}

// ======================================================
// AKTIFKAN BATCH
// ======================================================

bool aktifkanBatch(String batchId, String idSantri)
{
    String documentPath =
        "locker-config/" + String(ID_LOCKER);

    Document<Values::Value> dokumen;

    dokumen.add(
        "activeBatchId",
        Values::Value(Values::StringValue(batchId)));

    dokumen.add(
        "activeSantriId",
        Values::Value(Values::StringValue(idSantri)));

    dokumen.add(
        "idLocker",
        Values::Value(Values::StringValue(ID_LOCKER)));

    String waktu = dapatkanWaktuUTC();

    if (waktu.length() > 0)
    {
        dokumen.add(
            "updatedAt",
            Values::Value(Values::TimestampValue(waktu)));
    }

    PatchDocumentOptions patchOptions(
        DocumentMask("activeBatchId,activeSantriId,idLocker,updatedAt"),
        DocumentMask(),
        Precondition());

    Docs.patch(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        patchOptions,
        dokumen);

    return aClient.lastError().code() == 0;
}

// ======================================================
// RIWAYAT AKSES
// ======================================================

bool simpanRiwayatAkses(
    int idSensor,
    String idFingerprint,
    String idSantri,
    String namaSantri,
    String hasilAkses,
    String keterangan)
{
    if (!firebaseReady ||
        WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[RIWAYAT] Tidak disimpan: Firebase belum siap.");
        return false;
    }

    String documentId =
        String(ID_LOCKER) + "_" +
        String(millis()) + "_" +
        String((uint32_t)esp_random(), HEX);

    String documentPath =
        "locker-activity/" + documentId;

    Document<Values::Value> dokumen;

    dokumen.add(
        "idLocker",
        Values::Value(Values::StringValue(ID_LOCKER)));

    dokumen.add(
        "idPerangkat",
        Values::Value(Values::StringValue(ID_PERANGKAT)));

    dokumen.add(
        "idSensor",
        Values::Value(Values::IntegerValue(idSensor)));

    dokumen.add(
        "idFingerprint",
        Values::Value(Values::StringValue(idFingerprint)));

    dokumen.add(
        "idSantri",
        Values::Value(Values::StringValue(idSantri)));

    dokumen.add(
        "namaSantri",
        Values::Value(Values::StringValue(namaSantri)));

    dokumen.add(
        "hasilAkses",
        Values::Value(Values::StringValue(hasilAkses)));

    dokumen.add(
        "keterangan",
        Values::Value(Values::StringValue(keterangan)));

    String waktu = dapatkanWaktuUTC();

    if (waktu.length() > 0)
    {
        dokumen.add(
            "waktu",
            Values::Value(Values::TimestampValue(waktu)));
    }

    Docs.createDocument(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        DocumentMask(),
        dokumen);

    return aClient.lastError().code() == 0;
}

// ======================================================
// SENSOR PINTU
// ======================================================

static bool pintuInisialisasi = false;
static bool pintuTertutup = true;
static bool pembacaanTerakhir = true;

static unsigned long waktuPerubahanPembacaan = 0;

static bool statusPintuPerluDikirim = false;

static unsigned long waktuKirimStatusPintu = 0;

bool kirimStatusPintu(bool tertutup)
{
    if (!firebaseReady)
        return false;

    String documentPath =
        "locker-config/" + String(ID_LOCKER);

    Document<Values::Value> dokumen;

    dokumen.add(
        "statusPintu",
        Values::Value(
            Values::StringValue(
                tertutup ? "TERTUTUP" : "TERBUKA")));

    PatchDocumentOptions patchOptions(
        DocumentMask("statusPintu"),
        DocumentMask(),
        Precondition());

    Docs.patch(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        patchOptions,
        dokumen);

    return aClient.lastError().code() == 0;
}

// ======================================================
// PULIHKAN STANDBY
// ======================================================

void pulihkanStandby(const String &alasan)
{
    relayOff();

    aksesPintuTerotorisasi = false;
    statusSiklusPintu = LOCKER_TERKUNCI;

    Serial.print("[SOLENOID] ");
    Serial.println(alasan);

    Serial.println(
        "[SOLENOID] Locker terkunci. Kembali ke standby.");
}

// ======================================================
// CEK KESIAPAN LOCKER
// ======================================================

bool lockerSiapDibuka()
{
    if (statusSiklusPintu != LOCKER_TERKUNCI)
        return false;

    if (AKTIFKAN_SENSOR_PINTU)
        return pintuInisialisasi && pintuTertutup;

    return true;
}

// ======================================================
// BUKA LOCKER
// ======================================================

bool mulaiSiklusBukaLocker(const String &sumber)
{
    if (!lockerSiapDibuka())
    {
        Serial.println(
            "[BUKA] Locker belum siap.");

        return false;
    }

    // Tandai izin sebelum relay aktif agar pembacaan sensor berikutnya
    // tidak salah dianggap sebagai pembukaan paksa.
    aksesPintuTerotorisasi = true;
    statusSiklusPintu = MENUNGGU_PINTU_DIBUKA;

    relayOn();
    waktuMulaiSiklus = millis();

    Serial.print("[BUKA] Solenoid dibuka melalui: ");
    Serial.println(sumber);

    if (AKTIFKAN_SENSOR_PINTU)
    {
        Serial.println(
            "[BUKA] Menunggu pintu dibuka dan ditutup.");
    }
    else
    {
        Serial.print("[BUKA] Mode waktu tetap: ");
        Serial.print(DURASI_BUKA_MS / 1000);
        Serial.println(" detik.");
    }

    return true;
}

// ======================================================
// PERBARUI SIKLUS LOCKER
// ======================================================

void perbaruiSiklusLocker()
{
    if (statusSiklusPintu == LOCKER_TERKUNCI)
        return;

    if (!AKTIFKAN_SENSOR_PINTU)
    {
        if (millis() - waktuMulaiSiklus >= DURASI_BUKA_MS)
        {
            pulihkanStandby(
                "Waktu buka habis. Dikunci kembali.");
        }

        return;
    }

    if (BATAS_MAKS_SOLENOID_MS > 0 &&
        millis() - waktuMulaiSiklus >= BATAS_MAKS_SOLENOID_MS)
    {
        Serial.println(
            "[PERINGATAN] Batas pengaman solenoid tercapai.");

        if (firebaseReady)
        {
            simpanRiwayatAkses(
                0,
                "PINTU",
                "-",
                "-",
                "PERINGATAN",
                "Siklus pintu tidak selesai, solenoid dikunci paksa.");
        }

        pulihkanStandby(
            "Batas pengaman tercapai.");
    }
}

// ======================================================
// PERIKSA SENSOR PINTU
// ======================================================

void periksaSensorPintu()
{
    if (!AKTIFKAN_SENSOR_PINTU)
        return;

    int nilaiMentah = digitalRead(SENSOR_PINTU_PIN);
    bool tertutupMentah = (nilaiMentah == PINTU_TERTUTUP_LEVEL);

    // Inisialisasi pembacaan pertama agar tidak langsung memicu alarm.
    if (!pintuInisialisasi)
    {
        pintuInisialisasi = true;
        pintuTertutup = tertutupMentah;
        pembacaanTerakhir = tertutupMentah;
        waktuPerubahanPembacaan = millis();
        statusPintuPerluDikirim = KIRIM_STATUS_PINTU;

        Serial.print("[PINTU] Status awal: ");
        Serial.println(pintuTertutup ? "TERTUTUP" : "TERBUKA");
        return;
    }

    // Debounce perubahan sensor.
    if (tertutupMentah != pembacaanTerakhir)
    {
        pembacaanTerakhir = tertutupMentah;
        waktuPerubahanPembacaan = millis();
    }

    if (pembacaanTerakhir != pintuTertutup &&
        millis() - waktuPerubahanPembacaan >= DEBOUNCE_PINTU_MS)
    {
        pintuTertutup = pembacaanTerakhir;
        statusPintuPerluDikirim = KIRIM_STATUS_PINTU;

        if (!pintuTertutup)
        {
            Serial.println("[DOOR] OPEN.");

            // Pembukaan yang terjadi setelah akses sah bukan alarm.
            if (statusSiklusPintu == MENUNGGU_PINTU_DIBUKA &&
                aksesPintuTerotorisasi)
            {
                statusSiklusPintu = MENUNGGU_PINTU_DITUTUP;
                waktuMulaiMenungguTutup = millis();
                Serial.println("[PINTU] Pembukaan terotorisasi. Menunggu pintu ditutup.");
            }
            else if (statusSiklusPintu == LOCKER_TERKUNCI)
            {
                // Sensor belum terpasang/terkalibrasi: jangan membuat alarm palsu.
                if (!SENSOR_PINTU_TERPASANG)
                {
                    Serial.println("[INFO] Sensor pintu belum dipasang/diaktifkan untuk alarm. Event diabaikan.");
                }
                else
                {
                    Serial.println("[BAHAYA] Pintu terbuka saat locker terkunci tanpa akses sah!");

                    if (firebaseReady)
                    {
                        simpanRiwayatAkses(
                            0,
                            "PINTU",
                            "-",
                            "-",
                            "BAHAYA",
                            "Pintu terbuka saat locker terkunci tanpa autentikasi.");
                    }
                }
            }
        }
        else
        {
            Serial.println("[DOOR] CLOSED.");

            if (statusSiklusPintu == MENUNGGU_PINTU_DITUTUP)
            {
                pulihkanStandby("Pintu ditutup | Solenoid terkunci.");
            }
        }
    }

    // Kirim status pintu ke Firestore secara berkala setelah perubahan.
    if (statusPintuPerluDikirim &&
        firebaseReady &&
        millis() - waktuKirimStatusPintu >= 5000)
    {
        waktuKirimStatusPintu = millis();

        if (kirimStatusPintu(pintuTertutup))
            statusPintuPerluDikirim = false;
    }
}

// ROLLBACK SENSOR
// ======================================================

void rollbackSensor(int ids[], int jumlah)
{
    for (int i = 0; i < jumlah; i++)
    {
        if (ids[i] > 0)
        {
            Serial.print("[ROLLBACK] Menghapus ID ");
            Serial.println(ids[i]);

            hapusFingerprint(ids[i]);

            ids[i] = -1;
        }
    }
}

// ======================================================
// MASUK MODE STANDBY AUTENTIKASI
// ======================================================
//
// Dipanggil sebagai langkah terakhir setelah enrollment berhasil.
// Memastikan jari terakhir sudah diangkat, menutup mode enrollment,
// lalu mengatur ulang timer agar iterasi loop berikutnya langsung
// menjalankan autentikasi (tanpa GET Firestore yang memblokir dulu).

void masukModeStandbyAutentikasi()
{
    // Jari kelingking yang baru didaftarkan jangan sampai langsung
    // terbaca sebagai percobaan akses pertama.
    for (int i = 0; i < 2; i++)
    {
        if (tungguJariDiangkat())
            break;
    }

    prosesEnrollmentAktif = false;

    waktuCekEnrollment = millis();
    waktuCekLocker = millis();
    waktuCobaCache = 0;

    Serial.println();
    Serial.println("[STANDBY] Enrollment selesai.");
    Serial.println("[STANDBY] Mode autentikasi fingerprint AKTIF. Tempelkan jari untuk membuka locker.");

    if (!lockerSiapDibuka())
    {
        Serial.println(
            "[STANDBY] Catatan: locker belum siap dibuka "
            "(siklus pintu berjalan / sensor pintu belum terbaca TERTUTUP).");
    }
}

// ======================================================
// ENROLLMENT
// ======================================================

void cekPerintahEnrollment()
{
    if (prosesEnrollmentAktif ||
        !fingerprintReady ||
        !firebaseReady)
        return;

    if (millis() - waktuCekEnrollment <
        INTERVAL_CEK_ENROLLMENT)
        return;

    waktuCekEnrollment = millis();

    String hasil;

    String commandPath =
        "enrollment-commands/" + String(ID_PERANGKAT);

    if (!bacaDokumen(commandPath, hasil))
        return;

    JsonDocument doc;

    if (deserializeJson(doc, hasil))
        return;

    JsonObject fields = doc["fields"];

    if (fields.isNull())
        return;

    String command = ambilString(fields, "command");
    String status = ambilString(fields, "status");

    String idSantri = ambilString(fields, "idSantri");
    String namaSantri = ambilString(fields, "namaSantri");

    String idLocker = ambilString(fields, "idLocker");
    String idPerangkat = ambilString(fields, "idPerangkat");

    if (command != "START_ENROLL" ||
        status != "pending")
        return;

    if (idPerangkat != String(ID_PERANGKAT) ||
        idLocker != String(ID_LOCKER) ||
        idSantri.length() == 0)
    {
        updateStatusEnrollmentCommand(
            "failed",
            "Data perangkat, locker, atau santri tidak valid.");

        return;
    }

    prosesEnrollmentAktif = true;

    Serial.println(
        "\n=== ENROLLMENT DIMULAI (DARI FLUTTER) ===");

    Serial.print("Santri: ");
    Serial.println(namaSantri);

    if (!updateStatusEnrollmentCommand("processing"))
    {
        Serial.println(
            "[ENROLLMENT] Gagal mengubah status processing.");

        prosesEnrollmentAktif = false;

        return;
    }

    // ----------------------------------------------
    // HAPUS PEMILIK LAMA (GANTI PEMILIK)
    // ----------------------------------------------
    // Sensor dikosongkan dan mapping lama DIHAPUS dari Firestore,
    // sehingga enrollment baru tidak menumpuk di atas data lama.

    String configPath =
        "locker-config/" + String(ID_LOCKER);

    bool adaPemilikLama = false;

    Serial.println(
        "[ENROLLMENT] Membersihkan data pemilik lama...");

    if (!hapusPemilikLama(configPath, "", &adaPemilikLama))
    {
        updateStatusEnrollmentCommand(
            "failed",
            "Gagal menghapus data pemilik lama (sensor/Firestore).");

        prosesEnrollmentAktif = false;

        Serial.println("=== ENROLLMENT DIBATALKAN ===");

        return;
    }

    if (adaPemilikLama)
    {
        simpanRiwayatAkses(
            0,
            "GANTI_PEMILIK",
            "-",
            "Admin / Pembina",
            "DISETUJUI",
            "Pemilik lama dihapus karena enrollment pemilik baru.");
    }

    int idsSensor[5] = {
        -1, -1, -1, -1, -1};

    bool semuaBerhasil = true;

    String batchId =
        "B" + String((uint32_t)esp_random(), HEX);

    // ----------------------------------------------
    // DAFTAR LIMA JARI
    // ----------------------------------------------

    for (int i = 0; i < 5; i++)
    {
        Serial.println();

        Serial.print("Daftarkan jari ");
        Serial.print(i + 1);
        Serial.print("/5: ");
        Serial.println(namaJari[i]);

        int idSensor = cariIdKosong();

        if (idSensor <= 0)
        {
            Serial.println(
                "[ENROLLMENT] Tidak mendapatkan ID kosong.");

            semuaBerhasil = false;

            break;
        }

        bool templateTersimpan = false;

        bool berhasil = daftarSatuFingerprint(
            idSensor,
            namaJari[i],
            30000,
            &templateTersimpan);

        if (!berhasil)
        {
            Serial.print("[ENROLLMENT] Gagal: ");
            Serial.println(namaJari[i]);

            if (templateTersimpan)
            {
                Serial.println(
                    "[ENROLLMENT] Template sempat tersimpan. Rollback ID.");

                hapusFingerprint(idSensor);
            }
            else
            {
                Serial.println(
                    "[ENROLLMENT] Template belum tersimpan. ID tidak dihapus.");
            }

            semuaBerhasil = false;

            break;
        }

        idsSensor[i] = idSensor;

        Serial.print("[ENROLLMENT] Berhasil: ");
        Serial.print(namaJari[i]);
        Serial.print(" / ID ");
        Serial.println(idSensor);

        delay(500);
    }

    // ----------------------------------------------
    // ROLLBACK JIKA PENDAFTARAN GAGAL
    // ----------------------------------------------

    if (!semuaBerhasil)
    {
        Serial.println(
            "[ENROLLMENT] Pendaftaran tidak lengkap.");

        rollbackSensor(idsSensor, 5);

        updateStatusEnrollmentCommand(
            "failed",
            "Enrollment lima jari gagal atau tidak lengkap.");

        prosesEnrollmentAktif = false;

        Serial.println("=== ENROLLMENT GAGAL ===");

        return;
    }

    // ----------------------------------------------
    // SIMPAN MAPPING
    // ----------------------------------------------

    Serial.println(
        "[ENROLLMENT] Menyimpan mapping ke Firestore...");

    for (int i = 0; i < 5; i++)
    {
        bool mappingBerhasil = simpanMappingBatch(
            batchId,
            idsSensor[i],
            labelFingerprint[i],
            idSantri,
            namaSantri,
            namaJari[i],
            i + 1);

        if (!mappingBerhasil)
        {
            Serial.print(
                "[ENROLLMENT] Mapping gagal: ");

            Serial.println(labelFingerprint[i]);

            semuaBerhasil = false;

            break;
        }

        Serial.print("[ENROLLMENT] Mapping berhasil: ");
        Serial.println(labelFingerprint[i]);
    }

    if (!semuaBerhasil)
    {
        hapusMappingBatch(batchId);

        rollbackSensor(idsSensor, 5);

        updateStatusEnrollmentCommand(
            "failed",
            "Penyimpanan mapping Firestore gagal.");

        prosesEnrollmentAktif = false;

        Serial.println("=== ENROLLMENT GAGAL ===");

        return;
    }

    // ----------------------------------------------
    // AKTIFKAN BATCH
    // ----------------------------------------------

    if (!aktifkanBatch(batchId, idSantri))
    {
        Serial.println(
            "[ENROLLMENT] Gagal mengaktifkan batch.");

        hapusMappingBatch(batchId);

        rollbackSensor(idsSensor, 5);

        updateStatusEnrollmentCommand(
            "failed",
            "Gagal mengaktifkan batch di locker-config.");

        prosesEnrollmentAktif = false;

        return;
    }

    // ----------------------------------------------
    // PERBARUI CACHE
    // ----------------------------------------------

    for (int i = 0; i < 5; i++)
    {
        cacheMapping[i].ada = true;
        cacheMapping[i].idSensor = idsSensor[i];

        cacheMapping[i].idFingerprint =
            labelFingerprint[i];

        cacheMapping[i].idSantri = idSantri;
        cacheMapping[i].namaSantri = namaSantri;
        cacheMapping[i].idLocker = ID_LOCKER;
        cacheMapping[i].statusAktif = true;
    }

    cacheBatchId = batchId;
    cacheSiap = true;
    waktuCobaCache = 0;

    if (!updateStatusEnrollmentCommand("completed"))
    {
        Serial.println(
            "[PERINGATAN] Batch aktif, tetapi status command gagal diperbarui.");
    }

    Serial.println();
    Serial.println("======================================");
    Serial.println(" ENROLLMENT BERHASIL");
    Serial.println("======================================");

    Serial.print("Santri: ");
    Serial.println(namaSantri);

    Serial.print("Batch: ");
    Serial.println(batchId);

    for (int i = 0; i < 5; i++)
    {
        Serial.print(namaJari[i]);
        Serial.print(" -> ID ");
        Serial.println(idsSensor[i]);
    }

    // Langsung masuk mode autentikasi standby.
    masukModeStandbyAutentikasi();
}

// ======================================================
// REMOTE UNLOCK
// ======================================================

void prosesRemoteUnlock(const String &configPath)
{
    Serial.println(
        "\n[REMOTE UNLOCK] Perintah diterima.");

    if (!lockerSiapDibuka())
    {
        Serial.println(
            "[REMOTE UNLOCK] Locker belum siap. Perintah ditunda.");

        return;
    }

    Document<Values::Value> dokumen;

    dokumen.add(
        "remoteCommand",
        Values::Value(Values::StringValue("IDLE")));

    PatchDocumentOptions patchOptions(
        DocumentMask("remoteCommand"),
        DocumentMask(),
        Precondition());

    Docs.patch(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        configPath,
        patchOptions,
        dokumen);

    if (aClient.lastError().code() != 0)
    {
        Serial.println(
            "[REMOTE UNLOCK] Gagal mereset perintah.");

        return;
    }

    if (mulaiSiklusBukaLocker("REMOTE UNLOCK"))
    {
        simpanRiwayatAkses(
            0,
            "REMOTE_UNLOCK",
            "-",
            "Admin / Pembina",
            "DIIZINKAN",
            "Buka locker via Remote Unlock Flutter.");
    }
}

// ======================================================
// RESET LOCKER
// ======================================================

void prosesResetLocker(
    const String &configPath,
    const String &activeBatchId)
{
    Serial.println(
        "\n[RESET LOCKER] Perintah reset diterima.");

    // Sensor dikosongkan, mapping lama DIHAPUS dari Firestore, dan
    // locker-config dikosongkan (resetCommand kembali IDLE).
    // Jika ada langkah yang gagal, resetCommand tetap RESET_OWNER
    // sehingga perintah otomatis dicoba lagi pada polling berikutnya.
    if (!hapusPemilikLama(configPath, activeBatchId))
    {
        Serial.println(
            "[RESET LOCKER] Reset belum tuntas. Akan dicoba lagi.");

        return;
    }

    simpanRiwayatAkses(
        0,
        "RESET_OWNER",
        "-",
        "Admin / Pembina",
        "DISETUJUI",
        "Loker di-reset untuk penggantian pemilik baru.");

    Serial.println(
        "[RESET LOCKER] Loker berhasil di-reset. Sensor dan Firestore bersih.");
}

// ======================================================
// CEK PERINTAH LOCKER
// ======================================================

void cekPerintahLocker()
{
    if (prosesEnrollmentAktif)
        return;

    if (millis() - waktuCekLocker <
        INTERVAL_CEK_LOCKER)
        return;

    waktuCekLocker = millis();

    String hasil;

    String configPath =
        "locker-config/" + String(ID_LOCKER);

    if (!bacaDokumen(configPath, hasil))
        return;

    JsonDocument doc;

    if (deserializeJson(doc, hasil))
        return;

    JsonObject fields = doc["fields"];

    if (fields.isNull())
        return;

    String resetCommand =
        ambilString(fields, "resetCommand");

    String remoteCommand =
        ambilString(fields, "remoteCommand");

    String activeBatchId =
        ambilString(fields, "activeBatchId");

    if (resetCommand == "RESET_OWNER")
    {
        if (!fingerprintReady)
        {
            Serial.println(
                "[RESET LOCKER] Sensor belum siap. Reset ditunda.");

            return;
        }

        prosesResetLocker(
            configPath,
            activeBatchId);

        return;
    }

    if ((!cacheSiap ||
         activeBatchId != cacheBatchId) &&
        (waktuCobaCache == 0 ||
         millis() - waktuCobaCache >= JEDA_ULANG_CACHE_MS))
    {
        waktuCobaCache = millis();

        muatCacheMapping(activeBatchId);
    }

    if (remoteCommand == "UNLOCK")
    {
        prosesRemoteUnlock(configPath);
    }
}

// ======================================================
// CARI MAPPING DI CACHE
// ======================================================

bool cariMappingFingerprint(
    int idSensor,
    String &idFingerprint,
    String &idSantri,
    String &namaSantri,
    String &idLocker,
    bool &statusAktif)
{
    if (idSensor <= 0 ||
        !cacheSiap ||
        cacheBatchId.length() == 0)
        return false;

    for (int i = 0; i < 5; i++)
    {
        if (!cacheMapping[i].ada ||
            cacheMapping[i].idSensor != idSensor)
            continue;

        idFingerprint =
            cacheMapping[i].idFingerprint;

        idSantri =
            cacheMapping[i].idSantri;

        namaSantri =
            cacheMapping[i].namaSantri;

        idLocker =
            cacheMapping[i].idLocker;

        statusAktif =
            cacheMapping[i].statusAktif;

        return true;
    }

    return false;
}

// ======================================================
// AUTENTIKASI OTOMATIS
// ======================================================

void prosesAutentikasiOtomatis()
{
    if (prosesEnrollmentAktif ||
        !fingerprintReady ||
        !firebaseReady)
        return;

    if (!lockerSiapDibuka())
    {
        // Beri tahu mengapa autentikasi belum aktif (maks. tiap 10 detik).
        static unsigned long waktuLogTunggu = 0;

        if (millis() - waktuLogTunggu >= 10000)
        {
            waktuLogTunggu = millis();

            if (statusSiklusPintu != LOCKER_TERKUNCI)
                Serial.println("[STANDBY] Menunggu siklus pintu selesai.");
            else
                Serial.println("[STANDBY] Menunggu sensor pintu membaca TERTUTUP.");
        }

        return;
    }

    int idSensor = cariFingerprint();

    if (idSensor <= 0 && idSensor != -2)
        return;

    if (idSensor == -2)
    {
        Serial.println(
            "\n[AKSES DITOLAK] Sidik jari tidak terdaftar.");

        simpanRiwayatAkses(
            0,
            "TIDAK_DIKENAL",
            "",
            "",
            "DITOLAK",
            "Sidik jari tidak terdaftar pada sensor.");

        delay(1000);

        return;
    }

    String idFingerprint;
    String idSantri;
    String namaSantri;
    String idLocker;

    bool statusAktif = false;

    bool ditemukan = cariMappingFingerprint(
        idSensor,
        idFingerprint,
        idSantri,
        namaSantri,
        idLocker,
        statusAktif);

    if (!ditemukan ||
        !statusAktif ||
        idLocker != String(ID_LOCKER) ||
        idSantri.length() == 0)
    {
        Serial.println(
            "\n[AKSES DITOLAK] Mapping tidak valid atau tidak aktif.");

        simpanRiwayatAkses(
            idSensor,
            "TIDAK_VALID",
            idSantri,
            namaSantri,
            "DITOLAK",
            "Mapping fingerprint tidak aktif atau tidak valid.");

        delay(1000);

        return;
    }

    Serial.println(
        "\n[AKSES DITERIMA] Sidik jari valid!");

    Serial.print("Santri: ");
    Serial.println(namaSantri);

    if (mulaiSiklusBukaLocker("FINGERPRINT"))
    {
        simpanRiwayatAkses(
            idSensor,
            idFingerprint,
            idSantri,
            namaSantri,
            "DIIZINKAN",
            "Fingerprint valid. Akses locker diberikan.");
    }
}

// ======================================================
// FIREBASE
// ======================================================

void mulaiFirebase()
{
    if (firebaseStarted)
        return;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "[FIREBASE] Tidak dimulai karena WiFi terputus.");

        return;
    }

    Serial.println(
        "[FIREBASE] Memulai koneksi Firebase...");

    configTime(
        0,
        0,
        "pool.ntp.org",
        "time.nist.gov");

    ssl_client.setInsecure();

    initializeApp(
        aClient,
        app,
        getAuth(user_auth),
        120 * 1000);

    app.getApp<Firestore::Documents>(Docs);

    firebaseStarted = true;

    unsigned long mulaiFirebaseMs = millis();

    while (!app.ready() &&
           millis() - mulaiFirebaseMs < 20000)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println(
                "[FIREBASE] WiFi terputus saat inisialisasi.");

            break;
        }

        app.loop();

        periksaSensorPintu();

        delay(100);
    }

    if (app.ready())
    {
        firebaseReady = true;

        Serial.println(
            "[FIREBASE] Firebase Siap.");
    }
    else
    {
        firebaseReady = false;

        Serial.println(
            "[FIREBASE] Belum siap. Menunggu proses koneksi ulang.");
    }
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
    Serial.begin(115200);

    // WAJIB: label mapping FP01-FP05 (L01), FP06-FP10 (L02)
    inisialisasiLabelFingerprint();

    delay(1000);

    relayOff();

    pinMode(
        SENSOR_PINTU_PIN,
        INPUT_PULLUP);

    delay(50);

    periksaSensorPintu();

    Serial.println();
    Serial.println("+--------------------------------------+");
    Serial.printf("| SMART LOCKER | %-18s | %-3s |\n",
                  ID_PERANGKAT, ID_LOCKER);
    Serial.println("+--------------------------------------+");

    // WIFI
    if (hubungkanWiFi())
    {
        mulaiFirebase();
    }
    else
    {
        Serial.println(
            "[FIREBASE] Ditunda sampai WiFi tersambung.");
    }

    // HOOK FINGERPRINT
    setHookTunggu(layaniFirebase);

    // FINGERPRINT
    Serial.println(
        "Menginisialisasi Sensor Sidik Jari...");

    fingerprintReady = inisialisasiFingerprint();

    if (fingerprintReady)
    {
        Serial.println(
            "Sensor Sidik Jari Siap.");
    }
    else
    {
        Serial.println(
            "[SENSOR] Fingerprint belum siap. Akan dicoba ulang.");
    }

    waktuCekSensor = millis();

    Serial.println(
        "\n>>> SISTEM UTAMA SIAP <<<");
}

// ======================================================
// LOOP UTAMA
// ======================================================

void loop()
{
    // 1. Sensor pintu selalu diperiksa.
    periksaSensorPintu();

    perbaruiSiklusLocker();

    // 2. WIFI TERPUTUS
    if (WiFi.status() != WL_CONNECTED)
    {
        firebaseReady = false;

        if (millis() - waktuReconnectWiFi >=
            WIFI_RETRY_INTERVAL_MS)
        {
            waktuReconnectWiFi = millis();

            Serial.println(
                "[WIFI] Koneksi terputus. Memulai percobaan ulang.");

            // Memulai ulang proses koneksi dengan kredensial
            // yang sama. Tidak mengubah relay atau sensor.
            WiFi.disconnect(false, false);

            delay(100);

            WiFi.begin(
                WIFI_SSID,
                WIFI_PASSWORD);
        }

        delay(10);

        return;
    }

    // 3. WIFI SUDAH TERSAMBUNG, FIREBASE BELUM DIMULAI
    if (!firebaseStarted)
    {
        mulaiFirebase();
    }

    // 4. LAYANI FIREBASE
    if (firebaseStarted)
    {
        app.loop();

        Docs.loop();

        firebaseReady = app.ready();
    }

    // 5. PEMULIHAN FINGERPRINT
    if (!fingerprintReady &&
        millis() - waktuRetryFingerprint >= 10000)
    {
        waktuRetryFingerprint = millis();

        Serial.println(
            "[SENSOR] Mencoba inisialisasi ulang fingerprint.");

        fingerprintReady = inisialisasiFingerprint();
    }

    // 6. PERIKSA KONEKSI SENSOR FINGERPRINT
    if (fingerprintReady &&
        !prosesEnrollmentAktif &&
        millis() - waktuCekSensor >=
            INTERVAL_CEK_SENSOR_MS)
    {
        waktuCekSensor = millis();

        if (!periksaSensor())
        {
            Serial.println(
                "[SENSOR] Fingerprint tidak merespons.");

            fingerprintReady = false;

            waktuRetryFingerprint = 0;
        }
    }

    // 7. FIREBASE BELUM SIAP
    if (!firebaseReady)
    {
        delay(10);

        return;
    }

    // 8. PERINTAH LOCKER
    cekPerintahLocker();

    if (!fingerprintReady)
    {
        delay(10);

        return;
    }

    // 9. ENROLLMENT
    cekPerintahEnrollment();

    // 10. AUTENTIKASI
    prosesAutentikasiOtomatis();

    delay(20);
}
