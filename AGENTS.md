# AGENTS.md

## Projekt

Dieses Repository enthält eine PlatformIO-Firmware für einen ESP32:

- Board: `seeed_xiao_esp32s3`
- Framework: `arduino`
- Monitor-Baudrate: `115200`

## Arbeitsweise

- Antworte bevorzugt auf Deutsch, solange der Nutzer nichts anderes vorgibt.
- Halte Änderungen klein und passend zur vorhandenen PlatformIO-Struktur.
- Erzeuge keine Build- oder Editor-Artefakte im Repository. Generated output gehört in `.pio/` oder bleibt lokal.
- Lege echte WLAN-Zugangsdaten, API-Keys, Zertifikate und andere Secrets nie im Repo ab. Verwende dafür lokale Dateien wie `include/secrets.h` und dokumentiere bei Bedarf ein separates Beispiel ohne echte Werte.

## Nützliche Kommandos

```sh
pio run
pio run -t upload
pio device monitor -b 115200
pio run -t clean
```

Falls `pio` nicht im PATH liegt, zuerst die lokale PlatformIO-Installation bzw. die IDE-Umgebung prüfen.

## Git-Hinweise

- `.pio/`, generierte `.vscode`-Dateien, Firmware-Binaries, Logs, temporäre Dateien und lokale Secrets sollen nicht nach GitHub.
- Wenn eine neue lokale Konfigurationsdatei nötig ist, ergänze lieber eine `*.example`-Datei mit Platzhaltern statt echte Werte zu committen.
