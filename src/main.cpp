#include <Arduino.h>
#include <Ticker.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>
#include <RtcDS3231.h>

#define WIFI_SSID ""
#define WIFI_PWD ""
#define DHTPIN D4
#define DHTTYPE DHT22 /* DHT11, DHT22, DHT21 */
#define TICKER_PERIOD 600
#define TIME_ZONE 9
#define MAX_DATA_SIZE 144

typedef struct
{
  char strTime[20];
  float temperature;
  float humidity;
} TempHumidity;

int dataIndex = 0;
TempHumidity data[MAX_DATA_SIZE];

bool tickerFlag = true;
Ticker ticker;
ESP8266WebServer server(80);
DHT_Unified dht(DHTPIN, DHTTYPE);
RtcDS3231<TwoWire> Rtc(Wire);

void tickerHandler(void);
void initWiFi(void);
void initRTC(void);
void initDHT(void);
void initWebServer(void);
void gatherData(void);
void sendCurrentTemp(void);
void sendDailyTemp(void);
void sendCurrentHumidity(void);
void sendDailyHumidity(void);

void setup()
{
  Serial.begin(115200);
  
  initWiFi();
  initRTC();
  initDHT();
  initWebServer();

  ticker.attach(TICKER_PERIOD, tickerHandler);
}

void loop()
{
  server.handleClient();

  if (tickerFlag)
  {
    tickerFlag = false;
    gatherData();    
  }
}

void tickerHandler()
{
  tickerFlag = true;
}

void initWiFi()
{
  Serial.println("Initialize WiFi...");

  if (!WiFi.begin(WIFI_SSID, WIFI_PWD))
  {
    Serial.println("ERROR: WiFi.begin");
    return;
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("OK: WiFi connected");
  Serial.print("IP address: "); 
  Serial.println(WiFi.localIP());

  delay(1000);
}

void initRTC()
{
  WiFiUDP udp;
  NTPClient timeClient(udp, "pool.ntp.org", TIME_ZONE * 3600);
  
  Serial.println("Getting NTP time...");
  timeClient.update();

  /* 2000 - 1970 = 30 years = 946684800UL sec */
  unsigned long epochTime = timeClient.getEpochTime() - 946684800UL;
  
  Rtc.Begin();
  Rtc.SetDateTime(epochTime);

  if (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
  }
}

void initDHT()
{
  /* Power off & on DHT22 */
  digitalWrite(D5, HIGH);
  pinMode(D5, OUTPUT);
  delay(2000);

  /* Initialize DHT22 */
  dht.begin();
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Temperature");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);
  
  dht.humidity().getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.println("Humidity");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);  
}

void gatherData()
{ 
  /* When array is full, remove oldest data */
  if(dataIndex >= MAX_DATA_SIZE)
  {
    for(int i = 1; i < MAX_DATA_SIZE; i++)
    {
      data[i-1] = data[i];
    }
    dataIndex = MAX_DATA_SIZE - 1;
  }
    
  sensors_event_t event;

  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) return;
  data[dataIndex].temperature = event.temperature;

  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) return;
  data[dataIndex].humidity = event.relative_humidity;

  if (!Rtc.IsDateTimeValid()) return;  
  RtcDateTime now = Rtc.GetDateTime();
  sprintf(data[dataIndex].strTime, "%04d-%02d-%02d %02d:%02d:%02d", //%d allows to print an integer to the string
        now.Year(),                            //get year method
        now.Month(),                           //get month method
        now.Day(),                             //get day method
        now.Hour(),                            //get hour method
        now.Minute(),                          //get minute method
        now.Second()                           //get second method
  );
  
  dataIndex++;  
}

void initWebServer()
{
  Serial.println("Initialize SPIFFS...");
  
  if (!SPIFFS.begin())
  {
    Serial.println("ERROR: SPIFFS.begin");
    return;
  }
  
  server.on("/currenttemp", sendCurrentTemp);
  server.on("/currenthumid", sendCurrentHumidity);  
  server.on("/dailytemp", sendDailyTemp);
  server.on("/dailyhumid", sendDailyHumidity);  

  server.serveStatic("/img", SPIFFS, "/img");
  server.serveStatic("/", SPIFFS, "/index.html");

  Serial.println("Initialize Web Server...");
  server.begin();
}

void sendCurrentTemp()
{
  Serial.println("http://[YOUR IP]/currenttemp is called");

  if (dataIndex > 0)
  {
    TempHumidity th = data[dataIndex - 1];

    String json = "{\"time\":\"" + String(th.strTime) + "\",";
    json += "\"temperature\":\"" + String(th.temperature, 1) + "\"}";

    server.send(200, "application/json", json);
  }
  else
  {
    server.send(500, "application/json", "");
  }
}

void sendCurrentHumidity()
{
  Serial.println("http://[YOUR IP]/currenthumid is called");

  if (dataIndex > 0)
  {
    TempHumidity th = data[dataIndex - 1];

    String json = "{\"time\":\"" + String(th.strTime) + "\",";  
    json += "\"humidity\":\"" + String(th.humidity, 1) + "\"}";

    server.send(200, "application/json", json);
  }
  else
  {
    server.send(500, "application/json", "");
  }
}

void sendDailyTemp()
{
  Serial.println("http://[YOUR IP]/dailytemp is called");

  if (dataIndex > 0)
  {
    String json = "[";
    for(int i = 0; i < dataIndex; i++)
    {
      TempHumidity th = data[i];
      json += "{\"time\":\"" + String(th.strTime) + "\",";
      json += "\"temperature\":\"" + String(th.temperature, 1) + "\"},";
    }
    json.remove(json.lastIndexOf(','));
    json += "]";
    server.send(200, "application/json", json);
  }
  else
  {
    server.send(500, "application/json", "");
  }
}

void sendDailyHumidity()
{
  Serial.println("http://[YOUR IP]/dailyhumid is called");
 
  if (dataIndex > 0)
  {
    String json = "[";
    for(int i = 0; i < dataIndex; i++)
    {
      TempHumidity th = data[i];
      json += "{\"time\":\"" + String(th.strTime) + "\",";
      json += "\"humidity\":\"" + String(th.humidity, 1) + "\"},";
    }
    json.remove(json.lastIndexOf(','));
    json += "]";
    server.send(200, "application/json", json);
  }
  else
  {
    server.send(500, "application/json", "");
  }
}