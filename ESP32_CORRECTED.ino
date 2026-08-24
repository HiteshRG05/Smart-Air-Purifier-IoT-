/*
 * VayuKavach IoT Air Purifier - ESP32 Controller
 * 
 * ARCHITECTURE:
 * - ESP32 ONLY WRITES: sensor data (aqi, pm25, pm10, humidity, online)
 * - ESP32 ONLY READS: control values (mode, fan_speed, power)
 * - Prevents feedback loop with web dashboard
 * 
 * Firebase Structure:
 * device/
 *   sensors/         ← ESP32 WRITES only
 *     aqi
 *     pm25
 *     pm10
 *     humidity
 *   control/         ← ESP32 READS only
 *     mode
 *     fan_speed
 *     power
 *   status/          ← ESP32 WRITES only
 *     online
 *     filter_life
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
const unsigned long SENSOR_INTERVAL = 10000;  // Send sensor data every 10 seconds
const unsigned long CONTROL_INTERVAL = 1000;  // Check control values every 1 second
const unsigned long FILTER_UPDATE_INTERVAL = 60000; // Update filter life every 1 minute

// Filter life constants
const int HIGH_AQI_THRESHOLD = 100;           // AQI > 100 considered high
const unsigned long HIGH_AQI_TEST_DURATION = 5 * 60 * 1000;    // 5 minutes for testing
const unsigned long HIGH_AQI_REAL_DURATION = 4 * 60 * 60 * 1000; // 4 hours for real use
const int FILTER_DEGRADATION_PER_HIGH_AQI_PERIOD = 2; // 2% per high AQI period

bool firebaseReady = false;

// Mock sensor functions (replace with real sensors)
void readSensors() {
  // Replace with your actual sensor reading code
  // Example: Read from PMS5003, DHT22, etc.
  
  // Mock data with slight variation for demo
  currentSensors.aqi = random(30, 80);
  currentSensors.pm25 = random(10, 40) + random(0, 100) / 100.0;
  currentSensors.pm10 = random(20, 80) + random(0, 100) / 100.0;
  currentSensors.humidity = random(40, 60) + random(0, 100) / 100.0;
}

void applyControlSettings() {
  // Apply the control settings to your hardware
  Serial.println("=== Applying Control Settings ===");
  Serial.print("Power: "); Serial.println(currentControl.power ? "ON" : "OFF");
  Serial.print("Mode: "); Serial.println(currentControl.mode);
  Serial.print("Current AQI: "); Serial.println(currentSensors.aqi);
  
  if (!currentControl.power) {
    Serial.println("System OFF - Disabling all modules");
    // Turn off everything
    // digitalWrite(FAN_PIN, LOW);
    // digitalWrite(HUMIDIFIER_PIN, LOW);
    // digitalWrite(HEPA_PIN, LOW);
    return;
  }
  
  // Determine fan speed based on mode
  String fanSpeedToApply;
  bool shouldWriteFanSpeed = false;
  
  if (currentControl.mode == "Auto") {
    // AUTO MODE: ESP32 calculates fan speed based on AQI
    // ESP32 writes the calculated fan_speed to Firebase so UI can display it
    fanSpeedToApply = calculateOptimalFanSpeed("Auto", currentSensors.aqi);
    
    // Only write to Firebase if calculated speed is different from current
    if (fanSpeedToApply != currentControl.fan_speed) {
      shouldWriteFanSpeed = true;
      Serial.print("Auto mode - AQI-based fan speed: "); Serial.println(fanSpeedToApply);
    }
  } else {
    // OTHER MODES: Use fan_speed from Firebase (set by UI)
    // Asthma=High, Baby=Low, Senior=Medium
    fanSpeedToApply = currentControl.fan_speed;
    Serial.print("Using fan speed from Firebase: "); Serial.println(fanSpeedToApply);
  }
  
  // Mode-specific hardware control based on your flow diagram
  Serial.println("--- Mode-Specific Actions ---");
  
  if (currentControl.mode == "Auto") {
    Serial.println("🤖 AUTO MODE: AQI-proportional control");
    Serial.print("Calculated fan speed: "); Serial.println(fanSpeedToApply);
    // digitalWrite(HUMIDIFIER_PIN, LOW); // No humidifier in auto
    
    // Write calculated fan_speed back to Firebase so UI can display it
    if (shouldWriteFanSpeed) {
      if (Firebase.RTDB.setString(&fbdo, "device/control/fan_speed", fanSpeedToApply)) {
        Serial.print("✅ Wrote fan_speed to Firebase: "); Serial.println(fanSpeedToApply);
        currentControl.fan_speed = fanSpeedToApply; // Update local cache
      } else {
        Serial.println("❌ Failed to write fan_speed to Firebase");
      }
    }
    
  } else if (currentControl.mode == "Asthma") {
    Serial.println("🫁 ASTHMA MODE: Aggressive PM removal");
    Serial.print("Fan speed: "); Serial.println(fanSpeedToApply);
    // digitalWrite(HUMIDIFIER_PIN, LOW);   // No humidity for asthma
    // digitalWrite(HEPA_PIN, HIGH);        // Max HEPA filtration
    
  } else if (currentControl.mode == "Baby") {
    Serial.println("🍼 BABY MODE: Lowered fan noise, gentle humidity");
    Serial.print("Fan speed: "); Serial.println(fanSpeedToApply);
    // digitalWrite(HUMIDIFIER_PIN, HIGH);  // Gentle humidity
    
  } else if (currentControl.mode == "Senior") {
    Serial.println("🧓 SENIOR MODE: Humidity control for comfort");
    Serial.print("Fan speed: "); Serial.println(fanSpeedToApply);
    if (currentSensors.humidity < 40) {
      Serial.println("Low humidity detected - Turning ON Humidifier");
      // digitalWrite(HUMIDIFIER_PIN, HIGH);
    } else {
      Serial.println("Humidity adequate - Humidifier OFF");
      // digitalWrite(HUMIDIFIER_PIN, LOW);
    }
    
  } else {
    Serial.println("Default mode - Standard operation");
  }
  
  // Apply fan speed control
  applyFanSpeed(fanSpeedToApply);
  
  Serial.println("=== Control Settings Applied ===");
}

// Calculate optimal fan speed based on mode and current AQI
String calculateOptimalFanSpeed(String mode, int aqi) {
  Serial.print("Calculating fan speed for mode: "); Serial.print(mode); 
  Serial.print(", AQI: "); Serial.println(aqi);
  
  if (mode == "Auto") {
    // Auto mode: AQI-proportional speed
    if (aqi >= 150) return "High";      // Hazardous
    else if (aqi >= 100) return "High"; // Unhealthy  
    else if (aqi >= 50) return "Medium"; // Moderate
    else return "Low";                   // Good
    
  } else if (mode == "Asthma") {
    // Asthma mode: Always high speed for aggressive filtration
    return "High";
    
  } else if (mode == "Baby") {
    // Baby mode: Lower fan noise, but still responsive to high AQI
    if (aqi >= 100) return "Medium";     // Never high speed (too noisy)
    else if (aqi >= 50) return "Low";    // Gentle but effective
    else return "Low";                   // Always gentle
    
  } else if (mode == "Senior") {
    // Senior mode: Comfort-focused, moderate speeds
    if (aqi >= 150) return "High";       // High AQI override
    else if (aqi >= 75) return "Medium"; // Comfort balance
    else return "Low";                   // Quiet comfort
  }
  
  // Default fallback
  return "Medium";
}

void applyFanSpeed(String fanSpeed) {
  Serial.println("--- Fan Speed Control ---");
  Serial.print("Applying fan speed: "); Serial.println(fanSpeed);
  
  if (fanSpeed == "Low") {
    Serial.println("Fan: LOW speed (PWM ~25%) - Whisper quiet");
    // analogWrite(FAN_PWM_PIN, 64);   // 25% of 255
  } else if (fanSpeed == "Medium") {
    Serial.println("Fan: MEDIUM speed (PWM ~65%) - Balanced");
    // analogWrite(FAN_PWM_PIN, 166);  // 65% of 255  
  } else if (fanSpeed == "High") {
    Serial.println("Fan: HIGH speed (PWM ~100%) - Maximum purification");
    // analogWrite(FAN_PWM_PIN, 255);  // 100% of 255
  }
}

void setup() {
  Serial.begin(115200);
  
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
  Firebase.RTDB.setInt(&fbdo, "device/status/filter_life", 85);
  
  // Control section (Web UI writes, ESP32 reads)
  // Set defaults if they don't exist
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
}

void loop() {
  if (!firebaseReady) return;
  
  unsigned long now = millis();
  
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

// ============================================
// WRITE FUNCTIONS - ESP32 sends sensor data
// ============================================
void writeSensorData() {
  readSensors();
  
  Serial.println("--- Writing Sensors to Firebase ---");
  
  // Write to sensors path (not root device path)
  if (Firebase.RTDB.setInt(&fbdo, "device/sensors/aqi", currentSensors.aqi)) {
    Serial.print("  AQI: "); Serial.println(currentSensors.aqi);
  } else {
    Serial.println("  Failed to write AQI");
  }
  
  if (Firebase.RTDB.setFloat(&fbdo, "device/sensors/pm25", currentSensors.pm25)) {
    Serial.print("  PM2.5: "); Serial.println(currentSensors.pm25);
  }
  
  if (Firebase.RTDB.setFloat(&fbdo, "device/sensors/pm10", currentSensors.pm10)) {
    Serial.print("  PM10: "); Serial.println(currentSensors.pm10);
  }
  
  if (Firebase.RTDB.setFloat(&fbdo, "device/sensors/humidity", currentSensors.humidity)) {
    Serial.print("  Humidity: "); Serial.println(currentSensors.humidity);
  }
  
  // Update online status and filter life
  Firebase.RTDB.setBool(&fbdo, "device/status/online", true);
  Firebase.RTDB.setInt(&fbdo, "device/status/filter_life", filterData.currentLife);
}

// ============================================
// READ FUNCTIONS - ESP32 reads control values
// ============================================
void readControlData() {
  bool changed = false;
  
  Serial.println("--- Reading Control Values ---");
  
  // Read power state
  if (Firebase.RTDB.getBool(&fbdo, "device/control/power")) {
    bool newPower = fbdo.boolData();
    Serial.print("  Power from Firebase: "); Serial.println(newPower ? "ON" : "OFF");
    if (newPower != currentControl.power) {
      currentControl.power = newPower;
      changed = true;
      Serial.println("  >>> POWER CHANGED!");
    }
  } else {
    Serial.println("  Failed to read power");
  }
  
  // Read mode
  if (Firebase.RTDB.getString(&fbdo, "device/control/mode")) {
    String newMode = fbdo.stringData();
    Serial.print("  Mode from Firebase: "); Serial.println(newMode);
    if (newMode != currentControl.mode && newMode.length() > 0) {
      Serial.print("  >>> MODE CHANGED from "); Serial.print(currentControl.mode);
      Serial.print(" to "); Serial.println(newMode);
      currentControl.mode = newMode;
      changed = true;
    }
  } else {
    Serial.println("  Failed to read mode");
  }
  
  // Read fan speed
  if (Firebase.RTDB.getString(&fbdo, "device/control/fan_speed")) {
    String newFanSpeed = fbdo.stringData();
    Serial.print("  Fan speed from Firebase: "); Serial.println(newFanSpeed);
    if (newFanSpeed != currentControl.fan_speed && newFanSpeed.length() > 0) {
      Serial.print("  >>> FAN SPEED CHANGED from "); Serial.print(currentControl.fan_speed);
      Serial.print(" to "); Serial.println(newFanSpeed);
      currentControl.fan_speed = newFanSpeed;
      changed = true;
    }
  } else {
    Serial.println("  Failed to read fan_speed");
  }
  
  // Only apply if something changed
  if (changed) {
    Serial.println("========================================");
    Serial.println(">>> CONTROL VALUES CHANGED - APPLYING!");
    Serial.println("========================================");
    applyControlSettings();
  }
}

// FILTER LIFE MANAGEMENT
// Degrades filter based on high AQI exposure time
// ============================================
void updateFilterLife() {
  unsigned long now = millis();
  bool currentlyHighAqi = (currentSensors.aqi > HIGH_AQI_THRESHOLD);
  
  Serial.println("--- Filter Life Check ---");
  Serial.print("Current AQI: "); Serial.println(currentSensors.aqi);
  Serial.print("Current filter life: "); Serial.print(filterData.currentLife); Serial.println("%");
  
  // Check if entering high AQI period
  if (currentlyHighAqi && !filterData.isHighAqiPeriod) {
    filterData.isHighAqiPeriod = true;
    filterData.highAqiStartTime = now;
    Serial.println("🔴 HIGH AQI PERIOD STARTED - Filter degradation tracking begins");
  }
  
  // Check if exiting high AQI period  
  if (!currentlyHighAqi && filterData.isHighAqiPeriod) {
    filterData.isHighAqiPeriod = false;
    unsigned long highAqiDuration = now - filterData.highAqiStartTime;
    Serial.print("🔴 HIGH AQI PERIOD ENDED - Duration: "); 
    Serial.print(highAqiDuration / 1000); Serial.println(" seconds");
  }
  
  // Check if high AQI period exceeded threshold
  if (filterData.isHighAqiPeriod) {
    unsigned long currentHighAqiDuration = now - filterData.highAqiStartTime;
    
    // Use shorter duration for testing (5 minutes)
    unsigned long thresholdDuration = HIGH_AQI_TEST_DURATION;
    // For production, use: HIGH_AQI_REAL_DURATION;
    
    if (currentHighAqiDuration >= thresholdDuration) {
      // Degrade filter life
      filterData.currentLife -= FILTER_DEGRADATION_PER_HIGH_AQI_PERIOD;
      if (filterData.currentLife < 0) filterData.currentLife = 0;
      
      Serial.println("⚠️  FILTER LIFE DEGRADED!");
      Serial.print("High AQI exposure for "); 
      Serial.print(currentHighAqiDuration / 1000); Serial.println(" seconds");
      Serial.print("Filter life reduced to: "); 
      Serial.print(filterData.currentLife); Serial.println("%");
      
      // Reset the timer for next degradation cycle
      filterData.highAqiStartTime = now;
      
      // Write updated filter life to Firebase
      Firebase.RTDB.setInt(&fbdo, "device/status/filter_life", filterData.currentLife);
      
      // Send alert if filter life is critically low
      if (filterData.currentLife <= 10) {
        Serial.println("🚨 CRITICAL: Filter life <= 10% - REPLACEMENT REQUIRED!");
        // You could send a push notification here
      } else if (filterData.currentLife <= 25) {
        Serial.println("⚠️  WARNING: Filter life <= 25% - Consider replacement soon");
      }
    } else {
      Serial.print("High AQI ongoing for "); 
      Serial.print(currentHighAqiDuration / 1000); 
      Serial.print(" seconds (threshold: "); 
      Serial.print(thresholdDuration / 1000); Serial.println(" seconds)");
    }
  }
  
  // Always update total runtime (for general wear tracking)
  if (currentControl.power) {
    filterData.totalRunTime += FILTER_UPDATE_INTERVAL;
    // You could add general wear based on total runtime too
  }
}
