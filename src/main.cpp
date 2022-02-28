#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <melody_player.h>
#include <melody_factory.h>

const char *ssid = "HUAWEI nova 5T";
const char *password = "leppangang1";

#define API_KEY "AIzaSyCtaOal6QAq1P2-iiuo0zmLUM-Gg7zMDi4"
#define DATABASE_URL "kucing1-72e99-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define USER_EMAIL "esp32@test.com"
#define USER_PASSWORD "kucing"

#define FCM_SERVER_KEY "AAAAjvzAVEE:APA91bEWSEOGk-fOtfB2w7jU7CBt8cNL-9jc7wxURWKRNnGGVBFvwuQSNxR3gCINCOXzUTaxOFUAu_vtSnGX7-883FNxYNqTp0WuI9thThUmO0iYLlYRnm3qsSSBkla6nbEb832XZXTo"
#define FCM_TOPIC "esp32"

#define PESAN_KELUAR "Kucing anda keluar dari radius!"
#define PESAN_SUHU_MIN "Suhu kucing anda terlalu dingin!"
#define PESAN_SUHU_MAX "Suhu kucing anda terlalu panas!"

#define SUHU_MIN 37
#define SUHU_MAX 39

FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long dataMillis = 0;

#define RXD2 16
#define TXD2 17
#define ONEWIREBUS 25
#define BUZZERPIN 26

struct Data
{
  double latitude;
  double longitude;
  double suhu;
  double radius;
};

// Data lokasi GPS
Data data;
// Data lokasi sebelumnya
Data prevData;
// Data lokasi pengaturan
Data dataPengaturan;

TinyGPSPlus gps;

OneWire oneWire(ONEWIREBUS);
DallasTemperature sensors(&oneWire);

String notes[] = {"C4", "G3", "G3", "A3", "G3", "SILENCE", "B3", "C4"};
MelodyPlayer player(BUZZERPIN, 0, LOW);
Melody melody = MelodyFactory.load("Nice Melody", 175, notes, 8);

void initWifi();
void initFirebase();
void getLokasi();
void getSuhu();
void sendToFirebase();
void sendNotification(const char *pesan);
void streamPengaturanCallback(StreamData data);
void streamTimeoutCallback(bool timeout);
double haversine(double lat1, double lon1,
                 double lat2, double lon2);

void setup()
{
  Serial.begin(9600);
  // init hardwareserial gps
  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);

  initWifi();
  initFirebase();
}

void loop()
{
  while (Serial1.available() > 0)
    if (gps.encode(Serial1.read()))
    {
      getSuhu();
      getLokasi();
      sendToFirebase();

      delay(5000);
    }

  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println(F("Tidak ada GPS yang terdeteksi: Periksa kabel."));
    while (true)
      ;
  }
}

