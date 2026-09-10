#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <EEPROM.h>

// ============================================================
// bmax_sys
// Raspberry Pi Pico + W5500 MAVLink Bridge
// v1.0 candidate
//
// WORKING hardware mapping:
//
// Pico -> W5500
// 3V3  -> V
// GP20 -> RST
// GP16 -> MI  (MISO)
// GND  -> G
// GP19 -> MO  (MOSI)
// GP18 -> SCK
// GP17 -> CS
//
// Pico -> Flight Controller
// VSYS -> 5V
// GND  -> GND
// GP0  -> TX  -> FC RX
// GP1  -> RX  <- FC TX
//
// IMPORTANT:
// The proven W5500 initialization order is preserved.
// Ethernet starts first, then UART starts.
// ============================================================


// -------------------- Pins --------------------

static const uint8_t PIN_MISO = 16;
static const uint8_t PIN_CS   = 17;
static const uint8_t PIN_SCK  = 18;
static const uint8_t PIN_MOSI = 19;
static const uint8_t PIN_RST  = 20;

static const uint8_t UART_TX_PIN = 0;
static const uint8_t UART_RX_PIN = 1;


// -------------------- Defaults --------------------

static const uint32_t SETTINGS_MAGIC = 0x424D5031UL; // "BMP1"

struct Settings {
  uint32_t magic;

  uint8_t deviceIP[4];
  uint8_t subnet[4];
  uint8_t gateway[4];
  uint8_t targetIP[4];

  uint16_t udpPort;
  uint32_t baud;
};

Settings cfg;


// -------------------- Network --------------------

byte mac[] = {0x02, 0x42, 0x4D, 0x41, 0x58, 0x50};

EthernetServer server(80);
EthernetUDP udp;

IPAddress deviceIP;
IPAddress subnetIP;
IPAddress gatewayIP;
IPAddress targetIP;


// -------------------- Buffers --------------------

uint8_t uartBuffer[256];
uint8_t udpBuffer[256];


// ============================================================
// SETTINGS
// ============================================================

void loadDefaults() {
  cfg.magic = SETTINGS_MAGIC;

  cfg.deviceIP[0] = 192;
  cfg.deviceIP[1] = 168;
  cfg.deviceIP[2] = 88;
  cfg.deviceIP[3] = 50;

  cfg.subnet[0] = 255;
  cfg.subnet[1] = 255;
  cfg.subnet[2] = 255;
  cfg.subnet[3] = 0;

  cfg.gateway[0] = 192;
  cfg.gateway[1] = 168;
  cfg.gateway[2] = 88;
  cfg.gateway[3] = 1;

  cfg.targetIP[0] = 192;
  cfg.targetIP[1] = 168;
  cfg.targetIP[2] = 88;
  cfg.targetIP[3] = 11;

  cfg.udpPort = 14550;
  cfg.baud = 115200;
}


void loadSettings() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);

  if (cfg.magic != SETTINGS_MAGIC ||
      cfg.udpPort == 0 ||
      cfg.baud < 1200 ||
      cfg.baud > 921600) {

    loadDefaults();
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }
}


void saveSettings() {
  cfg.magic = SETTINGS_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}


void updateIPObjects() {
  deviceIP  = IPAddress(cfg.deviceIP[0], cfg.deviceIP[1], cfg.deviceIP[2], cfg.deviceIP[3]);
  subnetIP  = IPAddress(cfg.subnet[0], cfg.subnet[1], cfg.subnet[2], cfg.subnet[3]);
  gatewayIP = IPAddress(cfg.gateway[0], cfg.gateway[1], cfg.gateway[2], cfg.gateway[3]);
  targetIP  = IPAddress(cfg.targetIP[0], cfg.targetIP[1], cfg.targetIP[2], cfg.targetIP[3]);
}


// ============================================================
// W5500
// ============================================================

void resetW5500() {
  pinMode(PIN_RST, OUTPUT);

  digitalWrite(PIN_RST, HIGH);
  delay(100);

  digitalWrite(PIN_RST, LOW);
  delay(100);

  digitalWrite(PIN_RST, HIGH);
  delay(500);
}


