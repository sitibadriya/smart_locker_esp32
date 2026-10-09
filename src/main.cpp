#define ENABLE_USER_AUTH
#define ENABLE_FIRESTORE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>

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
// IDENTITAS LOCKER
// ======================================================

#define ID_PERANGKAT \
    "esp32-locker-01"

#define ID_LOCKER \
    "L01"

// ======================================================
// RELAY
// ======================================================

#define RELAY_PIN 23

#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ======================================================
// FIREBASE
// ======================================================

UserAuth user_auth(
    API_KEY,
    USER_EMAIL,
    USER_PASSWORD);

FirebaseApp app;

WiFiClientSecure ssl_client;

using AsyncClient =
    AsyncClientClass;

AsyncClient aClient(
    ssl_client);

Firestore::Documents Docs;

// ======================================================
// STATUS
// ======================================================

bool firebaseReady = false;

bool modeAutentikasi = false;

bool prosesEnrollmentAktif = false;

// ======================================================
// TIMER
// ======================================================

unsigned long waktuCekEnrollment = 0;

const unsigned long
    INTERVAL_CEK_ENROLLMENT = 2000;

// ======================================================
// WAKTU UTC
// ======================================================

String dapatkanWaktuUTC()
{
    time_t now;

    time(&now);

    struct tm timeinfo;

    gmtime_r(
        &now,
        &timeinfo);

    char buffer[30];

    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%dT%H:%M:%SZ",
        &timeinfo);

    return String(buffer);
}

// ======================================================
// UPDATE STATUS COMMAND
// ======================================================

bool updateStatusEnrollmentCommand(
    String status,
    String pesan = "")
{
    String documentPath =
        "enrollment-commands/" +
        String(ID_PERANGKAT);

    Document<Values::Value> dokumen;

    dokumen.add(
        "status",
        Values::Value(
            Values::StringValue(
                status)));

    dokumen.add(
        "errorMessage",
        Values::Value(
            Values::StringValue(
                pesan)));

    PatchDocumentOptions patchOptions(
        DocumentMask(
            "status,errorMessage"),
        DocumentMask(),
        Precondition());

    Docs.patch(
        aClient,
        Firestore::Parent(
            FIREBASE_PROJECT_ID),
        documentPath,
        patchOptions,
        dokumen);

    if (
        aClient.lastError().code() != 0)
    {
        Serial.println();
        Serial.println(
            "GAGAL UPDATE STATUS COMMAND!");

        Serial.print(
            "Kode error: ");

        Serial.println(
            aClient.lastError().code());

        Serial.print(
            "Pesan: ");

        Serial.println(
            aClient.lastError().message());

        return false;
    }

    return true;
}

// ======================================================
// SIMPAN SATU FINGERPRINT KE PENDING
// ======================================================

