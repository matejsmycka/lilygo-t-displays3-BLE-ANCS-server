
#include "Arduino.h"
#include "TFT_eSPI.h"
#include "pin_config.h"
#include "time.h"
#include <WiFi.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLEUtils.h"
#include "Task.h"

// WiFi and NTP configurations
const char *ssid = "Smyckovi";  // Your WiFi SSID
const char *password = "smyckovi"; // Your WiFi password
const char *ntpServer = "pool.ntp.org";

String lastNotification = "";
String lastSubNotification = "";

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

void renderLastNotification()
{
    if (lastNotification == "")
    {
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
    tft.drawString(lastNotification, 20, 70);

    if (lastSubNotification != "")
    {
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        int maxChar = 22; // if more than 20 characters, split the text in two lines
        if (lastSubNotification.length() > maxChar)
        {
            tft.drawString(lastSubNotification.substring(0, maxChar), 20, 100);
            String secondLine = lastSubNotification.substring(maxChar);
            // if first char is space, remove it and also if longer than max replace with ...
            if (secondLine.charAt(0) == ' ')
            {
                secondLine = secondLine.substring(1);
            }
            if (secondLine.length() > maxChar)
            {
                secondLine = secondLine.substring(0, maxChar - 3) + "...";
            }
    
            tft.drawString(lastSubNotification.substring(maxChar), 20, 120);
        }
        else
        {
            tft.drawString(lastSubNotification, 20, 110);
        }
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
static char LOG_TAG[] = "SampleServer";
static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

uint8_t latestMessageID[4];
boolean pendingNotification = false;
boolean incomingCall = false;
uint8_t acceptCall = 0;
String dataCallbackSource = "";
String dataCallbackMessage = "";
String dataCallbackDate = "";

class MySecurity : public BLESecurityCallbacks
{

    uint32_t onPassKeyRequest()
    {
        ESP_LOGI(LOG_TAG, "PassKeyRequest");
        return 123456;
    }

    void onPassKeyNotify(uint32_t pass_key)
    {
        ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
    }

    bool onSecurityRequest()
    {
        ESP_LOGI(LOG_TAG, "On Security Request");
        return true;
    }

    bool onConfirmPIN(unsigned int)
    {
        ESP_LOGI(LOG_TAG, "On Confrimed Pin Request");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
    {
        ESP_LOGI(LOG_TAG, "Starting BLE work!");
        if (cmpl.success)
        {
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            ESP_LOGD(LOG_TAG, "size: %d", length);
        }
    }
};

static void dataSourceNotifyCallback(
    BLERemoteCharacteristic *pDataSourceCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
    // Serial.print("Notify callback for characteristic ");
    // Serial.print(pDataSourceCharacteristic->getUUID().toString().c_str());
    // Serial.print(" of data length ");
    // Serial.println(length);
    String message = "";
    for (int i = 0; i < length; i++)
    {
        if (i > 7)
        {
            // skip non-ascii characters
            if (pData[i] < 32 || pData[i] > 126)
            {
                continue;
            }

            Serial.write(pData[i]);
            message += (char)pData[i];
        }
        /*else{
            Serial.print(pData[i], HEX);
            Serial.print(" ");
        }*/
    }
    if (message.startsWith("com."))
    {
        dataCallbackSource = message;
    }
    else if (message.startsWith("20"))
    {
        dataCallbackDate = message;
    }
    else
    {
        dataCallbackMessage = message;
    }

    if (dataCallbackSource != "" && dataCallbackMessage != "" && dataCallbackDate != "")
    {
        // source has x.x.x format so we need to extract the last part
        int lastDot = dataCallbackSource.lastIndexOf(".");
        dataCallbackSource = dataCallbackSource.substring(lastDot + 1);
        handleNotify(dataCallbackSource, dataCallbackMessage);

        dataCallbackSource = "";
        dataCallbackMessage = "";
        dataCallbackDate = "";
    }

    Serial.println();
}

static void NotificationSourceNotifyCallback(
    BLERemoteCharacteristic *pNotificationSourceCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
    if (pData[0] == 0)
    {
        Serial.println("New notification!");
        // Serial.println(pNotificationSourceCharacteristic->getUUID().toString().c_str());
        latestMessageID[0] = pData[4];
        latestMessageID[1] = pData[5];
        latestMessageID[2] = pData[6];
        latestMessageID[3] = pData[7];

        switch (pData[2])
        {
        case 0:
            Serial.println("Category: Other");
            break;
        case 1:
            incomingCall = true;
            Serial.println("Category: Incoming call");
            break;
        case 2:
            Serial.println("Category: Missed call");
            break;
        case 3:
            Serial.println("Category: Voicemail");
            break;
        case 4:
            Serial.println("Category: Social");
            break;
        case 5:
            Serial.println("Category: Schedule");
            break;
        case 6:
            Serial.println("Category: Email");
            break;
        case 7:
            Serial.println("Category: News");
            break;
        case 8:
            Serial.println("Category: Health");
            break;
        case 9:
            Serial.println("Category: Business");
            break;
        case 10:
            Serial.println("Category: Location");
            break;
        case 11:
            Serial.println("Category: Entertainment");
            break;
        default:
            break;
        }
    }
    else if (pData[0] == 1)
    {
        Serial.println("Notification Modified!");
        if (pData[2] == 1)
        {
            Serial.println("Call Changed!");
        }
    }
    else if (pData[0] == 2)
    {
        Serial.println("Notification Removed!");
        if (pData[2] == 1)
        {
            Serial.println("Call Gone!");
        }
    }
    // Serial.println("pendingNotification");
    pendingNotification = true;
}

/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient : public Task
{
    void run(void *data)
    {

        BLEAddress *pAddress = (BLEAddress *)data;
        BLEClient *pClient = BLEDevice::createClient();
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_IO);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        // Connect to the remove BLE Server.
        pClient->connect(*pAddress);

        /** BEGIN ANCS SERVICE **/
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService *pAncsService = pClient->getService(ancsServiceUUID);
        if (pAncsService == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", ancsServiceUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pNotificationSourceCharacteristic = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
        if (pNotificationSourceCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", notificationSourceCharacteristicUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
        if (pControlPointCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", controlPointCharacteristicUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pDataSourceCharacteristic = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
        if (pDataSourceCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", dataSourceCharacteristicUUID.toString().c_str());
            return;
        }
        const uint8_t v[] = {0x1, 0x0};
        pDataSourceCharacteristic->registerForNotify(dataSourceNotifyCallback);
        pDataSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)v, 2, true);
        pNotificationSourceCharacteristic->registerForNotify(NotificationSourceNotifyCallback);
        pNotificationSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)v, 2, true);

        /** END ANCS SERVICE **/

        while (1)
        {
            // Serial.print("running ");
            // Serial.println(pendingNotification);
            if (pendingNotification || incomingCall)
            {
                // CommandID: CommandIDGetNotificationAttributes
                // 32bit uid
                // AttributeID
                Serial.println("Requesting details...");
                const uint8_t vIdentifier[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x0};
                pControlPointCharacteristic->writeValue((uint8_t *)vIdentifier, 6, true);
                const uint8_t vTitle[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x1, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t *)vTitle, 8, true);
                const uint8_t vMessage[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x3, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t *)vMessage, 8, true);
                const uint8_t vDate[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x5};
                pControlPointCharacteristic->writeValue((uint8_t *)vDate, 6, true);
                Serial.println(pControlPointCharacteristic->toString().c_str());

                pendingNotification = false;
            }
            delay(100); // does not work without small delay
        }

    } // run
}; // MyClient

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
    {
        Serial.println("********************");
        Serial.println("**Device connected**");
        Serial.println(BLEAddress(param->connect.remote_bda).toString().c_str());
        Serial.println("********************");
        MyClient *pMyClient = new MyClient();
        pMyClient->setStackSize(18000);
        pMyClient->start(new BLEAddress(param->connect.remote_bda));
    };

    void onDisconnect(BLEServer *pServer)
    {
        Serial.println("************************");
        Serial.println("**Device  disconnected**");
        Serial.println("************************");
    }
};

