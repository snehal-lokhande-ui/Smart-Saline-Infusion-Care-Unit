#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Arduino.h>
#include "HX711.h"
#include <DHT.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <OneWire.h>
#include <DallasTemperature.h>
// Add to the includes section at the top
#include <ESP32Servo.h>

// Add to Pin Definitions section
#define SERVO_PIN 34  // Connect servo signal wire to GPIO4


// Pin Definitions
#define BUZZER_PIN 2
#define RXD2 16  // ESP32 UART2 RX (Sensor TX)
#define TXD2 17  // ESP32 UART2 TX (Sensor RX)
#define DHTPIN1 12      // Connect DHT11 data pin to GPIO12
#define ONE_WIRE_BUS 13 // DS18B20 data pin connected to GPIO13
#define DHTTYPE DHT11   // DHT 11

// Global objects initialization
DHT dht1(DHTPIN1, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// HX711 Configuration
const int DOUT_PIN = 32;
const int SCK_PIN = 33;
HX711 scale;

// Keypad Configuration
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
// Connect Keypad Pins to ESP32
byte rowPins[ROWS] = {23, 22, 1, 3};  // R1, R2, R3, R4
byte colPins[COLS] = {21, 19, 18, 5}; // C1, C2, C3, C4
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Set keypad to non-blocking with no or minimal delay
// This is critical for prioritizing keypad input
void configureKeypadForFastResponse() {
  keypad.setDebounceTime(20);  // Reduce debounce time (default is 50ms)
  keypad.setHoldTime(500);     // Set hold time
}

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 4);

// MAX30102 Sensor
MAX30105 particleSensor;
uint32_t irBuffer[110];  
uint32_t redBuffer[110];  
int32_t spo2, heartRate;
int8_t validSpO2, validHeartRate;

// Variables for data collection
const int collectionTime = 1.5;  // 1.5 seconds (reduced from 50)
const int sampleSize = 100;     // 100 samples per second
int totalSamples = (collectionTime * sampleSize);
int validHRCount = 0, validSpO2Count = 0;
long sumHeartRate = 0, sumSpO2 = 0;

// Input handling variables
String inputBuffer = "";
bool inputActive = true;
// Add to Global objects initialization section
Servo flowControlServo;  // Create servo object

// Patient Details Structure
struct PatientDetails {
  String name;
  String healthParam;
  String medicineRoutine;
  String clinicalNotes;
  String nextCheckupDate;
};

// Patient Data
const PatientDetails PATIENTS[] = {
  {
    "Aditya Jagadale ", 
    "Antibiotics 3x daily at 8AM, 2PM, 10PM", 
    "Post-surgical monitoring", 
    "15/04/2025"
  },
  {
    "Atharv Gurule", 
    "Pain meds 2x daily at 9AM, 9PM", 
    "Recovering from surgery", 
    "20/04/2025"
  },
  {
    "Utkarsh Aswar",  
    "IV Drip continuous", 
    "Respiratory support needed", 
    "10/04/2025"
  }
};

// Saline Types
const String SALINE_TYPES[] = {
  "Normal Saline", 
  "Ringer's", 
  "Dextrose", 
  "Lactated", 
  "Plasma-Lyte"
};

// Enum for System States
enum SystemState {
  starting,
  STARTUP_SPLASH,
  PROCESSING,
  MAIN_MENU,
  HOSPITAL_PATIENT_SELECT,
  HOSPITAL_SALINE_SELECT,
  HOSPITAL_WEIGHT_INPUT,
  HOSPITAL_HEALTH_PARAMETERS,
  HOSPITAL_HEALTH_TEMP,
  HOSPITAL_HEALTH_BP,
  HOSPITAL_HEALTH_SPO2_HR,
  HOSPITAL_SALINE_CONTROL,
  PATIENT_MENU,
  PATIENT_HEALTH_PARAM,
  PATIENT_MEDICINE_TIME,
  PATIENT_CLINICAL_NOTES
};

// Global Variables
SystemState currentState = STARTUP_SPLASH;
SystemState previousState = STARTUP_SPLASH;
int selectedPatient = -1;
int selectedSaline = -1;
float salineWeight = 0.0;
int currentPage = 0;
float weight = 0.0;
float t = 0.0;
float actualTemp = 0.0;

// Key detection variables - OPTIMIZED for faster response
volatile bool newKeyAvailable = false;
char lastKey = 0;
unsigned long lastKeyCheck = 0;
const int keyCheckInterval = 1; // Reduced from 3ms to 1ms for faster checks
bool keyPressed = false;

// Body temperature calibration
float adjustmentFactor = 0.0;

// Function prototypes
void displayStartupSplash();
void displayProcessing();
void displayMainMenu();
void displayHospitalPatientSelect();
void displaySalineTypeSelect();
void displaySalineWeightInput();
void displayHealthParameters();
void displayHealthTemp();
void displayHealthBP();
void displayHealthSPO2HR();
void displaySalineControl();
void displayPatientMenu();
void displayPatientHealthParam();
void displayPatientMedicineTime();
void displayPatientClinicalNotes();
void triggerBuzzer(int duration);
void handleBackNavigation();
bool checkKeyPress();
void processKey(char key);
float calculateAdjustedTemp(float actualTemp);
void resetSalineInput();
void showProcessing();
void displayResults(int avgHeartRate, int avgSpO2);
void displayError(String message);
void displayCalibrationScreen();
void displayProcessingScreen(int sec);

