#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "MAX30105.h"
#include <TinyGPS++.h>
#include <base64.h>
#include <AdafruitIO_WiFi.h>

// Initialize Modules
MAX30105 particleSensor;
TinyGPSPlus gps;

// GPS Module Pins
#define RXD2 16
#define TXD2 17

// Buzzer & Button
#define BUZZER_PIN 4
#define BUTTON_PIN 5

// WiFi Credentials
const char* ssid = "";
const char* password = "";

// Twilio Credentials  
const char* twilio_account_sid = "";
const char* twilio_auth_token = "";
const char* twilio_from_number = "";
const char* emergency_contact = "";  // For Call
const char* emergency_sms_contact = "";  // For SMS

// OpenCage API Key
const char* opencage_api_key = "";

// Adafruit IO Configuration
#define IO_USERNAME ""
#define IO_KEY ""
#define WIFI_SSID ""
#define WIFI_PASS ""

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Set up Adafruit IO feeds
AdafruitIO_Feed *heartRateFeed = io.feed("heart-rate");
AdafruitIO_Feed *locationFeed = io.feed("location");

// Global Variables
HardwareSerial gpsSerial(1);
bool alertTriggered = false;
unsigned long lastCallTime = 0; // To prevent call spamming
unsigned long lastAdafruitUpdate = 0; // To throttle Adafruit updates

void setup() {
    Serial.begin(115200);
    
    // Initialize WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    // Connect to Adafruit IO
    io.connect();
    while(io.status() < AIO_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println();
    Serial.println(io.statusText());

    // Initialize MAX30102
    Wire.begin();
    if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
        Serial.println("MAX30102 Not Found!");
        while (1);
    }
    particleSensor.setup();

    // Initialize GPS
    gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
    
    // Initialize Buzzer & Button
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
    io.run(); // Maintains connection to Adafruit IO
    
    updateGPS();
    
    float heartRate = readHeartRate();
    Serial.print("Heart Rate: "); Serial.println(heartRate);

    // Send data to Adafruit IO every 5 seconds
    if (millis() - lastAdafruitUpdate > 10000) {
        if (heartRate > 0) {
            heartRateFeed->save(heartRate);
            Serial.println("Sent heart rate to Adafruit IO");
        }
        
        lastAdafruitUpdate = millis();
    }

    if (millis() - lastAdafruitUpdate > 10000) {
       if (gps.location.isValid()) {
            String location = String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
            locationFeed->save(location.c_str());
            Serial.println("Sent location to Adafruit IO");
        }
        
        lastAdafruitUpdate = millis();
    }



    if (heartRate > 65) {  
        alertTriggered = false;
    }

    if (heartRate > 0 && heartRate < 65 && !alertTriggered) {  
        alertDriver();  

        unsigned long startTime = millis();  
        bool canceled = false;  

        while (millis() - startTime < 5000) {  
            if (digitalRead(BUTTON_PIN) == LOW) {  
                delay(200);  
                if (digitalRead(BUTTON_PIN) == LOW) {  
                    Serial.println("Alert Canceled by Driver!");  
                    alertTriggered = false;  
                    canceled = true;  
                    break;  
                }  
            }  
        }  

        if (!canceled) {  
            if ((millis() - lastCallTime) > 120000 || lastCallTime == 0) {  
                if (sendEmergencyAlert()) {  
                    lastCallTime = millis();  
                    alertTriggered = true;  
                }  
            } else {  
                Serial.println("Emergency alert skipped (Cooldown Active)");  
            }  
        }  
    }

    delay(2000);  
}

void updateGPS() {
    while (gpsSerial.available()) {
        gps.encode(gpsSerial.read());
    }
}

float readHeartRate() {
    long irValue = particleSensor.getIR();
    if (irValue < 50000) {
        Serial.println("No Finger Detected!");
        return -1;
    }

    float heartRate = particleSensor.getIR() / 1000.0; // Mock Calculation
    return (heartRate < 200) ? heartRate : -1; 
}

