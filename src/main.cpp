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

// ===============================
// WIFI
// ===============================
#define WIFI_SSID "vivo Y16"
#define WIFI_PASSWORD "sibad0202"

// ===============================
// FIREBASE
// ===============================
#define API_KEY "AIzaSyDLZr4hM-aYSckpFVSYZilsJAhgQqsK0yE"
#define FIREBASE_PROJECT_ID "smart-locker-iot-10a0e"
#define USER_EMAIL "smartlocker.esp32@gmail.com"
#define USER_PASSWORD "fingerprint"

// ======================================================
// IDENTITAS PERANGKAT
// ======================================================
#define ID_PERANGKAT "esp32-locker-01"
#define ID_LOCKER "L01"

// ======================================================
// PIN PERANGKAT KERAS (RELAY & SENSOR PINTU)
// ======================================================
#define RELAY_PIN 23
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#define DURASI_BUKA_MS 4000

#define SENSOR_PINTU_PIN 4 // Sensor magnetik YL-99

// ======================================================
// FIREBASE CLIENT
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
bool firebaseReady = false;
bool fingerprintReady = false;
bool prosesEnrollmentAktif = false;

unsigned long waktuCekEnrollment = 0;
const unsigned long INTERVAL_CEK_ENROLLMENT = 2000;

unsigned long waktuCekRemoteUnlock = 0;
const unsigned long INTERVAL_CEK_REMOTE = 1500;

// ======================================================
// STRUKTUR ENROLLMENT 5 JARI TETAP
// ======================================================
const String namaJari[5] = {"JEMPOL", "TELUNJUK", "TENGAH", "MANIS", "KELINGKING"};
const String labelFingerprint[5] = {"FP01", "FP02", "FP03", "FP04", "FP05"};

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
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buffer);
}

// ======================================================
// BANTUAN MEMBACA FIELD FIRESTORE
// ======================================================
String ambilString(JsonObject fields, const char *namaField)
{
    return fields[namaField]["stringValue"] | "";
}

int ambilInteger(JsonObject fields, const char *namaField, int nilaiDefault = -1)
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

bool ambilBoolean(JsonObject fields, const char *namaField, bool nilaiDefault = false)
{
    return fields[namaField]["booleanValue"] | nilaiDefault;
}

bool bacaDokumen(String documentPath, String &hasil)
{
    hasil = Docs.get(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, GetDocumentOptions());
    if (aClient.lastError().code() != 0)
        return false;
    return hasil.length() > 0;
}

// ======================================================
// UPDATE STATUS COMMAND ENROLLMENT
// ======================================================
bool updateStatusEnrollmentCommand(String status, String pesan = "")
{
    String documentPath = "enrollment-commands/" + String(ID_PERANGKAT);
    Document<Values::Value> dokumen;
    dokumen.add("status", Values::Value(Values::StringValue(status)));
    dokumen.add("errorMessage", Values::Value(Values::StringValue(pesan)));

    PatchDocumentOptions patchOptions(DocumentMask("status,errorMessage"), DocumentMask(), Precondition());
    Docs.patch(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, patchOptions, dokumen);
    return aClient.lastError().code() == 0;
}

// ======================================================
// SIMPAN MAPPING BATCH
// Format dokumen: L01_{batchId}_FP01 s.d. FP05
// ======================================================
bool simpanMappingBatch(String batchId, int idSensor, String idFingerprint, String idSantri, String namaSantri, String namaJariSekarang, int urutanJari)
{
    String documentId = String(ID_LOCKER) + "_" + batchId + "_" + idFingerprint;
    String documentPath = "fingerprint-mapping/" + documentId;

    Document<Values::Value> dokumen;
    dokumen.add("idFingerprint", Values::Value(Values::StringValue(idFingerprint)));
    dokumen.add("idSensor", Values::Value(Values::IntegerValue(idSensor)));
    dokumen.add("idSantri", Values::Value(Values::StringValue(idSantri)));
    dokumen.add("namaSantri", Values::Value(Values::StringValue(namaSantri)));
    dokumen.add("idLocker", Values::Value(Values::StringValue(ID_LOCKER)));
    dokumen.add("idPerangkat", Values::Value(Values::StringValue(ID_PERANGKAT)));
    dokumen.add("jari", Values::Value(Values::StringValue(namaJariSekarang)));
    dokumen.add("urutanJari", Values::Value(Values::IntegerValue(urutanJari)));
    dokumen.add("batchId", Values::Value(Values::StringValue(batchId)));
    dokumen.add("statusAktif", Values::Value(Values::BooleanValue(true)));

    String waktu = dapatkanWaktuUTC();
    if (waktu.length() > 0)
        dokumen.add("createdAt", Values::Value(Values::TimestampValue(waktu)));

    Docs.createDocument(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, DocumentMask(), dokumen);
    return aClient.lastError().code() == 0;
}

