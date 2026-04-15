#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <ctype.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_HumanDetection.h>

static const int SENSOR_RX_PIN = 4;
static const int SENSOR_TX_PIN = 5;
static const int RESET_BUTTON_PIN = 16;
static const int BUZZER_PIN = 10;
static const int OLED_SDA_PIN = 19;
static const int OLED_SCL_PIN = 20;

static const uint8_t SCREEN_WIDTH = 128;
static const uint8_t SCREEN_HEIGHT = 64;
static const int OLED_RESET = -1;
static const uint8_t SCREEN_ADDRESS = 0x3C;

static const int BR_MIN_VALID = 6;
static const int BR_MAX_VALID = 30;
static const int HR_MIN_VALID = 45;
static const int HR_MAX_VALID = 130;
static const size_t SAMPLE_WINDOW = 5;

static const unsigned long SENSOR_INTERVAL_MS = 500;
static const unsigned long OLED_INTERVAL_MS = 1000;
static const unsigned long SHEETS_INTERVAL_MS = 10000;
static const unsigned long BUZZER_ON_MS = 2000;

String scriptURL = "https://script.google.com/macros/s/AKfycbyzg8OJOV_6wYwyaUO8wTYY4XONhOgbBe9m932x0BSs408vU9tm5RpHYT4Av5nmBbbG/exec";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DFRobot_HumanDetection hu(&Serial1);
WebServer server(80);

String storedName = "Patient";
String storedAge = "0";
String healthStatus = "Idle";
String ipAddressText = "0.0.0.0";
bool patientContextSet = false;

bool monitoringStarted = false;
bool sensorReady = false;
bool displayReady = false;
bool buzzerActive = false;

int currentBR = 0;
int currentHR = 0;
int rawBR = 0;
int rawHR = 0;

int brSamples[SAMPLE_WINDOW] = {0};
int hrSamples[SAMPLE_WINDOW] = {0};
size_t brIndex = 0;
size_t hrIndex = 0;
size_t brCount = 0;
size_t hrCount = 0;