void alertDriver() {
    Serial.println("Critical Heart Rate Detected! Alerting Driver...");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(5000);
    digitalWrite(BUZZER_PIN, LOW);
}

bool sendEmergencyAlert() {
    if (!gps.location.isValid()) {
        Serial.println("GPS Signal Lost! Sending Last Known Location...");
    }

    float latitude = gps.location.lat();
    float longitude = gps.location.lng();
    
    Serial.print("Fetching address for: ");
    Serial.print(latitude, 6);
    Serial.print(", ");
    Serial.println(longitude, 6);

    String address = getAddressFromCoordinates(latitude, longitude);
    Serial.print("Resolved Address: ");
    Serial.println(address);

    String message = "Emergency! Driver ku heart rate abnormal ah iruku. location: " + address + " (Lat: " + String(latitude, 6) + ", Long: " + String(longitude, 6) + ")";

    // **Send Automated Call**
    if (sendTwilioCall(message, emergency_contact)) {
        Serial.println("📞 Emergency Call Sent!");
    } else {
        Serial.println("❌ Emergency Call Failed!");
    }

    // **Send SMS**
    if (sendTwilioSMS(message, emergency_sms_contact)) {
        Serial.println("📩 Emergency SMS Sent!");
    } else {
        Serial.println("❌ Emergency SMS Failed!");
    }

    return true;
}

String URLEncode(String input) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    
    for (int i = 0; i < input.length(); i++) {
        c = input.charAt(i);
        if (c == ' ') {
            encodedString += '+';
        } else if (isalnum(c)) {
            encodedString += c;
        } else {
            code0 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) code0 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code1 = c + '0';
            if (c > 9) code1 = c - 10 + 'A';
            encodedString += '%';
            encodedString += code1;
            encodedString += code0;
        }
    }
    
    return encodedString;
}

bool sendTwilioCall(String message, String recipient) {
    String encodedAuth = "Basic " + base64::encode(String(twilio_account_sid) + ":" + String(twilio_auth_token));

    String twiml = "<Response><Say voice='Pdveena'>" + message + "</Say></Response>";
    String encodedTwiml = URLEncode(twiml);
    String twilio_url = "http://twimlets.com/echo?Twiml=" + encodedTwiml;

    String postData = "From=" + String(twilio_from_number) + "&To=" + recipient + "&Url=" + URLEncode(twilio_url);
    
    HTTPClient http;
    http.begin("https://api.twilio.com/2010-04-01/Accounts/" + String(twilio_account_sid) + "/Calls.json");
    http.addHeader("Authorization", encodedAuth);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpResponseCode = http.POST(postData);
    Serial.print("Twilio Call Response Code: ");
    Serial.println(httpResponseCode);

    http.end();
    return (httpResponseCode == 201);
}

bool sendTwilioSMS(String message, String recipient) {
    String encodedAuth = "Basic " + base64::encode(String(twilio_account_sid) + ":" + String(twilio_auth_token));

    String postData = "From=" + String(twilio_from_number) + "&To=" + recipient + "&Body=" + URLEncode(message);
    
    HTTPClient http;
    http.begin("https://api.twilio.com/2010-04-01/Accounts/" + String(twilio_account_sid) + "/Messages.json");
    http.addHeader("Authorization", encodedAuth);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpResponseCode = http.POST(postData);
    Serial.print("Twilio SMS Response Code: ");
    Serial.println(httpResponseCode);

    http.end();
    return (httpResponseCode == 201);
}

String getAddressFromCoordinates(float lat, float lon) {
    String requestURL = "https://api.opencagedata.com/geocode/v1/json?q=" + String(lat, 6) + "+" + String(lon, 6) + "&key=" + opencage_api_key;
    
    HTTPClient http;
    http.begin(requestURL);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        String response = http.getString();
        int startIndex = response.indexOf("\"formatted\":\"") + 13;
        int endIndex = response.indexOf("\"", startIndex);
        http.end();
        return response.substring(startIndex, endIndex);
    } else {
        Serial.println("OpenCage API Error!");
        http.end();
        return "Unknown Location";
    }
}
