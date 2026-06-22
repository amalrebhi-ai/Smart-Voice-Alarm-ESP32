#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Arduino.h>
#include <pgmspace.h>
#include "audio.h"
#include "audio1.h"

#define AUDIO_PIN 25

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
WiFiClient client;


const char* ssid = "Amal";
const char* password = "amal1234amalamal";
const char* serverIP = "192.168.152.109";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// ===== TIMERS =====
unsigned long lastLCD  = 0;
unsigned long lastHTTP = 0;
const unsigned long LCD_INTERVAL  = 1000;
const unsigned long HTTP_INTERVAL = 30000;

// ===== Variables partagées =====
int g_day, g_month, g_year, g_hour, g_minute, g_second;
String g_action = "";
String g_heure  = "";

// ===== Variables alarme =====
bool   alarmActive   = false;
int    alarmHour     = -1;
int    alarmMinute   = -1;
bool   alarmRinging  = false;  // sonnerie en cours

// ===================================================
// AUDIO
// ===================================================
void playAlarmSound() {
  Serial.println("🔔 Alarme ! Lecture audio...");
  for (uint32_t i = 44; i < audioDataSize; i++) {
    uint8_t sample = pgm_read_byte(&audioData[i]);
    dacWrite(AUDIO_PIN, sample);
    delayMicroseconds(62); // 16000 Hz
  }
  Serial.println("Fin lecture alarme");
}

void playDeactivateSound() {
  Serial.println("🔕 Désactivation alarme, lecture audio1...");
  for (uint32_t i = 44; i < audioDataSize1; i++) {
    uint8_t sample = pgm_read_byte(&audioData1[i]);
    dacWrite(AUDIO_PIN, sample);
    delayMicroseconds(62);
  }
  Serial.println("Fin lecture désactivation");
}

// ===================================================
// ALARME : parse "HH:MM" depuis g_heure
// ===================================================
bool parseAlarmTime(const String& heureStr, int& h, int& m) {
  // Format attendu : "HH:MM"
  int colonPos = heureStr.indexOf(":");
  if (colonPos == -1) return false;

  String hStr = heureStr.substring(0, colonPos);
  String mStr = heureStr.substring(colonPos + 1);

  h = hStr.toInt();
  m = mStr.toInt();

  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  return true;
}

void setAlarm(const String& heureStr) {
  int h, m;
  if (!parseAlarmTime(heureStr, h, m)) {
    Serial.println("Heure alarme invalide : " + heureStr);
    return;
  }

  alarmHour    = h;
  alarmMinute  = m;
  alarmActive  = true;
  alarmRinging = false;

  // Calcul temps restant
  int nowTotalMin   = g_hour * 60 + g_minute;
  int alarmTotalMin = alarmHour * 60 + alarmMinute;
  int diffMin       = alarmTotalMin - nowTotalMin;

  if (diffMin < 0) diffMin += 24 * 60; // alarme demain

  int restH = diffMin / 60;
  int restM = diffMin % 60;

  Serial.printf("✅ Alarme réglée à %02d:%02d (dans %dh%02d)\n",
                alarmHour, alarmMinute, restH, restM);

  // Affichage sur LCD ligne 2
  lcd.setCursor(0, 1);
  lcd.print("Alarm:");
  if (alarmHour   < 10) lcd.print("0"); lcd.print(alarmHour);
  lcd.print(":");
  if (alarmMinute < 10) lcd.print("0"); lcd.print(alarmMinute);
  lcd.print(" -");
  if (restH < 10) lcd.print("0"); lcd.print(restH);
  lcd.print("h");
  if (restM < 10) lcd.print("0"); lcd.print(restM);
}

void deactivateAlarm() {
  alarmActive  = false;
  alarmRinging = false;
  alarmHour    = -1;
  alarmMinute  = -1;

  Serial.println("🔕 Alarme désactivée");
  playDeactivateSound();
}

// ===================================================
// VÉRIFICATION ALARME (appelée chaque seconde)
// ===================================================
void checkAlarm() {
  if (!alarmActive || alarmRinging) return;

  if (g_hour == alarmHour && g_minute == alarmMinute && g_second == 0) {
    alarmRinging = true;
    Serial.println("🔔 Heure de l'alarme !");
    playAlarmSound();
    // La sonnerie joue une fois, l'alarme reste active jusqu'à deactivate_alarm
    alarmRinging = false;
  }
}

