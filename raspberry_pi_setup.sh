# ============================================================
#  Mercedes-Benz Museum Robot
#  Raspberry Pi — Installatie & Configuratie
#  MQTT Broker + InfluxDB + Grafana Dashboard
# ============================================================

# ============================================================
#  STAP 1: Raspberry Pi updaten
# ============================================================
sudo apt update && sudo apt upgrade -y


# ============================================================
#  STAP 2: Mosquitto MQTT Broker installeren
# ============================================================
sudo apt install -y mosquitto mosquitto-clients

# Mosquitto starten bij boot
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

# Status controleren
sudo systemctl status mosquitto

# Configuratie aanpassen (anonieme verbindingen toestaan voor lokaal gebruik)
sudo nano /etc/mosquitto/mosquitto.conf

# Voeg deze regels toe aan het einde van het bestand:
# --------------------------------------------------
# listener 1883
# allow_anonymous true
# --------------------------------------------------

# Herstart na wijziging
sudo systemctl restart mosquitto

# Test: berichten ontvangen (open terminal 1)
mosquitto_sub -h localhost -t "mercedes/#" -v

# Test: bericht sturen (open terminal 2)
mosquitto_pub -h localhost -t "mercedes/robot1/commando" -m "STOP"
mosquitto_pub -h localhost -t "mercedes/robot1/commando" -m "START"


# ============================================================
#  STAP 3: InfluxDB installeren (database)
# ============================================================
# Voeg InfluxDB repository toe
curl https://repos.influxdata.com/influxdata-archive.key | gpg --dearmor | sudo tee /usr/share/keyrings/influxdb-archive-keyring.gpg > /dev/null

echo "deb [signed-by=/usr/share/keyrings/influxdb-archive-keyring.gpg] https://repos.influxdata.com/debian stable main" | sudo tee /etc/apt/sources.list.d/influxdb.list

sudo apt update
sudo apt install -y influxdb

# Starten
sudo systemctl enable influxdb
sudo systemctl start influxdb

# InfluxDB configureren
influx

# In de InfluxDB shell:
# > CREATE DATABASE mercedes_robots
# > USE mercedes_robots
# > exit


# ============================================================
#  STAP 4: Python MQTT → InfluxDB bridge installeren
# ============================================================
sudo apt install -y python3-pip
pip3 install paho-mqtt influxdb

# Bridge script aanmaken
cat > /home/pi/mqtt_bridge.py << 'EOF'
import paho.mqtt.client as mqtt
from influxdb import InfluxDBClient
import json
from datetime import datetime

# InfluxDB verbinding
influx = InfluxDBClient(host='localhost', port=8086, database='mercedes_robots')

def on_connect(client, userdata, flags, rc):
    print("MQTT verbonden, code:", rc)
    client.subscribe("mercedes/#")

def on_message(client, userdata, msg):
    topic   = msg.topic
    payload = msg.payload.decode()
    print(f"[{topic}] {payload}")

    # Batterij opslaan
    if "batterij" in topic:
        robot_id = topic.split("/")[1]
        punt = [{
            "measurement": "batterij",
            "tags": {"robot": robot_id},
            "time": datetime.utcnow().isoformat(),
            "fields": {"percentage": float(payload)}
        }]
        influx.write_points(punt)

    # Afstanden opslaan
    elif "afstand" in topic:
        robot_id = topic.split("/")[1]
        # formaat: "voor:25,links:40,rechts:35"
        try:
            waarden = dict(item.split(":") for item in payload.split(","))
            punt = [{
                "measurement": "afstanden",
                "tags": {"robot": robot_id},
                "time": datetime.utcnow().isoformat(),
                "fields": {
                    "voor":   float(waarden.get("voor",   0)),
                    "links":  float(waarden.get("links",  0)),
                    "rechts": float(waarden.get("rechts", 0))
                }
            }]
            influx.write_points(punt)
        except:
            pass

    # Status opslaan
    elif "status" in topic:
        robot_id = topic.split("/")[1]
        punt = [{
            "measurement": "status",
            "tags": {"robot": robot_id},
            "time": datetime.utcnow().isoformat(),
            "fields": {"toestand": payload}
        }]
        influx.write_points(punt)

    # Fouten opslaan
    elif "fout" in topic:
        robot_id = topic.split("/")[1]
        punt = [{
            "measurement": "fouten",
            "tags": {"robot": robot_id},
            "time": datetime.utcnow().isoformat(),
            "fields": {"fout": payload}
        }]
        influx.write_points(punt)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect("localhost", 1883, 60)
client.loop_forever()
EOF

# Bridge starten
python3 /home/pi/mqtt_bridge.py

# Bridge automatisch starten bij boot (optioneel)
sudo nano /etc/rc.local
# Voeg voor "exit 0" toe:
# python3 /home/pi/mqtt_bridge.py &


# ============================================================
#  STAP 5: Grafana installeren (dashboard)
# ============================================================
sudo apt install -y apt-transport-https software-properties-common wget

wget -q -O - https://packages.grafana.com/gpg.key | sudo apt-key add -
echo "deb https://packages.grafana.com/oss/deb stable main" | sudo tee /etc/apt/sources.list.d/grafana.list

sudo apt update
sudo apt install -y grafana

sudo systemctl enable grafana-server
sudo systemctl start grafana-server

# Grafana openen in browser:
# http://[IP-VAN-RASPBERRY-PI]:3000
# Standaard login: admin / admin


# ============================================================
#  STAP 6: Grafana configureren
# ============================================================
# 1. Ga naar: Configuration → Data Sources → Add data source
# 2. Kies: InfluxDB
# 3. URL: http://localhost:8086
# 4. Database: mercedes_robots
# 5. Klik: Save & Test

# Dashboard panels aanmaken:
# Panel 1 — Batterij per robot:
#   SELECT mean("percentage") FROM "batterij" WHERE $timeFilter GROUP BY time($__interval), "robot"
#
# Panel 2 — Afstanden:
#   SELECT mean("voor"), mean("links"), mean("rechts") FROM "afstanden" WHERE $timeFilter GROUP BY time($__interval), "robot"
#
# Panel 3 — Robot status:
#   SELECT last("toestand") FROM "status" GROUP BY "robot"
#
# Panel 4 — Foutmeldingen:
#   SELECT "fout" FROM "fouten" WHERE $timeFilter


# ============================================================
#  STAP 7: Robot besturen via MQTT commando's
# ============================================================

# Robot stoppen
mosquitto_pub -h localhost -t "mercedes/robot1/commando" -m "STOP"

# Robot starten
mosquitto_pub -h localhost -t "mercedes/robot1/commando" -m "START"

# Alle robots stoppen (indien meerdere)
mosquitto_pub -h localhost -t "mercedes/robot2/commando" -m "STOP"
mosquitto_pub -h localhost -t "mercedes/robot3/commando" -m "STOP"

# Live meekijken met alle berichten
mosquitto_sub -h localhost -t "mercedes/#" -v


# ============================================================
#  STAP 8: IP-adres Raspberry Pi vinden
# ============================================================
hostname -I
# Gebruik dit IP in de ESP32 code bij MQTT_SERVER


# ============================================================
#  SAMENVATTING POORTEN
# ============================================================
# MQTT Broker:  poort 1883
# InfluxDB:     poort 8086
# Grafana:      poort 3000
