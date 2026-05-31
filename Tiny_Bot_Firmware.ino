// Companion Bot — Full Integrated Firmware
// Newton School of Technology
// Components: NeoPixel · Buzzer · Touch · OLED
// Feature:    Live weather → mood via WiFi API
// Game:       Reaction Timer (double-tap to start)
// ─────────────────────────────────────────────

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ── WiFi + API credentials ──────────────────
const char* ssid       = "Wokwi-GUEST";
const char* password   = "";
const char* apiKey     = "9365f0dab87e313928ed176006f2b886";
const char* city       = "Bangalore";

// ── Pin definitions ─────────────────────────
const int BUZZER_PIN   = 13;
const int NEO_PIN      = 25;
const int TOUCH_PIN    = 27;
const int SDA_PIN      = 21;
const int SCL_PIN      = 22;

// ── OLED setup ──────────────────────────────
#define SCREEN_W 128
#define SCREEN_H  64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire);

// ── NeoPixel setup ──────────────────────────
Adafruit_NeoPixel pixel(1, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ── Global state ────────────────────────────
String moods[]        = {"happy", "sad", "angry", "calm"};
int    moodIndex      = 0;
float  currentTemp    = 0;
String weatherDesc    = "";
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 600000;

// ── Touch / double-tap detection ────────────
int  lastTouchState       = HIGH;
unsigned long lastTapTime = 0;
bool waitingSecondTap     = false;
const unsigned long DOUBLE_TAP_WINDOW = 400;

// ── Game state ──────────────────────────────
bool  gameActive      = false;
bool  waitingForFlash = false;
unsigned long flashAt = 0;
unsigned long reactionStart = 0;
bool  goSignalShown   = false;
bool  cheated         = false;
unsigned int  bestTime = 9999;

void setMoodColor(String mood) {
  if      (mood == "happy")  pixel.setPixelColor(0, pixel.Color(255, 200, 0));
  else if (mood == "sad")    pixel.setPixelColor(0, pixel.Color(0,   50,  255));
  else if (mood == "angry")  pixel.setPixelColor(0, pixel.Color(255, 0,   0));
  else if (mood == "calm")   pixel.setPixelColor(0, pixel.Color(0,   200, 180));
  pixel.show();
}

void beepMood(String mood) {
  if (mood == "happy") {
    tone(BUZZER_PIN, 659, 100); delay(120);
    tone(BUZZER_PIN, 784, 100); delay(120);
    tone(BUZZER_PIN, 880, 150); delay(200);
  } else if (mood == "sad") {
    tone(BUZZER_PIN, 294, 500); delay(600);
  } else if (mood == "angry") {
    for (int i = 0; i < 3; i++) {
      tone(BUZZER_PIN, 880, 80); delay(100);
    }
  } else if (mood == "calm") {
    tone(BUZZER_PIN, 262, 300); delay(350);
    tone(BUZZER_PIN, 330, 300); delay(350);
  }
}

void playStartupJingle() {
  tone(BUZZER_PIN, 523, 150); delay(180);
  tone(BUZZER_PIN, 659, 150); delay(180);
  tone(BUZZER_PIN, 784, 150); delay(180);
  tone(BUZZER_PIN, 880, 300); delay(800);
}

void beepGo() {
  tone(BUZZER_PIN, 1200, 80);
}

void beepSuccess(unsigned int ms) {
  int freq = map(constrain(ms, 100, 800), 100, 800, 1400, 600);
  tone(BUZZER_PIN, freq, 100); delay(120);
  tone(BUZZER_PIN, freq + 200, 150); delay(200);
}

void beepFail() {
  tone(BUZZER_PIN, 200, 400); delay(450);
}

void drawFace(String mood) {
  display.clearDisplay();
  if (mood == "happy") {
    display.fillRoundRect(30, 14, 20, 22, 4, SSD1306_WHITE);
    display.fillRoundRect(78, 14, 20, 22, 4, SSD1306_WHITE);
    display.drawLine(44, 46, 56, 52, SSD1306_WHITE);
    display.drawLine(56, 52, 72, 52, SSD1306_WHITE);
    display.drawLine(72, 52, 84, 46, SSD1306_WHITE);
  } else if (mood == "sad") {
    display.fillCircle(40, 24, 8, SSD1306_WHITE);
    display.fillCircle(88, 24, 8, SSD1306_WHITE);
    display.drawLine(44, 52, 56, 46, SSD1306_WHITE);
    display.drawLine(56, 46, 72, 46, SSD1306_WHITE);
    display.drawLine(72, 46, 84, 52, SSD1306_WHITE);
  } else if (mood == "angry") {
    display.fillRect(28, 18, 22, 16, SSD1306_WHITE);
    display.fillRect(78, 18, 22, 16, SSD1306_WHITE);
    display.drawLine(28, 18, 50, 25, SSD1306_BLACK);
    display.drawLine(78, 25, 100, 18, SSD1306_BLACK);
    display.drawLine(44, 50, 84, 50, SSD1306_WHITE);
  } else if (mood == "calm") {
    display.drawRoundRect(30, 16, 20, 20, 6, SSD1306_WHITE);
    display.drawRoundRect(78, 16, 20, 20, 6, SSD1306_WHITE);
    display.drawLine(50, 50, 78, 50, SSD1306_WHITE);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(weatherDesc.substring(0, 13));
  display.print(" ");
  display.print((int)currentTemp);
  display.print("C");
  display.display();
}

void drawGameReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 5);  display.print("REACTION TIMER");
  display.setCursor(10, 22); display.print("Wait for the flash,");
  display.setCursor(10, 34); display.print("then tap FAST!");
  display.setCursor(18, 50); display.print("Get ready...");
  display.display();
  pixel.setPixelColor(0, pixel.Color(80, 0, 0));
  pixel.show();
}

