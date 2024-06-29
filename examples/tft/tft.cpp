#include "Arduino.h"
#include "TFT_eSPI.h"
#include "pin_config.h"
#include "time.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// WiFi and NTP configurations
const char *ssid = "Smyckovi";
const char *password = "smyckovi";
const char *ntpServer = "pool.ntp.org";

String lastNotification = "";
String lastSubNotification = "";

AsyncWebServer server(80);

const long gmtOffset_sec = 3600; // GMT+1
const int daylightOffset_sec = 3600;

const int brightnessLevels[] = {0, 10, 20, 40, 80, 160, 255};
int currentBrightness = 4;

#define LCD_MODULE_CMD_1
TFT_eSPI tft = TFT_eSPI();

#define WAIT 10000
unsigned long targetTime = 0;

WiFiClient client;

void setBrightness(int level)
{
    ledcWrite(0, brightnessLevels[level]);
}


void showBrightnessDots()
{
    for (int i = 0; i < currentBrightness; i++)
    {
        tft.fillCircle(304, 30 + (i * 8), 2, TFT_WHITE);
    }
}

void renderLastNotification() {
    if (lastNotification == "") {
        return;
    }

   // GNOME LIKE NOTIFICATIONS

   // background
    tft.fillRect(0, 200, 320, 40, TFT_DARKGREY);
    // orange bell
    tft.fillCircle(10, 220, 5, TFT_ORANGE);
    tft.fillTriangle(10, 220, 10, 225, 15, 222, TFT_ORANGE);
    // white bell

    // text
    tft.setTextColor(TFT_ORANGE);
    tft.setTextFont(1);
    tft.setTextSize(3);
    tft.drawString(lastNotification, 20, 80);

    if (lastSubNotification != "") {
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.drawString(lastSubNotification, 20, 110);
    }

    

}


bool getTime(struct tm &timeinfo)
{
    if (!getLocalTime(&timeinfo))
    {
        return false;
    }
    return true;
}
void displayTime()
{
    struct tm timeinfo;
    if (!getTime(timeinfo))
    {
        return;
    }

    char timeHour[3];
    char timeMin[3];
    char timeSec[3];
    char day[3];
    char month[10];
    char year[5];
    char timeWeekDay[10];

    strftime(timeHour, sizeof(timeHour), "%H", &timeinfo);
    strftime(timeMin, sizeof(timeMin), "%M", &timeinfo);
    strftime(timeSec, sizeof(timeSec), "%S", &timeinfo);
    strftime(day, sizeof(day), "%d", &timeinfo);
    strftime(month, sizeof(month), "%B", &timeinfo);
    strftime(year, sizeof(year), "%Y", &timeinfo);
    strftime(timeWeekDay, sizeof(timeWeekDay), "%A", &timeinfo);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(1);
    tft.setTextSize(4);
    tft.drawString(String(timeHour) + ":" + String(timeMin), 90, 10);
}
void render()
{

    displayTime();
    showBrightnessDots();
    renderLastNotification();
}
void handleNotify(String message, String submessage)
{
    lastNotification = message;
    lastSubNotification = submessage;
    render();
}

void updateBrightness()
{
    static int debounce = 0;
    if (digitalRead(0) == LOW)
    {
        if (debounce == 0)
        {
            debounce = 1;
            currentBrightness = (currentBrightness + 1) % 7;
            setBrightness(currentBrightness);
            
           render();
        }
    }
    else
    {
        debounce = 0;
    }

    
}
void setup()
{
    // Initialize pins
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    pinMode(0, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);

    // Initialize display
    tft.begin();
    tft.setRotation(3);
    tft.setSwapBytes(true);
    delay(2000);
    delay(50);

    // Initialize LEDC for brightness control
    ledcSetup(0, 10000, 8);
    ledcAttachPin(38, 0);
    setBrightness(currentBrightness);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }

    server.on("/notify", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        String message = request->arg("message");
        String submessage = request->arg("submessage");
        handleNotify(message, submessage);
        request->send(200); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/plain", "Working!"); });

    // Configure time from NTP server
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    server.begin();
}

void loop()
{
    //render only if time changed else check button

    if (millis() > targetTime)
    {
        targetTime = millis() + WAIT;
        render();
    }
    updateBrightness();
    delay(100);
}