class MainBLEServer : public Task
{
    void run(void *data)
    {
        ESP_LOGD(LOG_TAG, "Starting BLE work!");
        esp_log_buffer_char(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));
        esp_log_buffer_hex(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));

        // Initialize device
        BLEDevice::init("ESP32-Notify");
        BLEServer *pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        // Advertising parameters:
        // Soliciting ANCS
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(0x01);
        _setServiceSolicitation(&oAdvertisementData, BLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"));
        pAdvertising->setAdvertisementData(oAdvertisementData);

        // Set security
        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_OUT);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        // Start advertising
        pAdvertising->start();

        ESP_LOGD(LOG_TAG, "Advertising started!");
        delay(portMAX_DELAY);
    }

    /**
     * @brief Set the service solicitation (UUID)
     * @param [in] uuid The UUID to set with the service solicitation data.  Size of UUID will be used.
     */
    void _setServiceSolicitation(BLEAdvertisementData *a, BLEUUID uuid)
    {
        char cdata[2];
        switch (uuid.bitSize())
        {
        case 16:
        {
            // [Len] [0x14] [UUID16] data
            cdata[0] = 3;
            cdata[1] = ESP_BLE_AD_TYPE_SOL_SRV_UUID; // 0x14
            a->addData(std::string(cdata, 2) + std::string((char *)&uuid.getNative()->uuid.uuid16, 2));
            break;
        }

        case 128:
        {
            // [Len] [0x15] [UUID128] data
            cdata[0] = 17;
            cdata[1] = ESP_BLE_AD_TYPE_128SOL_SRV_UUID; // 0x15
            a->addData(std::string(cdata, 2) + std::string((char *)uuid.getNative()->uuid.uuid128, 16));
            break;
        }

        default:
            return;
        }
    } // setServiceSolicitationData
};

void BLEServerClass(void)
{
    MainBLEServer *pMainBleServer = new MainBLEServer();
    pMainBleServer->setStackSize(20000);
    pMainBleServer->start();
}
void setup()
{
    Serial.begin(921600);
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

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Initialize BLE notifications
    BLEServerClass();
}

void loop()
{
    // render only if time changed else check button

    if (millis() > targetTime)
    {
        targetTime = millis() + WAIT;
        render();
    }
    updateBrightness();
    delay(100);
}