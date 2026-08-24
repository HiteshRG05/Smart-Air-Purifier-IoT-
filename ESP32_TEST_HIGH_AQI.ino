/*
 * VayuKavach IoT Air Purifier - ESP32 Controller (TEST VERSION)
 * 
 * This version simulates high AQI for testing filter degradation
 * Use this for demonstration, then switch to production version
 */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Firebase credentials
#define API_KEY "AIzaSyBF78P5pCnBYCI3rB9xgrr7MLJmD6Jo16g"
#define DATABASE_URL "https://vayu-kavach-123-default-rtdb.asia-southeast1.firebasedatabase.app"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Test mode for simulating high AQI
bool testModeHighAQI = true; // Set to false for normal operation
int testAQIValue = 150;      // Simulated high AQI for testing

// Sensor data - what we WRITE to Firebase
struct SensorData {
  int aqi;
  float pm25;
  float pm10;
  float humidity;
} currentSensors = {50, 12.5, 25.0, 45.0};

// Control data - what we READ from Firebase
struct ControlData {
  String mode;
  String fan_speed;
  bool power;
} currentControl = {"Auto", "Medium", true};

// Filter life management
struct FilterLifeData {
  int currentLife;        // 0-100%
  unsigned long highAqiStartTime; // When high AQI started
  bool isHighAqiPeriod;   // Currently in high AQI period
  unsigned long totalRunTime;     // Total filter usage time
} filterData = {85, 0, false, 0};

// Timing
unsigned long lastSensorWrite = 0;
unsigned long lastControlRead = 0;
unsigned long lastFilterUpdate = 0;
unsigned long testModeStartTime = 0;

const unsigned long SENSOR_INTERVAL = 3000;   // Send sensor data every 3 seconds
const unsigned long CONTROL_INTERVAL = 1000;  // Check control values every 1 second
const unsigned long FILTER_UPDATE_INTERVAL = 60000; // Update filter life every 1 minute

// Filter life constants
const int HIGH_AQI_THRESHOLD = 100;           // AQI > 100 considered high
const unsigned long HIGH_AQI_TEST_DURATION = 5 * 60 * 1000;    // 5 minutes for testing
const unsigned long HIGH_AQI_REAL_DURATION = 4 * 60 * 60 * 1000; // 4 hours for real use
const int FILTER_DEGRADATION_PER_HIGH_AQI_PERIOD = 2; // 2% per high AQI period

bool firebaseReady = false;

// Mock sensor functions with TEST MODE
void readSensors() {
  if (testModeHighAQI) {
    // Simulate consistently high AQI for testing
    currentSensors.aqi = testAQIValue;
    currentSensors.pm25 = random(45, 60) + random(0, 100) / 100.0;  // High PM2.5
    currentSensors.pm10 = random(80, 120) + random(0, 100) / 100.0; // High PM10
    currentSensors.humidity = random(40, 60) + random(0, 100) / 100.0;
    
    Serial.println("🧪 TEST MODE: Simulating HIGH AQI for filter degradation test");
  } else {
    // Normal operation - real sensor readings
    currentSensors.aqi = random(30, 80);
    currentSensors.pm25 = random(10, 40) + random(0, 100) / 100.0;
    currentSensors.pm10 = random(20, 80) + random(0, 100) / 100.0;
    currentSensors.humidity = random(40, 60) + random(0, 100) / 100.0;
  }
}

void setup() {
  Serial.begin(115200);
  
  if (testModeHighAQI) {
    Serial.println("🧪 STARTING IN TEST MODE - High AQI simulation enabled");
    Serial.println("Filter will degrade every 5 minutes of high AQI exposure");
    testModeStartTime = millis();
  }
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  // Sign up (anonymous)
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup OK");
  } else {
    Serial.printf("Firebase signup error: %s\n", config.signer.signupError.message.c_str());
  }
  
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Wait for Firebase to be ready
  Serial.println("Waiting for Firebase...");
  while (!Firebase.ready()) {
    delay(100);
  }
  firebaseReady = true;
  Serial.println("Firebase ready!");
  
  // Initialize Firebase structure with default values
  Serial.println("Initializing Firebase structure...");
  
  // Status section (ESP32 writes)
  Firebase.RTDB.setBool(&fbdo, "device/status/online", true);
  Firebase.RTDB.setInt(&fbdo, "device/status/filter_life", filterData.currentLife);
  
  // Control section (Web UI writes, ESP32 reads)
  if (!Firebase.RTDB.getString(&fbdo, "device/control/mode")) {
    Firebase.RTDB.setString(&fbdo, "device/control/mode", "Auto");
  }
  if (!Firebase.RTDB.getString(&fbdo, "device/control/fan_speed")) {
    Firebase.RTDB.setString(&fbdo, "device/control/fan_speed", "Medium");
  }
  if (!Firebase.RTDB.getBool(&fbdo, "device/control/power")) {
    Firebase.RTDB.setBool(&fbdo, "device/control/power", true);
  }
  
  Serial.println("Firebase structure initialized!");
  
  if (testModeHighAQI) {
    Serial.println("📊 WATCH THE FILTER LIFE:");
    Serial.println("   - Every 5 minutes of high AQI → filter life -2%");
    Serial.println("   - Watch web dashboard filter bar decrease");
    Serial.println("   - Change 'testModeHighAQI = false' for normal operation");
  }
}

void loop() {
  if (!firebaseReady) return;
  
  unsigned long now = millis();
  
  // Test mode auto-disable after 30 minutes
  if (testModeHighAQI && (now - testModeStartTime) > (30 * 60 * 1000)) {
    Serial.println("🧪 TEST MODE AUTO-DISABLED after 30 minutes");
    testModeHighAQI = false;
  }
  
  // WRITE sensor data periodically
  if (now - lastSensorWrite >= SENSOR_INTERVAL) {
    lastSensorWrite = now;
    writeSensorData();
  }
  
  // READ control values periodically
  if (now - lastControlRead >= CONTROL_INTERVAL) {
    lastControlRead = now;
    readControlData();
  }
  
  // UPDATE filter life periodically
  if (now - lastFilterUpdate >= FILTER_UPDATE_INTERVAL) {
    lastFilterUpdate = now;
    updateFilterLife();
  }
}

// [Include all other functions from the main ESP32_CORRECTED.ino file]
// writeSensorData(), readControlData(), applyControlSettings(), 
// calculateOptimalFanSpeed(), applyFanSpeed(), updateFilterLife()