/*
 * ============================================================
 *  Mercedes-Benz Museum Robot
 *  ESP32 DevKit V1 — VERSIE: RICHTING VOLGENS EERSTE SENSOR
 * ============================================================
 *
 *  OPSTARTEN (kalibratie blijft):
 *  Zet de robot OP DE LIJN (midden=zwart, zijkanten=wit) en
 *  zet hem dan pas aan. 3x groen = OK, 5x rood = fout, herstart.
 *
 *  NIEUWE STUURLOGICA (zoals jij beschreef):
 *  - LINKER  sensor ziet de lijn  → draai naar RECHTS
 *  - RECHTER sensor ziet de lijn  → draai naar LINKS
 *  - MIDDEN  ziet de lijn         → rechtdoor
 *  Dit geldt zowel bij normaal rijden als bij het terugvinden
 *  van de lijn na kwijtraken.
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
//  WiFi & MQTT instellingen
// ============================================================
const char* WIFI_SSID     = "embed";
const char* WIFI_PASS     = "weareincontrol";
const char* MQTT_SERVER   = "192.168.1.100";
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "robot_1";

const char* TOPIC_STATUS    = "mercedes/robot1/status";
const char* TOPIC_AFSTAND   = "mercedes/robot1/afstand";
const char* TOPIC_BATTERIJ  = "mercedes/robot1/batterij";
const char* TOPIC_FOUT      = "mercedes/robot1/fout";
const char* TOPIC_COMMANDO  = "mercedes/robot1/commando";

// ============================================================
//  PIN DEFINITIES
// ============================================================
#define IR_LINKS    32
#define IR_MIDDEN   34
#define IR_RECHTS   35

#define VOOR_TRIG   27
#define VOOR_ECHO   14
#define LINKS_TRIG  26
#define LINKS_ECHO  25
#define RECHTS_TRIG 33
#define RECHTS_ECHO 23

#define IN1  17
#define IN2  16
#define ENA  2
#define IN3  18
#define IN4  19
#define ENB  5

#define KNOP_STOP   4
#define KNOP_SKIP   13

#define LED_GROEN   21
#define LED_GEEL    22
#define LED_ROOD    15

#define BATTERIJ_PIN  36

// ============================================================
//  SNELHEDEN
// ============================================================
#define PWM_FREQ      1000
#define PWM_RES       8
#define SNELHEID      140   // normale rijsnelheid
#define ZACHT_SNEL    100   // voorzichtig vooruit na herstel
#define DRAAI_SNEL    150   // obstakel-manoeuvre
#define BIJSTUUR_SNEL 110   // bijsturen als zijsensor de lijn ziet
#define ZOEK_SNEL     85    // voorzichtig zoeken bij lijn kwijt

// ============================================================
//  DREMPELWAARDEN
// ============================================================
#define OBSTAKEL_STOP       30
#define OBSTAKEL_MAX        50
#define DWARSLIJN_TIJD      30000
#define STOP_TIJD           30000
#define DWARSLIJN_LEZINGEN  3
#define DWARSLIJN_COOLDOWN  2000
#define ZOEK_OMSCHAKEL      2500
#define ZACHT_DUUR          400
#define KNOP_DEBOUNCE       50
#define BATTERIJ_GEEL       30
#define BATTERIJ_ROOD       10

// ============================================================
//  GLOBALE VARIABELEN
// ============================================================
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

enum Toestand {
  RIJDEN,
  GESTOPT_KNOP,
  GESTOPT_COMMANDO,
  GESTOPT_BATTERIJ,
  OBSTAKEL_VERMIJDEN,
  DWARSLIJN_WACHT
};

Toestand toestand = RIJDEN;

enum LijnModus { VOLGEN, ZOEKEN };
LijnModus lijnModus = VOLGEN;

enum ObstakelFase { OBST_KIJK, OBST_DRAAI1, OBST_VOORUIT, OBST_DRAAI2 };
ObstakelFase obstFase = OBST_KIJK;
unsigned long obstFaseStart = 0;
bool obstNaarLinks = true;

unsigned long stopTijd          = 0;
unsigned long mqttTijd          = 0;
unsigned long mqttReconnectTijd = 0;
unsigned long sonarTijd         = 0;
unsigned long printTijd         = 0;
unsigned long dwarslijnNegeren  = 0;
unsigned long zoekStart         = 0;
unsigned long zachtStart        = 0;

bool zachtRijden       = false;
int  dwarslijnTeller   = 0;
int  obstakelTeller    = 0;
int  batterijKritiekTeller = 0;

bool knopStopVorig = HIGH;
bool knopSkipVorig = HIGH;
unsigned long knopStopTijd = 0;
unsigned long knopSkipTijd = 0;

// Welke sensor zag de lijn het laatst: -1 = links, +1 = rechts, 0 = midden
int laatsteSensor = 0;

// ============================================================
//  AUTO-GEKALIBREERDE SENSORWAARDEN
// ============================================================
int WAARDE_ZWART = HIGH;
int WAARDE_WIT   = LOW;

bool isZwart(int pin) { return digitalRead(pin) == WAARDE_ZWART; }

// ============================================================
//  KALIBRATIE — robot staat op de lijn bij het opstarten
// ============================================================
void kalibreerSensoren() {
  Serial.println("Kalibreren... (midden=zwart, zijkanten=wit)");
  delay(500);

  int somM = 0, somL = 0, somR = 0;
  for (int i = 0; i < 20; i++) {
    somM += digitalRead(IR_MIDDEN);
    somL += digitalRead(IR_LINKS);
    somR += digitalRead(IR_RECHTS);
    delay(10);
  }
  int midden  = (somM >= 10) ? HIGH : LOW;
  int zijkant = ((somL + somR) >= 20) ? HIGH : LOW;

  if (midden == zijkant) {
    Serial.println("KALIBRATIE MISLUKT! Robot stond niet goed op de lijn.");
    Serial.println("Standaard gebruikt: ZWART=HIGH. Herstart op de lijn!");
    WAARDE_ZWART = HIGH;
    WAARDE_WIT   = LOW;
    for (int i = 0; i < 5; i++) {
      zetLeds(false, false, true); delay(200);
      zetLeds(false, false, false); delay(200);
    }
  } else {
    WAARDE_ZWART = midden;
    WAARDE_WIT   = zijkant;
    Serial.printf("Kalibratie OK: ZWART=%s, WIT=%s\n",
                  WAARDE_ZWART == HIGH ? "HIGH" : "LOW",
                  WAARDE_WIT   == HIGH ? "HIGH" : "LOW");
    for (int i = 0; i < 3; i++) {
      zetLeds(true, false, false); delay(150);
      zetLeds(false, false, false); delay(150);
    }
  }
  zetLeds(true, false, false);
}

// ============================================================
//  WIFI & MQTT
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
  for (unsigned int i = 0; i < length; i++) bericht += (char)payload[i];
  Serial.println("MQTT ontvangen: " + bericht);

  if (bericht == "STOP") {
    toestand = GESTOPT_COMMANDO;
    stopMotoren();
    publiceer(TOPIC_STATUS, "GESTOPT_COMMANDO");
  }
  if (bericht == "START") {
    toestand = RIJDEN;
    lijnModus = VOLGEN;
    publiceer(TOPIC_STATUS, "RIJDEN");
  }
}

void verbindMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (millis() - mqttReconnectTijd < 5000) return;
  mqttReconnectTijd = millis();

  Serial.print("MQTT verbinden...");
  if (mqtt.connect(MQTT_CLIENT)) {
    Serial.println(" Verbonden!");
    mqtt.subscribe(TOPIC_COMMANDO);
    mqtt.publish(TOPIC_STATUS, "OPGESTART");
  } else {
    Serial.println(" MQTT MISLUKT, code: " + String(mqtt.state()));
  }
}

void publiceer(const char* topic, const char* bericht) {
  if (mqtt.connected()) mqtt.publish(topic, bericht);
}

// ============================================================
//  MOTOR FUNCTIES
// ============================================================
void motorLinks(int snelheid, bool vooruitRichting) {
  digitalWrite(IN1, vooruitRichting ? HIGH : LOW);
  digitalWrite(IN2, vooruitRichting ? LOW  : HIGH);
  ledcWrite(ENA, snelheid);
}

void motorRechts(int snelheid, bool vooruitRichting) {
  digitalWrite(IN3, vooruitRichting ? HIGH : LOW);
  digitalWrite(IN4, vooruitRichting ? LOW  : HIGH);
  ledcWrite(ENB, snelheid);
}

void stopMotoren() {
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

void vooruit()      { motorLinks(SNELHEID, true);   motorRechts(SNELHEID, true);  }
void zachtVooruit() { motorLinks(ZACHT_SNEL, true); motorRechts(ZACHT_SNEL, true); }
void achteruit()    { motorLinks(SNELHEID, false);  motorRechts(SNELHEID, false); }

// Bijsturen tijdens het rijden (zachte bocht, blijft vooruit gaan)
void stuurRechts() { motorLinks(SNELHEID, true);      motorRechts(BIJSTUUR_SNEL/2, true); }
void stuurLinks()  { motorLinks(BIJSTUUR_SNEL/2, true); motorRechts(SNELHEID, true); }

// Draaien op de plaats
void draaiLinks(int snelheid)  { motorLinks(snelheid, false); motorRechts(snelheid, true);  }
void draaiRechts(int snelheid) { motorLinks(snelheid, true);  motorRechts(snelheid, false); }

// ============================================================
//  SENSOR FUNCTIES
// ============================================================
long meetAfstand(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duur = pulseIn(echoPin, HIGH, 30000);
  if (duur == 0) return 999;
  long afstand = duur * 0.034 / 2;
  if (afstand > OBSTAKEL_MAX) return 999;
  return afstand;
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

// ============================================================
//  BATTERIJ METING
// ============================================================
int leesBatterij() {
  long som = 0;
  for (int i = 0; i < 10; i++) som += analogRead(BATTERIJ_PIN);
  int raw = som / 10;
  int percentage = map(raw, 1500, 3100, 0, 100);
  return constrain(percentage, 0, 100);
}

// ============================================================
//  LIJNVOLGEN — RICHTING VOLGENS EERSTE SENSOR
//
//  LINKS  ziet lijn → naar RECHTS sturen
//  RECHTS ziet lijn → naar LINKS sturen
//  MIDDEN ziet lijn → rechtdoor
//  Niets ziet lijn  → voorzichtig draaien in de richting die
//                     bij de laatst geziene sensor hoort
// ============================================================
void volgLijn() {
  bool l = isZwart(IR_LINKS);
  bool m = isZwart(IR_MIDDEN);
  bool r = isZwart(IR_RECHTS);

  if (millis() - printTijd > 200) {
    printTijd = millis();
    Serial.printf("IR zwart? L=%d M=%d R=%d | modus=%d | laatste=%d\n",
                  l, m, r, lijnModus, laatsteSensor);
  }

  // ---------- DWARSLIJN: alle 3 zwart ----------
  if (l && m && r) {
    if (millis() < dwarslijnNegeren) { vooruit(); return; }
    dwarslijnTeller++;
    if (dwarslijnTeller >= DWARSLIJN_LEZINGEN) {
      stopMotoren();
      Serial.println("Dwarslijn! Wacht 30s...");
      publiceer(TOPIC_STATUS, "DWARSLIJN");
      toestand = DWARSLIJN_WACHT;
      stopTijd = millis();
      dwarslijnTeller = 0;
      lijnModus = VOLGEN;
    } else {
      vooruit();
    }
    return;
  }
  dwarslijnTeller = 0;

  // ============================================================
  //  MODUS: ZOEKEN — lijn was kwijt
  // ============================================================
  if (lijnModus == ZOEKEN) {

    // Een sensor heeft de lijn weer → direct overschakelen
    if (l || m || r) {
      stopMotoren();
      delay(30);                 // korte rem, niet voorbij draaien
      lijnModus = VOLGEN;
      zachtRijden = true;        // voorzichtig weer oppakken
      zachtStart = millis();
      Serial.println("Lijn teruggevonden -> voorzichtig verder");
      // GEEN return: meteen hieronder de juiste stuurkeuze maken
    } else {
      // Nog niets → voorzichtig draaien volgens de laatst geziene sensor
      // laatste was LINKS  → wij sturen RECHTS, dus ook rechts zoeken
      // laatste was RECHTS → wij sturen LINKS, dus ook links zoeken
      unsigned long zoekDuur = millis() - zoekStart;
      if (zoekDuur < ZOEK_OMSCHAKEL) {
        if (laatsteSensor == -1)      draaiRechts(ZOEK_SNEL);
        else if (laatsteSensor == 1)  draaiLinks(ZOEK_SNEL);
        else                          zachtVooruit();   // midden was laatst → rustig door
      } else if (zoekDuur < 3 * ZOEK_OMSCHAKEL) {
        // Niet gevonden → andere kant proberen
        if (laatsteSensor == -1)      draaiLinks(ZOEK_SNEL);
        else                          draaiRechts(ZOEK_SNEL);
      } else {
        zoekStart = millis();
      }
      return;
    }
  }

  // ============================================================
  //  MODUS: VOLGEN — jouw stuurregels
  // ============================================================

  // Lijn helemaal kwijt → zoekmodus
  if (!l && !m && !r) {
    lijnModus = ZOEKEN;
    zoekStart = millis();
    return;
  }

  // Na herstel even voorzichtig rijden
  bool zacht = zachtRijden && (millis() - zachtStart < ZACHT_DUUR);
  if (zachtRijden && !zacht) zachtRijden = false;

  // MIDDEN ziet de lijn → rechtdoor
  if (m && !l && !r) {
    laatsteSensor = 0;
    if (zacht) zachtVooruit(); else vooruit();
    return;
  }

  // LINKER sensor ziet de lijn → naar RECHTS sturen
  if (l && !m) {
    laatsteSensor = -1;
    stuurRechts();
    return;
  }

  // RECHTER sensor ziet de lijn → naar LINKS sturen
  if (r && !m) {
    laatsteSensor = 1;
    stuurLinks();
    return;
  }

  // Midden + links → licht naar rechts blijven sturen
  if (m && l) {
    laatsteSensor = -1;
    stuurRechts();
    return;
  }

  // Midden + rechts → licht naar links blijven sturen
  if (m && r) {
    laatsteSensor = 1;
    stuurLinks();
    return;
  }

  if (zacht) zachtVooruit(); else vooruit();
}

// ============================================================
//  OBSTAKELVERMIJDING — niet-blokkerend
// ============================================================
void startObstakelManoeuvre() {
  stopMotoren();
  obstFase = OBST_KIJK;
  obstFaseStart = millis();
  toestand = OBSTAKEL_VERMIJDEN;
  publiceer(TOPIC_STATUS, "OBSTAKEL_VERMIJDEN");
  Serial.println("Obstakel! Manoeuvre starten...");
}

void doeObstakelStap() {
  switch (obstFase) {

    case OBST_KIJK:
      if (millis() - obstFaseStart >= 300) {
        long aL = meetAfstand(LINKS_TRIG,  LINKS_ECHO);
        long aR = meetAfstand(RECHTS_TRIG, RECHTS_ECHO);
        obstNaarLinks = (aL > aR);
        Serial.printf("Links: %ld Rechts: %ld -> %s\n",
                      aL, aR, obstNaarLinks ? "LINKS" : "RECHTS");
        if (obstNaarLinks) draaiLinks(DRAAI_SNEL); else draaiRechts(DRAAI_SNEL);
        obstFase = OBST_DRAAI1;
        obstFaseStart = millis();
      }
      break;

    case OBST_DRAAI1:
      if (millis() - obstFaseStart >= 600) {
        vooruit();
        obstFase = OBST_VOORUIT;
        obstFaseStart = millis();
      }
      break;

    case OBST_VOORUIT:
      if (millis() - sonarTijd > 100) {
        sonarTijd = millis();
        if (meetAfstand(VOOR_TRIG, VOOR_ECHO) < OBSTAKEL_STOP) {
          startObstakelManoeuvre();
          return;
        }
      }
      if (millis() - obstFaseStart >= 800) {
        if (obstNaarLinks) draaiRechts(DRAAI_SNEL); else draaiLinks(DRAAI_SNEL);
        obstFase = OBST_DRAAI2;
        obstFaseStart = millis();
      }
      break;

    case OBST_DRAAI2:
      if (millis() - obstFaseStart >= 600) {
        lijnModus = ZOEKEN;
        zoekStart = millis();
        toestand = RIJDEN;
        publiceer(TOPIC_STATUS, "RIJDEN");
        Serial.println("Manoeuvre klaar, lijn zoeken...");
      }
      break;
  }
}

// ============================================================
//  KNOPPEN — niet-blokkerend
// ============================================================
void checkKnoppen() {
  bool stopNu = digitalRead(KNOP_STOP);
  if (stopNu == LOW && knopStopVorig == HIGH &&
      millis() - knopStopTijd > KNOP_DEBOUNCE) {
    knopStopTijd = millis();
    if (toestand == GESTOPT_KNOP) {
      toestand = RIJDEN;
      lijnModus = VOLGEN;
      publiceer(TOPIC_STATUS, "RIJDEN");
      Serial.println("Verder rijden via knop");
    } else {
      stopMotoren();
      toestand = GESTOPT_KNOP;
      stopTijd = millis();
      publiceer(TOPIC_STATUS, "GESTOPT_KNOP");
      Serial.println("STOP knop ingedrukt");
    }
  }
  knopStopVorig = stopNu;

  bool skipNu = digitalRead(KNOP_SKIP);
  if (skipNu == LOW && knopSkipVorig == HIGH &&
      millis() - knopSkipTijd > KNOP_DEBOUNCE) {
    knopSkipTijd = millis();
    toestand = RIJDEN;
    lijnModus = VOLGEN;
    dwarslijnNegeren = millis() + DWARSLIJN_COOLDOWN;
    publiceer(TOPIC_STATUS, "SKIP");
    Serial.println("SKIP knop ingedrukt");
  }
  knopSkipVorig = skipNu;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("=== Mercedes Robot opstarten ===");

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  pinMode(VOOR_TRIG,   OUTPUT); pinMode(VOOR_ECHO,   INPUT);
  pinMode(LINKS_TRIG,  OUTPUT); pinMode(LINKS_ECHO,  INPUT);
  pinMode(RECHTS_TRIG, OUTPUT); pinMode(RECHTS_ECHO, INPUT);
  pinMode(IR_LINKS,    INPUT);
  pinMode(IR_MIDDEN,   INPUT);
  pinMode(IR_RECHTS,   INPUT);

  pinMode(KNOP_STOP, INPUT_PULLUP);
  pinMode(KNOP_SKIP, INPUT_PULLUP);

  pinMode(LED_GROEN, OUTPUT);
  pinMode(LED_GEEL,  OUTPUT);
  pinMode(LED_ROOD,  OUTPUT);

  // *** KALIBRATIE: robot moet OP DE LIJN staan! ***
  kalibreerSensoren();

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
  if (!mqtt.connected()) verbindMqtt();
  mqtt.loop();

  checkKnoppen();

  // Telemetrie elke 5 seconden
  if (millis() - mqttTijd > 5000) {
    mqttTijd = millis();
    int batt = leesBatterij();
    publiceer(TOPIC_BATTERIJ, String(batt).c_str());
    updateLedBatterij(batt);

    long voor   = meetAfstand(VOOR_TRIG,   VOOR_ECHO);
    long links  = meetAfstand(LINKS_TRIG,  LINKS_ECHO);
    long rechts = meetAfstand(RECHTS_TRIG, RECHTS_ECHO);
    String afstanden = "voor:" + String(voor) + ",links:" + String(links) + ",rechts:" + String(rechts);
    publiceer(TOPIC_AFSTAND, afstanden.c_str());

    if (batt <= BATTERIJ_ROOD) {
      batterijKritiekTeller++;
      if (batterijKritiekTeller >= 3 && toestand == RIJDEN) {
        stopMotoren();
        toestand = GESTOPT_BATTERIJ;
        publiceer(TOPIC_FOUT, "BATTERIJ_KRITIEK");
        Serial.println("Batterij kritiek! Robot gestopt.");
      }
    } else {
      batterijKritiekTeller = 0;
    }
  }

  switch (toestand) {

    case RIJDEN: {
      if (millis() - sonarTijd > 100) {
        sonarTijd = millis();
        long voorAfstand = meetAfstand(VOOR_TRIG, VOOR_ECHO);
        if (voorAfstand < OBSTAKEL_STOP) {
          obstakelTeller++;
          if (obstakelTeller >= 2) {
            obstakelTeller = 0;
            startObstakelManoeuvre();
            break;
          }
        } else {
          obstakelTeller = 0;
        }
      }
      volgLijn();
      break;
    }

    case GESTOPT_KNOP:
      stopMotoren();
      zetLeds(false, true, false);
      if (millis() - stopTijd > STOP_TIJD) {
        toestand = RIJDEN;
        lijnModus = VOLGEN;
        publiceer(TOPIC_STATUS, "RIJDEN");
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
      doeObstakelStap();
      break;

    case DWARSLIJN_WACHT:
      stopMotoren();
      zetLeds(false, true, false);
      if (millis() - stopTijd > DWARSLIJN_TIJD) {
        toestand = RIJDEN;
        lijnModus = VOLGEN;
        dwarslijnNegeren = millis() + DWARSLIJN_COOLDOWN;
        publiceer(TOPIC_STATUS, "RIJDEN");
        Serial.println("Dwarslijn wacht voorbij, verder");
      }
      break;
  }
}
