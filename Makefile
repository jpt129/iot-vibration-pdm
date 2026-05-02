# IoT Vibration PdM — operator convenience targets
.PHONY: help up down restart logs ps build psql mqtt-sub demo-fault \
        demo-malformed demo-null demo-oor demo-schema demo-late demo-reboot \
        seed-passwd backup health flash-node monitor

help:
	@echo "Common targets:"
	@echo "  make up            - start the full stack (compose up -d)"
	@echo "  make down          - stop everything"
	@echo "  make logs [svc=..] - tail logs (default: all)"
	@echo "  make ps            - service health status"
	@echo "  make psql          - open psql into TimescaleDB"
	@echo "  make mqtt-sub      - subscribe to all MQTT topics"
	@echo "  make seed-passwd   - (first run) create mosquitto passwd file"
	@echo "  make demo-fault    - fire all fault-injection scenarios"
	@echo "  make flash-node    - build + upload ESP32 firmware"
	@echo "  make monitor       - serial monitor of ESP32"

up:
	docker compose up -d
	@echo "Waiting for services..."
	@sleep 5 && docker compose ps

down:
	docker compose down

restart:
	docker compose restart

logs:
	docker compose logs -f $(svc)

ps:
	docker compose ps

build:
	docker compose build --pull

psql:
	docker compose exec timescale psql -U iotpdm -d iotpdm

mqtt-sub:
	docker compose exec mosquitto mosquitto_sub \
	  -h localhost -u demo -P demopass -t 'iotpdm/#' -v

health:
	@echo "== mosquitto =="; docker compose exec mosquitto mosquitto_sub \
	  -h localhost -u demo -P demopass -t '$$SYS/broker/uptime' -C 1 -W 3
	@echo "== timescale =="; docker compose exec timescale pg_isready -U iotpdm
	@echo "== worker ==";    curl -fs http://localhost:9100/healthz && echo
	@echo "== api ==";       curl -fs http://localhost:8080/healthz && echo

seed-passwd:
	@mkdir -p config/mosquitto
	@touch config/mosquitto/passwd
	@chmod 600 config/mosquitto/passwd
	docker run --rm -v "$$(pwd)/config/mosquitto/passwd:/mosquitto/config/passwd" \
	  eclipse-mosquitto:2.0.22 mosquitto_passwd -b /mosquitto/config/passwd \
	  worker worker-secret-change-me
	docker run --rm -v "$$(pwd)/config/mosquitto/passwd:/mosquitto/config/passwd" \
	  eclipse-mosquitto:2.0.22 mosquitto_passwd -b /mosquitto/config/passwd \
	  esp32-a17 node-secret-change-me
	docker run --rm -v "$$(pwd)/config/mosquitto/passwd:/mosquitto/config/passwd" \
	  eclipse-mosquitto:2.0.22 mosquitto_passwd -b /mosquitto/config/passwd \
	  demo demopass
	@echo "passwd file generated: 3 users"

MQTTPUB = docker compose exec mosquitto mosquitto_pub -h localhost -u demo -P demopass

demo-malformed:
	@echo "Sending malformed payload..."
	$(MQTTPUB) -t iotpdm/lab/fan01/esp32-a17/features -m 'not-json-at-all'

demo-null:
	@echo "Telling device to inject null..."
	$(MQTTPUB) -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"cmd":"inject_null"}'

demo-oor:
	@echo "Telling device to inject out-of-range..."
	$(MQTTPUB) -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"cmd":"inject_oor"}'

demo-schema:
	@echo "Telling device to inject schema mismatch..."
	$(MQTTPUB) -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"cmd":"inject_schema"}'

demo-reboot:
	@echo "Rebooting ESP32 over MQTT..."
	$(MQTTPUB) -t iotpdm/lab/fan01/esp32-a17/cmd -m '{"cmd":"reboot"}'

demo-fault: demo-malformed demo-null demo-oor demo-schema
	@echo "All fault scenarios fired. Check DLQ: make psql -> SELECT * FROM dlq ORDER BY time DESC LIMIT 20;"

flash-node:
	cd firmware/sensor-node && pio run -e esp32s3-devkitc -t upload

monitor:
	cd firmware/sensor-node && pio device monitor

backup:
	docker compose exec timescale pg_dump -U iotpdm iotpdm | gzip > backup_$$(date +%Y%m%d_%H%M%S).sql.gz
	@echo "backup written"