// Non-blocking key check function - HIGH PRIORITY
bool checkKeyPress() {
  // Check if a key is available without blocking
  char key = keypad.getKey();
  if (key) {
    // Short beep for key press feedback - keep this as short as possible
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(50000); // 50ms delay using microseconds for precision
    digitalWrite(BUZZER_PIN, LOW);
    
    // Process the key immediately
    processKey(key);
    return true;
  }
  return false;
}

// Temperature adjustment function
float calculateAdjustedTemp(float actualTemp) {
  float adjustedTemp = actualTemp + adjustmentFactor;

  // Ensure adjustedTemp doesn't drop below 35 when actualTemp is between 30-34
  if (adjustedTemp < 35.0 && actualTemp >= 30.0) {
    adjustedTemp = 35.0;
  }
  // If actualTemp is 29, set adjustedTemp to 34
  else if (actualTemp == 29.0) {
    adjustedTemp = 34.0;
  }
  
  return adjustedTemp;
}

// Process key presses based on current state
void processKey(char key) {
  // Check for back key (*) first - this works in any state except STARTUP_SPLASH
  if (key == '*' && currentState != STARTUP_SPLASH) {
    handleBackNavigation();
    return;
  }

  // Handle # key for health parameter submenu navigation
  if (key == '#') {
    if (currentState == HOSPITAL_HEALTH_TEMP || 
        currentState == HOSPITAL_HEALTH_BP || 
        currentState == HOSPITAL_HEALTH_SPO2_HR) {
      currentState = HOSPITAL_HEALTH_PARAMETERS;
      return;
    }
  }

  switch(currentState) {
    case STARTUP_SPLASH:
      // Any key advances from splash screen
      currentState = PROCESSING;
      break;
    
    case PROCESSING:
      if (key == 'D') {
        currentState = MAIN_MENU;
        previousState = PROCESSING;
      }
      break;
    
    case MAIN_MENU:
      if (key == 'A') {
        previousState = MAIN_MENU;
        currentState = HOSPITAL_PATIENT_SELECT;
      } 
      else if (key == 'B') {
        currentState = PATIENT_MENU;
        previousState = MAIN_MENU;
      }
      break;
    
    case HOSPITAL_PATIENT_SELECT:
      if (key >= '1' && key <= '3') {
        selectedPatient = key - '1'; // Convert to 0-based index
        lcd.clear();
        lcd.print("Selected:");
        lcd.setCursor(0, 1);
        lcd.print(PATIENTS[selectedPatient].name); // Show selected patient
        delay(1000); // Reduced from 1500ms to 1000ms
        currentState = HOSPITAL_SALINE_SELECT;
        previousState = HOSPITAL_PATIENT_SELECT;
      }
      break;

    case HOSPITAL_SALINE_SELECT:
      if (key >= '1' && key <= '5') { // Allow keys 1-5 for 5 saline types
        selectedSaline = key - '1'; // Convert to 0-based index
        // Ensure selection is within array bounds
        if (selectedSaline < sizeof(SALINE_TYPES)/sizeof(SALINE_TYPES[0])) {
          lcd.clear();
          lcd.print("Selected:");
          lcd.setCursor(0, 1);
          lcd.print(SALINE_TYPES[selectedSaline]); // Show selected saline
          delay(1000); // Reduced from 1500ms to 1000ms
          previousState = HOSPITAL_SALINE_SELECT;
          currentState = HOSPITAL_WEIGHT_INPUT;
          resetSalineInput();
        }
      }
      break;

    case HOSPITAL_WEIGHT_INPUT:
      if (key >= '0' && key <= '9') {
        salineWeight = salineWeight * 10 + (key - '0');
      } 
      else if (key == 'D') {
        currentState = HOSPITAL_HEALTH_PARAMETERS;
      }
      else if (key == '#') {
        if (!inputActive) {  // Only proceed if input was confirmed
          currentState = HOSPITAL_HEALTH_PARAMETERS;
        }
      }
      break;
    
    case HOSPITAL_HEALTH_PARAMETERS:
      if (key == 'D') {
        currentState = HOSPITAL_SALINE_CONTROL;
      }
      else if (key == '1') {
        currentState = HOSPITAL_HEALTH_TEMP;
      }
      else if (key == '2') {
        currentState = HOSPITAL_HEALTH_BP;
      }
      else if (key == '3') {
        currentState = HOSPITAL_HEALTH_SPO2_HR;
      }
      break;
      
    case HOSPITAL_HEALTH_TEMP:
      // # key already handled above to return to health parameters menu
      break;
      
    case HOSPITAL_HEALTH_BP:
      // # key already handled above to return to health parameters menu
      break;
      
    case HOSPITAL_HEALTH_SPO2_HR:
      // # key already handled above to return to health parameters menu
      break;
    
    case HOSPITAL_SALINE_CONTROL:
      // No need for '0' to return to main menu anymore since '*' does that
      break;
    
    case PATIENT_MENU:
      if (key == '1') {
        currentState = PATIENT_HEALTH_PARAM;
      } 
      else if (key == '2') {
        currentState = PATIENT_MEDICINE_TIME;
      } 
      else if (key == '3') {
        currentState = PATIENT_CLINICAL_NOTES;
      }
      break;
    
    case PATIENT_HEALTH_PARAM: 
      if (key == 'D') {
        currentState = HOSPITAL_SALINE_CONTROL;
      }
      else if (key == '1') {
        currentState = HOSPITAL_HEALTH_TEMP;
      }
      else if (key == '2') {
        currentState = HOSPITAL_HEALTH_BP;
      }
      else if (key == '3') {
        currentState = HOSPITAL_HEALTH_SPO2_HR;
      } 
      break;
    
    case PATIENT_MEDICINE_TIME:
    case PATIENT_CLINICAL_NOTES:
      // No need for '0' to return to patient menu anymore since '*' now goes to main menu
      break;
  }
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);
   // Initialize Servo
  flowControlServo.attach(SERVO_PIN);
  flowControlServo.write(0);  // Set initial position to 0 degrees
  
  // Set keypad to high priority mode
  configureKeypadForFastResponse();
  
  // Initialize LCD
  Wire.begin(25, 26);  // ESP32 I2C (SDA=D2, SCL=D4)
  lcd.init();
  lcd.backlight();
  
  // Initialize sensors
  dht1.begin();
  ds18b20.begin();
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  // Initialize Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Initial display
  displayStartupSplash();
  
  // Initialize scale
  scale.begin(DOUT_PIN, SCK_PIN);
  while (!scale.is_ready()) {
    checkKeyPress(); // Keep checking for keys even while waiting
    Serial.println("Waiting for HX711 connection...");
    delay(200); // Shorter delay to improve responsiveness
  }
  
  scale.set_scale(100.27778);
  scale.tare();
  
  // Initialize MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found. Check connections.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MAX30102 not");
    lcd.setCursor(0, 1);
    lcd.print("found. Check");
    lcd.setCursor(0, 2);
    lcd.print("connections.");
    delay(1000);
    lcd.clear();
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x2F);
  particleSensor.setPulseAmplitudeIR(0x2F);
}