void initWifi()
{
  // connect to WiFi
  Serial.printf("Menghubungkan ke %s ...", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Terhubung");
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void initFirebase()
{
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.reconnectWiFi(true);

  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);

  fbdo.fcm.begin(FCM_SERVER_KEY);
  fbdo.fcm.setTopic(FCM_TOPIC);
  fbdo.fcm.setPriority("high");
  fbdo.fcm.setTimeToLive(1000);

  if (!Firebase.beginStream(stream, "/pengaturan"))
    Serial.printf("sream begin error, %s\n\n", stream.errorReason().c_str());

  Firebase.setStreamCallback(stream, streamPengaturanCallback, streamTimeoutCallback);
}

void streamPengaturanCallback(StreamData streamData)
{
  FirebaseJson *json = streamData.to<FirebaseJson *>();

  size_t len = json->iteratorBegin();
  FirebaseJson::IteratorValue value;

  Serial.println("Load data dari firebase");

  for (size_t i = 0; i < len; i++)
  {
    value = json->valueAt(i);

    if (value.key.equals("latitude"))
    {
      dataPengaturan.latitude = value.value.toDouble();
      Serial.printf("Latitude Rumah: %f \n", dataPengaturan.latitude);

      if (data.latitude == 0.0)
        data.latitude = dataPengaturan.latitude;
    }
    else if (value.key.equals("longitude"))
    {
      dataPengaturan.longitude = value.value.toDouble();
      Serial.printf("longitude Rumah: %f \n", dataPengaturan.longitude);

      if (data.longitude == 0.0)
        data.longitude = dataPengaturan.longitude;
    }
    else if (value.key.equals("radius"))
    {
      dataPengaturan.radius = value.value.toDouble();
      Serial.printf("radius : %4.2f M \n", dataPengaturan.radius);
    }
  }
  Serial.println("----------------------------------");

  json->iteratorEnd();
  json->clear();
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
    Serial.println("stream timed out, resuming...\n");

  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}

void getSuhu()
{
  sensors.requestTemperatures();
  data.suhu = sensors.getTempCByIndex(0);
  Serial.printf("Suhu : %4.2f C \n", data.suhu);

  if (data.suhu < SUHU_MIN)
    sendNotification(PESAN_SUHU_MIN);
  else if (data.suhu > SUHU_MAX)
    sendNotification(PESAN_SUHU_MAX);
}

void getLokasi()
{
  if (gps.location.isValid())
  {
    data.latitude = gps.location.lat();
    data.longitude = gps.location.lng();
  }

  Serial.printf("Latitude : %f \n", data.latitude);
  Serial.printf("Longitude : %f \n", data.longitude);

  if (dataPengaturan.latitude != 0.0 || dataPengaturan.longitude != 0.0)
  {
    double jarak = haversine(data.latitude, data.longitude, dataPengaturan.latitude, dataPengaturan.longitude);
    Serial.printf("Jarak : %4.2f M \n", jarak);
    Serial.println("----------------------------------");

    if (jarak > dataPengaturan.radius)
    {
      sendNotification(PESAN_KELUAR);
      player.play(melody);
    }
  }
  else
    Serial.println("----------------------------------");
}

void sendToFirebase()
{
  if (millis() - dataMillis > 5000 && Firebase.ready())
  {
    dataMillis = millis();

    // Jika nilai sebelumnya berbeda, maka data akan disimpan
    if (prevData.latitude != data.latitude || prevData.longitude != data.longitude || prevData.suhu != data.suhu)
    {
      FirebaseJson json;

      json.set("latitude", data.latitude);
      json.set("longitude", data.longitude);
      json.set("suhu", data.suhu);
      json.set("waktu/.sv", "timestamp");

      Serial.printf("Simpan data ke firebase... %s\n", Firebase.setJSON(fbdo, "/data", json) ? "berhasil" : fbdo.errorReason().c_str());
      Serial.println("----------------------------------");

      prevData.latitude = data.latitude;
      prevData.longitude = data.longitude;
      prevData.suhu = data.suhu;
    }
  }
}

void sendNotification(const char *pesan)
{
  fbdo.fcm.setNotifyMessage("Informasi", pesan);
  Serial.println("Kirim notifikasi... ");
  Serial.printf("%s\n", Firebase.sendTopic(fbdo) ? "ok" : fbdo.errorReason().c_str());
  Serial.println("----------------------------------");
}

double haversine(double lat1, double lon1,
                 double lat2, double lon2)
{
  double m_pi = 3.1415926536;

  double dLat = (lat2 - lat1) *
                m_pi / 180.0;
  double dLon = (lon2 - lon1) *
                m_pi / 180.0;

  lat1 = (lat1)*m_pi / 180.0;
  lat2 = (lat2)*m_pi / 180.0;

  double a = pow(sin(dLat / 2), 2) +
             pow(sin(dLon / 2), 2) *
                 cos(lat1) * cos(lat2);
  double rad = 6371;
  double c = 2 * asin(sqrt(a));
  return (rad * c) * 1000; // ubah ke meter
}