// ======================================================
// AKTIFKAN BATCH BARU DI LOCKER-CONFIG
// ======================================================
bool aktifkanBatch(String batchId, String idSantri)
{
    String documentPath = "locker-config/" + String(ID_LOCKER);

    Document<Values::Value> dokumen;
    dokumen.add("activeBatchId", Values::Value(Values::StringValue(batchId)));
    dokumen.add("activeSantriId", Values::Value(Values::StringValue(idSantri)));
    dokumen.add("idLocker", Values::Value(Values::StringValue(ID_LOCKER)));

    String waktu = dapatkanWaktuUTC();
    if (waktu.length() > 0)
        dokumen.add("updatedAt", Values::Value(Values::TimestampValue(waktu)));

    PatchDocumentOptions patchOptions(DocumentMask("activeBatchId,activeSantriId,idLocker,updatedAt"), DocumentMask(), Precondition());
    Docs.patch(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, patchOptions, dokumen);
    return aClient.lastError().code() == 0;
}

// ======================================================
// SIMPAN RIWAYAT AKSES KE FIRESTORE (COLLECTION: locker-activity)
// ======================================================
bool simpanRiwayatAkses(int idSensor, String idFingerprint, String idSantri, String namaSantri, String hasilAkses, String keterangan)
{
    String documentId = String(ID_LOCKER) + "_" + String(millis()) + "_" + String((uint32_t)esp_random(), HEX);
    String documentPath = "locker-activity/" + documentId;

    Document<Values::Value> dokumen;
    dokumen.add("idLocker", Values::Value(Values::StringValue(ID_LOCKER)));
    dokumen.add("idPerangkat", Values::Value(Values::StringValue(ID_PERANGKAT)));
    dokumen.add("idSensor", Values::Value(Values::IntegerValue(idSensor)));
    dokumen.add("idFingerprint", Values::Value(Values::StringValue(idFingerprint)));
    dokumen.add("idSantri", Values::Value(Values::StringValue(idSantri)));
    dokumen.add("namaSantri", Values::Value(Values::StringValue(namaSantri)));
    dokumen.add("hasilAkses", Values::Value(Values::StringValue(hasilAkses)));
    dokumen.add("keterangan", Values::Value(Values::StringValue(keterangan)));

    String waktu = dapatkanWaktuUTC();
    if (waktu.length() > 0)
        dokumen.add("waktu", Values::Value(Values::TimestampValue(waktu)));

    Docs.createDocument(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), documentPath, DocumentMask(), dokumen);
    return aClient.lastError().code() == 0;
}