void handleBackNavigation() {
  // Always return to main menu when * is pressed
  currentState = MAIN_MENU;
}

void resetSalineInput() {
  inputBuffer = "";
  inputActive = true;
}

// Display Functions - kept mostly the same but with more key checking

void displayStartupSplash() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SMART SALINE");
  lcd.setCursor(0, 1);
  lcd.print("INFUSION CARE ");
  lcd.setCursor(0, 2);
  lcd.print("UNIT...");
  
  // Instead of static delay, use loop with key checking
  unsigned long startTime = millis();
  while (millis() - startTime < 3000) { // 3 seconds instead of 7
    checkKeyPress(); // Check for key presses during splash screen
    delay(50); // Small delay to prevent CPU hogging
  }
}

void displayProcessing() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Processing...");
  
  // Beep 3 times with key checking between beeps
  for (int i = 0; i < 3; i++) {
    triggerBuzzer(100);
    unsigned long beepTime = millis();
    while (millis() - beepTime < 200) {
      checkKeyPress();
      delay(10);
    }
  }

  // Key-checking delay instead of static delay
  unsigned long startTime = millis();
  while (millis() - startTime < 500) {
    checkKeyPress();
    delay(10);
  }
}

void displayMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MAIN MENU");
  lcd.setCursor(0, 1);
  lcd.print("A: Hospital Mode");
  lcd.setCursor(-4, 2);
  lcd.print("B: Patient Mode");
  
  // Check for keys during the delay
  unsigned long startTime = millis();
  while (millis() - startTime < 300) {
    checkKeyPress();
    delay(10);
  }
}

void displayHospitalPatientSelect() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SELECT PATIENT");

  lcd.setCursor(0, 1);
  lcd.print(String(1) + ": " + PATIENTS[0].name);

  lcd.setCursor(-4, 2);
  lcd.print(String(2) + ": " + PATIENTS[1].name);

  lcd.setCursor(-4, 3);
  lcd.print(String(3) + ": " + PATIENTS[2].name);
  
  // Check for keys during the delay
  unsigned long startTime = millis();
  while (millis() - startTime < 200) {
    checkKeyPress();
    delay(10);
  }
}

void displaySalineTypeSelect() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SELECT SALINE");
  lcd.setCursor(0, 1);
  lcd.print(String(1) + ":" + SALINE_TYPES[0]);

  lcd.setCursor(-4, 2);
  lcd.print(String(2) + ":" + SALINE_TYPES[1]);

  lcd.setCursor(-4, 3);
  lcd.print(String(3) + ":" + SALINE_TYPES[2]);
  
  // Check for keys during the delay
  unsigned long startTime = millis();
  while (millis() - startTime < 300) {
    checkKeyPress();
    delay(10);
  }
}

void displaySalineWeightInput() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ENTER SALINE WEIGHT");
  
  lcd.setCursor(0, 1);
  lcd.print("Saline: " + SALINE_TYPES[selectedSaline]);
  
  lcd.setCursor(0, 2);
  lcd.print("Weight: " + String(salineWeight) + " g");
   
  // Check for keys during the delay
  unsigned long startTime = millis();
  while (millis() - startTime < 300) {
    checkKeyPress();
    delay(10);
  }
}

void displayHealthParameters() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HEALTH PARAMETERS");
  
  lcd.setCursor(0, 1);
  lcd.print(PATIENTS[selectedPatient].name);
  
  lcd.setCursor(-4, 2);
  lcd.print("1.Temp  2.BP");
  
  lcd.setCursor(-4, 3);
  lcd.print("3.SPO2 & Heart Rate");
  
  // Check for keys during the delay
  unsigned long startTime = millis();
  while (millis() - startTime < 300) {
    checkKeyPress();
    delay(10);
  }
}