bool kirimFingerprintPending(
    int idSensor,
    String namaJari,
    int urutanJari,
    String idFingerprint,
    String idSantri,
    String namaSantri,
    String idLocker)
{
    Serial.println();
    Serial.println(
        "--------------------------------------");

    Serial.println(
        " MENYIMPAN FINGERPRINT PENDING");

    Serial.println(
        "--------------------------------------");

    Serial.print(
        "ID Fingerprint : ");

    Serial.println(
        idFingerprint);

    Serial.print(
        "Urutan jari    : ");

    Serial.println(
        urutanJari);

    Serial.print(
        "Jari           : ");

    Serial.println(
        namaJari);

    Serial.print(
        "ID Sensor      : ");

    Serial.println(
        idSensor);

    // --------------------------------------------------
    // DOKUMEN
    // --------------------------------------------------

    Document<Values::Value> dokumen;

    dokumen.add(
        "idFingerprint",
        Values::Value(
            Values::StringValue(
                idFingerprint)));

    dokumen.add(
        "idSensor",
        Values::Value(
            Values::IntegerValue(
                idSensor)));

    dokumen.add(
        "urutanJari",
        Values::Value(
            Values::IntegerValue(
                urutanJari)));

    dokumen.add(
        "jari",
        Values::Value(
            Values::StringValue(
                namaJari)));

    dokumen.add(
        "idPerangkat",
        Values::Value(
            Values::StringValue(
                ID_PERANGKAT)));

    dokumen.add(
        "idLocker",
        Values::Value(
            Values::StringValue(
                idLocker)));

    dokumen.add(
        "idSantri",
        Values::Value(
            Values::StringValue(
                idSantri)));

    dokumen.add(
        "namaSantri",
        Values::Value(
            Values::StringValue(
                namaSantri)));

    dokumen.add(
        "status",
        Values::Value(
            Values::StringValue(
                "belum_dilabeli")));

    dokumen.add(
        "createdAt",
        Values::Value(
            Values::TimestampValue(
                dapatkanWaktuUTC())));

    // --------------------------------------------------
    // CREATE FIRESTORE
    // --------------------------------------------------

    Docs.createDocument(
        aClient,
        Firestore::Parent(
            FIREBASE_PROJECT_ID),
        "fingerprint-pending",
        DocumentMask(),
        dokumen);

    if (
        aClient.lastError().code() != 0)
    {
        Serial.println();
        Serial.println(
            "GAGAL MENYIMPAN PENDING!");

        Serial.print(
            "Kode error: ");

        Serial.println(
            aClient.lastError().code());

        Serial.print(
            "Pesan: ");

        Serial.println(
            aClient.lastError().message());

        return false;
    }

    Serial.println(
        "Fingerprint masuk fingerprint-pending.");

    return true;
}

// ======================================================
// CARI COMMAND ENROLLMENT
// ======================================================

