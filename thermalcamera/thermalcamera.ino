#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <WebSocketsServer.h>

// ---------- WiFi config ----------
const char* ssid     = "ESP32-Thermal";
const char* password = "thermal123"; 

// Hardware Trigger Pin (Built-in LED on most ESP32s)
const int TRIGGER_PIN = 2; 

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
Adafruit_MLX90640 mlx;

float frame[768];
uint8_t wsData[8 + 768 * 2]; 

// ---------- HTML page ----------
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 Fire Detection AI</title>
<style>
  body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:20px}
  canvas{border:2px solid #333; border-radius: 8px; width:640px;height:480px;background:#000; image-rendering:auto; cursor:crosshair; box-shadow: 0 4px 15px rgba(0,0,0,0.5);}
  #info{margin-top:15px;font-size:16px;color:#0f0; font-weight:bold;}
  #hover{font-size:14px;color:#fff; margin-top:5px;}
  #fire-alert { display: none; background: #ff0000; color: white; padding: 15px; font-size: 24px; font-weight: bold; margin-bottom: 20px; border-radius: 8px; animation: flash 0.5s infinite alternate; }
  @keyframes flash { from { opacity: 1; } to { opacity: 0.5; } }
</style>
</head>
<body>
<h2>MLX90640 Fire Detection</h2>
<div id="fire-alert">🔥 FIRE DETECTED! 🔥</div>
<canvas id="c" width="320" height="240"></canvas>
<div id="info">Connecting...</div>
<div id="hover">Hover over the image to read temperature</div>

<script>
const c = document.getElementById('c');
const ctx = c.getContext('2d');
const info = document.getElementById('info');
const hover = document.getElementById('hover');
const fireAlert = document.getElementById('fire-alert');
ctx.imageSmoothingEnabled = true;

// ==========================================
// 🔥 AI / ALGORITHM SETTINGS
// ==========================================
const FIRE_THRESHOLD_C = 60.0; // Temperature to consider as fire (in Celsius)
const MIN_FIRE_PIXELS = 3;     // How many hot pixels must be together to trigger
// ==========================================

let minT = 20, maxT = 40; 
let latestDataView = null;
let isFireAlertActive = false;

function colorMap(v){
  v = Math.max(0, Math.min(1, v));
  let r=0, g=0, b=0;
  if (v < 0.25){ r=0; g=0; b=Math.floor(255*(v/0.25)); }
  else if (v < 0.5){ r=0; g=Math.floor(255*((v-0.25)/0.25)); b=255; }
  else if (v < 0.75){ r=Math.floor(255*((v-0.5)/0.25)); g=255; b=Math.floor(255*(1-(v-0.5)/0.25)); }
  else { r=255; g=Math.floor(255*(1-(v-0.75)/0.25)); b=0; }
  return [r,g,b];
}

var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
ws.binaryType = "arraybuffer";

ws.onopen = function() { info.textContent = "LIVE - System Ready"; };
ws.onclose = function() { info.textContent = "Disconnected."; };

ws.onmessage = function(event) {
  const buf = event.data;
  latestDataView = new DataView(buf);
  
  const fMin = latestDataView.getFloat32(0, true);
  const fMax = latestDataView.getFloat32(4, true);
  
  minT = minT*0.95 + fMin*0.05;
  maxT = maxT*0.95 + fMax*0.05;
  if (maxT - minT < 2) maxT = minT + 2;

  const rawCanvas = document.createElement('canvas');
  rawCanvas.width = 32;
  rawCanvas.height = 24;
  const rawCtx = rawCanvas.getContext('2d');
  const img = rawCtx.createImageData(32, 24);

  let hotPixelCount = 0;
  let fireMinX = 32, fireMinY = 24, fireMaxX = 0, fireMaxY = 0;

  for (let i=0; i<768; i++){
    const t = latestDataView.getInt16(8 + i*2, true) / 100.0;
    
    // --- FIRE DETECTION LOGIC ---
    if (t >= FIRE_THRESHOLD_C) {
      hotPixelCount++;
      const x = i % 32;
      const y = Math.floor(i / 32);
      if (x < fireMinX) fireMinX = x;
      if (x > fireMaxX) fireMaxX = x;
      if (y < fireMinY) fireMinY = y;
      if (y > fireMaxY) fireMaxY = y;
    }

    // Visual rendering
    const norm = (t - minT) / (maxT - minT);
    const [r,g,b] = colorMap(norm);
    img.data[i*4+0] = r;
    img.data[i*4+1] = g;
    img.data[i*4+2] = b;
    img.data[i*4+3] = 255;
  }
  rawCtx.putImageData(img, 0, 0);
  
  // Upscale image
  ctx.drawImage(rawCanvas, 0, 0, 320, 240);
  
  // --- DRAW BOUNDING BOX ---
  if (hotPixelCount >= MIN_FIRE_PIXELS) {
    // Trigger Alert ON
    if (!isFireAlertActive) {
      isFireAlertActive = true;
      fireAlert.style.display = 'block';
      ws.send("TRIGGER_ON"); // Send signal to ESP32
    }

    // Draw Red Box (Scale from 32x24 to 320x240 means multiply by 10)
    ctx.strokeStyle = '#ff0000';
    ctx.lineWidth = 3;
    ctx.strokeRect(fireMinX * 10, fireMinY * 10, (fireMaxX - fireMinX + 1) * 10, (fireMaxY - fireMinY + 1) * 10);
    
    // Draw Text
    ctx.fillStyle = '#ff0000';
    ctx.font = 'bold 16px Arial';
    ctx.fillText('FIRE', fireMinX * 10 + 5, fireMinY * 10 - 5);

  } else {
    // Trigger Alert OFF
    if (isFireAlertActive) {
      isFireAlertActive = false;
      fireAlert.style.display = 'none';
      ws.send("TRIGGER_OFF"); // Send signal to ESP32
    }
  }
  
  info.textContent = `min ${minT.toFixed(1)}°C | max ${maxT.toFixed(1)}°C`;
};
</script>
</body>
</html>
)rawliteral";

// --- HANDLE MESSAGES FROM BROWSER ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)payload);
    
    if (message == "TRIGGER_ON") {
      Serial.println("FIRE DETECTED! Turning ON trigger.");
      digitalWrite(TRIGGER_PIN, HIGH); // Turn on LED/Buzzer/Relay
    } 
    else if (message == "TRIGGER_OFF") {
      Serial.println("Fire gone. Turning OFF trigger.");
      digitalWrite(TRIGGER_PIN, LOW);  // Turn off LED/Buzzer/Relay
    }
  }
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TRIGGER_PIN, OUTPUT);
  digitalWrite(TRIGGER_PIN, LOW);

  Wire.begin(21, 22);
  Wire.setClock(400000); 

  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("MLX90640 not found!");
    while (1) delay(1000);
  }
  
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX90640_16_HZ); 

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP()); 

  server.on("/", handleRoot);
  server.begin();
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("WebSocket Server started");
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (mlx.getFrame(frame) == 0) {
    float mn = 1000, mx = -1000;
    for (int i=0; i<768; i++){
      if (frame[i] < mn) mn = frame[i];
      if (frame[i] > mx) mx = frame[i];
    }

    memcpy(wsData+0, &mn, 4);
    memcpy(wsData+4, &mx, 4);
    int16_t* p = (int16_t*)(wsData+8);
    for (int i=0; i<768; i++){
      p[i] = (int16_t)(frame[i] * 100.0f);
    }

    if (webSocket.connectedClients() > 0) {
      webSocket.broadcastBIN(wsData, sizeof(wsData));
    }
  }
  
  delay(10); 
}