// ======================================================
// PROSES ENROLLMENT OTOMATIS BERDASARKAN PERINTAH FLUTTER
// ======================================================
void cekPerintahEnrollment()
{
    if (prosesEnrollmentAktif || !fingerprintReady)
        return;
    if (millis() - waktuCekEnrollment < INTERVAL_CEK_ENROLLMENT)
        return;
    waktuCekEnrollment = millis();

    String hasil;
    String commandPath = "enrollment-commands/" + String(ID_PERANGKAT);

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

    if (command != "START_ENROLL" || status != "pending")
        return;

    if (idPerangkat != String(ID_PERANGKAT) || idLocker != String(ID_LOCKER) || idSantri.length() == 0)
    {
        updateStatusEnrollmentCommand("failed", "Data perangkat, locker, atau santri tidak valid.");
        return;
    }

    prosesEnrollmentAktif = true;
    Serial.println("\n=== ENROLLMENT DIMULAI (DARI FLUTTER) ===");
    Serial.print("Santri: ");
    Serial.println(namaSantri);

    if (!updateStatusEnrollmentCommand("processing"))
    {
        prosesEnrollmentAktif = false;
        return;
    }

    int idsSensor[5] = {-1, -1, -1, -1, -1};
    bool semuaBerhasil = true;
    String batchId = "B" + String((uint32_t)esp_random(), HEX);

    // Mendaftarkan 5 jari secara berurutan: Jempol, Telunjuk, Tengah, Manis, Kelingking
    for (int i = 0; i < 5; i++)
    {
        Serial.print("Daftarkan jari ");
        Serial.print(i + 1);
        Serial.print("/5: ");
        Serial.println(namaJari[i]);
        int idSensor = cariIdKosong();

        if (idSensor <= 0)
        {
            semuaBerhasil = false;
            break;
        }

        bool berhasil = daftarSatuFingerprint(idSensor, namaJari[i], 30000);
        if (!berhasil)
        {
            semuaBerhasil = false;
            break;
        }

        idsSensor[i] = idSensor;
        delay(500);
    }

    if (!semuaBerhasil)
    {
        updateStatusEnrollmentCommand("failed", "Enrollment 5 jari gagal/tidak lengkap.");
        prosesEnrollmentAktif = false;
        return;
    }

    for (int i = 0; i < 5; i++)
    {
        if (!simpanMappingBatch(batchId, idsSensor[i], labelFingerprint[i], idSantri, namaSantri, namaJari[i], i + 1))
        {
            semuaBerhasil = false;
            break;
        }
    }

    if (!semuaBerhasil)
    {
        updateStatusEnrollmentCommand("failed", "Penyimpanan mapping ke Firestore gagal.");
        prosesEnrollmentAktif = false;
        return;
    }

    if (!aktifkanBatch(batchId, idSantri))
    {
        updateStatusEnrollmentCommand("failed", "Gagal mengaktifkan batch di locker-config.");
        prosesEnrollmentAktif = false;
        return;
    }

    updateStatusEnrollmentCommand("completed");
    Serial.println("=== ENROLLMENT SUKSES SELESAI ===");
    prosesEnrollmentAktif = false;
}

// ======================================================
// CEK REMOTE UNLOCK DARI FLUTTER
// ======================================================
void cekPerintahRemoteUnlock()
{
    if (prosesEnrollmentAktif)
        return;
    if (millis() - waktuCekRemoteUnlock < INTERVAL_CEK_REMOTE)
        return;
    waktuCekRemoteUnlock = millis();

    String hasil;
    String configPath = "locker-config/" + String(ID_LOCKER);

    if (!bacaDokumen(configPath, hasil))
        return;

    JsonDocument doc;
    if (deserializeJson(doc, hasil))
        return;

    JsonObject fields = doc["fields"];
    if (fields.isNull())
        return;

    String remoteCommand = ambilString(fields, "remoteCommand");
    if (remoteCommand == "UNLOCK")
    {
        Serial.println("\n[REMOTE UNLOCK] Perintah pembukaan jarak jauh diterima!");

        // Reset remoteCommand agar kembali ke status IDLE
        Document<Values::Value> dokumen;
        dokumen.add("remoteCommand", Values::Value(Values::StringValue("IDLE")));
        PatchDocumentOptions patchOptions(DocumentMask("remoteCommand"), DocumentMask(), Precondition());
        Docs.patch(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), configPath, patchOptions, dokumen);

        simpanRiwayatAkses(0, "REMOTE_UNLOCK", "-", "Admin / Pembina", "DIIZINKAN", "Buka locker via Remote Unlock Flutter.");

        digitalWrite(RELAY_PIN, RELAY_ON);
        delay(DURASI_BUKA_MS);
        digitalWrite(RELAY_PIN, RELAY_OFF);
        Serial.println("[REMOTE UNLOCK] Solenoid kembali terkunci.");
    }
}

