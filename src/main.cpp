#define ENABLE_USER_AUTH
#define ENABLE_FIRESTORE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include "fingerprint.h"
#include <time.h>
#include <ArduinoJson.h>

// ======================================================
// WIFI
// ======================================================

#define WIFI_SSID "vivo Y16"
#define WIFI_PASSWORD "sibad0202"

// ======================================================
// FIREBASE
// ======================================================

#define API_KEY "AIzaSyDLZr4hM-aYSckpFVSYZilsJAhgQqsK0yE"
#define USER_EMAIL "smartlocker.esp32@gmail.com"
#define USER_PASSWORD "fingerprint"
#define FIREBASE_PROJECT_ID "smart-locker-iot-10a0e"

// ======================================================
// IDENTITAS PERANGKAT
// ======================================================

#define ID_PERANGKAT "esp32-locker-01"
#define ID_LOCKER "L01"

// ======================================================
// FIREBASE OBJECT
// ======================================================

UserAuth user_auth(
    API_KEY,
    USER_EMAIL,
    USER_PASSWORD,
    3000);

FirebaseApp app;

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client);

Firestore::Documents Docs;

bool firebaseReady = false;

// ======================================================
// NTP / WAKTU
// ======================================================

const char *NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 7 * 3600;

const int DAYLIGHT_OFFSET_SEC = 0;

// ======================================================
// AUTH CALLBACK
// ======================================================

void authCallback(AsyncResult &aResult)
{
    if (aResult.isEvent())
    {
        Serial.printf(
            "Event task: %s, msg: %s, code: %d\n",
            aResult.uid().c_str(),
            aResult.appEvent().message().c_str(),
            aResult.appEvent().code());
    }

    if (aResult.isDebug())
    {
        Serial.printf(
            "Debug task: %s, msg: %s\n",
            aResult.uid().c_str(),
            aResult.debug().c_str());
    }

    if (aResult.isError())
    {
        Serial.printf(
            "Error task: %s, msg: %s, code: %d\n",
            aResult.uid().c_str(),
            aResult.error().message().c_str(),
            aResult.error().code());
    }
}

// ======================================================
// MENDAPATKAN WAKTU UTC
// ======================================================

String dapatkanWaktuUTC()
{
    time_t sekarang = time(nullptr);

    struct tm waktu;

    gmtime_r(&sekarang, &waktu);

    char buffer[30];

    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%dT%H:%M:%SZ",
        &waktu);

    return String(buffer);
}

// ======================================================
// MENCARI MAPPING FINGERPRINT DI FIRESTORE
// ======================================================

bool cariMappingFingerprint(
    int idSensor,
    String &idSidikJari,
    String &idSantri,
    String &namaSantri,
    String &idLocker)
{
    Serial.println();
    Serial.println("======================================");
    Serial.println(" MENCARI MAPPING FINGERPRINT");
    Serial.println("======================================");

    String nomorFP = "";

    if (idSensor < 10)
    {
        nomorFP = "FP0" + String(idSensor);
    }
    else
    {
        nomorFP = "FP" + String(idSensor);
    }

    String documentPath =
        "fingerprint-mapping/" + nomorFP;

    Serial.print("ID Sensor : ");
    Serial.println(idSensor);

    Serial.print("Dokumen   : ");
    Serial.println(nomorFP);

    Serial.print("Path      : ");
    Serial.println(documentPath);

    // --------------------------------------------------
    // AMBIL DOKUMEN DARI FIRESTORE
    // --------------------------------------------------

    String payload = Docs.get(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        GetDocumentOptions());

    // --------------------------------------------------
    // CEK HASIL
    // --------------------------------------------------

    if (aClient.lastError().code() != 0)
    {
        Serial.println();
        Serial.println("GAGAL MEMBACA MAPPING FIRESTORE!");

        Serial.print("Error code : ");
        Serial.println(aClient.lastError().code());

        Serial.print("Pesan      : ");
        Serial.println(aClient.lastError().message());

        return false;
    }

    if (payload.length() == 0)
    {
        Serial.println();
        Serial.println("DATA MAPPING KOSONG!");

        return false;
    }
    Serial.println();
    Serial.println("========== RAW FIRESTORE JSON ==========");
    Serial.println(payload);
    Serial.println("========================================");

    // --------------------------------------------------
    // PARSE JSON
    // --------------------------------------------------

    JsonDocument doc;

    DeserializationError error =
        deserializeJson(doc, payload);

    if (error)
    {
        Serial.println();
        Serial.println("GAGAL MEMBACA FORMAT JSON!");

        Serial.print("Error JSON: ");
        Serial.println(error.c_str());

        return false;
    }

    // --------------------------------------------------
    // AMBIL DATA FIELD
    // --------------------------------------------------

    idSidikJari =
        doc["fields"]["idFingerprint"]["stringValue"] | "";

    idSantri =
        doc["fields"]["idSantri"]["stringValue"] | "";

    namaSantri =
        doc["fields"]["namaSantri"]["stringValue"] | "";

    idLocker =
        doc["fields"]["idLocker"]["stringValue"] | "";

    // --------------------------------------------------
    // VALIDASI DATA
    // --------------------------------------------------

    if (
        idSidikJari.length() == 0 ||
        idSantri.length() == 0 ||
        namaSantri.length() == 0 ||
        idLocker.length() == 0)
    {
        Serial.println();
        Serial.println("DATA MAPPING TIDAK LENGKAP!");

        return false;
    }

    // --------------------------------------------------
    // TAMPILKAN HASIL
    // --------------------------------------------------

    Serial.println();
    Serial.println("MAPPING DITEMUKAN!");

    Serial.print("Fingerprint : ");
    Serial.println(idSidikJari);

    Serial.print("ID Santri   : ");
    Serial.println(idSantri);

    Serial.print("Nama Santri : ");
    Serial.println(namaSantri);

    Serial.print("Locker      : ");
    Serial.println(idLocker);

    Serial.println();

    return true;
}

