#include <Arduino.h>
#include <WiFi.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <melody_player.h>
#include <melody_factory.h>

const char *ssid = "HUAWEI nova 5T";
const char *password = "leppangang1";

#define RXD2 16
#define TXD2 17
#define ONEWIREBUS 4
#define BUZZERPIN 18
#define BUZZERCHANNEL 0

// double latitude = -3.9843201;
// double longitude = 119.6521232;
double latitude = 0;
double longitude = 0;
double suhu = 0;

TinyGPSPlus gps;

OneWire oneWire(ONEWIREBUS);

DallasTemperature sensors(&oneWire);

String notes[] = {"C4", "G3", "G3", "A3", "G3", "SILENCE", "B3", "C4"};

MelodyPlayer player(BUZZERPIN, 0, LOW);

void getLokasi();
void getSuhu();

void setup()
{
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // connect to WiFi
  // Serial.printf("Menghubungkan ke %s ...", ssid);
  // WiFi.mode(WIFI_STA);
  // WiFi.begin(ssid, password);
  // while (WiFi.status() != WL_CONNECTED)
  // {
  //   delay(500);
  //   Serial.print(".");
  // }
  // Serial.println(" Terhubung");
  // Serial.println("");
  // Serial.println("WiFi connected.");
  // Serial.println("IP address: ");
  // Serial.println(WiFi.localIP());
}

void loop()
{
  // This sketch displays information every time a new sentence is correctly encoded.
  while (Serial1.available() > 0)
    if (gps.encode(Serial1.read()))
    {
      getSuhu();
      getLokasi();
      Serial.println("----------------------------------");

      // Load and play a correct melody
      Melody melody = MelodyFactory.load("Nice Melody", 175, notes, 8);

      if (suhu > 30)
        player.play(melody);

      delay(2000);
    }

  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println(F("Tidak ada GPS yang terdeteksi: Periksa kabel."));
    while (true)
      ;
  }
}

// GPS getLokasi
void getLokasi()
{
  if (gps.location.isValid())
  {
    latitude = (gps.location.lat());
    longitude = (gps.location.lng());
  }

  Serial.printf("Latitude : %f \n", latitude);
  Serial.printf("Longitude : %f \n", longitude);
}

void getSuhu()
{
  sensors.requestTemperatures();
  suhu = sensors.getTempCByIndex(0);
  Serial.printf("Suhu : %4.2f C \n", suhu);
}