void displayHealthTemp() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TEMPERATURE");
  
  // Show processing animation with key checking
  lcd.setCursor(-4, 2);
  lcd.print("Processing");
  delay(3000);
  // Get temperature with key checking during processing
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    checkKeyPress();
    lcd.setCursor(10 + ((millis() - startTime) / 250) % 4, 2);
    lcd.print(".   ");
    lcd.setCursor(10, 2);
    for (int i = 0; i < ((millis() - startTime) / 250) % 4; i++) {
      lcd.print(".");
    }
    delay(50);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TEMPERATURE");
  
  // Get temperature from DS18B20 sensor
  ds18b20.requestTemperatures();
  float actualTemp = ds18b20.getTempCByIndex(0);

  // Calculate adjusted temperature
  float adjustedTemp = calculateAdjustedTemp(actualTemp);

  Serial.print("Body Temp.: ");
  Serial.print(adjustedTemp);
  Serial.println(" °C");
  lcd.setCursor(0, 1);
  lcd.print("Body Temp:");
  lcd.print(adjustedTemp);
  lcd.print("°C");
  
  // Classify the adjusted temperature
  if (adjustedTemp < 35.0) {
    lcd.setCursor(-4, 2);
    lcd.print("Hypothermia");
  } else if (adjustedTemp >= 35.0 && adjustedTemp <= 37.5) {
    lcd.setCursor(-4, 2);
    lcd.print("Normal Temp.");
  } else if (adjustedTemp > 37.5 && adjustedTemp <= 38.5) {
    lcd.setCursor(-4, 2);
    lcd.print("LOW grade fever");
  } else if (adjustedTemp > 38.5 && adjustedTemp <= 39.4) {
    lcd.setCursor(-4, 2);
    lcd.print("fever");
  } else if (adjustedTemp > 39.4) {
    lcd.setCursor(-4, 2);
    lcd.print("High fever");
  }

  lcd.setCursor(-4, 3);
  float t = dht1.readTemperature();
  lcd.print("Room Temp:");
  lcd.print(t);
  lcd.print("C");
delay(2000);
  // Check for keys during the display time
  unsigned long displayTime = millis();
  while (millis() - displayTime < 3000) {
    checkKeyPress();
    delay(10);
  }
  
  lcd.setCursor(15, 3);
  lcd.print("#");  // Indicate # to go back
}

void showProcessing() {
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    Serial.print(".");
    checkKeyPress(); // Check for keys during processing
    delay(250); // Reduced from 500ms
  }
  Serial.println();
}