// ======================================================
// CARI MAPPING BERDASARKAN BATCH AKTIF
// ======================================================
bool cariMappingFingerprint(int idSensor, String &idFingerprint, String &idSantri, String &namaSantri, String &idLocker, bool &statusAktif)
{
    if (idSensor <= 0)
        return false;

    String configHasil;
    String configPath = "locker-config/" + String(ID_LOCKER);

    if (!bacaDokumen(configPath, configHasil))
        return false;

    JsonDocument configDoc;
    if (deserializeJson(configDoc, configHasil))
        return false;

    JsonObject configFields = configDoc["fields"];
    String activeBatchId = ambilString(configFields, "activeBatchId");

    if (activeBatchId.length() == 0)
        return false;

    for (int i = 0; i < 5; i++)
    {
        String documentId = String(ID_LOCKER) + "_" + activeBatchId + "_" + labelFingerprint[i];
        String documentPath = "fingerprint-mapping/" + documentId;
        String hasil;

        if (!bacaDokumen(documentPath, hasil))
            continue;

        JsonDocument mappingDoc;
        if (deserializeJson(mappingDoc, hasil))
            continue;

        JsonObject fields = mappingDoc["fields"];
        if (fields.isNull())
            continue;

        int sensorMapping = ambilInteger(fields, "idSensor", -1);
        if (sensorMapping != idSensor)
            continue;

        idFingerprint = ambilString(fields, "idFingerprint");
        idSantri = ambilString(fields, "idSantri");
        namaSantri = ambilString(fields, "namaSantri");
        idLocker = ambilString(fields, "idLocker");
        statusAktif = ambilBoolean(fields, "statusAktif", false);

        return true;
    }
    return false;
}

// ======================================================
// PROSES AUTENTIKASI OTOMATIS (REAL-TIME SCANNER)
// ======================================================
void prosesAutentikasiOtomatis()
{
    if (prosesEnrollmentAktif || !fingerprintReady || !firebaseReady)
        return;

    // Memindai sidik jari pada sensor
    int idSensor = cariFingerprint();
    if (idSensor <= 0 && idSensor != -2)
        return;

    // Jika sidik jari terdeteksi di sensor fisik tapi tidak ada di database lokal sensor
    if (idSensor == -2)
    {
        Serial.println("\n[AKSES DITOLAK] Sidik jari tidak terdaftar pada sensor.");
        simpanRiwayatAkses(0, "TIDAK_DIKENAL", "", "", "DITOLAK", "Sidik jari tidak terdaftar pada sensor.");
        delay(1000);
        return;
    }

    String idFingerprint, idSantri, namaSantri, idLocker;
    bool statusAktif = false;

    bool ditemukan = cariMappingFingerprint(idSensor, idFingerprint, idSantri, namaSantri, idLocker, statusAktif);

    if (!ditemukan || !statusAktif || idLocker != String(ID_LOCKER) || idSantri.length() == 0)
    {
        Serial.println("\n[AKSES DITOLAK] Sidik jari terdeteksi tetapi mapping tidak valid/aktif.");
        simpanRiwayatAkses(idSensor, "TIDAK_VALID", idSantri, namaSantri, "DITOLAK", "Mapping fingerprint tidak aktif/valid.");
        delay(1000);
        return;
    }

    Serial.println("\n[AKSES DITERIMA] Sidik jari valid!");
    Serial.print("Santri: ");
    Serial.println(namaSantri);

    simpanRiwayatAkses(idSensor, idFingerprint, idSantri, namaSantri, "DIIZINKAN", "Fingerprint valid. Akses locker diberikan.");

    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(DURASI_BUKA_MS);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    Serial.println("Solenoid kembali terkunci.");
}
// ======================================================
// CEK PERINTAH RESET / GANTI PEMILIK DARI FLUTTER
// ======================================================
void cekPerintahResetLocker()
{
    if (prosesEnrollmentAktif)
        return;

    String hasil;
    String configPath = "locker-config/" + String(ID_LOCKER);

    if (!bacaDokumen(configPath, hasil))
        return;

    JsonDocument doc;
    if (deserializeJson(doc, hasil))
        return;

    JsonObject fields = doc["fields"];
    if (fields.isNull())
        return;

    String resetCommand = ambilString(fields, "resetCommand");
    if (resetCommand == "RESET_OWNER")
    {
        Serial.println("\n[RESET LOCKER] Perintah Reset / Ganti Pemilik diterima!");

        // 1. Ambil activeBatchId yang sedang aktif untuk menonaktifkan mapping-nya
        String activeBatchId = ambilString(fields, "activeBatchId");

        Serial.println("[RESET LOCKER] Menghapus template sidik jari dari sensor fisik...");

        // Hapus slot ID 1 sampai 5 dari sensor sidik jari fisik
        for (int i = 1; i <= 5; i++)
        {
            uint8_t p = finger.deleteModel(i);
            if (p == FINGERPRINT_OK)
            {
                Serial.print("ID Sensor ");
                Serial.print(i);
                Serial.println(" berhasil dihapus.");
            }
        }

        // 2. Jika ada batch aktif, ubah statusAktif mapping di Firestore menjadi false (opsional / pembersihan)
        if (activeBatchId.length() > 0)
        {
            for (int i = 1; i <= 5; i++)
            {
                String labelFP = "FP0" + String(i);
                String mappingDocId = String(ID_LOCKER) + "_" + activeBatchId + "_" + labelFP;
                String mappingPath = "fingerprint-mapping/" + mappingDocId;

                Document<Values::Value> patchMap;
                patchMap.add("statusAktif", Values::Value(Values::BooleanValue(false)));
                PatchDocumentOptions optMap(DocumentMask("statusAktif"), DocumentMask(), Precondition());
                Docs.patch(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), mappingPath, optMap, patchMap);
            }
        }

        // 3. Reset konfigurasi locker di Firestore (kosongkan activeBatchId dan activeSantriId)
        Document<Values::Value> dokumen;
        dokumen.add("activeBatchId", Values::Value(Values::StringValue("")));
        dokumen.add("activeSantriId", Values::Value(Values::StringValue("")));
        dokumen.add("resetCommand", Values::Value(Values::StringValue("IDLE")));

        String waktu = dapatkanWaktuUTC();
        if (waktu.length() > 0)
        {
            dokumen.add("updatedAt", Values::Value(Values::TimestampValue(waktu)));
        }

        PatchDocumentOptions patchOptions(DocumentMask("activeBatchId,activeSantriId,resetCommand,updatedAt"), DocumentMask(), Precondition());
        Docs.patch(aClient, Firestore::Parent(FIREBASE_PROJECT_ID), configPath, patchOptions, dokumen);

        // 4. Simpan riwayat aktivitas
        simpanRiwayatAkses(0, "RESET_OWNER", "-", "Admin / Pembina", "DISETUJUI", "Loker di-reset untuk penggantian pemilik baru.");

        Serial.println("[RESET LOCKER] Loker berhasil di-reset total. Siap untuk pendaftaran pemilik baru.");
    }
}