unsigned long lastSensorRead = 0;
unsigned long lastOLEDUpdate = 0;
unsigned long lastUpload = 0;
unsigned long buzzerStart = 0;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>SPAND Monitor</title>
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
  <style>
    :root {
      --bg1: #0b1726;
      --bg2: #132a45;
      --card: rgba(255,255,255,0.12);
      --border: rgba(255,255,255,0.16);
      --text: #f3f7fb;
      --muted: #b7c7d9;
      --accent: #59d3b4;
      --ok: #2ecc71;
      --alert: #ff5d73;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: Arial, sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(89,211,180,0.25), transparent 35%),
        radial-gradient(circle at bottom right, rgba(255,138,101,0.25), transparent 30%),
        linear-gradient(135deg, var(--bg1), var(--bg2));
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .wrap {
      width: 100%;
      max-width: 960px;
      background: rgba(8, 18, 30, 0.82);
      border: 1px solid var(--border);
      border-radius: 20px;
      padding: 22px;
    }
    .panel {
      background: var(--card);
      border-radius: 18px;
      padding: 18px;
      border: 1px solid var(--border);
    }
    .hidden { display: none; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 14px;
      margin-bottom: 16px;
    }
    label {
      display: block;
      margin-bottom: 6px;
      font-weight: 700;
    }
    input {
      width: 100%;
      padding: 12px;
      border-radius: 12px;
      border: 1px solid rgba(255,255,255,0.16);
      background: rgba(255,255,255,0.08);
      color: var(--text);
    }
    button {
      border: 0;
      padding: 12px 18px;
      border-radius: 12px;
      background: linear-gradient(135deg, var(--accent), #32b7ff);
      color: #072032;
      font-weight: 800;
      cursor: pointer;
    }
    .stats {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 14px;
      margin: 16px 0;
    }
    .stat {
      background: rgba(255,255,255,0.08);
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 16px;
      padding: 16px;
    }
    .stat span {
      display: block;
      color: var(--muted);
      margin-bottom: 8px;
    }
    .stat strong {
      font-size: 2rem;
    }
    .status {
      display: inline-block;
      padding: 8px 14px;
      border-radius: 999px;
      font-weight: 700;
      background: rgba(46,204,113,0.15);
      color: var(--ok);
    }
    .status.alert {
      background: rgba(255,93,115,0.15);
      color: var(--alert);
    }
    .chartBox {
      height: 300px;
      background: rgba(255,255,255,0.06);
      border-radius: 16px;
      padding: 12px;
      border: 1px solid rgba(255,255,255,0.08);
    }
    .msg {
      min-height: 22px;
      color: var(--muted);
      margin-top: 8px;
    }
  </style>
</head>
<body>
  <div class='wrap'>
    <h2>SPAND Patient Monitor</h2>


    <div id='formView' class='panel'>
      <div class='grid'>
        <div>
          <label for='patientName'>Patient Name</label>
          <input id='patientName' type='text' placeholder='Enter patient name'>
        </div>
        <div>
          <label for='patientAge'>Age</label>
          <input id='patientAge' type='number' min='0' max='120' placeholder='Enter age'>
        </div>
      </div>
      <button id='startButton' type='button'>START MONITORING</button>
      <div id='formMessage' class='msg'></div>
    </div>

    <div id='dashboardView' class='panel hidden'>
      <div id='patientSummary'>Patient: -</div>
      <div id='statusChip' class='status'>Normal</div>
      <div class='stats'>
        <div class='stat'>
          <span>Breath Rate</span>
          <strong><span id='brValue'>0</span> BPM</strong>
        </div>
        <div class='stat'>
          <span>Heart Rate</span>
          <strong><span id='hrValue'>0</span> BPM</strong>
        </div>
        <div class='stat'>
          <span>Status</span>
          <strong id='liveStatus'>Waiting</strong>
        </div>
      </div>
      <div class='chartBox'>
  <div style="margin-bottom:10px;">
    <button onclick="setMode('live')">Live</button>
    <button onclick="setMode('day')">Day</button>
    <button onclick="setMode('week')">Week</button>
    <button onclick="setMode('month')">Month</button>
  </div>
  <canvas id='vitalsChart'></canvas>
</div>
      <div id='dashMessage' class='msg'>Waiting for live data...</div>
    </div>
  </div>

  <script>
    const formView = document.getElementById('formView');
    const dashboardView = document.getElementById('dashboardView');
    const patientNameInput = document.getElementById('patientName');
    const patientAgeInput = document.getElementById('patientAge');
    const startButton = document.getElementById('startButton');
    const formMessage = document.getElementById('formMessage');
    const patientSummary = document.getElementById('patientSummary');
    const brValue = document.getElementById('brValue');
    const hrValue = document.getElementById('hrValue');
    const liveStatus = document.getElementById('liveStatus');
    const statusChip = document.getElementById('statusChip');
    const dashMessage = document.getElementById('dashMessage');

    const savedName = localStorage.getItem('spand_patient_name');
    const savedAge = localStorage.getItem('spand_patient_age');
    if (savedName) patientNameInput.value = savedName;
    if (savedAge) patientAgeInput.value = savedAge;

    let currentMode = 'live';
const GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbyzg8OJOV_6wYwyaUO8wTYY4XONhOgbBe9m932x0BSs408vU9tm5RpHYT4Av5nmBbbG/exec";

function setMode(mode) {
  currentMode = mode;

  // clear chart
  chart.data.labels = [];
  chart.data.datasets[0].data = [];
  chart.data.datasets[1].data = [];
  chart.update();

  if (mode !== 'live') {
    loadSheetData(mode);
  } else {
    dashMessage.textContent = "Switched to live mode";
  }
}

    const chart = new Chart(document.getElementById('vitalsChart').getContext('2d'), {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'Breath Rate', data: [], borderColor: '#59d3b4', tension: 0.35, borderWidth: 3 },
          { label: 'Heart Rate', data: [], borderColor: '#ff8a65', tension: 0.35, borderWidth: 3 }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false
      }
    });

    let pollHandle = null;
    
    async function loadSheetData(type) {
  try {
    dashMessage.textContent = "Loading " + type + " data...";

    const res = await fetch(GOOGLE_SCRIPT_URL + "?type=" + type);
    if (!res.ok) throw new Error('history fetch failed');

    const data = await res.json();
    if (!Array.isArray(data)) throw new Error('unexpected payload');

    chart.data.labels = data.map(d => d.time);
    chart.data.datasets[0].data = data.map(d => Number(d.br));
    chart.data.datasets[1].data = data.map(d => Number(d.hr));

    chart.update();

    dashMessage.textContent = type.toUpperCase() + " data loaded";
  } catch (err) {
    dashMessage.textContent = "Failed to load Google data";
  }
}

    function showDashboard(nameValue, ageValue) {
      formView.classList.add('hidden');
      dashboardView.classList.remove('hidden');
      patientSummary.textContent = 'Patient: ' + nameValue + ' | Age: ' + ageValue;
      if (!pollHandle) pollHandle = setInterval(fetchData, 1000);
      fetchData();
    }

   async function fetchData() {
  if (currentMode !== 'live') return; // stop live updates in history mode

  try {
    const response = await fetch('/data', { cache: 'no-store' });
    const data = await response.json();

    brValue.textContent = data.br;
    hrValue.textContent = data.hr;
    liveStatus.textContent = data.status;
    statusChip.textContent = data.status;
    statusChip.classList.toggle('alert', data.status === 'Alert');

    chart.data.labels.push(new Date().toLocaleTimeString());
    chart.data.datasets[0].data.push(Number(data.br));
    chart.data.datasets[1].data.push(Number(data.hr));

    if (chart.data.labels.length > 20) {
      chart.data.labels.shift();
      chart.data.datasets[0].data.shift();
      chart.data.datasets[1].data.shift();
    }

    chart.update('none');

  } catch (error) {
    dashMessage.textContent = "Live data error";
  }
}

    startButton.addEventListener('click', async () => {
      const nameValue = patientNameInput.value.trim();
      const ageValue = patientAgeInput.value.trim();

      if (!nameValue || !ageValue) {
        formMessage.textContent = 'Please enter both patient name and age.';
        return;
      }

      localStorage.setItem('spand_patient_name', nameValue);
      localStorage.setItem('spand_patient_age', ageValue);

      const body = new URLSearchParams();
      body.append('name', nameValue);
      body.append('age', ageValue);

      try {
        const response = await fetch('/submit', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: body.toString()
        });

        if (!response.ok) throw new Error('submit failed');
        formMessage.textContent = '';
        showDashboard(nameValue, ageValue);
      } catch (error) {
        formMessage.textContent = 'ESP32 did not accept the patient data.';
      }
    });
  </script>