void drawGameGo() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(38, 20);
  display.print("GO!");
  display.display();
  pixel.setPixelColor(0, pixel.Color(0, 255, 0));
  pixel.show();
  beepGo();
}

void drawGameResult(unsigned int ms, bool isNewBest) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 2); display.print("YOUR TIME:");
  display.setTextSize(2);
  display.setCursor(28, 18);
  display.print(ms); display.print("ms");
  display.setTextSize(1);
  if (isNewBest) {
    display.setCursor(22, 44); display.print("** NEW BEST! **");
  } else {
    display.setCursor(14, 44);
    display.print("Best: "); display.print(bestTime); display.print("ms");
  }
  display.setCursor(8, 56); display.print("2x tap to play again");
  display.display();
}

void drawGameCheat() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 10); display.print("TOO EARLY!");
  display.setCursor(12, 28); display.print("Wait for GO! :p");
  display.setCursor(8, 50);  display.print("2x tap to try again");
  display.display();
  pixel.setPixelColor(0, pixel.Color(255, 50, 0));
  pixel.show();
  beepFail();
}

void drawGameExit() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 24); display.print("Back to mood bot!");
  display.display();
  delay(1000);
}

void startGameRound() {
  gameActive      = true;
  waitingForFlash = true;
  goSignalShown   = false;
  cheated         = false;
  unsigned long waitMs = random(1500, 4500);
  flashAt = millis() + waitMs;
  drawGameReady();
  Serial.print("[Game] Round started. GO in "); Serial.print(waitMs); Serial.println("ms");
}

void handleGameTap() {
  if (!gameActive) return;
  if (waitingForFlash && !goSignalShown) {
    cheated         = true;
    waitingForFlash = false;
    drawGameCheat();
    Serial.println("[Game] Tapped too early!");
  } else if (goSignalShown) {
    unsigned int elapsed = (unsigned int)(millis() - reactionStart);
    bool isNewBest = (elapsed < bestTime);
    if (isNewBest) bestTime = elapsed;
    drawGameResult(elapsed, isNewBest);
    beepSuccess(elapsed);
    pixel.setPixelColor(0, pixel.Color(0, 180, 255));
    pixel.show();
    Serial.print("[Game] Reaction time: "); Serial.print(elapsed); Serial.println("ms");
    goSignalShown   = false;
    waitingForFlash = false;
    gameActive      = false;
  }
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Weather] WiFi not connected, skipping fetch.");
    return;
  }
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q="
             + String(city)
             + "&appid=" + String(apiKey)
             + "&units=metric";
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    Serial.println("[Weather] Raw payload:"); Serial.println(payload);
    Serial.print("[Weather] Payload length: "); Serial.println(payload.length());
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("[Weather] JSON parse error: "); Serial.println(err.c_str());
      http.end(); return;
    }
    currentTemp = doc["main"]["temp"].as<float>();
    weatherDesc = doc["weather"][0]["description"].as<String>();
    Serial.print("[Weather] Temp: "); Serial.println(currentTemp);
    Serial.print("[Weather] Desc: "); Serial.println(weatherDesc);
    if      (currentTemp > 33)  moodIndex = 2;
    else if (currentTemp > 25)  moodIndex = 0;
    else if (currentTemp > 18)  moodIndex = 3;
    else                         moodIndex = 1;
  } else {
    Serial.print("[Weather] HTTP error code: "); Serial.println(code);
  }
  http.end();
}

void onSingleTap() {
  if (gameActive) { handleGameTap(); return; }
  moodIndex = (moodIndex + 1) % 4;
  String mood = moods[moodIndex];
  setMoodColor(mood);
  beepMood(mood);
  drawFace(mood);
  Serial.println("Mood: " + mood);
}

void onDoubleTap() {
  if (!gameActive) {
    startGameRound();
  } else if (!waitingForFlash) {
    startGameRound();
  } else {
    gameActive = false; waitingForFlash = false; goSignalShown = false;
    drawGameExit();
    setMoodColor(moods[moodIndex]);
    drawFace(moods[moodIndex]);
    Serial.println("[Game] Exited game.");
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT_PULLUP);
  pixel.begin();
  pixel.setBrightness(80);
  pixel.clear(); pixel.show();
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.display();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print("Connecting WiFi...");
  display.display();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500); tries++; Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
    fetchWeather();
    lastWeatherFetch = millis();
  } else {
    Serial.print("\n[WiFi] Failed. Status: ");
    Serial.println(WiFi.status());
  }
  playStartupJingle();
  setMoodColor(moods[moodIndex]);
  drawFace(moods[moodIndex]);
}

void loop() {
  unsigned long now = millis();
  int state = digitalRead(TOUCH_PIN);
  if (state == LOW && lastTouchState == HIGH) {
    if (waitingSecondTap && (now - lastTapTime) < DOUBLE_TAP_WINDOW) {
      waitingSecondTap = false;
      onDoubleTap();
    } else {
      waitingSecondTap = true;
      lastTapTime = now;
    }
    delay(50);
  }
  if (waitingSecondTap && (now - lastTapTime) > DOUBLE_TAP_WINDOW) {
    waitingSecondTap = false;
    onSingleTap();
  }
  lastTouchState = state;
  if (gameActive && waitingForFlash && !goSignalShown && now >= flashAt) {
    goSignalShown   = true;
    waitingForFlash = false;
    reactionStart   = millis();
    drawGameGo();
  }
  if (!gameActive && (now - lastWeatherFetch > WEATHER_INTERVAL)) {
    fetchWeather();
    lastWeatherFetch = now;
    drawFace(moods[moodIndex]);
  }
}
