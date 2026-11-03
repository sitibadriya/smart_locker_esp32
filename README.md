# Smart Locker IoT (ESP32) — Locker 1 dan Locker 2

Satu kode (`src/main.cpp`) dipakai untuk semua locker. Yang membedakan hanya
`ID_PERANGKAT`, `ID_LOCKER`, dan port COM, yang diatur di `platformio.ini`.

## 1. Struktur proyek

```
smart_locker_final/
├── platformio.ini            environment locker01 dan locker02
├── .gitignore
├── include/
│   ├── fingerprint.h
│   └── secrets.example.h     salin menjadi secrets.h lalu isi
└── src/
    ├── main.cpp              logika utama (Firebase, relay, pintu, perintah)
    └── fingerprint.cpp       sensor sidik jari
```

## 2. Perangkat per locker

Tiap locker berdiri sendiri: 1 ESP32, 1 sensor sidik jari, 1 modul relay (active-LOW),
1 solenoid lock 12V, 1 power supply 12V, dan opsional 1 sensor pintu YL-99.
Jangan berbagi satu sensor sidik jari untuk dua locker, karena template tersimpan di sensornya.

## 3. Pinout

| Fungsi               | Pin ESP32     | Keterangan                            |
| -------------------- | ------------- | ------------------------------------- |
| Sensor sidik jari TX | GPIO 16 (RX2) | TX sensor ke RX ESP32                 |
| Sensor sidik jari RX | GPIO 17 (TX2) | RX sensor ke TX ESP32                 |
| Relay IN             | GPIO 18       | VCC relay ke VIN/5V, GND relay ke GND |
| Sensor pintu OUT     | GPIO 4        | VCC ke 3V3, GND ke GND                |

Kabel beban: power supply (+) → **COM** relay, **NO** relay → solenoid (+),
solenoid (−) → power supply (−). Pasang dioda 1N4007 paralel di solenoid
(garis ke kutub +). Hubungkan GND ESP32, relay, dan power supply.

Perilaku solenoid (alur sama di kedua locker, tanpa timer):

1. Standby: pintu tertutup, solenoid terkunci (batang keluar).
2. Akses diterima (sidik jari valid atau remote unlock): solenoid terbuka (batang masuk).
3. Sensor mendeteksi pintu dibuka.
4. Sensor mendeteksi pintu ditutup: solenoid terkunci otomatis.

Locker yang tidak punya sensor pintu bisa memakai mode waktu tetap (`AKTIFKAN_SENSOR_PINTU=false`,
solenoid terbuka 10 detik).

Peringatan: pada mode sensor tidak ada batas waktu. Bila sensor pintu tidak mendeteksi perubahan
(rusak, kabel lepas, atau posisi salah), solenoid tetap terbuka dan locker tidak terkunci lagi sampai
ESP32 di-restart. Pastikan sensor terbukti berubah saat pintu dibuka dan ditutup sebelum dipasang
permanen. Untuk pengaman, aktifkan `-DBATAS_MAKS_SOLENOID_MS=60000`.

## 4. Persiapan Firestore

Koleksi yang dipakai: `locker-config`, `enrollment-commands`, `fingerprint-mapping`, `locker-activity`.

Buat dokumen berikut **sekali** untuk tiap locker (contoh L01, ulangi untuk L02):

`locker-config/L01`

| Field          | Nilai awal    |
| -------------- | ------------- |
| idLocker       | `L01`         |
| remoteCommand  | `IDLE`        |
| resetCommand   | `IDLE`        |
| activeBatchId  | `""` (kosong) |
| activeSantriId | `""` (kosong) |

Field `statusPintu` ditulis otomatis oleh ESP32 bila sensor pintu diaktifkan.

Perintah dari aplikasi Flutter:

- **Enrollment:** tulis `enrollment-commands/esp32-locker-01` dengan `command = START_ENROLL`,
  `status = pending`, `idSantri`, `namaSantri`, `idLocker = L01`, `idPerangkat = esp32-locker-01`.
- **Remote unlock:** set `locker-config/L01.remoteCommand = UNLOCK`.
- **Reset / ganti pemilik:** set `locker-config/L01.resetCommand = RESET_OWNER`.

Untuk locker 2 pakai `L02` dan `esp32-locker-02`.

Security Rules: hanya akun yang berwenang yang boleh menulis `remoteCommand`, `resetCommand`,
dan `enrollment-commands`. Jangan memakai aturan `allow read, write: if true`.

## 5. Persiapan kode

1. Salin `include/secrets.example.h` menjadi `include/secrets.h`, lalu isi Wi-Fi dan kredensial Firebase.
   File `secrets.h` sudah ada di `.gitignore`, jangan di-commit.
2. Cek port COM di `platformio.ini` (bawaan: locker01 = COM5, locker02 = COM8). Sesuaikan jika berbeda.
3. Pilih environment `locker01` atau `locker02`, lalu build dan upload.
4. Buka Serial Monitor. Banner harus menampilkan ID yang benar, misalnya
   `SMART LOCKER - esp32-locker-02 (L02)`.

Saat upload, colok hanya satu ESP32. Bila gagal: kabel USB data yang pendek, lepas relay dan sensor
dari ESP32, cabut power 12V, dan tahan tombol BOOT saat `Connecting...`.

