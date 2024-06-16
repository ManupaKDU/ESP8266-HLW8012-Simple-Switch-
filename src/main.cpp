#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ThingSpeak.h>
#include "HLW8012.h"

#define SERIAL_BAUDRATE                 9600

// GPIOs
#define LED_PIN                         2
#define RELAY_PIN                       12
#define SEL_PIN                         5
#define CF1_PIN                         13
#define CF_PIN                          14

// WiFi credentials (for AP mode)
const char* ssid = "ESP-GPIO";
const char* password = "12345678";

// WiFi credentials (for Connection)
const char* ssid_connect = "WIFI-to-Connect"; // Enter the SSID
const char* password_connect = "password";   // Enter the Password

// ThingSpeak credentials
unsigned long myChannelNumber = <enter-channelno>; // Should be a long
const char* myWriteAPIKey = "enter-key"; // should be a string

// NTP settings
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

WiFiClient client;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Update interval
#define UPDATE_TIME                     2000

// Set SEL_PIN to HIGH to sample current
#define CURRENT_MODE                    HIGH

// Resistor values for the circuit
#define CURRENT_RESISTOR                0.001
#define VOLTAGE_RESISTOR_UPSTREAM       ( 5 * 470000 ) // Real: 2280k
#define VOLTAGE_RESISTOR_DOWNSTREAM     ( 1000 ) // Real 1.009k

int pinState = 0;
bool manualControl = false; // Flag to check if manual control is enabled
int startHour = 12; // Default start hour for time-based operation
int endHour = 18; // Default end hour for time-based operation

HLW8012 hlw8012;

void printGPIOStates() {
    Serial.print("LED_PIN (GPIO 2): ");
    Serial.println(digitalRead(LED_PIN));
    Serial.print("RELAY_PIN (GPIO 12): ");
    Serial.println(digitalRead(RELAY_PIN));
}

void handleRoot() {
    String html = "<html><body><h1>GPIO Control</h1>";
    html += "<p>LED_PIN (GPIO 2): " + String(digitalRead(LED_PIN)) + "</p>";
    html += "<p>RELAY_PIN (GPIO 12): " + String(digitalRead(RELAY_PIN)) + "</p>";
    html += "<a href=\"/toggle?pin=2\">Toggle LED</a><br>";
    html += "<a href=\"/toggle?pin=12\">Toggle Relay</a><br>";
    html += "<a href=\"/manual?state=" + String(!manualControl) + "\">";
    html += manualControl ? "Switch to Time-based Control" : "Switch to Manual Control";
    html += "</a><br>";
    html += "<form action=\"/settime\" method=\"get\">";
    html += "Start Hour: <input type=\"number\" name=\"starthour\" value=\"" + String(startHour) + "\"><br>";
    html += "End Hour: <input type=\"number\" name=\"endhour\" value=\"" + String(endHour) + "\"><br>";
    html += "<input type=\"submit\" value=\"Set Time\">";
    html += "</form>";
    html += "<a href=\"/update\">Update Firmware</a><br>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    printGPIOStates();
}

void handleToggle() {
    if (server.hasArg("pin")) {
        int pin = server.arg("pin").toInt();
        if (pin == LED_PIN || pin == RELAY_PIN) {
            digitalWrite(pin, !digitalRead(pin));
        }
    }
    handleRoot(); // Redirect to the root page to show updated state
}

void handleManualControl() {
    if (server.hasArg("state")) {
        manualControl = server.arg("state") == "1";
    }
    handleRoot(); // Redirect to the root page to show updated state
}

void handleSetTime() {
    if (server.hasArg("starthour") && server.hasArg("endhour")) {
        startHour = server.arg("starthour").toInt();
        endHour = server.arg("endhour").toInt();
        Serial.print("Time-based operation set from ");
        Serial.print(startHour);
        Serial.print(" to ");
        Serial.println(endHour);
    }
    handleRoot(); // Redirect to the root page to show updated state
}

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    Serial.println();

    WiFi.begin(ssid_connect, password_connect);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Initialize GPIOs
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Initialize WiFi in AP mode
    WiFi.softAP(ssid, password);
    Serial.println("Access Point Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Initialize ThingSpeak
    ThingSpeak.begin(client);

    // Initialize NTP
    timeClient.begin();

    // Initialize HLW8012
    hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, false, 500000);
    hlw8012.setResistors(CURRENT_RESISTOR, VOLTAGE_RESISTOR_UPSTREAM, VOLTAGE_RESISTOR_DOWNSTREAM);

    // Start web server
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/manual", handleManualControl);
    server.on("/settime", handleSetTime);

    // Initialize OTA
    httpUpdater.setup(&server);
    server.begin();
    Serial.println("HTTP server started");

    Serial.println("[HLW] Initialized");
    printGPIOStates(); // Print initial states
}

void loop() {
    server.handleClient();

    static unsigned long lastUpdate = millis();
    timeClient.update();

    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();

    Serial.print("Time: "); Serial.print(currentHour); Serial.print(":"); Serial.println(currentMinute);

    // Relay control logic
    if (!manualControl) {
        // Time-based control
        if ((currentHour >= startHour && currentHour < endHour)) {
            digitalWrite(RELAY_PIN, LOW);
            Serial.println("RELAY IS ON (Time-based)");
            pinState = 0;
        } else {
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("RELAY IS OFF (Time-based)");
            pinState = 1;
        }
    }

    // Print GPIO states periodically
    printGPIOStates();

    // Update and upload data every UPDATE_TIME milliseconds
    if ((millis() - lastUpdate) > UPDATE_TIME) {
        lastUpdate = millis();

        float activePower = hlw8012.getActivePower();
        float voltage = hlw8012.getVoltage();
        float current = hlw8012.getCurrent();
        float apparentPower = hlw8012.getApparentPower();
        float powerFactor = hlw8012.getPowerFactor();

        Serial.print("[HLW] Active Power (W)    : "); Serial.println(activePower);
        Serial.print("[HLW] Voltage (V)         : "); Serial.println(voltage);
        Serial.print("[HLW] Current (A)         : "); Serial.println(current);
        Serial.print("[HLW] Apparent Power (VA) : "); Serial.println(apparentPower);
        Serial.print("[HLW] Power Factor (%)    : "); Serial.println((int) (100 * powerFactor));
        Serial.println();

        // Upload data to ThingSpeak
        ThingSpeak.setField(1, activePower);
        ThingSpeak.setField(2, "230");
        ThingSpeak.setField(3, current);
        ThingSpeak.setField(4, apparentPower);
        ThingSpeak.setField(5, powerFactor);
        ThingSpeak.setField(6, pinState);
        ThingSpeak.setField(7, currentHour);
        ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

        hlw8012.toggleMode();
    }
}