// ======================================================
// SETUP
// ======================================================
void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF);

    pinMode(SENSOR_PINTU_PIN, INPUT_PULLUP);

    Serial.println("\n==================================");
    Serial.println(" SMART LOCKER - ESP32-LOCKER-01");
    Serial.println("==================================");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Menghubungkan WiFi");

    unsigned long mulaiWiFi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - mulaiWiFi < 20000)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("WiFi Terhubung! IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("WiFi Gagal! Sistem berjalan standby.");
        return;
    }

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ssl_client.setInsecure();

    Serial.println("Menghubungkan Firebase...");
    initializeApp(aClient, app, getAuth(user_auth), 120 * 1000);
    app.getApp<Firestore::Documents>(Docs);

    unsigned long mulaiFirebase = millis();
    while (!app.ready() && millis() - mulaiFirebase < 20000)
    {
        app.loop();
        delay(100);
    }

    if (app.ready())
    {
        firebaseReady = true;
        Serial.println("Firebase Siap.");
    }

    Serial.println("Menginisialisasi Sensor Sidik Jari...");
    fingerprintReady = inisialisasiFingerprint();

    if (fingerprintReady)
    {
        Serial.println("Sensor Sidik Jari Siap.");
    }

    Serial.println("\n>>> SISTEM UTAMA SIAP - AUTENTIKASI OTOMATIS AKTIF <<<");
}

// ======================================================
// LOOP UTAMA
// ======================================================
void loop()
{
    app.loop();
    Docs.loop();

    if (!firebaseReady || !fingerprintReady || WiFi.status() != WL_CONNECTED)
    {
        digitalWrite(RELAY_PIN, RELAY_OFF);
        delay(10);
        return;
    }

    // 1. Memeriksa perintah enrollment dari aplikasi Flutter secara real-time
    cekPerintahEnrollment();

    // 2. Memeriksa perintah remote unlock dari aplikasi Flutter
    cekPerintahRemoteUnlock();

    // 3. Memeriksa perintah reset / ganti pemilik dari aplikasi Flutter
    cekPerintahResetLocker();

    // 4. Autentikasi otomatis berjalan terus-menerus secara real-time
    prosesAutentikasiOtomatis();

    delay(20);
}