void cekPerintahEnrollment()
{
    // --------------------------------------------------
    // JANGAN CEK SAAT ENROLLMENT
    // --------------------------------------------------

    if (
        prosesEnrollmentAktif)
    {
        return;
    }

    // --------------------------------------------------
    // INTERVAL
    // --------------------------------------------------

    if (
        millis() -
            waktuCekEnrollment <
        INTERVAL_CEK_ENROLLMENT)
    {
        return;
    }

    waktuCekEnrollment =
        millis();

    // --------------------------------------------------
    // PATH COMMAND
    // --------------------------------------------------

    String documentPath =
        "enrollment-commands/" +
        String(ID_PERANGKAT);

    // --------------------------------------------------
    // BACA COMMAND
    // --------------------------------------------------

    String hasil =
        Docs.get(
            aClient,
            Firestore::Parent(
                FIREBASE_PROJECT_ID),
            documentPath,
            GetDocumentOptions());

    if (
        aClient.lastError().code() != 0)
    {
        return;
    }

    if (
        hasil.length() == 0)
    {
        return;
    }

    // --------------------------------------------------
    // PARSE JSON
    // --------------------------------------------------

    DynamicJsonDocument doc(
        8192);

    DeserializationError error =
        deserializeJson(
            doc,
            hasil);

    if (error)
    {
        Serial.println();
        Serial.println(
            "JSON command tidak valid.");

        Serial.println(
            error.c_str());

        return;
    }

    JsonObject fields =
        doc["fields"];

    if (
        fields.isNull())
    {
        return;
    }

    // --------------------------------------------------
    // AMBIL DATA COMMAND
    // --------------------------------------------------

    String command =
        fields["command"]["stringValue"] |
        "";

    String status =
        fields["status"]["stringValue"] |
        "";

    String idSantri =
        fields["idSantri"]["stringValue"] |
        "";

    String namaSantri =
        fields["namaSantri"]["stringValue"] |
        "";

    String idLocker =
        fields["idLocker"]["stringValue"] |
        "";

    String idPerangkat =
        fields["idPerangkat"]["stringValue"] |
        "";

    // --------------------------------------------------
    // VALIDASI COMMAND
    // --------------------------------------------------

    if (
        command != "START_ENROLL")
    {
        return;
    }

    if (
        status != "pending")
    {
        return;
    }

    if (
        idPerangkat !=
        String(ID_PERANGKAT))
    {
        Serial.println();
        Serial.println(
            "ID perangkat tidak sesuai.");

        return;
    }

    if (
        idLocker !=
        String(ID_LOCKER))
    {
        updateStatusEnrollmentCommand(
            "failed",
            "Locker tidak sesuai dengan perangkat.");

        return;
    }

    if (
        idSantri.length() == 0)
    {
        updateStatusEnrollmentCommand(
            "failed",
            "ID santri kosong.");

        return;
    }

    // ==================================================
    // MULAI PROSES
    // ==================================================

    prosesEnrollmentAktif = true;

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " START ENROLLMENT DITERIMA");

    Serial.println(
        "======================================");

    Serial.print(
        "ID Santri    : ");

    Serial.println(
        idSantri);

    Serial.print(
        "Nama Santri  : ");

    Serial.println(
        namaSantri);

    Serial.print(
        "ID Locker    : ");

    Serial.println(
        idLocker);

    // ==================================================
    // STATUS PROCESSING
    // ==================================================

    updateStatusEnrollmentCommand(
        "processing");

    // ==================================================
    // RESET DATABASE SENSOR
    // ==================================================

    Serial.println();
    Serial.println(
        "Menyiapkan pendaftaran baru...");

    Serial.println(
        "Fingerprint lama akan dihapus dari sensor.");

    bool resetBerhasil =
        hapusSemuaFingerprint();

    if (
        !resetBerhasil)
    {
        Serial.println();
        Serial.println(
            "GAGAL MEMBERSIHKAN DATABASE SENSOR.");

        updateStatusEnrollmentCommand(
            "failed",
            "Gagal membersihkan database fingerprint sensor.");

        prosesEnrollmentAktif = false;

        return;
    }

    delay(1000);

    // ==================================================
    // URUTAN 5 JARI
    // ==================================================

    const String namaJari[5] =
        {
            "JEMPOL",
            "TELUNJUK",
            "TENGAH",
            "MANIS",
            "KELINGKING"};

    const String labelFingerprint[5] =
        {
            "FP01",
            "FP02",
            "FP03",
            "FP04",
            "FP05"};

    // ==================================================
    // DAFTAR 5 JARI
    // ==================================================

    for (
        int i = 0;
        i < 5;
        i++)
    {
        // ----------------------------------------------
        // CARI ID SENSOR KOSONG
        // ----------------------------------------------

        int idSensor =
            cariIdKosong();

        if (
            idSensor == -1)
        {
            Serial.println();
            Serial.println(
                "Tidak ada ID sensor kosong.");

            updateStatusEnrollmentCommand(
                "failed",
                "Tidak ada ID sensor yang tersedia.");

            prosesEnrollmentAktif = false;

            return;
        }

        // ----------------------------------------------
        // TAMPILKAN PROGRESS
        // ----------------------------------------------

        Serial.println();
        Serial.println(
            "======================================");

        Serial.print(
            " FINGERPRINT ");

        Serial.print(
            i + 1);

        Serial.println(
            " DARI 5");

        Serial.println(
            "======================================");

        Serial.print(
            "Jari       : ");

        Serial.println(
            namaJari[i]);

        Serial.print(
            "Label      : ");

        Serial.println(
            labelFingerprint[i]);

        Serial.print(
            "ID Sensor  : ");

        Serial.println(
            idSensor);

        // ----------------------------------------------
        // DAFTAR SATU JARI
        // ----------------------------------------------

        bool berhasil =
            daftarSatuFingerprint(
                idSensor,
                namaJari[i],
                30000);

        if (
            !berhasil)
        {
            Serial.println();
            Serial.println(
                "======================================");

            Serial.print(
                " ENROLLMENT GAGAL PADA JARI KE-");

            Serial.println(
                i + 1);

            Serial.print(
                "Jari: ");

            Serial.println(
                namaJari[i]);

            Serial.println(
                "======================================");

            String pesan =
                "Pendaftaran " +
                namaJari[i] +
                " gagal.";

            updateStatusEnrollmentCommand(
                "failed",
                pesan);

            prosesEnrollmentAktif = false;

            return;
        }

        // ----------------------------------------------
        // SIMPAN PENDING
        // ----------------------------------------------

        bool pendingBerhasil =
            kirimFingerprintPending(
                idSensor,
                namaJari[i],
                i + 1,
                labelFingerprint[i],
                idSantri,
                namaSantri,
                idLocker);

        if (
            !pendingBerhasil)
        {
            Serial.println();
            Serial.println(
                "Fingerprint berhasil tersimpan "
                "di sensor.");

            Serial.println(
                "Namun Firebase gagal menerima data.");

            updateStatusEnrollmentCommand(
                "failed",
                "Fingerprint tersimpan di sensor tetapi gagal disimpan ke Firebase.");

            prosesEnrollmentAktif = false;

            return;
        }

        // ----------------------------------------------
        // BERHASIL
        // ----------------------------------------------

        Serial.println();
        Serial.println(
            "--------------------------------------");

        Serial.print(
            "✓ ");

        Serial.print(
            labelFingerprint[i]);

        Serial.print(
            " / ");

        Serial.print(
            namaJari[i]);

        Serial.println(
            " BERHASIL");

        Serial.print(
            "ID Sensor: ");

        Serial.println(
            idSensor);

        Serial.println(
            "--------------------------------------");

        // ----------------------------------------------
        // JEDA SEBELUM JARI BERIKUTNYA
        // ----------------------------------------------

        if (
            i < 4)
        {
            Serial.println();
            Serial.print(
                "Lanjut ke jari ");

            Serial.print(
                i + 2);

            Serial.println(
                " dari 5.");

            delay(1000);
        }
    }

    // ==================================================
    // SEMUA SELESAI
    // ==================================================

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " SEMUA 5 FINGERPRINT BERHASIL");

    Serial.println(
        "======================================");

    Serial.println(
        "FP01 → JEMPOL     ✓");

    Serial.println(
        "FP02 → TELUNJUK   ✓");

    Serial.println(
        "FP03 → TENGAH     ✓");

    Serial.println(
        "FP04 → MANIS      ✓");

    Serial.println(
        "FP05 → KELINGKING ✓");

    Serial.println();

    Serial.println(
        "Semua fingerprint telah disimpan "
        "ke fingerprint-pending.");

    // ==================================================
    // COMMAND SELESAI
    // ==================================================

    updateStatusEnrollmentCommand(
        "completed");

    prosesEnrollmentAktif = false;
}

