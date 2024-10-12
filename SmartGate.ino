#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <Wire.h>
#include "tiny_code_reader.h"

const char *ssidAP = "SmartGate";
const char *passwordAP = "foobar123";

const uint8_t INTERRUPT_PIN = D4;
const uint8_t RELAY_PIN = D5;
const uint8_t I2C_SCL = D6;
const uint8_t I2C_SDA = D7;

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

void checkQRCode()
{

  tiny_code_reader_results_t results = {};
  // Perform a read action on the I2C address of the sensor to get the
  // current face information detected.
  if (!tiny_code_reader_read(&results))
  {
    Serial.println("No person sensor results found on the i2c bus");
    return;
  }

  if (results.content_length == 0)
  {
    Serial.println("No code found");
  }
  else
  {
    Serial.print("Found '");
    Serial.print((char*)results.content_bytes);
    Serial.println("'\n");

    bool ok = sendHTTPRequest((char*)results.content_bytes);
    Serial.print("Ok = ");
    Serial.println(ok);

    if (ok)
    {
      openGate();
    }
  }
}

void openGate()
{
  digitalWrite(RELAY_PIN, HIGH);
  delay(1000);
  digitalWrite(RELAY_PIN, LOW);
}

void setup()
{
  EEPROM.begin(512);
  Wire.begin(I2C_SDA, I2C_SCL);

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
  }

  pinMode(INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(INTERRUPT_PIN, isr, FALLING);

  pinMode(RELAY_PIN, OUTPUT);
}

void loop()
{
  if (!wifiNetworkConfigured)
  {
    server.handleClient();
    delay(100);
  }
  else
  {

    checkQRCode();
    delay(200);
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
  
}