// ======================================================
// KIRIM AKTIVITAS KE FIRESTORE
// ======================================================

void kirimAktivitas(
    int idSensor,
    int tingkatKecocokan,
    String idSidikJari,
    String idSantri,
    String namaSantri,
    String status,
    String dataSidikJari)
{
    Serial.println();
    Serial.println("======================================");
    Serial.println(" MENGIRIM AKTIVITAS KE FIRESTORE");
    Serial.println("======================================");

    String waktuAkses = dapatkanWaktuUTC();

    Serial.print("Locker       : ");
    Serial.println(ID_LOCKER);

    Serial.print("ID Sensor    : ");
    Serial.println(idSensor);

    Serial.print("Sidik Jari   : ");
    Serial.println(idSidikJari);

    Serial.print("Santri       : ");
    Serial.println(namaSantri);

    Serial.print("Status       : ");
    Serial.println(status);

    Serial.print("Kecocokan    : ");
    Serial.println(tingkatKecocokan);

    Serial.print("Waktu        : ");
    Serial.println(waktuAkses);

    String jalurDokumen =
        "aktivitas-locker";

    Document<Values::Value> dokumen;

    dokumen.add(
        "idPerangkat",
        Values::Value(
            Values::StringValue(ID_PERANGKAT)));

    dokumen.add(
        "idSensor",
        Values::Value(
            Values::IntegerValue(idSensor)));

    dokumen.add(
        "namaSantri",
        Values::Value(
            Values::StringValue(namaSantri)));

    dokumen.add(
        "idSidikJari",
        Values::Value(
            Values::StringValue(idSidikJari)));

    dokumen.add(
        "waktuAkses",
        Values::Value(
            Values::TimestampValue(waktuAkses)));

    dokumen.add(
        "tingkatKecocokan",
        Values::Value(
            Values::IntegerValue(tingkatKecocokan)));

    dokumen.add(
        "idSantri",
        Values::Value(
            Values::StringValue(idSantri)));

    dokumen.add(
        "idLocker",
        Values::Value(
            Values::StringValue(ID_LOCKER)));

    dokumen.add(
        "status",
        Values::Value(
            Values::StringValue(status)));

    dokumen.add(
        "metodeAkses",
        Values::Value(
            Values::StringValue("sidik_jari")));

    dokumen.add(
        "dataSidikJari",
        Values::Value(
            Values::StringValue(dataSidikJari)));

    Serial.println();
    Serial.println("Mengirim request createDocument...");

    String hasil = Docs.createDocument(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        jalurDokumen,
        DocumentMask(),
        dokumen);

    Serial.println();
    Serial.println("createDocument selesai dipanggil.");

    if (aClient.lastError().code() != 0)
    {
        Serial.println();
        Serial.println("ERROR FIRESTORE!");

        Serial.print("Error code : ");
        Serial.println(aClient.lastError().code());

        Serial.print("Pesan      : ");
        Serial.println(aClient.lastError().message());
    }
    else if (hasil.length() > 0)
    {
        Serial.println();
        Serial.println("AKTIVITAS BERHASIL DISIMPAN!");
    }
    else
    {
        Serial.println();
        Serial.println("GAGAL MENYIMPAN AKTIVITAS!");
    }

    Serial.println();
    Serial.println("SELESAI PROSES KIRIM AKTIVITAS.");
    Serial.println();
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("======================================");
    Serial.println(" SMART LOCKER IoT");
    Serial.println(" ESP32 + FINGERPRINT + FIRESTORE");
    Serial.println("======================================");

    // --------------------------------------------------
    // INISIALISASI FINGERPRINT
    // --------------------------------------------------

    if (!inisialisasiFingerprint())
    {
        Serial.println();
        Serial.println("Fingerprint gagal diinisialisasi.");
    }

    // --------------------------------------------------
    // KONEKSI WIFI
    // --------------------------------------------------

    Serial.println();
    Serial.println("======================================");
    Serial.println(" MENGHUBUNGKAN KE WIFI");
    Serial.println("======================================");

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD);

    Serial.print("Menghubungkan");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);

        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi TERHUBUNG!");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // --------------------------------------------------
    // WAKTU NTP
    // --------------------------------------------------

    configTime(
        GMT_OFFSET_SEC,
        DAYLIGHT_OFFSET_SEC,
        NTP_SERVER);

    // --------------------------------------------------
    // SSL
    // --------------------------------------------------

    ssl_client.setInsecure();

    // --------------------------------------------------
    // FIREBASE
    // --------------------------------------------------

    Serial.println();
    Serial.println("======================================");
    Serial.println(" MENGHUBUNGKAN KE FIREBASE");
    Serial.println("======================================");

    initializeApp(
        aClient,
        app,
        getAuth(user_auth),
        authCallback,
        "authTask");

    app.getApp<Firestore::Documents>(Docs);

    Serial.println("Firebase sedang melakukan autentikasi...");
}

