#include <Arduino.h>
#include <WiFi.h>
#include <firebase.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <melody_player.h>
#include <melody_factory.h>
#include <ezTime.h>

const char *ssid = "HUAWEI nova 5T";
const char *password = "leppangang1";

#define PESAN_KELUAR "Kucing anda keluar dari radius!"

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
  double jarak;
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

Timezone myTZ;

void initWifi();
void initFirebase();
void getLokasi();
void getSuhu();
void sendToFirebase();
void sendNotification(const char *pesan);
void streamPengaturanCallback(FirebaseStream data);
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

  waitForSync();
  myTZ.setLocation(F("Asia/Makassar"));
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

  config.service_account.data.client_email = FIREBASE_CLIENT_EMAIL;
  config.service_account.data.project_id = FIREBASE_PROJECT_ID;
  config.service_account.data.private_key = PRIVATE_KEY;

  Firebase.reconnectWiFi(true);

  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);

  if (!Firebase.RTDB.beginStream(&stream, "/pengaturan"))
    Serial.printf("sream begin error, %s\n\n", stream.errorReason().c_str());

  Firebase.RTDB.setStreamCallback(&stream, streamPengaturanCallback, streamTimeoutCallback);
}

void streamPengaturanCallback(FirebaseStream streamData)
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
      data.radius = value.value.toDouble();
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
    data.jarak = haversine(data.latitude, data.longitude, dataPengaturan.latitude, dataPengaturan.longitude);
    Serial.printf("Jarak : %4.2f M \n", data.jarak);
    Serial.println("----------------------------------");

    if (data.jarak > dataPengaturan.radius)
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

    if (data.latitude == 0 || data.longitude == 0)
      return;

    // Jika nilai sebelumnya berbeda, maka data akan disimpan ke firebase
    if (prevData.latitude != data.latitude || prevData.longitude != data.longitude || prevData.suhu != data.suhu)
    {
      FirebaseJson json;

      json.set("fields/latitude/doubleValue", data.latitude);
      json.set("fields/longitude/doubleValue", data.longitude);
      json.set("fields/jarak/doubleValue", data.jarak);
      json.set("fields/radius/doubleValue", data.radius);
      json.set("fields/suhu/doubleValue", data.suhu);
      json.set("fields/waktu/timestampValue", myTZ.dateTime(RFC3339));

      String documentPath = "data";

      Serial.printf("Simpan data ke firebase...");

      if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), json.raw()))
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      else
        Serial.println(fbdo.errorReason());

      Serial.println("----------------------------------");

      prevData.latitude = data.latitude;
      prevData.longitude = data.longitude;
      prevData.suhu = data.suhu;
    }
  }
}

void sendNotification(const char *pesan)
{
  FCM_HTTPv1_JSON_Message msg;

  msg.topic = FCM_TOPIC;
  msg.notification.title = "Informasi";
  msg.notification.body = pesan;

  Serial.println("Kirim notifikasi... ");
  Serial.printf("%s\n", Firebase.FCM.send(&fbdo, &msg) ? "ok" : fbdo.errorReason().c_str());
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