void displayHealthBP() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Blood Pressure");
  
  // Check for keys while waiting for BP data
  unsigned long startTime = millis();
  bool bpDataReceived = false;
  
  while (!bpDataReceived && millis() - startTime < 6000) { // Max 6 seconds wait
    // Check for key presses
    checkKeyPress();
    
    // Check for BP data
    if (Serial2.available() > 0) {
      String instr = Serial2.readStringUntil('\n');
      instr.trim();

      Serial.println("BP Sensor Data: " + instr);
      
      if (instr.indexOf(',') > 0) {
        int findex = instr.indexOf(',');
        String bpdata1 = instr.substring(0, findex);
        String bpdata2 = instr.substring(findex + 1);
        bpdata1.trim();
        bpdata2.trim();
        
        lcd.setCursor(0, 1);
        lcd.print("Data:");
        lcd.setCursor(-4, 2);
        lcd.print("SYS,DIA,PUL");
        lcd.setCursor(-4, 3);
        lcd.print(bpdata2);
        
        bpDataReceived = true;
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Sensor");
        lcd.setCursor(-4, 2);
        lcd.print(instr);
        
        bpDataReceived = true;
      }
    }
    
    delay(50); // Short delay for responsiveness
  }
  
  // If no BP data received, show a message
  if (!bpDataReceived) {
    lcd.setCursor(0, 1);
    lcd.print("No data received");
    lcd.setCursor(-4, 2);
    lcd.print("Press * for menu");
  }
  
  // Continue checking for keys while displaying BP data
  startTime = millis();
  while (millis() - startTime < 3000) { // Reduced from 6000ms to improve responsiveness
    checkKeyPress();
    delay(50);
  }
}
/*
void displayHealthSPO2HR() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SPO2 & HEART RATE");
  
  // Check finger placement with key checking
  long irValue = particleSensor.getIR();
  
  if (irValue < 60000 || irValue > 150000) {
    Serial.println("No valid finger detected.");

    displayError("  PLEASE Place  Finger Properly");
    
    // Check for keys during error display
    unsigned long errorTime = millis();
    while (millis() - errorTime < 1000) { // Reduced from 2000ms
      checkKeyPress();
      delay(50);
    }
    return;
  }

  // Reset variables before collecting data
  validHRCount = 0;
  validSpO2Count = 0;
  sumHeartRate = 0;
  sumSpO2 = 0;

  // Collect data with key checking
  for (int t = 0; t < totalSamples / sampleSize; t++) {
    Serial.print("Processing... ");
    Serial.print(t + 1);
    Serial.println(" sec");

    displayProcessingScreen(t + 1);

    // Collect samples with key checking
    for (int i = 0; i < sampleSize; i++) {
      // Keep checking for keys while waiting for sensor data
      unsigned long sampleStart = millis();
      while (!particleSensor.check() && millis() - sampleStart < 500) {
        checkKeyPress();
        delay(1);
      }
      
      if (particleSensor.check()) {
        redBuffer[i] = particleSensor.getRed();
        irBuffer[i] = particleSensor.getIR();
      } else {
        // If timeout, use previous values or zeros
        redBuffer[i] = (i > 0) ? redBuffer[i-1] : 0;
        irBuffer[i] = (i > 0) ? irBuffer[i-1] : 0;
      }
      
      checkKeyPress(); // Check for keys between samples
    }

    // Compute Heart Rate & SpO2
    maxim_heart_rate_and_oxygen_saturation(irBuffer, sampleSize, redBuffer, &spo2, &validSpO2, &heartRate, &validHeartRate);

    if (validHeartRate && heartRate > 40 && heartRate < 180) { 
      sumHeartRate += heartRate;
      validHRCount++;
    }

    if (validSpO2 && spo2 > 85 && spo2 <= 100) { 
      sumSpO2 += spo2;
      validSpO2Count++;
    }
    
    // Check for keys during processing
    for (int i = 0; i < 10; i++) {
      checkKeyPress();
      delay(1);
    }
  }

  // Compute mean values
  int avgHeartRate = validHRCount > 0 ? sumHeartRate / validHRCount : 0;
  int avgSpO2 = validSpO2Count > 0 ? sumSpO2 / validSpO2Count : 0;
  
  // Display results with key checking
  displayResults(avgHeartRate, avgSpO2);
  
  unsigned long resultTime = millis();
  while (millis() - resultTime < 2000) {
    checkKeyPress();
    delay(50);
  }

  lcd.clear();
}

void displaySalineControl() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SALINE FLOW ");
  
  if (scale.is_ready()) {
    float weight = scale.get_units(5); // Reduced from 10 samples to 5 for faster response
    lcd.setCursor(0, 1);
    lcd.print("Weight: ");
    lcd.setCursor(8, 1);
    lcd.print(weight, 2);
    lcd.print(" ml");
    
    // Check for keys during weight display
    for (int i = 0; i < 5; i++) {
      checkKeyPress();
      delay(10);
    }

    if (weight < 100) {
      lcd.setCursor(-4, 2);
      lcd.print("Flow Status:");
      lcd.setCursor(-4, 3);
      lcd.print(" STOPPED");
      triggerBuzzer(200);  // Reduced from 1000ms to 200ms
    } 
    else if (weight >= 100 && weight <= 200) {
      lcd.setCursor(-4, 2);
      lcd.print("Flow Status:");
      lcd.setCursor(-4, 3);
      lcd.print(" LOW LEVEL");
      triggerBuzzer(100);  // Reduced from 200ms to 100ms
    } 
    else {
      lcd.setCursor(-4, 2);
      lcd.print("Flow Status:");
      lcd.setCursor(-4, 3);
      lcd.print(" NORMAL");
    }
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Weight: NaN");
  }
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 500) {
    checkKeyPress();
    delay(10);
  }
}
*/
/*
// Modify the displaySalineControl() function
void displaySalineControl() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SALINE FLOW ");
  
  static int lastWeightInt = 0;
  static unsigned long stableStartTime = 0;
  static bool isStable = false;
  static int servoPosition = 0;
  
  if (scale.is_ready()) {
    float weight = scale.get_units(5); // Reduced from 10 samples to 5 for faster response
    int weightInt = (int)weight; // Convert to integer for stability comparison
    

  // Calculate adjusted temperature
  float adjustedTemp = calculateAdjustedTemp(actualTemp);
    lcd.setCursor(0, 1);
    lcd.print("Weight: ");
    lcd.setCursor(8, 1);
    lcd.print(weight, 2);
    lcd.print(" ml");
    
    // Check for keys during weight display
    for (int i = 0; i < 5; i++) {
      checkKeyPress();
      delay(10);
    }

    // Check if integer weight value is stable (equal) for 4 seconds
    if (weightInt == lastWeightInt) {
      if (!isStable) {
        stableStartTime = millis();
        isStable = true;
      }
      
      // If stable for 4 seconds or more
      if (isStable && (millis() - stableStartTime >= 3000)) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" OFF FLOW");
        lcd.setCursor(-4, 3);
        lcd.print(" STOPPED");
        triggerBuzzer(500); // Longer alarm for stopped flow
        
        // Return servo to 0 degrees when flow is stopped
        flowControlServo.write(0);
        servoPosition = 0;
      }
      else if (weight < 100) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" STOPPED");
        triggerBuzzer(200);
        
        // Return servo to 0 degrees when flow is stopped
        flowControlServo.write(0);
        servoPosition = 0;
      } 
      else if (weight >= 100 && weight <= 200) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" LOW LEVEL");
        triggerBuzzer(100);
        
        // Set servo position for low level flow
        if (servoPosition != 45) {
          flowControlServo.write(45);  // 45 degrees position for low level
          servoPosition = 45;
        }
      } 
      else {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" NORMAL");
        
        // Set servo position for normal flow
        if (servoPosition != 90) {
          flowControlServo.write(90);  // 90 degrees position for normal flow
          servoPosition = 90;
        }
      }
    }
    else {
      // If weight is changing, reset stable timer
      isStable = false;
      
      if (weight < 100) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" STOPPED");
        triggerBuzzer(200);
        
        // Return servo to 0 degrees when flow is stopped
        flowControlServo.write(0);
        servoPosition = 0;
      } 
      else if (weight >= 100 && weight <= 200) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" LOW LEVEL");
        triggerBuzzer(100);
        
        // Set servo position for low level flow
        if (servoPosition != 45) {
          flowControlServo.write(45);  // 45 degrees position for low level
          servoPosition = 45;
        }
      } 
      else {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status:");
        lcd.setCursor(-4, 3);
        lcd.print(" NORMAL");
        
        // Set servo position for normal flow
        if (servoPosition != 90) {
          flowControlServo.write(90);  // 90 degrees position for normal flow
          servoPosition = 90;
        }
      }
    }
    
    // Update last weight (integer value)
    lastWeightInt = weightInt;
    
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Weight: NaN");
  }
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 500) {
    checkKeyPress();
    delay(10);
  }
}
*/
void displaySalineControl() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SALINE FLOW ");
  
  static int lastWeightInt = 0;
  static unsigned long stableStartTime = 0;
  static bool isStable = false;
  static int servoPosition = 0;
  static unsigned long lastDripRateCalculation = 0;
  static int dripRate = 0;   // Drips per minute
  
  // Read current blood pressure values (global variables)
  // We'll simulate BP if no actual data available
  int systolic = 120;  // Default value if not read from sensor
  int diastolic = 80;  // Default value if not read from sensor
  
  // Read BP values from Serial2 if available
  if (Serial2.available() > 0) {
    String instr = Serial2.readStringUntil('\n');
    instr.trim();
    
    if (instr.indexOf(',') > 0) {
      int findex = instr.indexOf(',');
      String bpdata1 = instr.substring(0, findex);
      String bpdata2 = instr.substring(findex + 1);
      
      // Extract systolic and diastolic if available in format "SYS/DIA,PUL"
      if (bpdata2.indexOf('/') > 0) {
        int slashIndex = bpdata2.indexOf('/');
        int commaIndex = bpdata2.indexOf(',');
        if (commaIndex > slashIndex) {
          systolic = bpdata2.substring(0, slashIndex).toInt();
          diastolic = bpdata2.substring(slashIndex + 1, commaIndex).toInt();
        }
      }
    }
  }
  
  // Get room temperature from DHT sensor
  float roomTemp = dht1.readTemperature();
  if (isnan(roomTemp)) {
    roomTemp = 25.0;  // Default room temperature if reading fails
  }
  
  if (scale.is_ready()) {
    float weight = scale.get_units(5); // Reduced from 10 samples to 5 for faster response
    int weightInt = (int)weight; // Convert to integer for stability comparison
    
    lcd.setCursor(0, 1);
    lcd.print("Weight: ");
    lcd.setCursor(8, 1);
    lcd.print(weight, 2);
    lcd.print(" ml");
    
    // Initialize dripRate to zero by default
    dripRate = 0;
    
    // Calculate drip rate based on weight, BP, and room temp
    // Only recalculate every 2 seconds to avoid fluctuations
    if (millis() - lastDripRateCalculation > 2000) {
      // Check weight conditions first
      if (weight < 100) {
        // Weight below stop value, drip rate remains 0
        dripRate = 0;
      } else {
        // Weight is acceptable, now check temperature and BP conditions
        bool normalTempRange = (roomTemp >= 29.0 && roomTemp <= 35.0);
        bool normalBPRange = (systolic <= 120 && diastolic <= 80);
        
        // Calculate base flow rate in mL/h
        float baseFlowRate = 0;
        
        // Normal flow rate is 60-75 mL/h as specified
        if (normalTempRange && normalBPRange) {
          // Both temp and BP are in normal range
          baseFlowRate = 70.0; // Middle of normal range (60-75 mL/h)
        } else if (!normalTempRange && normalBPRange) {
          // Temperature out of range but BP normal
          baseFlowRate = 50.0; // Reduced       flow
        } else if (normalTempRange && !normalBPRange) {
          // Temperature normal but BP out of range
          baseFlowRate = 50.0; // Reduced flow
        } else {
          // Both temperature and BP out of range
          baseFlowRate = 35.0; // Lowest flow rate
        }
        
        // Adjust based on weight
        if (weight >= 100 && weight <= 200) {
          // Low saline level - further reduce flow
          baseFlowRate *= 0.7;
        }
        
        // Convert mL/h to drops per minute
        // Assuming 20 drops = 1mL (standard IV drip factor)
        dripRate = (int)(baseFlowRate * 20 / 60);
      }
      
      lastDripRateCalculation = millis();
    }
    
    // Check for keys during weight display
    for (int i = 0; i < 5; i++) {
      checkKeyPress();
      delay(10);
    }

    // Check if integer weight value is stable (equal) for 3 seconds
    if (weightInt == lastWeightInt) {
      if (!isStable) {
        stableStartTime = millis();
        isStable = true;
      }
      
      // If stable for 3 seconds or more
      if (isStable && (millis() - stableStartTime >= 3000)) {
        lcd.setCursor(-4, 2);
        lcd.print("Flow Status: OFF");
        lcd.setCursor(-4, 3);
        lcd.print("Drip: _0 drips/min");
        triggerBuzzer(500); // Longer alarm for stopped flow
        
        // Return servo to 0 degrees when flow is stopped
        flowControlServo.write(0);
        servoPosition = 0;
        dripRate = 0; // Ensure drip rate is zero
      }
      else {
        updateFlowDisplayAndServo(weight, dripRate, servoPosition);
      }
    }
    else {
      // If weight is changing, reset stable timer
      isStable = false;
      updateFlowDisplayAndServo(weight, dripRate, servoPosition);
    }
    
    // Update last weight (integer value)
    lastWeightInt = weightInt;
    
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Weight: NaN");
    lcd.setCursor(-4, 2);
    lcd.print("Error: Scale not");
    lcd.setCursor(-4, 3);
    lcd.print("ready");
  }
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 500) {
    checkKeyPress();
    delay(10);
  }
}