// ======================================================
// LOOP
// ======================================================

void loop()
{
    app.loop();

    Docs.loop();

    // --------------------------------------------------
    // TUNGGU FIREBASE SIAP
    // --------------------------------------------------

    if (!app.ready())
    {
        delay(10);

        return;
    }

    // --------------------------------------------------
    // FIREBASE SUDAH SIAP
    // --------------------------------------------------

    if (!firebaseReady)
    {
        firebaseReady = true;

        Serial.println();
        Serial.println("======================================");
        Serial.println(" AUTENTIKASI FIREBASE BERHASIL");
        Serial.println("======================================");

        Serial.print("UID Pengguna: ");
        Serial.println(app.getUid());

        Serial.println();
        Serial.println("SISTEM SIAP");
        Serial.println("Silakan tempelkan jari...");
        Serial.println();
    }

    // --------------------------------------------------
    // MEMBACA FINGERPRINT
    // --------------------------------------------------

    int idSensor = cariFingerprint();

    // Tidak ada jari / proses belum selesai
    if (idSensor == -1)
    {
        delay(100);

        return;
    }

    // --------------------------------------------------
    // FINGERPRINT TIDAK TERDAFTAR
    // --------------------------------------------------

    if (idSensor == 0)
    {
        kirimAktivitas(
            0,
            0,
            "tidak_dikenal",
            "",
            "",
            "ditolak",
            "jari tidak terdaftar");

        Serial.println();
        Serial.println("AKSES DITOLAK!");

        tungguJariDiangkat();

        return;
    }

    // ==================================================
    // MENCARI PEMILIK DARI FIRESTORE
    // ==================================================

    String idSidikJari;
    String idSantri;
    String namaSantri;
    String idLocker;

    bool mappingDitemukan =
        cariMappingFingerprint(
            idSensor,
            idSidikJari,
            idSantri,
            namaSantri,
            idLocker);

    // --------------------------------------------------
    // MAPPING TIDAK DITEMUKAN
    // --------------------------------------------------

    if (!mappingDitemukan)
    {
        Serial.println();
        Serial.println("PEMILIK FINGERPRINT TIDAK DITEMUKAN!");

        kirimAktivitas(
            idSensor,
            finger.confidence,
            "FP" + String(idSensor),
            "",
            "",
            "ditolak",
            "mapping fingerprint tidak ditemukan");

        Serial.println();
        Serial.println("AKSES DITOLAK!");

        tungguJariDiangkat();

        return;
    }

    // --------------------------------------------------
    // MAPPING DITEMUKAN
    // --------------------------------------------------

    Serial.println();
    Serial.println("======================================");
    Serial.println(" PEMILIK LOCKER DITEMUKAN!");
    Serial.println("======================================");

    Serial.print("Fingerprint : ");
    Serial.println(idSidikJari);

    Serial.print("ID Santri   : ");
    Serial.println(idSantri);

    Serial.print("Nama Santri : ");
    Serial.println(namaSantri);

    Serial.print("Locker      : ");
    Serial.println(idLocker);

    // --------------------------------------------------
    // KIRIM AKTIVITAS
    // --------------------------------------------------

    kirimAktivitas(
        idSensor,
        finger.confidence,
        idSidikJari,
        idSantri,
        namaSantri,
        "diterima",
        "jari pemilik");

    Serial.println();
    Serial.println("AKSES DITERIMA!");

    // --------------------------------------------------
    // TUNGGU JARI DIANGKAT
    // --------------------------------------------------

    tungguJariDiangkat();

    return;
}