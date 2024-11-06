#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

const char *ssidAP = "SmartGate";
const char *passwordAP = "foobar123";

const uint8_t INTERRUPT_PIN = D4;
const uint8_t RELAY_PIN = D5;
const uint8_t ANALOG_PIN = A0;
const uint8_t I2C_SCL = D6;
const uint8_t I2C_SDA = D7;

const int MAX_ANALOG_VALUE = 1024;
const int TOLERANCE = 10;
const int THESHOLDS[16] = { 200, 149, 90, 29, 370, 340, 298, 261, 484, 463, 437, 411, 565, 550, 532, 513 };
const char KEYPAD_VALUES[16] = { '1', '2', '3', 'A', '4', '5', '6', 'B', '7', '8', '9', 'C', '*', '0', '#', 'D' };

IPAddress ip(192, 168, 1, 200);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);

static const int EEPROM_CHECK_OFFSET = 0;
static const int EEPROM_OFFSET = 1;
bool doReset = false;
bool doCleanEEPROM = false;
bool wifiNetworkConfigured = false;

int buffer_pointer = -1;
char buffer[16];

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

bool sendHTTPRequest(const char* value)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String serverPath = "http://192.168.18.204:3000/barcode_check?id=" + String(value);
  Serial.print(serverPath);
  Serial.print(":");

  http.begin(client, serverPath.c_str());
  int httpResponseCode = http.PUT("");

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  http.end();

  return httpResponseCode == HTTP_CODE_NO_CONTENT;
}

void checkKeyPad()
{
  int adcValue = analogRead(ANALOG_PIN); /* Read the Analog Input value */

  if (adcValue == MAX_ANALOG_VALUE)
  {
    return;
  }

  for (int i = 0; i < 16; ++i)
  {
    if (abs(adcValue - THESHOLDS[i]) < TOLERANCE)
    {
      updateBuffer(KEYPAD_VALUES[i]);

      while (analogRead(ANALOG_PIN) < 1000)
      {
        delay (100);
      }
    }
  }
}

void updateBuffer(const char key)
{
  if (key == '*')
  {
    // We need to add 2 for the null termination and because buffer pointer starts at 0
    char subBuffer[buffer_pointer + 2];
    memcpy(subBuffer, buffer, (buffer_pointer + 1) * sizeof(char));
    subBuffer[buffer_pointer + 1] = '\0';
    sendHTTPRequest(subBuffer);

    buffer_pointer = -1;
    memset(buffer, 0, 16 * sizeof(char));
    lcd.clear();
  }
  else
  {
    buffer_pointer++;
    buffer[buffer_pointer] = key;
  }
}

void displayKeys()
{
  if (buffer_pointer > -1)
  {
    lcd.setCursor(buffer_pointer, 0); // move cursor to (column_cursor, 0)
    lcd.print(buffer[buffer_pointer]);
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

  lcd.init(); // Initialize the LCD I2C display
  lcd.backlight();
  lcd.clear();

  memset(buffer, 0, 16 * sizeof(char));

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
    checkKeyPad();
    displayKeys();
    delay(100);
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
