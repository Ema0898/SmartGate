#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>

const char *ssidAP = "SmartGate";
const char *passwordAP = "foobar123";

const uint8_t INTERRUPT_PIN = D6;

IPAddress ip(192, 168, 1, 200);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);

static const int EEPROM_CHECK_OFFSET = 0;
static const int EEPROM_OFFSET = 1;
bool doReset = false;
bool doCleanEEPROM = false;
bool wifiNetworkConfigured = false;

void ICACHE_RAM_ATTR isr()
{
  doCleanEEPROM = true;
}

void writeBoolToEEPROM(int addrOffset, const bool boolToWrite)
{
  EEPROM.write(addrOffset, boolToWrite);
}

void readBoolFromEEPROM(int addrOffset, bool &boolToRead)
{
  boolToRead = EEPROM.read(addrOffset);
}

int writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);

  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }

  return addrOffset + 1 + len;
}

int readStringFromEEPROM(int addrOffset, String &strToRead)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];

  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }

  data[newStrLen] = '\0';
  strToRead = String(data);
  return addrOffset + 1 + newStrLen;
}

bool sendHTTPRequest(const String& value)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String serverPath = "http://192.168.18.19:3000/barcode_check?id=" + value;

  http.begin(client, serverPath.c_str());
  int httpResponseCode = http.PUT("");

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  http.end();

  return httpResponseCode == HTTP_CODE_NO_CONTENT;
}

void setup()
{
  EEPROM.begin(512);

 	Serial.begin(115200);

  String ssid = "";
  String password = "";

  int eepromOffset = readStringFromEEPROM(EEPROM_OFFSET, ssid);
  readStringFromEEPROM(eepromOffset, password);
  readBoolFromEEPROM(EEPROM_CHECK_OFFSET, wifiNetworkConfigured);

  Serial.println(ssid);
  Serial.println(password);
  Serial.println(wifiNetworkConfigured);

  if (!wifiNetworkConfigured)
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ip, gateway, subnet);

    while(!WiFi.softAP(ssidAP, passwordAP))
    {
      Serial.println(".");
      delay(100);
    }

    server.on("/wifi/configuration", HTTP_PUT, []() 
    {
        String ssidToConnect = server.arg(String("ssid"));
        String passwordToConnect = server.arg(String("password"));

        const bool validSSID = ssidToConnect != "";
        const bool validPassword = passwordToConnect != "";

        if (validSSID && validPassword)
        {
          int eepromOffset = writeStringToEEPROM(EEPROM_OFFSET, ssidToConnect);
          writeStringToEEPROM(eepromOffset, passwordToConnect);
          writeBoolToEEPROM(EEPROM_CHECK_OFFSET, true);
          EEPROM.commit();
          doReset = true;

          server.send(204);
        }
        else
        {
          server.send(400);
        }

    });

    server.begin();
    Serial.println("HTTP server started");
  }
  else
  {
    int retries = 0;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.print('.');
      delay(1000);

      if (retries == 60)
      {
        Serial.println("Exiting by max retires....");
        break;
      }

      retries++;
    }

    Serial.println(WiFi.status() == WL_CONNECTED ? "WIFI Connected" : "WIFI not Connected");
    bool ok = sendHTTPRequest("hola");
    Serial.print("Ok = ");
    Serial.println(ok);
  }

  pinMode(INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(INTERRUPT_PIN, isr, FALLING);
}

void loop()
{
  if (!wifiNetworkConfigured)
  {
    server.handleClient();
  }

  if (doCleanEEPROM)
  {
    writeBoolToEEPROM(EEPROM_CHECK_OFFSET, false);
    EEPROM.commit();
    delay(100);
    doReset = true;
  }

  if (doReset)
  {
    ESP.reset();
  }

  delay(100);
}
