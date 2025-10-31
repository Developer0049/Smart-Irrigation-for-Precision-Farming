#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define WIFI_SSID "Nooh"
#define WIFI_PASS "a2s9cthn"
#define API_KEY "AIzaSyBi2lQSVxmxiDjtBXW02TbJTYkFRaS48zo"
#define DATABASE_URL "https://smart-farmer-bd192-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL "farmer@example.com"
#define USER_PASSWORD "PASSWORD"

#define MOISTURE_PIN 36   // Soil moisture sensor
#define TRIG_PIN 5        // Ultrasonic trigger
#define ECHO_PIN 18       // Ultrasonic echo
#define RELAY_PIN 23      // Relay (try GPIO22 if issues)
#define DHT_PIN 4         // DHT11 data
#define RAINDROP_PIN 15   // Raindrop sensor digital out
#define SCREEN_WIDTH 128  // OLED width
#define SCREEN_HEIGHT 64  // OLED height

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RAINDROP_PIN, INPUT);
  digitalWrite(RELAY_PIN, HIGH); // Start OFF (active-low)

  // Test relay
  Serial.println("Testing relay...");
  digitalWrite(RELAY_PIN, LOW); // ON (active-low)
  Serial.println("Relay ON (LOW)");
  delay(2000);
  digitalWrite(RELAY_PIN, HIGH); // OFF
  Serial.println("Relay OFF (HIGH)");
  delay(2000);

  // Initialize DHT11
  dht.begin();

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed to start"));
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Smart Farmer"));
  display.display();

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase initialized");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  if (Firebase.ready()) {
    // Read raindrop sensor (LOW = rain detected)
    bool isRaining = digitalRead(RAINDROP_PIN) == LOW;
    if (isRaining) {
      digitalWrite(RELAY_PIN, HIGH); // Force OFF (active-low)
      Firebase.RTDB.setString(&fbdo, "control/pump", "OFF");
      Serial.println("Rain detected, pump forced OFF");
    }

    // Update OLED
    display.clearDisplay();
    display.setCursor(0, 0);

    // Read soil moisture
    int moisture = analogRead(MOISTURE_PIN);
    int moisturePercent = map(moisture, 4095, 0, 0, 100);
    if (Firebase.RTDB.setInt(&fbdo, "sensors/moisture1", moisturePercent)) {
      Serial.println("Moisture1 updated: " + String(moisturePercent) + "%");
    } else {
      Serial.println("Failed to update Moisture1: " + fbdo.errorReason());
    }
    display.print("Moisture: ");
    display.print(moisturePercent);
    display.println("%");

    // Read tank level
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance = duration * 0.034 / 2;
    int tankLevel = map(distance, 20, 5, 0, 100);
    if (Firebase.RTDB.setInt(&fbdo, "sensors/tank_level", tankLevel)) {
      Serial.println("Tank level updated: " + String(tankLevel) + "%");
    } else {
      Serial.println("Failed to update Tank level: " + fbdo.errorReason());
    }
    display.print("Tank: ");
    display.print(tankLevel);
    display.println("%");

    // Read DHT11
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(humidity)) {
      if (Firebase.RTDB.setFloat(&fbdo, "sensors/temperature", temperature)) {
        Serial.println("Temperature updated: " + String(temperature) + "C");
      } else {
        Serial.println("Failed to update Temperature: " + fbdo.errorReason());
      }
      if (Firebase.RTDB.setFloat(&fbdo, "sensors/humidity", humidity)) {
        Serial.println("Humidity updated: " + String(humidity) + "%");
      } else {
        Serial.println("Failed to update Humidity: " + fbdo.errorReason());
      }
      display.print("Temp: ");
      display.print(temperature);
      display.println("C");
      display.print("Humidity: ");
      display.print(humidity);
      display.println("%");
    } else {
      Serial.println("Failed to read DHT11");
      display.println("DHT11: Error");
    }

    // Rain status
    display.print("Rain: ");
    display.println(isRaining ? "Yes" : "No");

    // Read pump command (only if no rain)
    String pumpCmd = "OFF";
    if (!isRaining) {
      if (Firebase.RTDB.getString(&fbdo, "control/pump")) {
        pumpCmd = fbdo.to<String>(); // Use to<String>() for Firebase_ESP_Client
        Serial.println("Raw pumpCmd: '" + pumpCmd + "', length: " + String(pumpCmd.length()));
        if (pumpCmd == "ON") {
          digitalWrite(RELAY_PIN, LOW); // Active-low
          Serial.println("Pump turned ON (GPIO23 LOW)");
        } else if (pumpCmd == "OFF") {
          digitalWrite(RELAY_PIN, HIGH); // Active-low
          Serial.println("Pump turned OFF (GPIO23 HIGH)");
        } else {
          digitalWrite(RELAY_PIN, HIGH); // Default OFF
          Serial.println("Unknown pumpCmd: '" + pumpCmd + "', defaulting OFF");
        }
      } else {
        digitalWrite(RELAY_PIN, HIGH); // Default OFF
        Serial.println("Failed to read pump command: " + fbdo.errorReason());
      }
    }
    display.print("Pump: ");
    display.println(pumpCmd);

    display.display();
  } else {
    Serial.println("Firebase not ready: " + fbdo.errorReason());
    digitalWrite(RELAY_PIN, HIGH); // Default OFF
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Firebase Error");
    display.display();
  }
  delay(5000);
}