void startEthernet() {
  // EXACT initialization order from the verified working build.
  SPI.setRX(PIN_MISO);
  SPI.setCS(PIN_CS);
  SPI.setSCK(PIN_SCK);
  SPI.setTX(PIN_MOSI);
  SPI.begin();

  resetW5500();

  Ethernet.init(PIN_CS);

  Ethernet.begin(
    mac,
    deviceIP,
    gatewayIP,  // DNS
    gatewayIP,
    subnetIP
  );

  delay(1000);

  Serial.print("Hardware status: ");
  switch (Ethernet.hardwareStatus()) {
    case EthernetNoHardware: Serial.println("NO HARDWARE"); break;
    case EthernetW5100:      Serial.println("W5100"); break;
    case EthernetW5200:      Serial.println("W5200"); break;
    case EthernetW5500:      Serial.println("W5500 detected"); break;
    default:                 Serial.println("Unknown"); break;
  }

  Serial.print("Link status: ");
  switch (Ethernet.linkStatus()) {
    case Unknown: Serial.println("Unknown"); break;
    case LinkON:  Serial.println("ON"); break;
    case LinkOFF: Serial.println("OFF"); break;
  }

  Serial.print("Local IP: ");
  Serial.println(Ethernet.localIP());

  udp.begin(cfg.udpPort);
  server.begin();

  Serial.println("HTTP server started");
}


// ============================================================
// UART
// ============================================================

void startUART() {
  // Correct Pico UART0 mapping:
  // GP0 = TX
  // GP1 = RX
  Serial1.setTX(UART_TX_PIN);
  Serial1.setRX(UART_RX_PIN);
  Serial1.begin(cfg.baud);

  Serial.print("UART started: GP0 TX / GP1 RX @ ");
  Serial.println(cfg.baud);

  Serial.print("UDP target: ");
  Serial.print(targetIP);
  Serial.print(":");
  Serial.println(cfg.udpPort);
}


// ============================================================
// MAVLINK TRANSPARENT BRIDGE
// ============================================================

void uartToUdp() {
  int count = 0;

  while (Serial1.available() && count < (int)sizeof(uartBuffer)) {
    uartBuffer[count++] = (uint8_t)Serial1.read();
  }

  if (count > 0) {
    udp.beginPacket(targetIP, cfg.udpPort);
    udp.write(uartBuffer, count);
    udp.endPacket();
  }
}


void udpToUart() {
  int packetSize = udp.parsePacket();

  if (packetSize <= 0) return;

  while (packetSize > 0) {
    int chunk = packetSize;

    if (chunk > (int)sizeof(udpBuffer)) {
      chunk = sizeof(udpBuffer);
    }

    int len = udp.read(udpBuffer, chunk);

    if (len <= 0) break;

    Serial1.write(udpBuffer, len);
    packetSize -= len;
  }
}


// ============================================================
// WEB HELPERS
// ============================================================

void printIP(EthernetClient &client, const uint8_t ip[4]) {
  client.print(ip[0]); client.print('.');
  client.print(ip[1]); client.print('.');
  client.print(ip[2]); client.print('.');
  client.print(ip[3]);
}


