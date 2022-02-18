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

FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long dataMillis = 0;

#define RXD2 16
#define TXD2 17
#define ONEWIREBUS 4
#define BUZZERPIN 18
#define BUZZERCHANNEL 0

// latitude gps
double latitude = 0.0;
// longitude gps
double longitude = 0.0;
// suhu alat
double suhu = 0.0;

/* === Variabel untuk menampung nilai looping sebelumnya === */
double prevLatitude = 0.0;
double prevLongitude = 0.0;
double prevSuhu = 0.0;
/* ========================================================= */

double latitudeRumah = 0.0;
double longitudeRumah = 0.0;
double radius = 0.0;

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
void sendNotification();
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

void streamPengaturanCallback(StreamData data)
{
  FirebaseJson *json = data.to<FirebaseJson *>();

  size_t len = json->iteratorBegin();
  FirebaseJson::IteratorValue value;

  Serial.println("Load data dari firebase");

  for (size_t i = 0; i < len; i++)
  {
    value = json->valueAt(i);

    if (value.key.equals("latitude"))
    {
      latitudeRumah = value.value.toDouble();
      Serial.printf("Latitude Rumah: %f \n", latitudeRumah);

      if (latitude == 0.0)
        latitude = latitudeRumah;
    }
    else if (value.key.equals("longitude"))
    {
      longitudeRumah = value.value.toDouble();
      Serial.printf("longitude Rumah: %f \n", longitudeRumah);

      if (longitude == 0.0)
        longitude = longitudeRumah;
    }
    else if (value.key.equals("radius"))
    {
      radius = value.value.toDouble();
      Serial.printf("radius : %4.2f M \n", radius);
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
  suhu = sensors.getTempCByIndex(0);
  Serial.printf("Suhu : %4.2f C \n", suhu);
}

void getLokasi()
{
  if (gps.location.isValid())
  {
    latitude = (gps.location.lat());
    longitude = (gps.location.lng());
  }

  Serial.printf("Latitude : %f \n", latitude);
  Serial.printf("Longitude : %f \n", longitude);

  if (latitudeRumah != 0.0 || longitudeRumah != 0.0)
  {
    double jarak = haversine(latitude, longitude, latitudeRumah, longitudeRumah);
    Serial.printf("Jarak : %4.2f M \n", jarak);
    Serial.println("----------------------------------");

    if (jarak > radius)
    {
      sendNotification();
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
    if (prevLatitude != latitude || prevLongitude != longitude || prevSuhu != suhu)
    {
      FirebaseJson json;

      json.set("latitude", latitude);
      json.set("longitude", longitude);
      json.set("suhu", suhu);
      json.set("waktu/.sv", "timestamp");

      Serial.printf("Simpan data ke firebase... %s\n", Firebase.setJSON(fbdo, "/data", json) ? "berhasil" : fbdo.errorReason().c_str());
      Serial.println("----------------------------------");

      prevLatitude = latitude;
      prevLongitude = longitude;
      prevSuhu = suhu;
    }
  }
}

void sendNotification()
{
  fbdo.fcm.setNotifyMessage("Informasi", "Kucing anda keluar dari radius! ");
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