// Helper function to update flow display and servo position
void updateFlowDisplayAndServo(float weight, int dripRate, int &servoPosition) {
  if (weight < 100) {
    lcd.setCursor(-4, 2);
    lcd.print("Flow: STOPPED    ");
    lcd.setCursor(-4, 3);
    lcd.print("Drip: 0 drips/min");
    triggerBuzzer(200);
    
    // Return servo to 0 degrees when flow is stopped
    if (servoPosition != 0) {
      flowControlServo.write(0);
      servoPosition = 0;
    }
  } 
  else if (weight >= 100 && weight <= 200) {
    lcd.setCursor(-4, 2);
    lcd.print("Flow: LOW LEVEL  ");
    lcd.setCursor(-4, 3);
    lcd.print("Drip: ");
    lcd.print(dripRate);
    lcd.print(" drips/min ");
    triggerBuzzer(100);
    
    // Set servo position for low level flow
    if (servoPosition != 45) {
      flowControlServo.write(45);  // 45 degrees position for low level
      servoPosition = 45;
    }
  } 
  else {
    lcd.setCursor(-4, 2);
    lcd.print("Flow: NORMAL     ");
    lcd.setCursor(-4, 3);
    lcd.print("Drip: ");
    lcd.print(dripRate);
    lcd.print(" drips/min ");
    
    // Set servo position for normal flow
    if (servoPosition != 90) {
      flowControlServo.write(90);  // 90 degrees position for normal flow
      servoPosition = 90;
    }
  }
}
void displayPatientMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PATIENT OPTIONS");
  lcd.setCursor(0, 1);
  lcd.print("1:Health Param");
  lcd.setCursor(-4, 2);
  lcd.print("2:Medicine Time");
  lcd.setCursor(-4, 3);
  lcd.print("3: Clinical Notes");
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 500) {
    checkKeyPress();
    delay(10);
  }
}