## 6. Alur pemilik

Pemilik tiap locker dipisahkan oleh `ID_LOCKER`. Dokumen mapping berawalan `L01_` atau `L02_`,
jadi jari locker 1 tidak membuka locker 2.

**Pendaftaran pemilik baru (per locker)**

1. Kirim **reset** ke locker itu. Reset mengosongkan seluruh template di sensor locker tersebut.
2. Kirim **enrollment** dengan data santri. ESP32 meminta 5 jari secara berurutan
   (jempol, telunjuk, tengah, manis, kelingking), masing-masing dua kali scan.
3. Status enrollment di dokumen perintah berubah `processing`, lalu `completed` atau `failed`.

**Ganti pemilik:** ulangi langkah 1 dan 2. Jangan reset dari firmware locker yang salah, karena
reset menghapus semua template pada sensor yang sedang terpasang.

## 7. Fitur

| Fitur             | Perilaku                                                                                                                                                                                         |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Autentikasi       | Jari cocok dan mapping aktif: solenoid terbuka, lalu terkunci otomatis saat pintu ditutup. Selain itu ditolak dan dicatat.                                                                       |
| Enrollment 5 jari | Rollback otomatis bila gagal di tengah (template di sensor dihapus).                                                                                                                             |
| Remote unlock     | Perintah dari Flutter dibaca tiap 6 detik. Tidak terbuka berulang.                                                                                                                               |
| Reset pemilik     | Sensor dikosongkan lebih dulu, baru konfigurasi Firestore diperbarui.                                                                                                                            |
| Riwayat           | Semua akses, penolakan, remote unlock, reset, dan peringatan pintu masuk ke `locker-activity`.                                                                                                   |
| Sensor pintu      | Menentukan kapan solenoid terkunci (tanpa timer). Pintu dibuka tanpa akses sah dicatat sebagai peringatan, dan `statusPintu` dikirim ke Firestore. Sensor tidak pernah membuka locker saat boot. |
| Pemulihan         | Wi-Fi, Firebase, dan sensor sidik jari dicoba lagi otomatis bila terputus.                                                                                                                       |

Pengaturan per locker bisa ditimpa lewat `build_flags`, misalnya `-DAKTIFKAN_SENSOR_PINTU=false`
(locker tanpa sensor), `-DBATAS_MAKS_SOLENOID_MS=60000` (pengaman), atau `-DPINTU_TERTUTUP_LEVEL=HIGH`
bila sensor terbalik.
`-DBATAS_MAKS_SOLENOID_MS=90000`, atau `-DPINTU_TERTUTUP_LEVEL=HIGH` bila sensor terbalik.

## 8. Daftar tes serah terima (per locker)

1. Boot: solenoid keluar (terkunci), banner ID benar, Firebase siap, sensor sidik jari siap.
2. Reset lalu enrollment lima jari dari Flutter: status `completed`.
3. Tiap jari terdaftar: `[AKSES DITERIMA]`, solenoid masuk 10 detik lalu keluar.
4. Jari tidak terdaftar: `[AKSES DITOLAK]` dan tercatat di `locker-activity`.
5. Jari dari locker lain: ditolak.
6. Remote unlock dari Flutter: solenoid terbuka sekali.
7. Matikan Wi-Fi lalu nyalakan lagi: sistem pulih sendiri.
8. Setelah reset: jari pemilik lama ditolak.
9. Sensor pintu: saat pintu tertutup, serial menampilkan `[PINTU] Status awal: TERTUTUP`.
   Tempel jari terdaftar: solenoid terbuka. Tarik pintu: `[PINTU] Pintu TERBUKA`. Tutup pintu:
   `[PINTU] Pintu TERTUTUP` dan solenoid terkunci sendiri.
10. Buka pintu paksa tanpa akses: muncul peringatan dan tercatat di `locker-activity`.

## 9. Masalah umum

| Gejala                                 | Penyebab dan solusi                                                                                              |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Relay menyala terus                    | Modul active-LOW dengan sinyal 3,3V. Kode memakai `relayOn()`/`relayOff()`, jangan diganti `digitalWrite(HIGH)`. |
| Solenoid terbalik                      | Kabel solenoid di terminal NC. Pindahkan ke NO.                                                                  |
| `[AKSES DITOLAK]` padahal jari dikenal | Mapping di Firestore tidak aktif, atau firmware salah locker. Cek banner dan `activeBatchId`.                    |
| Perintah Flutter tidak terbaca         | Dokumen `locker-config/Lxx` belum dibuat, atau ID di Flutter tidak sama dengan firmware.                         |
| Upload gagal `chip stopped responding` | Lihat catatan upload di bagian 5.                                                                                |
| Pesan `ssl_client ... (-80)`           | Peringatan koneksi TLS menganggur, bisa diabaikan bila akses tetap diterima.                                     |

## 10. Keterbatasan (untuk dokumen SKPL)

- Bergantung pada internet dan listrik.
- Koneksi TLS memakai `setInsecure()` (sertifikat server tidak diverifikasi).
- Semua ESP32 memakai satu akun Firebase yang sama.
- Perintah remote berupa string di Firestore, perlu dilindungi Security Rules.
- Sensor pintu dan notifikasi push belum menjadi bagian inti.
