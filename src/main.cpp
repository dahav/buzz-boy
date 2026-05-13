#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif

// ===== Konfiguration =====

const char* WIFI_SSID = "DIBO13";
const char* WIFI_PASS = "b82@qPSt";
const char* HOSTNAME  = "vibro";

// Bei XIAO-Arduino-Pinbelegung D2 (GPIO 1) verwenden
static constexpr int MOTOR_PIN = 1;

static constexpr int PWM_FREQ    = 20000;
static constexpr int PWM_BITS    = 8;
static constexpr int PWM_MAX_DUTY = 170; // ca. 67 % von 255

WebServer server(80);

// ===== Onboard-LED =====

#if defined(ARDUINO_XIAO_ESP32C3)
  // XIAO C3: LED ist active LOW (LOW = an, HIGH = aus)
  static constexpr int  LED_PIN        = LED_BUILTIN;
  static constexpr bool LED_ACTIVE_LOW = true;
#else
  // XIAO S3, DevKit und die meisten anderen: active HIGH
  static constexpr int  LED_PIN        = LED_BUILTIN;
  static constexpr bool LED_ACTIVE_LOW = false;
#endif

inline void ledOn()  { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? LOW  : HIGH); }
inline void ledOff() { digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);  }

// ===== PWM-Kompatibilität Arduino-ESP32 2.x / 3.x =====

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void pwmInit() {
  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_BITS);
}
void pwmWriteValue(int duty) {
  ledcWrite(MOTOR_PIN, duty);
}
#else
static constexpr int PWM_CH = 0;
void pwmInit() {
  ledcSetup(PWM_CH, PWM_FREQ, PWM_BITS);
  ledcAttachPin(MOTOR_PIN, PWM_CH);
}
void pwmWriteValue(int duty) {
  ledcWrite(PWM_CH, duty);
}
#endif

void motorOff() {
  pwmWriteValue(0);
  ledOff();
}

void motorDuty(int duty) {
  duty = constrain(duty, 0, PWM_MAX_DUTY);
  pwmWriteValue(duty);
  if (duty > 0) ledOn();
  else          ledOff();
}

// ===== Muster-Zustandsmaschine =====

enum class Phase {
  Idle,
  PulseOn,
  PulseOff,
  GroupPause
};

struct Pattern {
  bool active = false;
  Phase phase = Phase::Idle;

  int groups[8] = {0};
  int groupCount = 0;
  int groupIndex = 0;
  int pulseIndex = 0;

  int onMs    = 120;
  int offMs   = 80;
  int pauseMs = 500;
  int duty    = PWM_MAX_DUTY;

  unsigned long nextAt = 0;
};

Pattern pattern;

bool timeReached(unsigned long t) {
  return (int32_t)(millis() - t) >= 0;
}

void stopPattern() {
  pattern.active = false;
  pattern.phase  = Phase::Idle;
  motorOff();
}

void startPulse() {
  motorDuty(pattern.duty);
  pattern.phase  = Phase::PulseOn;
  pattern.nextAt = millis() + pattern.onMs;
}

bool parseSequence(const String& seq) {
  pattern.groupCount = 0;

  int start = 0;
  while (start < seq.length() && pattern.groupCount < 8) {
    int comma  = seq.indexOf(',', start);
    String part = comma >= 0 ? seq.substring(start, comma) : seq.substring(start);
    part.trim();

    int value = part.toInt();
    if (value < 1 || value > 9) {
      return false;
    }

    pattern.groups[pattern.groupCount++] = value;

    if (comma < 0) break;
    start = comma + 1;
  }

  return pattern.groupCount > 0;
}

void startPatternFromRequest() {
  stopPattern();

  pattern.onMs    = server.hasArg("on")    ? server.arg("on").toInt()    : 120;
  pattern.offMs   = server.hasArg("off")   ? server.arg("off").toInt()   : 80;
  pattern.pauseMs = server.hasArg("pause") ? server.arg("pause").toInt() : 500;
  pattern.duty    = server.hasArg("duty")  ? server.arg("duty").toInt()  : PWM_MAX_DUTY;

  pattern.onMs    = constrain(pattern.onMs,    20,    2000);
  pattern.offMs   = constrain(pattern.offMs,   20,    2000);
  pattern.pauseMs = constrain(pattern.pauseMs,  0,    5000);
  pattern.duty    = constrain(pattern.duty,      0, PWM_MAX_DUTY);

  bool ok = false;

  if (server.hasArg("seq")) {
    ok = parseSequence(server.arg("seq"));
  } else if (server.hasArg("n")) {
    int n = constrain(server.arg("n").toInt(), 1, 9);
    pattern.groups[0] = n;
    pattern.groupCount = 1;
    ok = true;
  } else {
    pattern.groups[0] = 1;
    pattern.groupCount = 1;
    ok = true;
  }

  if (!ok) {
    server.send(400, "application/json", "{\"error\":\"bad sequence\"}");
    return;
  }

  pattern.active     = true;
  pattern.groupIndex = 0;
  pattern.pulseIndex = 0;

  server.send(200, "application/json", "{\"ok\":true}");

  startPulse();
}

void patternLoop() {
  if (!pattern.active)           return;
  if (!timeReached(pattern.nextAt)) return;

  switch (pattern.phase) {
    case Phase::PulseOn:
      motorOff();
      pattern.pulseIndex++;

      if (pattern.pulseIndex < pattern.groups[pattern.groupIndex]) {
        pattern.phase  = Phase::PulseOff;
        pattern.nextAt = millis() + pattern.offMs;
      } else {
        pattern.groupIndex++;
        pattern.pulseIndex = 0;

        if (pattern.groupIndex < pattern.groupCount) {
          pattern.phase  = Phase::GroupPause;
          pattern.nextAt = millis() + pattern.pauseMs;
        } else {
          stopPattern();
        }
      }
      break;

    case Phase::PulseOff:
    case Phase::GroupPause:
      startPulse();
      break;

    case Phase::Idle:
    default:
      stopPattern();
      break;
  }
}

// ===== HTTP =====

void handleRoot() {
  String html;
  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Vibro</title></head><body>";
  html += "<h2>Vibro</h2>";
  html += "<p><a href='/vibe?n=1'>1x</a></p>";
  html += "<p><a href='/vibe?n=2'>2x</a></p>";
  html += "<p><a href='/vibe?n=3'>3x</a></p>";
  html += "<p><a href='/vibe?seq=2,3'>2x, Pause, 3x</a></p>";
  html += "<p><a href='/vibe?seq=2,3&on=120&off=80&pause=500&duty=160'>2,3 angepasst</a></p>";
  html += "<p><a href='/off'>Aus</a></p>";
  html += "<p><a href='/status'>Status</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"active\":";
  json += pattern.active ? "true" : "false";
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"host\":\"";
  json += HOSTNAME;
  json += ".local\"}";
  server.send(200, "application/json", json);
}

void setupHttp() {
  server.on("/", handleRoot);
  server.on("/vibe", startPatternFromRequest);
  server.on("/off", []() {
    stopPattern();
    server.send(200, "application/json", "{\"off\":true}");
  });
  server.on("/status", handleStatus);
  server.begin();
}

// ===== WLAN =====

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(HOSTNAME)) {
    Serial.print("mDNS: http://");
    Serial.print(HOSTNAME);
    Serial.println(".local/");
  } else {
    Serial.println("mDNS konnte nicht gestartet werden");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  pwmInit();
  motorOff();

  connectWifi();
  setupHttp();

  Serial.println("Bereit.");
}

void loop() {
  server.handleClient();
  patternLoop();

  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}