// ===================================================
// TEMPS
// ===================================================
void updateTime() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    DateTime ntpTime(epochTime);
    rtc.adjust(ntpTime);
    g_day    = ntpTime.day();
    g_month  = ntpTime.month();
    g_year   = ntpTime.year();
    g_hour   = ntpTime.hour();
    g_minute = ntpTime.minute();
    g_second = ntpTime.second();
  } else {
    DateTime now = rtc.now();
    g_day    = now.day();
    g_month  = now.month();
    g_year   = now.year();
    g_hour   = now.hour();
    g_minute = now.minute();
    g_second = now.second();
  }
}

// ===================================================
// LCD
// ===================================================
void updateLCD() {
  updateTime();
  checkAlarm();

  lcd.clear();

  // Ligne 0 : date
  lcd.setCursor(0, 0);
  lcd.print("Date:");
  if (g_day   < 10) lcd.print("0"); lcd.print(g_day);   lcd.print("/");
  if (g_month < 10) lcd.print("0"); lcd.print(g_month); lcd.print("/");
  lcd.print(g_year);

  // Ligne 1 : heure OU alarme si active
  lcd.setCursor(0, 1);
  if (alarmActive) {
    // Affiche "Alrm HH:MM" au lieu de l'heure courante
    lcd.print("Alrm ");
    if (alarmHour   < 10) lcd.print("0"); lcd.print(alarmHour);
    lcd.print(":");
    if (alarmMinute < 10) lcd.print("0"); lcd.print(alarmMinute);
  } else {
    lcd.print("Heure:");
    if (g_hour   < 10) lcd.print("0"); lcd.print(g_hour);   lcd.print(":");
    if (g_minute < 10) lcd.print("0"); lcd.print(g_minute); lcd.print(":");
    if (g_second < 10) lcd.print("0"); lcd.print(g_second);
  }
}

// ===================================================
// HTTP
// ===================================================
void doHTTPRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Pas de WiFi, requête ignorée.");
    return;
  }

  Serial.println("Connexion au serveur...");
  if (!client.connect(serverIP, 5000)) {
    Serial.println("Connexion serveur échouée !");
    return;
  }

  client.println("GET /command HTTP/1.1");
  client.println("Host: 192.168.40.78");
  client.println("Connection: close");
  client.println();

  unsigned long start = millis();
  String response = "";

  while (millis() - start < 5000) {
    while (client.available()) {
      response += (char)client.read();
      start = millis();
    }
    if (!client.connected()) break;
    delay(1);
  }
  client.stop();

  Serial.println("=== RÉPONSE ===");
  Serial.println(response);

  // Parse JSON
  int jsonStart = response.indexOf("{");
  if (jsonStart == -1) {
    Serial.println("Aucun JSON trouvé !");
    return;
  }

  String json = response.substring(jsonStart);

  // Extraire "action"
  int p = json.indexOf("\"action\":\"");
  if (p != -1) {
    int s = p + 10;
    g_action = json.substring(s, json.indexOf("\"", s));
    Serial.println("Action: " + g_action);
  }

  // Extraire "heure"
  p = json.indexOf("\"heure\":\"");
  if (p != -1) {
    int s = p + 9;
    g_heure = json.substring(s, json.indexOf("\"", s));
    Serial.println("Heure: " + g_heure);
  }

  // ===== Traitement des actions =====
  if (g_action == "set_alarm") {
    setAlarm(g_heure);

  } else if (g_action == "deactivate_alarm") {
    deactivateAlarm();
  }
}

// ===================================================
// SETUP / LOOP
// ===================================================
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  if (!rtc.begin()) {
    Serial.println("RTC non détecté !");
    while (1);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecté !");
    timeClient.begin();
  } else {
    Serial.println("\nMode RTC (sans WiFi)");
  }
}

void loop() {
  unsigned long now = millis();

  // Tâche 1 : LCD + vérification alarme toutes les secondes
  if (now - lastLCD >= LCD_INTERVAL) {
    lastLCD = now;
    updateLCD();
  }

  // Tâche 2 : requête serveur toutes les 30 secondes
  if (now - lastHTTP >= HTTP_INTERVAL) {
    lastHTTP = now;
    doHTTPRequest();
  }
}