// ======================================================
// MULAI AUTENTIKASI
// ======================================================

void mulaiAutentikasi()
{
    if (
        modeAutentikasi)
    {
        return;
    }

    modeAutentikasi = true;

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " MODE AUTENTIKASI FINGERPRINT");

    Serial.println(
        "======================================");

    Serial.println();

    Serial.println(
        "Tempelkan jari pada sensor.");
}

// ======================================================
// CARI MAPPING BERDASARKAN ID SENSOR
// ======================================================

bool cariMappingFingerprint(
    int idSensor,
    String &idFingerprint,
    String &idSantri,
    String &namaSantri,
    String &idLocker,
    bool &statusAktif)
{
    if (
        idSensor <= 0)
    {
        return false;
    }

    // --------------------------------------------------
    // CARI FP01 - FP50
    // --------------------------------------------------

    for (
        int nomor = 1;
        nomor <= 50;
        nomor++)
    {
        String label;

        if (
            nomor < 10)
        {
            label =
                "FP0" +
                String(nomor);
        }
        else
        {
            label =
                "FP" +
                String(nomor);
        }

        String documentPath =
            "fingerprint-mapping/" +
            label;

        String hasil =
            Docs.get(
                aClient,
                Firestore::Parent(
                    FIREBASE_PROJECT_ID),
                documentPath,
                GetDocumentOptions());

        if (
            aClient.lastError().code() != 0)
        {
            continue;
        }

        if (
            hasil.length() == 0)
        {
            continue;
        }

        DynamicJsonDocument doc(
            8192);

        DeserializationError error =
            deserializeJson(
                doc,
                hasil);

        if (error)
        {
            continue;
        }

        JsonObject fields =
            doc["fields"];

        if (
            fields.isNull())
        {
            continue;
        }

        int sensorMapping =
            fields["idSensor"]["integerValue"] |
            -1;

        if (
            sensorMapping != idSensor)
        {
            continue;
        }

        idFingerprint =
            fields["idFingerprint"]["stringValue"] |
            label;

        idSantri =
            fields["idSantri"]["stringValue"] |
            "";

        namaSantri =
            fields["namaSantri"]["stringValue"] |
            "";

        idLocker =
            fields["idLocker"]["stringValue"] |
            "";

        statusAktif =
            fields["statusAktif"]["booleanValue"] |
            false;

        return true;
    }

    return false;
}

