#include <ESP8266WiFi.h> //Библиотека для работы с WIFI
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h> // Библиотека для OTA-прошивки
#include <PubSubClient.h>
#include <TimerMs.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_ADS1X15.h>
#include <BH1750.h>
#include "Adafruit_VEML6075.h"

Adafruit_VEML6075 uv = Adafruit_VEML6075();
BH1750 lightMeter(0x23);
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_ADS1015 ads;
// (период, мс), (0 не запущен / 1 запущен), (режим: 0 период / 1 таймер)
TimerMs OTA_Wifi(10, 1, 0);
TimerMs wind_data(7500, 1, 0);
const char *ssid = "Beeline";              // Имя точки доступа WIFI
const char *name_client = "solar-wind";    // Имя клиента и сетевого порта для ОТА
const char *password = "sl908908908908sl"; // пароль точки доступа WIFI
const char *mqtt_server = "192.168.1.221";
const char *mqtt_reset = "solar-wind_reset"; // Имя топика для перезагрузки
String s;
int data, grafig = 0, wind_sr_tik = 0;
float wind_sr = 0;
unsigned long wind_tik = 0, t2 = 0, wind_raw = 0;
uint8_t pinD, pinLock = 0;

void callback(char *topic, byte *payload, unsigned int length) // Функция Приема сообщений
{
  String s = ""; // очищаем перед получением новых данных
  for (unsigned int i = 0; i < length; i++)
  {
    s = s + ((char)payload[i]); // переводим данные в String
  }

  int data = atoi(s.c_str()); // переводим данные в int
  // float data_f = atof(s.c_str()); //переводим данные в float
  if ((String(topic)) == mqtt_reset && data == 1)
  {
    ESP.restart();
  }
}

void wi_fi_con()
{
  WiFi.mode(WIFI_STA);
  WiFi.hostname(name_client); // Имя клиента в сети
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.print("stop");
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.setHostname(name_client); // Задаем имя сетевого порта
  ArduinoOTA.begin();                  // Инициализируем OTA
}

void publish_send(const char *top, float &ex_data, unsigned char prec) // Отправка Показаний с сенсоров
{
  char send_mqtt[10];
  dtostrf(ex_data, -2, prec, send_mqtt);
  client.publish(top, send_mqtt, 1);
}

void loop()
{
  pinD = digitalRead(D7);
  if (!pinD && pinLock)
  {
    delay(5);
    pinLock = pinD;
    t2 = millis();
  }

  if (pinD && !pinLock)
  {
    delay(5);
    pinLock = pinD;

    unsigned long wind_time = 0;
    wind_time = millis() - t2;
    if (wind_time < 3500 && wind_time > 80)
    {
      wind_tik++;
      wind_raw = wind_raw + wind_time;
    }
  }

  if (wind_data.tick())
  {
    int wind = 0;
    if (wind_raw > 0 && wind_tik < 30)
    {
      wind = wind_raw / wind_tik;
    }
    float wind_send = wind;
    float wind_m = 0;
    if (wind_send > 0)
    {
      wind_m = 1900 / wind_send; // в метры секунды
      wind_sr = wind_sr + wind_m;
      wind_sr_tik++;
    }
    publish_send("wind_t", wind_send, 0); // время одного оборота
    publish_send("wind", wind_m, 1);      // метры секунды
    float lux1 = lightMeter.readLightLevel();
    publish_send("lux", lux1, 0);
    // 77 - "0"
    float lux_analog = ads.readADC_SingleEnded(1);
    // lux_analog=map(lux_analog,77,xx,0,xxx);
    publish_send("lux_analog", lux_analog, 0);
    float UV = uv.readUVI();
    publish_send("UV", UV, 1);

    grafig++;
    if (grafig == 80 || grafig == 160)
    {
      wind_sr = wind_sr / wind_sr_tik;
      publish_send("wind_graf", wind_sr, 1);
      publish_send("lux_grfig", lux1, 1);
      wind_sr = 0;
      wind_sr_tik = 0;
      if (grafig == 160)
      {
        grafig = 0;
      }
    }

    wind_tik = 0;
    wind_raw = 0;
  }

  if (OTA_Wifi.tick()) // Поддержание "WiFi" и "OTA"  и Пинок :) "watchdog" и подписка на "Топики Mqtt"
  {
    ArduinoOTA.handle();     // Всегда готовы к прошивке
    client.loop();           // Проверяем сообщения и поддерживаем соедениние
    if (!client.connected()) // Проверка на подключение к MQTT
    {
      while (!client.connected())
      {
        // ESP.wdtFeed();                   // Пинок :) "watchdog"
        if (client.connect(name_client)) // имя на сервере mqtt
        {
          client.subscribe(mqtt_reset);  // подписались на топик "ESP8_test_reset"
          client.subscribe(name_client); // подписались на топик
          // Отправка IP в mqtt
          char IP_ch[20];
          String IP = (WiFi.localIP().toString().c_str());
          IP.toCharArray(IP_ch, 20);
          client.publish(name_client, IP_ch);
        }
        else
        {
          delay(5000);
        }
      }
    }
  }
}

void setup()
{
  ads.begin();
  ads.setGain(GAIN_FOUR);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  uv.begin();
  pinMode(D7, INPUT);
  digitalWrite(D7, HIGH);
  wi_fi_con();
  Serial.begin(9600);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  // ESP.wdtDisable();   // Активация watchdog
}
