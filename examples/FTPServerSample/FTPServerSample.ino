#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined ESP32
#include <WiFi.h>
#endif

#include <ESP8266FtpServer.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASS";

#define SD_CS_PIN 5
FtpServer ftpServer; // set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial

void setup(void)
{
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ftpServer.addUser("sdfs", "password", SD_CS_PIN);     // username, password for FTP SD server
  ftpServer.addUser("littlefs", "password", NOT_A_PIN); // username, password for FTP LittleFS server
  ftpServer.begin();
}

void loop(void)
{
  ftpServer.handleFTP(); // make sure in loop you call handleFTP()!!
}