</body>
</html>
)rawliteral";

void resetSamples() {
  memset(brSamples, 0, sizeof(brSamples));
  memset(hrSamples, 0, sizeof(hrSamples));
  brIndex = 0;
  hrIndex = 0;
  brCount = 0;
  hrCount = 0;
  rawBR = 0;
  rawHR = 0;
  currentBR = 0;
  currentHR = 0;
}

void addSample(int *buffer, size_t &index, size_t &count, int value) {
  buffer[index] = value;
  index = (index + 1) % SAMPLE_WINDOW;
  if (count < SAMPLE_WINDOW) {
    count++;
  }
}

int averageSamples(const int *buffer, size_t count) {
  if (count == 0) {
    return 0;
  }

  long sum = 0;
  for (size_t i = 0; i < count; i++) {
    sum += buffer[i];
  }
  return static_cast<int>(sum / static_cast<long>(count));
}

bool isValidBR(int value) {
  return value >= BR_MIN_VALID && value <= BR_MAX_VALID;
}

bool isValidHR(int value) {
  return value >= HR_MIN_VALID && value <= HR_MAX_VALID;
}

String urlEncode(const String &value) {
  String encoded;
  const char *hex = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); i++) {
    unsigned char c = static_cast<unsigned char>(value.charAt(i));
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

void triggerBuzzer() {
  if (!buzzerActive) {
    buzzerActive = true;
    buzzerStart = millis();
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("Buzzer ON");
  }
}

void updateBuzzer() {
  if (buzzerActive && millis() - buzzerStart >= BUZZER_ON_MS) {
    buzzerActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Buzzer OFF");
  }
}

String buildStatus() {
  const bool noBreathing = (rawBR == 0);
  const bool brAlert = (rawBR > 0) && !isValidBR(rawBR);
  const bool hrAlert = !isValidHR(rawHR);

  if (noBreathing || brAlert || hrAlert) {
    return "Alert";
  }
  return "Normal";
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleSubmit() {
  if (!server.hasArg("name") || !server.hasArg("age")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  String incomingName = server.arg("name");
  String incomingAge = server.arg("age");
  incomingName.trim();
  incomingAge.trim();
  if (incomingName.length() == 0 || incomingAge.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid patient context\"}");
    return;
  }

  storedName = incomingName;
  storedAge = incomingAge;
  monitoringStarted = true;
  patientContextSet = true;
  healthStatus = "Normal";
  lastUpload = millis();
  resetSamples();

  Serial.println("Monitoring started");
  Serial.println("Name: " + storedName);
  Serial.println("Age: " + storedAge);

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleData() {
  String json = "{";
  json += "\"br\":" + String(currentBR) + ",";
  json += "\"hr\":" + String(currentHR) + ",";
  json += "\"status\":\"" + healthStatus + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleResetButton() {
  static bool pressed = false;
  if (digitalRead(RESET_BUTTON_PIN) == LOW && !pressed) {
    pressed = true;
    Serial.println("Reset button pressed, clearing WiFi settings...");
    WiFiManager wm;
    wm.resetSettings();
    delay(300);
    ESP.restart();
  }
  if (digitalRead(RESET_BUTTON_PIN) == HIGH) {
    pressed = false;
  }
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  Serial.println("Starting WiFiManager...");
  bool connected = wm.autoConnect("SPAND_Portal");
  if (!connected) {
    Serial.println("WiFi failed. Restarting...");
    delay(1000);
    ESP.restart();
  }

  ipAddressText = WiFi.localIP().toString();
  Serial.println("WiFi connected");
  Serial.println("IP: " + ipAddressText);
}

void setupSensor() {
  Serial1.begin(115200, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  Serial.println("Initializing radar...");

  if (hu.begin() != 0) {
    Serial.println("Radar init failed");
    sensorReady = false;
    return;
  }

  if (hu.configWorkMode(hu.eSleepMode) != 0) {
    Serial.println("Radar sleep mode config failed");
    sensorReady = false;
    return;
  }

  hu.configLEDLight(hu.eHPLed, 1);
  hu.sensorRet();
  sensorReady = true;
  Serial.println("Radar initialized");
}

void setupDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    displayReady = false;
    return;
  }

  displayReady = true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SPAND Initializing...");
  display.display();
}

void updateOLED() {
  if (!displayReady) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SPAND Monitor");
  display.print("IP: ");
  display.println(ipAddressText);
  display.drawFastHLine(0, 18, 128, SSD1306_WHITE);

  display.setCursor(0, 24);
  display.print("BR:");
  display.setTextSize(2);
  display.print(currentBR);
  display.setTextSize(1);
  display.println(" BPM");

  display.setCursor(0, 46);
  display.print("HR:");
  display.setTextSize(2);
  display.print(currentHR);
  display.setTextSize(1);
  display.println(" BPM");

  display.display();
}

void readSensorTask() {
  if (!sensorReady) {
    currentBR = 0;
    currentHR = 0;
    healthStatus = monitoringStarted ? "Alert" : "Idle";
    return;
  }

  if (hu.smHumanData(hu.eHumanPresence) == 1) {
    rawBR = hu.getBreatheValue();
    rawHR = hu.getHeartRate();
  } else {
    rawBR = 0;
    rawHR = 0;
  }

  if (isValidBR(rawBR)) {
    addSample(brSamples, brIndex, brCount, rawBR);
    currentBR = averageSamples(brSamples, brCount);
  } else {
    currentBR = 0;
  }

  if (isValidHR(rawHR)) {
    addSample(hrSamples, hrIndex, hrCount, rawHR);
    currentHR = averageSamples(hrSamples, hrCount);
  } else {
    currentHR = 0;
  }

  healthStatus = monitoringStarted ? buildStatus() : "Idle";
  if (monitoringStarted && healthStatus == "Alert") {
    triggerBuzzer();
  }

  Serial.print("Raw BR: ");
  Serial.print(rawBR);
  Serial.print(" Raw HR: ");
  Serial.print(rawHR);
  Serial.print(" Avg BR: ");
  Serial.print(currentBR);
  Serial.print(" Avg HR: ");
  Serial.print(currentHR);
  Serial.print(" Status: ");
  Serial.println(healthStatus);
}

void sendToGoogleSheet() {
  if (!monitoringStarted || !patientContextSet || WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(10000);

  String url = scriptURL;
  url += "?name=" + urlEncode(storedName);
  url += "&age=" + urlEncode(storedAge);
  url += "&br=" + urlEncode(String(currentBR));
  url += "&hr=" + urlEncode(String(currentHR));
  url += "&status=" + urlEncode(healthStatus);

  Serial.println("Uploading data...");
  Serial.println(url);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);
    if (httpCode > 0) {
      Serial.println(http.getString());
    }
    http.end();
  } else {
    Serial.println("HTTP begin failed");
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/data", HTTP_GET, handleData);
  server.begin();
  Serial.println("Web server started");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("SPAND boot");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  setupDisplay();
  setupWiFi();
  setupSensor();
  setupRoutes();

  updateOLED();
}

void loop() {
  server.handleClient();
  handleResetButton();
  updateBuzzer();

  unsigned long now = millis();

  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    readSensorTask();
  }

  if (now - lastOLEDUpdate >= OLED_INTERVAL_MS) {
    lastOLEDUpdate = now;
    updateOLED();
  }

  if (monitoringStarted && now - lastUpload >= SHEETS_INTERVAL_MS) {
    lastUpload = now;
    sendToGoogleSheet();
  }
}