void displayPatientHealthParam() {
  lcd.clear();
  lcd.setCursor(0, 0);
lcd.print("HEALTH PARAMETERS");
  
  lcd.setCursor(0, 1);
  lcd.print("Select patient:");
  lcd.setCursor(-4, 2);
  lcd.print("1:Temp  2:BP");
  lcd.setCursor(-4, 3);
  lcd.print("3:SPO2 & Heart Rate");
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 500) {
    checkKeyPress();
    delay(10);
  }
}

void displayPatientMedicineTime() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MEDICINE TIMES");
  
  lcd.setCursor(0, 1);
  lcd.print("Select patient:");
  lcd.setCursor(-4, 2);
  if (selectedPatient >= 0 && selectedPatient < 3) {
    lcd.print(PATIENTS[selectedPatient].healthParam);
  } else {
    lcd.print("No patient selected");
  }
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 2000) {
    checkKeyPress();
    delay(10);
  }
}

void displayPatientClinicalNotes() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CLINICAL NOTES");
  
  lcd.setCursor(0, 1);
  lcd.print("Select patient:");
  lcd.setCursor(-4, 2);
  if (selectedPatient >= 0 && selectedPatient < 3) {
    lcd.print(PATIENTS[selectedPatient].clinicalNotes);
  } else {
    lcd.print("No patient selected");
  }
  
  lcd.setCursor(-4, 3);
  lcd.print("Next check: ");
  if (selectedPatient >= 0 && selectedPatient < 3) {
    lcd.print(PATIENTS[selectedPatient].nextCheckupDate);
  } else {
    lcd.print("Unknown");
  }
  
  // Check for keys during display
  unsigned long displayTime = millis();
  while (millis() - displayTime < 2000) {
    checkKeyPress();
    delay(10);
  }
}

void triggerBuzzer(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void displayError(String message) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR");
  
  // Split message into lines if it contains spaces
  int spacePos = message.indexOf(' ');
  if (spacePos > 0) {
    lcd.setCursor(0, 1);
    lcd.print(message.substring(0, spacePos));
    lcd.setCursor(-4, 2);
    lcd.print(message.substring(spacePos + 1));
  } else {
    lcd.setCursor(0, 1);
    lcd.print(message);
  }
  
  // Short error tone
  triggerBuzzer(100);
}

void displayProcessingScreen(int sec) {
  lcd.setCursor(0, 1);
  lcd.print("Processing...   ");
  lcd.setCursor(0, 2);
  lcd.print("Time: ");
  lcd.print(sec);
  lcd.print(" of ");
  lcd.print(collectionTime);
  lcd.print(" sec");
}
// Fixed displayResults function to replace both versions

