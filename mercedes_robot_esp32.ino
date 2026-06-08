/*
 * ============================================================
 *  Mercedes-Benz Museum Robot
 *  ESP32 DevKit V1 — Volledige code
 *  Functies: Lijnvolgen, Obstakelvermijding, MQTT, LEDs, Knoppen
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
//  WiFi & MQTT instellingen
// ============================================================
const char* WIFI_SSID     = "JOUW_WIFI_NAAM";
const char* WIFI_PASS     = "JOUW_WIFI_WACHTWOORD";
const char* MQTT_SERVER   = "192.168.1.XXX";   // IP van Raspberry Pi
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "robot_1";

// MQTT Topics
const char* TOPIC_STATUS    = "mercedes/robot1/status";
const char* TOPIC_AFSTAND   = "mercedes/robot1/afstand";
const char* TOPIC_BATTERIJ  = "mercedes/robot1/batterij";
const char* TOPIC_FOUT      = "mercedes/robot1/fout";
const char* TOPIC_COMMANDO  = "mercedes/robot1/commando";  // ontvangen

// ============================================================
//  PIN DEFINITIES
// ============================================================
// IR Lijnvolg sensoren (TCRT5000)
#define IR_LINKS    34
#define IR_MIDDEN   35
#define IR_RECHTS   32

// HC-SR04 Ultrasone sensoren
#define VOOR_TRIG   27
#define VOOR_ECHO   14
#define LINKS_TRIG  26
#define LINKS_ECHO  25
#define RECHTS_TRIG 33
#define RECHTS_ECHO 23

// Motor L298N
#define IN1  16
#define IN2  17
#define ENA  4
#define IN3  18
#define IN4  19
#define ENB  5

// Knoppen
#define KNOP_STOP   2
#define KNOP_SKIP   13

// LEDs
#define LED_GROEN   21
#define LED_GEEL    22
#define LED_ROOD    15

// ============================================================
//  PWM INSTELLINGEN
// ============================================================
#define PWM_FREQ    1000
#define PWM_RES     8
#define SNELHEID    160     // Rijsnelheid 0-255
#define DRAAI_SNEL  130     // Draaisnelheid

// ============================================================
//  DREMPELWAARDEN
// ============================================================
#define OBSTAKEL_AFSTAND    25   // cm — stop als object dichter is
#define DWARSLIJN_TIJD      30000 // 30 seconden wachten bij dwarslijn
#define STOP_TIJD           30000 // 30 seconden wachten bij knop stop
#define VASTGELOPEN_TIJD    5000  // 5 seconden op zelfde plek = vastgelopen
#define BATTERIJ_GEEL       30   // % — gele LED
#define BATTERIJ_ROOD       10   // % — rode LED + stop

// ============================================================
//  GLOBALE VARIABELEN
// ============================================================
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// Robot toestand
enum Toestand {
  RIJDEN,
  GESTOPT_KNOP,
  GESTOPT_COMMANDO,
  GESTOPT_BATTERIJ,
  OBSTAKEL_VERMIJDEN,
  DWARSLIJN_WACHT,
  VASTGELOPEN
};

Toestand toestand = RIJDEN;

unsigned long stopTijd        = 0;
unsigned long vastgelopenTijd = 0;
unsigned long mqttTijd        = 0;
unsigned long positieTijd     = 0;

bool remoteStop    = false;
bool vastgelopen   = false;
int  positieTeller = 0;

// ============================================================
//  WIFI & MQTT FUNCTIES
// ============================================================
void verbindWifi() {
  Serial.print("WiFi verbinden...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int pogingen = 0;
  while (WiFi.status() != WL_CONNECTED && pogingen < 20) {
    delay(500);
    Serial.print(".");
    pogingen++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Verbonden! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println(" WiFi MISLUKT — robot rijdt zonder MQTT");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String bericht = "";
  for (int i = 0; i < length; i++) bericht += (char)payload[i];
  Serial.println("MQTT ontvangen: " + bericht);

  if (bericht == "STOP") {
    remoteStop = true;
    toestand = GESTOPT_COMMANDO;
    stopMotoren();
    mqtt.publish(TOPIC_STATUS, "GESTOPT_COMMANDO");
  }
  if (bericht == "START") {
    remoteStop = false;
    toestand = RIJDEN;
    mqtt.publish(TOPIC_STATUS, "RIJDEN");
  }
}

void verbindMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  Serial.print("MQTT verbinden...");
  if (mqtt.connect(MQTT_CLIENT)) {
    Serial.println(" Verbonden!");
    mqtt.subscribe(TOPIC_COMMANDO);
    mqtt.publish(TOPIC_STATUS, "OPGESTART");
  } else {
    Serial.println(" MQTT MISLUKT, code: " + String(mqtt.state()));
  }
}

// ============================================================
//  MOTOR FUNCTIES
// ============================================================
void motorLinks(int snelheid, bool vooruit) {
  digitalWrite(IN1, vooruit ? HIGH : LOW);
  digitalWrite(IN2, vooruit ? LOW  : HIGH);
  ledcWrite(ENA, snelheid);
}

void motorRechts(int snelheid, bool vooruit) {
  digitalWrite(IN3, vooruit ? HIGH : LOW);
  digitalWrite(IN4, vooruit ? LOW  : HIGH);
  ledcWrite(ENB, snelheid);
}

void stopMotoren() {
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

void vooruit()    { motorLinks(SNELHEID, true);  motorRechts(SNELHEID, true);  }
void achteruit()  { motorLinks(SNELHEID, false); motorRechts(SNELHEID, false); }
void draaiLinks() { motorLinks(DRAAI_SNEL, false); motorRechts(DRAAI_SNEL, true);  }
void draaiRechts(){ motorLinks(DRAAI_SNEL, true);  motorRechts(DRAAI_SNEL, false); }

void corrigeerLinks()  { motorLinks(SNELHEID/2, true); motorRechts(SNELHEID, true); }
void corrigeerRechts() { motorLinks(SNELHEID, true); motorRechts(SNELHEID/2, true); }

// ============================================================
//  SENSOR FUNCTIES
// ============================================================
long meetAfstand(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duur = pulseIn(echoPin, HIGH, 25000);
  if (duur == 0) return 999; // geen signaal = vrij
  return duur * 0.034 / 2;
}

int leesIR(int pin) {
  return digitalRead(pin); // LOW = lijn, HIGH = geen lijn
}

// ============================================================
//  LED FUNCTIES
// ============================================================
void zetLeds(bool groen, bool geel, bool rood) {
  digitalWrite(LED_GROEN, groen);
  digitalWrite(LED_GEEL,  geel);
  digitalWrite(LED_ROOD,  rood);
}

void updateLedBatterij(int percentage) {
  if      (percentage <= BATTERIJ_ROOD) zetLeds(false, false, true);
  else if (percentage <= BATTERIJ_GEEL) zetLeds(false, true,  false);
  else                                  zetLeds(true,  false, false);
}

// Simuleer batterij via ADC (pas pin aan indien nodig)
int leesBatterij() {
  // Pas aan voor jouw batterij meetcircuit
  // int raw = analogRead(36); // VP pin
  // return map(raw, 0, 4095, 0, 100);
  return 85; // tijdelijk vast voor testing
}

// ============================================================
//  LIJNVOLGEN LOGICA
// ============================================================
void volgLijn() {
  int l = leesIR(IR_LINKS);
  int m = leesIR(IR_MIDDEN);
  int r = leesIR(IR_RECHTS);

  // Alle 3 op lijn = dwarslijn → stop 30 seconden
  if (l == LOW && m == LOW && r == LOW) {
    stopMotoren();
    Serial.println("Dwarslijn gedetecteerd! Wacht 30s...");
    mqtt.publish(TOPIC_STATUS, "DWARSLIJN");
    toestand = DWARSLIJN_WACHT;
    stopTijd = millis();
    return;
  }

  // Lijn volgen
  if      (m == LOW && l == HIGH && r == HIGH) vooruit();           // Midden → rechtdoor
  else if (l == LOW && m == HIGH)               corrigeerLinks();    // Links → naar links
  else if (r == LOW && m == HIGH)               corrigeerRechts();   // Rechts → naar rechts
  else if (l == LOW && m == LOW)                corrigeerLinks();    // Links + midden
  else if (r == LOW && m == LOW)                corrigeerRechts();   // Rechts + midden
  else                                          vooruit();           // Geen lijn → rechtdoor
}

// ============================================================
//  OBSTAKELVERMIJDING
// ============================================================
void vermijdObstakel() {
  Serial.println("Obstakel! Vermijden...");
  mqtt.publish(TOPIC_STATUS, "OBSTAKEL_VERMIJDEN");

  stopMotoren(); delay(300);

  // Kijk links en rechts
  long afstandLinks  = meetAfstand(LINKS_TRIG,  LINKS_ECHO);
  long afstandRechts = meetAfstand(RECHTS_TRIG, RECHTS_ECHO);

  if (afstandLinks > afstandRechts) {
    // Meer ruimte links → draai links
    draaiLinks(); delay(600);
    vooruit();    delay(800);
    draaiRechts();delay(600);
  } else {
    // Meer ruimte rechts → draai rechts
    draaiRechts();delay(600);
    vooruit();    delay(800);
    draaiLinks(); delay(600);
  }

  toestand = RIJDEN;
  mqtt.publish(TOPIC_STATUS, "RIJDEN");
}

// ============================================================
//  VASTGELOPEN DETECTIE
// ============================================================
void checkVastgelopen() {
  // Eenvoudige check: teller verhogen als motoren aan zijn maar lijn niet gevonden
  // Uitbreidbaar met encoder feedback
  if (millis() - vastgelopenTijd > VASTGELOPEN_TIJD) {
    Serial.println("Mogelijk vastgelopen!");
    mqtt.publish(TOPIC_FOUT, "VASTGELOPEN");
    toestand = VASTGELOPEN;
    vastgelopenTijd = millis();
  }
}

void herstelVastgelopen() {
  Serial.println("Herstel: achteruit rijden...");
  achteruit(); delay(1000);
  draaiLinks(); delay(800);
  stopMotoren();
  toestand = RIJDEN;
  vastgelopenTijd = millis();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("=== Mercedes Robot opstarten ===");

  // Motor pinnen
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  // Sensor pinnen
  pinMode(VOOR_TRIG,   OUTPUT); pinMode(VOOR_ECHO,   INPUT);
  pinMode(LINKS_TRIG,  OUTPUT); pinMode(LINKS_ECHO,  INPUT);
  pinMode(RECHTS_TRIG, OUTPUT); pinMode(RECHTS_ECHO, INPUT);
  pinMode(IR_LINKS,    INPUT);
  pinMode(IR_MIDDEN,   INPUT);
  pinMode(IR_RECHTS,   INPUT);

  // Knoppen (interne pull-up)
  pinMode(KNOP_STOP, INPUT_PULLUP);
  pinMode(KNOP_SKIP, INPUT_PULLUP);

  // LEDs
  pinMode(LED_GROEN, OUTPUT);
  pinMode(LED_GEEL,  OUTPUT);
  pinMode(LED_ROOD,  OUTPUT);
  zetLeds(true, false, false); // Start groen

  // WiFi & MQTT
  verbindWifi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  verbindMqtt();

  stopMotoren();
  Serial.println("=== Klaar! ===");
}

// ============================================================
//  HOOFDLOOP
// ============================================================
void loop() {
  // MQTT verbinding behouden
  if (!mqtt.connected()) verbindMqtt();
  mqtt.loop();

  // Batterij check (elke 5 seconden)
  if (millis() - mqttTijd > 5000) {
    mqttTijd = millis();
    int batt = leesBatterij();
    mqtt.publish(TOPIC_BATTERIJ, String(batt).c_str());
    updateLedBatterij(batt);

    // Afstanden publiceren
    long voor   = meetAfstand(VOOR_TRIG,   VOOR_ECHO);
    long links  = meetAfstand(LINKS_TRIG,  LINKS_ECHO);
    long rechts = meetAfstand(RECHTS_TRIG, RECHTS_ECHO);
    String afstanden = "voor:" + String(voor) + ",links:" + String(links) + ",rechts:" + String(rechts);
    mqtt.publish(TOPIC_AFSTAND, afstanden.c_str());

    // Batterij kritiek → stop
    if (batt <= BATTERIJ_ROOD && toestand == RIJDEN) {
      stopMotoren();
      toestand = GESTOPT_BATTERIJ;
      mqtt.publish(TOPIC_FOUT, "BATTERIJ_KRITIEK");
      Serial.println("Batterij kritiek! Robot gestopt.");
    }
  }

  // Knop STOP check
  if (digitalRead(KNOP_STOP) == LOW) {
    delay(50); // debounce
    if (digitalRead(KNOP_STOP) == LOW) {
      if (toestand == RIJDEN) {
        stopMotoren();
        toestand = GESTOPT_KNOP;
        stopTijd = millis();
        mqtt.publish(TOPIC_STATUS, "GESTOPT_KNOP");
        Serial.println("STOP knop ingedrukt");
      } else if (toestand == GESTOPT_KNOP) {
        // Nieuwe druk → verder rijden
        toestand = RIJDEN;
        mqtt.publish(TOPIC_STATUS, "RIJDEN");
        Serial.println("Verder rijden via knop");
      }
      while (digitalRead(KNOP_STOP) == LOW) delay(10); // wacht loslaten
    }
  }

  // Knop SKIP check
  if (digitalRead(KNOP_SKIP) == LOW) {
    delay(50);
    if (digitalRead(KNOP_SKIP) == LOW) {
      toestand = RIJDEN;
      mqtt.publish(TOPIC_STATUS, "SKIP");
      Serial.println("SKIP knop ingedrukt");
      while (digitalRead(KNOP_SKIP) == LOW) delay(10);
    }
  }

  // ============================================================
  //  TOESTAND MACHINE
  // ============================================================
  switch (toestand) {

    case RIJDEN: {
      // Obstakel check (voor)
      long voorAfstand = meetAfstand(VOOR_TRIG, VOOR_ECHO);
      if (voorAfstand < OBSTAKEL_AFSTAND) {
        stopMotoren();
        toestand = OBSTAKEL_VERMIJDEN;
        break;
      }
      volgLijn();
      break;
    }

    case GESTOPT_KNOP:
      stopMotoren();
      zetLeds(false, true, false); // Geel knipperen
      // Na 30 seconden automatisch verder
      if (millis() - stopTijd > STOP_TIJD) {
        toestand = RIJDEN;
        mqtt.publish(TOPIC_STATUS, "RIJDEN");
        Serial.println("30s voorbij, verder rijden");
      }
      break;

    case GESTOPT_COMMANDO:
      stopMotoren();
      zetLeds(false, true, false);
      break;

    case GESTOPT_BATTERIJ:
      stopMotoren();
      zetLeds(false, false, true);
      break;

    case OBSTAKEL_VERMIJDEN:
      vermijdObstakel();
      break;

    case DWARSLIJN_WACHT:
      stopMotoren();
      zetLeds(false, true, false);
      if (millis() - stopTijd > DWARSLIJN_TIJD) {
        toestand = RIJDEN;
        mqtt.publish(TOPIC_STATUS, "RIJDEN");
        Serial.println("Dwarslijn wacht voorbij, verder");
      }
      break;

    case VASTGELOPEN:
      herstelVastgelopen();
      break;
  }
}
