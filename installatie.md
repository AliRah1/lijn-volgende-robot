#Installatie

Stappen om het project na te bouwen.

## 1. ESP32 flashen

1. Installeer de Arduino IDE en voeg het ESP32-board toe.
2. Open de firmware uit de map `firmware/`.
3. Vul je WiFi-gegevens en het IP-adres van de MQTT-broker in.
4. Verbind de ESP32 via USB en upload de code.

## 2. Raspberry Pi voorbereiden

1. Installeer een besturingssysteem op de Raspberry Pi (bv. Raspberry Pi OS).
2. Zorg dat de Pi op hetzelfde netwerk zit als de robot.
3. Installeer Docker en Docker Compose.

## 3. Server starten met Docker

In de map `raspberry-pi/` staat een `docker-compose.yml` die Mosquitto,
InfluxDB en Grafana samen opstart:

```bash
cd raspberry-pi
docker compose up -d
```

Dit start de drie diensten op de achtergrond.

## 4. Grafana instellen

1. Open Grafana in je browser via `http://<ip-van-de-pi>:3000`.
2. Voeg InfluxDB toe als databron.
3. Maak een dashboard aan met de gewenste grafieken.

> Vermeld waar nodig de standaardpoorten en hoe je de wachtwoorden instelt
> (via een `.env`-bestand, niet hardcoded).