// ======================================================
// PROSES AUTENTIKASI
// ======================================================

void prosesAutentikasi()
{
    int idSensor =
        cariFingerprint();

    if (
        idSensor <= 0)
    {
        modeAutentikasi = false;

        return;
    }

    String idFingerprint;
    String idSantri;
    String namaSantri;
    String idLocker;

    bool statusAktif = false;

    bool ditemukan =
        cariMappingFingerprint(
            idSensor,
            idFingerprint,
            idSantri,
            namaSantri,
            idLocker,
            statusAktif);

    // --------------------------------------------------
    // TIDAK ADA MAPPING
    // --------------------------------------------------

    if (
        !ditemukan)
    {
        Serial.println();
        Serial.println(
            "======================================");

        Serial.println(
            " AKSES DITOLAK");

        Serial.println(
            "======================================");

        Serial.println(
            "Fingerprint belum memiliki mapping.");

        modeAutentikasi = false;

        return;
    }

    // --------------------------------------------------
    // TAMPILKAN MAPPING
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " DATA MAPPING");

    Serial.println(
        "======================================");

    Serial.print(
        "ID Fingerprint : ");

    Serial.println(
        idFingerprint);

    Serial.print(
        "ID Sensor      : ");

    Serial.println(
        idSensor);

    Serial.print(
        "ID Santri      : ");

    Serial.println(
        idSantri);

    Serial.print(
        "Nama Santri    : ");

    Serial.println(
        namaSantri);

    Serial.print(
        "ID Locker      : ");

    Serial.println(
        idLocker);

    Serial.print(
        "Status Aktif   : ");

    Serial.println(
        statusAktif
            ? "true"
            : "false");

    // --------------------------------------------------
    // CEK STATUS AKTIF
    // --------------------------------------------------

    if (
        !statusAktif)
    {
        Serial.println();
        Serial.println(
            "AKSES DITOLAK");

        Serial.println(
            "Fingerprint tidak aktif.");

        modeAutentikasi = false;

        return;
    }

    // --------------------------------------------------
    // CEK LOCKER
    // --------------------------------------------------

    if (
        idLocker !=
        String(ID_LOCKER))
    {
        Serial.println();
        Serial.println(
            "AKSES DITOLAK");

        Serial.println(
            "Fingerprint bukan milik locker ini.");

        modeAutentikasi = false;

        return;
    }

    // ==================================================
    // AKSES DITERIMA
    // ==================================================

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " AKSES DITERIMA");

    Serial.println(
        "======================================");

    Serial.println(
        "Membuka locker...");

    // --------------------------------------------------
    // BUKA SOLENOID
    // --------------------------------------------------

    digitalWrite(
        RELAY_PIN,
        RELAY_ON);

    Serial.println(
        "Relay AKTIF.");

    Serial.println(
        "Solenoid membuka locker.");

    // --------------------------------------------------
    // TAHAN 4 DETIK
    // --------------------------------------------------

    delay(4000);

    // --------------------------------------------------
    // KUNCI KEMBALI
    // --------------------------------------------------

    digitalWrite(
        RELAY_PIN,
        RELAY_OFF);

    Serial.println(
        "Relay NONAKTIF.");

    Serial.println(
        "Locker kembali terkunci.");

    modeAutentikasi = false;
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
    Serial.begin(
        115200);

    delay(1000);

    // --------------------------------------------------
    // RELAY
    // --------------------------------------------------

    pinMode(
        RELAY_PIN,
        OUTPUT);

    digitalWrite(
        RELAY_PIN,
        RELAY_OFF);

    // --------------------------------------------------
    // BANNER
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " SMART LOCKER");

    Serial.println(
        " ESP32 + FINGERPRINT + FIRESTORE");

    Serial.println(
        "======================================");

    // --------------------------------------------------
    // WIFI
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "Menghubungkan WiFi...");

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD);

    while (
        WiFi.status() !=
        WL_CONNECTED)
    {
        delay(500);

        Serial.print(".");
    }

    Serial.println();

    Serial.println(
        "WiFi TERHUBUNG.");

    Serial.print(
        "IP Address : ");

    Serial.println(
        WiFi.localIP());

    // --------------------------------------------------
    // WAKTU
    // --------------------------------------------------

    configTime(
        7 * 3600,
        0,
        "pool.ntp.org",
        "time.nist.gov");

    delay(1000);

    // --------------------------------------------------
    // SSL
    // --------------------------------------------------

    ssl_client.setInsecure();

    // --------------------------------------------------
    // FIREBASE
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "Menghubungkan Firebase...");

    initializeApp(
        aClient,
        app,
        getAuth(user_auth),
        120 * 1000);

    app.getApp<
        Firestore::Documents>(
        Docs);

    while (
        !app.ready())
    {
        app.loop();

        delay(100);
    }

    firebaseReady = true;

    Serial.println();
    Serial.println(
        "FIREBASE TERHUBUNG.");

    // --------------------------------------------------
    // FINGERPRINT
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "Menginisialisasi fingerprint...");

    if (
        inisialisasiFingerprint())
    {
        Serial.println(
            "Fingerprint SIAP.");
    }
    else
    {
        Serial.println(
            "Fingerprint TIDAK TERDETEKSI.");

        Serial.println(
            "Periksa TX/RX/GND.");
    }

    // --------------------------------------------------
    // READY
    // --------------------------------------------------

    Serial.println();
    Serial.println(
        "======================================");

    Serial.println(
        " SISTEM SIAP");

    Serial.println(
        "======================================");

    Serial.println(
        "Enrollment dikendalikan melalui Flutter.");

    Serial.println(
        "a + Enter = autentikasi fingerprint");

    Serial.println(
        "======================================");
}

// ======================================================
// LOOP
// ======================================================

void loop()
{
    app.loop();

    Docs.loop();

    if (
        !app.ready())
    {
        delay(10);

        return;
    }

    // --------------------------------------------------
    // AUTENTIKASI
    // --------------------------------------------------

    if (
        modeAutentikasi)
    {
        prosesAutentikasi();

        return;
    }

    // --------------------------------------------------
    // COMMAND DARI FLUTTER
    // --------------------------------------------------

    cekPerintahEnrollment();

    // --------------------------------------------------
    // SERIAL TEST
    // --------------------------------------------------

    if (
        Serial.available())
    {
        char perintah =
            Serial.read();

        if (
            perintah == 'a' ||
            perintah == 'A')
        {
            mulaiAutentikasi();
        }
    }

    delay(10);
}