bool parseIP(const char *text, uint8_t out[4]) {
  int a, b, c, d;

  if (sscanf(text, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }

  if (a < 0 || a > 255 ||
      b < 0 || b > 255 ||
      c < 0 || c > 255 ||
      d < 0 || d > 255) {
    return false;
  }

  out[0] = (uint8_t)a;
  out[1] = (uint8_t)b;
  out[2] = (uint8_t)c;
  out[3] = (uint8_t)d;

  return true;
}


bool getQueryValue(const char *request, const char *key, char *out, size_t outSize) {
  char search[24];
  snprintf(search, sizeof(search), "%s=", key);

  const char *p = strstr(request, search);

  if (!p) return false;

  p += strlen(search);

  size_t i = 0;

  while (*p &&
         *p != '&' &&
         *p != ' ' &&
         i < outSize - 1) {

    out[i++] = *p++;
  }

  out[i] = '\0';
  return true;
}


void copyIP(uint8_t dst[4], const uint8_t src[4]) {
  for (int i = 0; i < 4; i++) {
    dst[i] = src[i];
  }
}


// ============================================================
// WEB UI
// ============================================================

void sendHeader(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=UTF-8"));
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();
}


void sendMainPage(EthernetClient &client) {
  sendHeader(client);

  client.println(F("<!DOCTYPE html>"));
  client.println(F("<html><head><meta charset='UTF-8'>"));
  client.println(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
  client.println(F("<title>bmax_sys | Pico W5500 MAVLink Bridge</title>"));
  client.println(F("<style>"));
  client.println(F("body{margin:0;background:#000;color:#6f7f49;font-family:'Courier New',monospace;}"));
  client.println(F(".wrap{max-width:980px;margin:40px auto;padding:20px;}"));
  client.println(F(".head{display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #6f7f49;padding-bottom:18px;margin-bottom:28px;}"));
  client.println(F(".brand{font-size:30px;font-weight:900;letter-spacing:3px;}"));
  client.println(F(".sub{font-size:11px;color:#888;margin-top:5px;}"));
  client.println(F(".ver{font-size:12px;font-weight:900;text-align:right;}"));
  client.println(F(".panel{border:2px solid #6f7f49;padding:28px;background:#050505;}"));
  client.println(F("h2{text-align:center;letter-spacing:3px;margin:0 0 28px;}"));
  client.println(F(".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:20px 28px;}"));
  client.println(F("label{display:block;font-size:12px;font-weight:900;margin-bottom:8px;}"));
  client.println(F("input,select{width:100%;height:44px;background:#000;color:#ddd;border:2px solid #6f7f49;padding:0 10px;font:700 15px 'Courier New',monospace;box-sizing:border-box;}"));
  client.println(F(".btn{display:block;width:100%;height:50px;margin-top:26px;background:#6f7f49;color:#000;border:0;font:900 14px 'Courier New',monospace;letter-spacing:2px;cursor:pointer;}"));
  client.println(F(".status{text-align:center;color:#999;font-size:11px;line-height:1.8;margin-top:20px;}"));
  client.println(F(".ok{color:#84965a;font-weight:900;}"));
  client.println(F(".foot{text-align:center;color:#666;font-size:10px;margin-top:18px;}"));
  client.println(F("@media(max-width:700px){.grid{grid-template-columns:1fr}.wrap{margin:10px auto}.head{display:block}.ver{text-align:left;margin-top:12px}}"));
  client.println(F("</style></head><body>"));

  client.println(F("<div class='wrap'>"));
  client.println(F("<div class='head'>"));
  client.println(F("<div><div class='brand'>bmax_sys</div><div class='sub'>UART / ETHERNET MAVLink BRIDGE</div></div>"));
  client.println(F("<div class='ver'>PICO + W5500<br>v1.0</div>"));
  client.println(F("</div>"));

  client.println(F("<div class='panel'>"));
  client.println(F("<h2>NETWORK SETTINGS</h2>"));
  client.println(F("<form method='GET' action='/save'>"));
  client.println(F("<div class='grid'>"));

  client.print(F("<div><label>DEVICE IP</label><input name='deviceIP' value='"));
  printIP(client, cfg.deviceIP);
  client.println(F("' required></div>"));

  client.print(F("<div><label>TARGET IP</label><input name='targetIP' value='"));
  printIP(client, cfg.targetIP);
  client.println(F("' required></div>"));

  client.print(F("<div><label>SUBNET MASK</label><input name='mask' value='"));
  printIP(client, cfg.subnet);
  client.println(F("' required></div>"));

  client.print(F("<div><label>GATEWAY</label><input name='gateway' value='"));
  printIP(client, cfg.gateway);
  client.println(F("' required></div>"));

  client.print(F("<div><label>UDP PORT</label><input type='number' name='port' value='"));
  client.print(cfg.udpPort);
  client.println(F("' min='1' max='65535' required></div>"));

  client.print(F("<div><label>UART BAUDRATE</label><select name='baud'>"));

  const uint32_t baudRates[] = {
    9600UL, 19200UL, 38400UL, 57600UL,
    115200UL, 230400UL, 460800UL, 921600UL
  };

  for (uint8_t i = 0; i < 8; i++) {
    client.print(F("<option value='"));
    client.print(baudRates[i]);
    client.print('\'');
    if (cfg.baud == baudRates[i]) client.print(F(" selected"));
    client.print('>');
    client.print(baudRates[i]);
    client.println(F("</option>"));
  }

  client.println(F("</select></div>"));
  client.println(F("</div>"));
  client.println(F("<button class='btn' type='submit'>SAVE SETTINGS</button>"));
  client.println(F("</form>"));

  client.println(F("<div class='status'>"));
  client.println(F("ETHERNET: <span class='ok'>ONLINE</span><br>"));
  client.println(F("MAVLINK: UART &lt;-&gt; UDP<br>"));
  client.println(F("GP0 TX -&gt; FC RX / GP1 RX &lt;- FC TX"));
  client.println(F("</div>"));

  client.println(F("<div class='foot'>EMBEDDED // NETWORKING // ROBOTICS // bmax_sys</div>"));
  client.println(F("</div></div></body></html>"));

  delay(20);
}

// ============================================================
// SAVED PAGE
// ============================================================

void sendSavedPage(EthernetClient &client) {
  sendHeader(client);

  client.println(F(
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>bmax_sys | Saved</title>"
    "<style>"
    "body{"
      "margin:0;"
      "background:#000;"
      "color:#6f7f49;"
      "font-family:'Courier New',monospace;"
      "display:flex;"
      "align-items:center;"
      "justify-content:center;"
      "height:100vh;"
      "text-align:center;"
    "}"
    ".box{border:2px solid #6f7f49;padding:44px;}"
    "h1{letter-spacing:3px;font-size:25px;}"
    "p{color:#999;line-height:1.7;}"
    "</style>"
    "</head><body>"
    "<div class='box'>"
      "<h1>SETTINGS SAVED</h1>"
      "<p>Configuration stored successfully.</p>"
      "<p>Power-cycle the Pico to apply all network settings.</p>"
      "<p>bmax_sys</p>"
    "</div>"
    "</body></html>"
  ));
}


// ============================================================
// SAVE REQUEST
// ============================================================

void handleSave(EthernetClient &client, const char *request) {
  char value[32];
  uint8_t parsed[4];

  Settings newCfg = cfg;

  if (!getQueryValue(request, "deviceIP", value, sizeof(value)) ||
      !parseIP(value, parsed)) {
    sendMainPage(client);
    return;
  }
  copyIP(newCfg.deviceIP, parsed);

  if (!getQueryValue(request, "targetIP", value, sizeof(value)) ||
      !parseIP(value, parsed)) {
    sendMainPage(client);
    return;
  }
  copyIP(newCfg.targetIP, parsed);

  if (!getQueryValue(request, "mask", value, sizeof(value)) ||
      !parseIP(value, parsed)) {
    sendMainPage(client);
    return;
  }
  copyIP(newCfg.subnet, parsed);

  if (!getQueryValue(request, "gateway", value, sizeof(value)) ||
      !parseIP(value, parsed)) {
    sendMainPage(client);
    return;
  }
  copyIP(newCfg.gateway, parsed);

  if (!getQueryValue(request, "port", value, sizeof(value))) {
    sendMainPage(client);
    return;
  }

  long portValue = atol(value);

  if (portValue < 1 || portValue > 65535) {
    sendMainPage(client);
    return;
  }

  newCfg.udpPort = (uint16_t)portValue;

  if (!getQueryValue(request, "baud", value, sizeof(value))) {
    sendMainPage(client);
    return;
  }

  long baudValue = atol(value);

  if (baudValue < 1200 || baudValue > 921600) {
    sendMainPage(client);
    return;
  }

  newCfg.baud = (uint32_t)baudValue;

  cfg = newCfg;
  saveSettings();

  sendSavedPage(client);
}


// ============================================================
// HTTP SERVER
// ============================================================

void handleWeb() {
  EthernetClient client = server.available();

  if (!client) return;

  char requestLine[300];
  size_t pos = 0;
  unsigned long started = millis();

  while (client.connected() &&
         millis() - started < 1200 &&
         pos < sizeof(requestLine) - 1) {

    if (client.available()) {
      char c = (char)client.read();

      if (c == '\n') break;

      if (c != '\r') {
        requestLine[pos++] = c;
      }
    }
  }

  requestLine[pos] = '\0';

  // Drain remaining HTTP headers.
  bool emptyLine = false;
  uint16_t lineLength = 0;
  started = millis();

  while (client.connected() &&
         millis() - started < 500 &&
         !emptyLine) {

    if (client.available()) {
      char c = (char)client.read();

      if (c == '\n') {
        if (lineLength == 0) {
          emptyLine = true;
        }

        lineLength = 0;
      }
      else if (c != '\r') {
        lineLength++;
      }
    }
  }

  if (strncmp(requestLine, "GET /save?", 10) == 0) {
    handleSave(client, requestLine);
  }
  else {
    sendMainPage(client);
  }

  delay(2);
  client.stop();
}


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(2500);

  Serial.println();
  Serial.println("bmax_sys Pico + W5500 MAVLink Bridge");
  Serial.println("-----------------------------------");

  loadSettings();
  updateIPObjects();

  // Keep the proven order:
  // 1. W5500 / Ethernet
  // 2. UDP + Web
  // 3. UART
  startEthernet();
  startUART();
}


// ============================================================
// LOOP
// ============================================================

void loop() {
  uartToUdp();
  udpToUart();
  handleWeb();
}
