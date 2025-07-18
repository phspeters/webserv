#!/bin/bash

# script.sh

URL="https://example.com"
LOGFILE="status.log"

echo "Checking status of $URL..."

HTTP_STATUS=$(curl -o /dev/null -s -w "%{http_code}\n" "$URL")

if [ "$HTTP_STATUS" -eq 200 ]; then
  echo "$(date): $URL is reachable." >> "$LOGFILE"
  echo "✅ $URL is reachable."
else
  echo "$(date): $URL is not reachable. Status code: $HTTP_STATUS" >> "$LOGFILE"
  echo "❌ $URL is not reachable. Status code: $HTTP_STATUS"
fi