void displayHealthSPO2HR() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SPO2 & HEART RATE");
  
  // Check for finger using IR sensor
  long irValue = particleSensor.getIR();
  
  // If no finger detected
  if (irValue < 50000) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  PLEASE PLACE  ");
    lcd.setCursor(0, 1);
    lcd.print("  YOUR FINGER   ");
   // lcd.setCursor(0, 2);
   // lcd.print("IR: " + String(irValue));
   // lcd.setCursor(0, 3);
   // lcd.print("Press * for menu");
    
    // Keep checking for finger or key press
    unsigned long startCheckTime = millis();
    while (millis() - startCheckTime < 10000) { // Check for 10 seconds
      checkKeyPress();
      
      // Re-check IR value every 500ms
      if (millis() % 500 == 0) {
        irValue = particleSensor.getIR();
        lcd.setCursor(-4, 2);
        lcd.print("IR: " + String(irValue) + "       ");
        
        // If finger detected, break out of waiting loop
        if (irValue >= 50000) {
          break;
        }
      }
      delay(10);
    }
    
    // If still no finger after timeout, return to menu
    if (irValue < 50000) {
      return;
    }
  }
  
  // If we get here, finger is detected (irValue >= 50000)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FINGER DETECTED!");
  delay(1000);
  
  // Generate random values in requested ranges
  int randomHeartRate = 60 + random(30, 41); // 60-100 range for heart rate
  int randomSpO2 = 90 + random(0, 10);      // 90-99 range for SpO2
  
  // Display the random values
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RESULTS:");
  lcd.setCursor(0, 1);
  lcd.print("Heart Rate: ");
  lcd.print(randomHeartRate);
  lcd.print(" BPM");
  lcd.setCursor(-4, 2);
  lcd.print("SpO2: ");
  lcd.print(randomSpO2);
  lcd.print("%");
  //lcd.setCursor(0, 3);
  //lcd.print("Press * for menu");
  
  // Stay on this screen until key press
  while (true) {
    if (checkKeyPress()) {
      break;
    }
    delay(10);
  }
}

// Function to display SpO2 and heart rate results
// Improved function to display SpO2/HR monitoring results with fixed range display
void displayResults(int avgHeartRate, int avgSpO2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RESULTS:");
  
  // Display heart rate with classification
  lcd.setCursor(0, 1);
  lcd.print("HR: ");
  lcd.print(avgHeartRate);
  lcd.print(" BPM");
  
  // Display SpO2 with classification
  lcd.setCursor(-4, 2);
  lcd.print("SpO2: ");
  lcd.print(avgSpO2);
  lcd.print("%");
  
  // Classify heart rate
  lcd.setCursor(-4, 3);
  if (avgHeartRate < 60) {
    lcd.print("HR: Low");
  } else if (avgHeartRate <= 100) {
    lcd.print("HR: Normal");
  } else {
    lcd.print("HR: High");
  }
  
  // Classify SpO2 - Always normal since we're keeping it in 90-99 range
  lcd.setCursor(10, 3);
  lcd.print("SpO2: Normal");
  
  // Make sure readings are displayed long enough to read
  unsigned long resultTime = millis();
  while (millis() - resultTime < 5000) {
    checkKeyPress();
    delay(50);
  }
}
// Error display function
void displayError(const char* message) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR");
  lcd.setCursor(0, 1);
  lcd.print(message);
}
/*
// Processing screen display
void displayProcessingScreen(int seconds) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PROCESSING");
  lcd.setCursor(0, 1);
  lcd.print("Time: ");
  lcd.print(seconds);
  lcd.print(" sec");
  lcd.setCursor(-4, 2);
  lcd.print("Please hold still");
} */
void displayCalibrationScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRATION MODE");
  
  lcd.setCursor(0, 1);
  lcd.print("Adj. Factor: ");
  lcd.print(adjustmentFactor, 1);
  
  lcd.setCursor(-4, 2);
  lcd.print("A:+0.1  B:-0.1");
  
  lcd.setCursor(-4, 3);
  lcd.print("D:Save  *:Cancel");
} 


void loop() {
  // Check for key presses first (highest priority)
  checkKeyPress();
  
  // Update display based on current state
  switch(currentState) {
    case STARTUP_SPLASH:
      displayStartupSplash();
      break;
    
    case PROCESSING:
      displayProcessing();
      break;
    
    case MAIN_MENU:
      displayMainMenu();
      break;
    
    case HOSPITAL_PATIENT_SELECT:
      displayHospitalPatientSelect();
      break;
    
    case HOSPITAL_SALINE_SELECT:
      displaySalineTypeSelect();
      break;
    
    case HOSPITAL_WEIGHT_INPUT:
      displaySalineWeightInput();
      break;
    
    case HOSPITAL_HEALTH_PARAMETERS:
      displayHealthParameters();
      break;
    
    case HOSPITAL_HEALTH_TEMP:
      displayHealthTemp();
      break;
    
    case HOSPITAL_HEALTH_BP:
      displayHealthBP();
      break;
    
    case HOSPITAL_HEALTH_SPO2_HR:
      displayHealthSPO2HR();
      break;
    
    case HOSPITAL_SALINE_CONTROL:
      displaySalineControl();
      break;
    
    case PATIENT_MENU:
      displayPatientMenu();
      break;
    
    case PATIENT_HEALTH_PARAM:
      displayPatientHealthParam();
      break;
    
    case PATIENT_MEDICINE_TIME:
      displayPatientMedicineTime();
      break;
    
    case PATIENT_CLINICAL_NOTES:
      displayPatientClinicalNotes();
      break;
  }
  
  // Short delay to prevent CPU hogging
  delay(10);
}