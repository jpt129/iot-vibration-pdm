#!/bin/bash
# Toggle Pi between home WiFi and standalone AP mode
set -e
HOME_CONN="taylor51"
AP_CONN="iot-pdm-ap"
case "$1" in
  ap|on)
    echo "Switching to AP mode..."
    sudo nmcli connection up "${AP_CONN}"
    echo "AP active. Pi is at 192.168.50.1"
    ;;
  home|off)
    echo "Switching to home WiFi..."
    sudo nmcli connection up "${HOME_CONN}"
    sleep 3
    echo "Connected to home WiFi. Pi IP: $(hostname -I | awk '{print $1}')"
    ;;
  status|*)
    echo "Active connection: $(nmcli -t -f NAME connection show --active | grep -E "^(${HOME_CONN}|${AP_CONN})$" || echo none)"
    echo "Pi IPs: $(hostname -I)"
    echo "Usage: ap-toggle [ap|home|status]"
    ;;
esac
