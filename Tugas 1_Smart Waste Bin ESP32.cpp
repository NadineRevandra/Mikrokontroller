#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>

// DEKLARASI PIN
#define OLED_SDA      21   // I2C SDA
#define OLED_SCL      22   // I2C SCL
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define TRIG_PIN      4
#define ECHO_PIN      16

#define DHT_PIN       27
#define DHT_TYPE      DHT11

#define SERVO_PIN     13   // PWM Output
#define BUZZER_PIN    14

#define BUTTON_PIN    15   // INTERRUPT Pin (Push Button Manual)
#define POT_PIN       34   // ADC Pin (Potensiometer)

// KONFIGURASI
const int SERVO_OPEN_ANGLE   = 0;      // Sudut TERBUKA
const int SERVO_CLOSE_ANGLE  = 90;     // Sudut TERTUTUP
const unsigned long HOLD_TIME = 5000;  // Durasi terbuka otomatis via sensor (5 detik)

// OBJEK & VARIABEL
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);
Servo servoSampah;

bool isLidOpen = false;
bool manualMode = false;               // Status mode manual dari Push Button
unsigned long openTimer = 0;
unsigned long lastDHTReadTime = 0;

float humidity = 0.0;
float temperature = 0.0;
int rawADC = 0;                        // Nilai mentah ADC Potensio (0 - 4095)
int distanceThreshold = 15;            // Batas jarak pemicu otomatis (cm)

// Variabel Interrupt
volatile bool buttonStateChanged = false;
unsigned long lastDebounceTime = 0;

// ISR (INTERRUPT SERVICE ROUTINE)
void IRAM_ATTR handleButtonInterrupt() {
  if (millis() - lastDebounceTime > 300) { // Anti-debounce 300 ms
    buttonStateChanged = true;
    lastDebounceTime = millis();
  }
}

// FUNGSI BUZZER & SERVO
void bunyiPip() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(120);
  digitalWrite(BUZZER_PIN, LOW);
}

void putarServoSatuKali(int sudut) {
  servoSampah.attach(SERVO_PIN, 500, 2400);
  servoSampah.write(sudut);
  delay(600);
  servoSampah.detach(); // Matikan sinyal agar servo diam total
}

float readSingleDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  if (duration == 0) return 999.0;

  return (duration * 0.0343) / 2.0;
}

// UPDATE DISPLAY (I2C) & SERIAL MONITOR (UART)
void updateDisplayAndUART() {
  // 1. Tampilan Layar OLED (I2C)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("=== SMART TRASH ==="));
  
  display.setCursor(0, 14);
  display.print(F("Status : "));
  display.println(isLidOpen ? F("[ TERBUKA ]") : F("[ TERTUTUP ]"));

  display.setCursor(0, 26);
  display.print(F("Mode   : "));
  display.println(manualMode ? F("MANUAL") : F("OTOMATIS"));

  display.setCursor(0, 38);
  display.print(F("T/Hum: ")); 
  display.print(temperature, 1); display.print(F("C / "));
  display.print(humidity, 1); display.println(F("%"));

  display.setCursor(0, 50);
  display.print(F("Set Jarak: ")); 
  display.print(distanceThreshold); display.println(F(" cm"));

  display.display();

  // 2. Transmisi Data UART (Serial Logging ke Komputer)
  Serial.print(F("[UART LOG] Status: ")); Serial.print(isLidOpen ? F("OPEN") : F("CLOSE"));
  Serial.print(F(" | Mode: ")); Serial.print(manualMode ? F("MANUAL") : F("AUTO"));
  Serial.print(F(" | ADC Potensio: ")); Serial.print(rawADC);
  Serial.print(F(" | Set Jarak: ")); Serial.print(distanceThreshold); Serial.println(F(" cm"));
}

// ==================== SETUP ====================
void setup() {
  // Inisialisasi UART Communication (115200 Baud)
  Serial.begin(115200);

  // Inisialisasi Pin Sinyal
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Inisialisasi External Interrupt pada Push Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  // Inisialisasi Input ADC Potensiometer
  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db); // Setting ADC membaca 0V - 3.3V

  // Inisialisasi PWM Servo
  ESP32PWM::allocateTimer(0);
  servoSampah.setPeriodHertz(50);
  putarServoSatuKali(SERVO_CLOSE_ANGLE); // Posisi awal tertutup

  // Inisialisasi Sensor DHT11
  dht.begin();

  // Inisialisasi OLED Display (I2C)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED SSD1306 Gagal Diinisialisasi!"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 25);
    display.println(F("Sistem Siap..."));
    display.display();
    delay(1000);
  }
}
// ==================== MAIN LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Baca Sensor DHT11 & Pembacaan ADC Potensio
  if (currentMillis - lastDHTReadTime >= 500) { 
    lastDHTReadTime = currentMillis;
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
    
    // Baca ADC Potensiometer (0 - 4095) dan konversi jadi Set Jarak (5 cm - 30 cm)
    rawADC = analogRead(POT_PIN);
    distanceThreshold = map(rawADC, 0, 4095, 5, 30);
  }

  // 2. LOGIKA INTERRUPT (Push Button Toggle Buka/Tutup Instan)
  if (buttonStateChanged) {
    buttonStateChanged = false; // Reset pemicu interrupt
    
    if (!isLidOpen) {
      // Jika TERTUTUP -> Tekan tombol -> BUKA LANGSUNG (Mode Manual Aktif)
      Serial.println(F(">>> INTERRUPT: Buka Manual Ditekan <<<"));
      bunyiPip();
      putarServoSatuKali(SERVO_OPEN_ANGLE);
      isLidOpen = true;
      manualMode = true; // Kunci ke mode manual agar timer 5 detik tidak jalan
    } else {
      // Jika TERBUKA -> Tekan tombol lagi -> TUTUP LANGSUNG SEKETIKA
      Serial.println(F(">>> INTERRUPT: Tutup Manual Ditekan <<<"));
      bunyiPip();
      putarServoSatuKali(SERVO_CLOSE_ANGLE);
      isLidOpen = false;
      manualMode = false; // Kembali ke mode otomatis
    }
  }

  // 3. LOGIKA OTOMATIS SENSOR ULTRASONIK (Hanya jalan jika TIDAK dalam Mode Manual Button)
  if (!manualMode) {
    float distance = readSingleDistance();

    // A. Tangan Mendekat (<= Batas Jarak Potensio) -> Buka Otomatis 5 Detik
    if (!isLidOpen && (distance <= distanceThreshold && distance > 2.0)) {
      Serial.println(F("Tangan Terdeteksi -> Membuka Otomatis"));
      bunyiPip();
      putarServoSatuKali(SERVO_OPEN_ANGLE);
      isLidOpen = true;
      openTimer = currentMillis;
    }

    // B. Setelah 5 Detik Terbuka via Sensor -> Menutup Otomatis
    if (isLidOpen && (currentMillis - openTimer >= HOLD_TIME)) {
      Serial.println(F("Sudah 5 Detik -> Menutup Otomatis"));
      putarServoSatuKali(SERVO_CLOSE_ANGLE);
      isLidOpen = false;
    }
  }
  // 4. Update OLED Display (I2C) & Serial Log (UART)
  updateDisplayAndUART();

  delay(100);
}
