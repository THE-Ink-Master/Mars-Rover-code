#include <Arduino.h>

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADT7410.h>
#include "Adafruit_ThinkInk.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>


#define SRAM_CS     32
#define EPD_CS      15
#define EPD_DC      33
#define EPD_RESET   -1
#define EPD_BUSY    -1


ThinkInk_213_Mono_B72 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();


const char* ssid = "MarsRoverBluey";
const char* password = "";


// Static IP + DNS for Google streaming
IPAddress local_IP(192, 168, 1, 7);
IPAddress gateway(192, 168, 1, 8);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(192, 168, 1, 8);


// Google Apps Script DoPost URL
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzOuwPfS-okXHiHduOyuGty6SnhYR84COfCVHdJ85V4groIByQt_ksPWJNX5kymd9tb/exec";


WebServer server(80);


// Mission / charting / streaming variables
bool isRecording = false;
bool isCharting = false;
bool streamToWeb = false;
bool streamToGoogle = false;
String currentFilename = "";
float lowThreshold = 22.0;
float normalLowThreshold = 23.0;
float highThreshold = 26.0;
String currentMessage = "";
unsigned long lastMessageUpdate = 0;
float currentTempC = 0.0;
float latestTemperature = 0.0;
unsigned long lastGoogleUpdate = 0;


// Sampling interval (ms)
unsigned long samplingInterval = 5000;


// ===== Mission / MySQL functions =====
void sendTemperatureToServer(float temperature) {
  if (WiFi.status() == WL_CONNECTED && isRecording) {
    HTTPClient http;
    http.begin("http://192.168.1.8/insert_temp.php");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "temp=" + String(temperature, 2) + "&filename=" + currentFilename;
    http.POST(postData);
    http.end();
  }
}


void exportDataToCSV() {
  if (currentFilename == "") return;
  HTTPClient http;
  http.begin("http://192.168.1.8/export_data.php?filename=" + currentFilename);
  http.GET();
  http.end();
}


void clearDatabase() {
  HTTPClient http;
  http.begin("http://192.168.1.8/clear_data.php");
  http.GET();
  http.end();
  isRecording = false;
  isCharting = false;
  currentFilename = "";
}


// ===== Google streaming =====
void sendTempToGoogleSheets(float temperature) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String postData = "temperature=" + String(temperature, 2);


    Serial.println("Posting to Google Sheets: " + String(GOOGLE_SCRIPT_URL));
    Serial.println("Post data: " + postData);


    http.begin(client, GOOGLE_SCRIPT_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);


    int httpResponseCode = http.POST(postData);


    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);


    String response = http.getString();
    Serial.println("Full response from server:");
    Serial.println(response);


    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}


// ===== Web server handlers =====
void handleRoot();
void handleTemp();
void handleStart();
void handleStop();
void handleClear();
void handleChartStart();
void handleChartStop();
void handleChartClear();
void handleStartStream();
void handleStopStream();
void handleStartGoogle();
void handleStopGoogle();


void setup() {
  Serial.begin(115200);
  Wire.begin();


  display.begin();
  display.setTextSize(1);
  display.setTextColor(EPD_BLACK);


  if (!tempsensor.begin()) {
    Serial.println("ADT7410 not found!");
    while (1);
  }
  tempsensor.setResolution(ADT7410_16BIT);


  configTime(0, 0, "pool.ntp.org");


  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Failed to configure static IP with DNS");
  }


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");


  server.on("/", handleRoot);
  server.on("/temp", handleTemp);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/clear", handleClear);
  server.on("/chart_start", handleChartStart);
  server.on("/chart_stop", handleChartStop);
  server.on("/chart_clear", handleChartClear);
  server.on("/start_stream", handleStartStream);
  server.on("/stop_stream", handleStopStream);
  server.on("/start_google", handleStartGoogle);
  server.on("/stop_google", handleStopGoogle);


  server.begin();
}


// ===== Loop =====
void loop() {
  server.handleClient();


  static unsigned long lastEInkUpdate = 0;
  if (millis() - lastEInkUpdate > 60000) {
    float tempC = tempsensor.readTempC();
    display.clearBuffer();
    display.setCursor(0, 10);
    display.print("Mars Rover Bluey");
    display.setCursor(0, 30);
    display.print("Temp: ");
    display.print(tempC, 1);
    display.print(" C");
    display.setCursor(0, 50);
    display.print("Msg: ");
    display.print(currentMessage);
    display.setCursor(0, 70);
    display.print("IP: ");
    display.print(WiFi.localIP());
    display.display();
    lastEInkUpdate = millis();
  }
}


// ===== Web / Temp / Chart handlers =====
void handleRoot() { currentTempC = tempsensor.readTempC(); server.send(200, "text/html", getWebPage(currentTempC)); }


void handleTemp() {
  currentTempC = tempsensor.readTempC();


  // Charting uses this endpoint only
  server.send(200, "text/plain", isCharting ? String(currentTempC, 2) : "null");


  // MySQL logging
  if (isRecording) sendTemperatureToServer(currentTempC);


  // Update Google streaming independently
  latestTemperature = currentTempC;
  if (streamToGoogle && millis() - lastGoogleUpdate >= 5000) {
    sendTempToGoogleSheets(latestTemperature);
    lastGoogleUpdate = millis();
  }


  // Update e-ink message
  if (millis() - lastMessageUpdate > 60000) {
    if (currentTempC < lowThreshold)
      currentMessage = "STOP! You might have found ice = water on Mars";
    else if (currentTempC > highThreshold)
      currentMessage = "STOP! Too hot - seek shelter";
    else
      currentMessage = "All systems normal";
    lastMessageUpdate = millis();
  }
}


void handleStart() {
  if (!isRecording) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "mission_%Y%m%d_%H%M%S", timeinfo);
    currentFilename = String(buffer);
  }
  isRecording = true;
  server.send(200, "text/plain", "Started");
}


void handleStop() { isRecording = false; exportDataToCSV(); server.send(200, "text/plain", "Stopped"); }
void handleClear() { clearDatabase(); server.send(200, "text/plain", "Cleared"); }
void handleChartStart() { isCharting = true; server.send(200, "text/plain", "Charting Started"); }
void handleChartStop() { isCharting = false; server.send(200, "text/plain", "Charting Stopped"); }
void handleChartClear() { isCharting = false; server.send(200, "text/plain", "Chart Cleared"); }


// ===== Web streaming / Google handlers =====
void handleStartStream() { streamToWeb = true; server.send(200, "text/plain", "Streaming to web enabled"); }
void handleStopStream()  { streamToWeb = false; streamToGoogle = false; server.send(200, "text/plain", "Streaming to web stopped"); }
void handleStartGoogle() { 
  if (streamToWeb) { streamToGoogle = true; server.send(200, "text/plain", "Google upload started"); } 
  else server.send(200, "text/plain", "Streaming must be ON before uploading to Google"); 
}
void handleStopGoogle() { streamToGoogle = false; server.send(200, "text/plain", "Google upload stopped"); }


// ===== Webpage generator =====
String getWebPage(float currentTemp) {
  // Return original dashboard HTML including Sample Rate, Chart, Mission Controls
  // Add streaming buttons after sample rate
  String html = R"rawliteral(
  <!DOCTYPE html><html><head>
  <title>Mars Rover Bluey Mission Control</title>
  <style>
  body { background-color: #001f3f; color: white; font-family: Arial; text-align: center; }
  canvas { background-color: #003366; border: 1px solid white; margin-top: 20px; cursor: crosshair; }
  button, input, select { padding: 5px 10px; margin: 3px; font-size: 14px; }
  input[type="number"] { width: 80px; }
  #tempDash { font-weight: bold; margin-left: 10px; }
  </style></head><body>
  <h1>Mars Rover Bluey Mission Control</h1>
  <p>WiFi Signal: RSSI )rawliteral" + String(WiFi.RSSI()) + R"rawliteral( dBm</p>
  <p>Sensor IP: )rawliteral" + WiFi.localIP().toString() + R"rawliteral(</p>


  <h2>Set Thresholds</h2>
  <input id="low" placeholder="Low < 22" type="number" step="0.1">
  <input id="normalLow" placeholder="Normal >= 23" type="number" step="0.1">
  <input id="high" placeholder="> High 26" type="number" step="0.1">
  <button onclick="setThresholds()">Set Thresholds</button>


  <h2>Mission Controls</h2>
  <button onclick="startRecording()">Start Mission Recording</button>
  <button onclick="stopRecording()">Stop & Export Mission</button>
  <button onclick="clearDatabase()">Clear Database</button>
  <button onclick="window.open('http://192.168.1.8/Mission_Downloads.php')">View Downloads</button>


  <h2>Charting Controls</h2>
  <button onclick="startCharting()">Start Charting</button>
  <button onclick="stopCharting()">Stop Charting</button>
  <button onclick="clearChart()">Clear Chart</button>


  <h2>Set Sample Rate (seconds)</h2>
  <select id="intervalSlider" onchange="updateInterval()">
    <option value="250">0.25</option>
    <option value="500">0.5</option>
    <option value="1000">1</option>
    <option value="2000">2</option>
    <option value="5000" selected>5</option>
    <option value="10000">10</option>
  </select>


  <h2>Streaming / Google Upload</h2>
<div id="streamBox" style="border: 2px solid black; padding: 10px; display: inline-block;">
  <button id="startStreamBtn" onclick="toggleStartStream()">Start Stream</button>
  <button id="stopStreamBtn" onclick="toggleStopStream()">Stop Stream</button>
  <span id="streamIndicator" style="display:inline-block;width:15px;height:15px;border-radius:50%;background:red;margin-left:10px;"></span>
  <br>
  <button id="startGoogleBtn" onclick="toggleStartGoogle()" disabled>Start Google Upload</button>
  <button id="stopGoogleBtn" onclick="toggleStopGoogle()">Stop Google Upload</button>
  <span id="googleIndicator" style="display:inline-block;width:15px;height:15px;border-radius:50%;background:red;margin-left:10px;"></span>
  <p style="font-size: 12px; color: yellow; margin-top:5px;">Upload only available after Start Stream is enabled</p>
</div>




  <h2>
    <span id="alertMessage"></span>
    <span id="tempDash">-- °C</span>
  </h2>


  <canvas id="tempChart" width="900" height="500"></canvas>


  <script>
const temps = [];
const maxPoints = 60;
let low = 22, normLow = 23, high = 26;
let currentInterval = 5000;
let tempInterval;
let lastTemp = null;


const canvas = document.getElementById("tempChart");
const ctx = canvas.getContext("2d");
const alertElem = document.getElementById("alertMessage");
const tempDash = document.getElementById("tempDash");
let clickedIndex = -1;


const streamIndicator = document.getElementById("streamIndicator");
const googleIndicator = document.getElementById("googleIndicator");


function toggleStartStream() {
  fetch("/start_stream").then(() => {
    alert("Stream started");
    streamActive = true;
    startGoogleBtn.disabled = false;
    streamIndicator.style.background = "green";   // turn light green
  });
}


function toggleStopStream() {
  fetch("/stop_stream").then(() => {
    alert("Stream stopped");
    streamActive = false;
    startGoogleBtn.disabled = true;
    streamIndicator.style.background = "red";     // turn light red
    googleIndicator.style.background = "red";     // also turn off Google indicator
  });
}


function toggleStartGoogle() {
  fetch("/start_google")
    .then(res => res.text())
    .then(t => {
      alert(t);
      if(t.includes("started")) googleIndicator.style.background = "green"; // green when active
    });
}


function toggleStopGoogle() {
  fetch("/stop_google").then(() => {
    alert("Google upload stopped");
    googleIndicator.style.background = "red";      // back to red
  });
}




// ===== Thresholds & Interval =====
function setThresholds() {
  low = parseFloat(document.getElementById("low").value) || low;
  normLow = parseFloat(document.getElementById("normalLow").value) || normLow;
  high = parseFloat(document.getElementById("high").value) || high;
}


function updateInterval() {
  const sel = document.getElementById("intervalSlider");
  currentInterval = parseInt(sel.value);
  clearInterval(tempInterval);
  tempInterval = setInterval(fetchTemp, currentInterval);
  clickedIndex = -1;
}


// ===== Fetch temperature & chart =====
function fetchTemp() {
  fetch("/temp")
    .then(res => res.text())
    .then(temp => {
      let val = parseFloat(temp);
      if (!isNaN(val)) drawChart(val);
    });
}


function drawChart(currentTemp) {
  if (!isNaN(currentTemp)) {
    if (temps.length >= maxPoints) temps.shift();
    temps.push(currentTemp);
    lastTemp = currentTemp;
  }
  ctx.clearRect(0, 0, 900, 500);
  let minTemp = Math.min(...temps, low) - 1;
  let maxTemp = Math.max(...temps, high) + 1;
  drawAxes(minTemp, maxTemp);


  if (temps.length > 0) {
    ctx.beginPath();
    for (let i = 0; i < temps.length; i++) {
      let x = 50 + (i / (maxPoints - 1)) * (880 - 50);
      let y = 480 - ((temps[i] - minTemp) / (maxTemp - minTemp)) * (480 - 20);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    let last = temps[temps.length - 1];
    if (last < low) ctx.strokeStyle = "blue";
    else if (last >= normLow && last <= high) ctx.strokeStyle = "green";
    else if (last > high) ctx.strokeStyle = "red";
    else ctx.strokeStyle = "yellow";
    ctx.lineWidth = 2;
    ctx.stroke();


    for (let i = 0; i < temps.length; i++) {
      let x = 50 + (i / (maxPoints - 1)) * (880 - 50);
      let y = 480 - ((temps[i] - minTemp) / (maxTemp - minTemp)) * (480 - 20);
      ctx.beginPath();
      ctx.arc(x, y, 5, 0, 2 * Math.PI);
      ctx.fillStyle = "white";
      ctx.fill();
    }


    if (clickedIndex >= 0 && clickedIndex < temps.length) {
      let x = 50 + (clickedIndex / (maxPoints - 1)) * (880 - 50);
      let y = 480 - ((temps[clickedIndex] - minTemp) / (maxTemp - minTemp)) * (480 - 20);
      ctx.fillStyle = "yellow";
      ctx.font = "16px Arial";
      ctx.textAlign = "center";
      ctx.fillText(temps[clickedIndex].toFixed(1) + "°C", x, y - 15);
    }


    let msg = "Proceed with caution";
    let color = "yellow";
    if (last < low) { msg = "STOP! You might have found ice = water on Mars"; color = "blue"; }
    else if (last > high) { msg = "STOP! Too hot - seek shelter"; color = "red"; }
    else if (last >= normLow && last <= high) { msg = "All systems normal"; color = "green"; }


    alertElem.innerText = msg;
    alertElem.style.color = color;
    tempDash.innerText = last.toFixed(1) + " °C";
  }
}


function drawAxes(minTemp, maxTemp) {
  ctx.strokeStyle = "#ffffff88";
  ctx.font = "14px Arial";
  ctx.fillStyle = "white";
  ctx.textAlign = "right";
  ctx.beginPath();
  ctx.moveTo(50, 20);
  ctx.lineTo(50, 480);
  ctx.stroke();


  let steps = 6;
  for(let i=0;i<=steps;i++){
    let y=480-(i/steps)*(480-20);
    let val=minTemp+(i/steps)*(maxTemp-minTemp);
    ctx.fillText(val.toFixed(1),45,y+5);
    ctx.beginPath();
    ctx.moveTo(50,y);
    ctx.lineTo(55,y);
    ctx.stroke();
  }


  ctx.beginPath();
  ctx.moveTo(50,480);
  ctx.lineTo(880,480);
  ctx.stroke();


  ctx.textAlign="center";
  let totalSeconds=maxPoints*(currentInterval/1000);
  let labelStep=5;
  let approxLabels=10;
  let intervalStep=Math.max(labelStep,Math.floor(totalSeconds/approxLabels));
  for(let sec=0;sec<=totalSeconds;sec+=intervalStep){
    let x=50+(sec/totalSeconds)*(880-50);
    ctx.fillText(sec+"s",x,495);
    ctx.beginPath();
    ctx.moveTo(x,480);
    ctx.lineTo(x,475);
    ctx.stroke();
  }
}


// ===== Canvas click for tooltip =====
canvas.addEventListener("click",(event)=>{
  if(temps.length===0)return;
  const rect=canvas.getBoundingClientRect();
  const clickX=event.clientX-rect.left;
  const clickY=event.clientY-rect.top;
  let minDist=1e9;
  let closestIndex=-1;
  for(let i=0;i<temps.length;i++){
    let x=50+(i/(maxPoints-1))*(880-50);
    let minTemp=Math.min(...temps,low)-1;
    let maxTemp=Math.max(...temps,high)+1;
    let y=480-((temps[i]-minTemp)/(maxTemp-minTemp))*(480-20);
    let dist=Math.sqrt((clickX-x)**2+(clickY-y)**2);
    if(dist<minDist){ minDist=dist; closestIndex=i; }
  }
  if(minDist<=10) clickedIndex=closestIndex;
  else clickedIndex=-1;
});


// ===== Mission / chart / database buttons =====
function startRecording(){ fetch("/start").then(()=>alert("Recording started")); }
function stopRecording(){ fetch("/stop").then(()=>alert("Mission stopped and data exported")); }
function clearDatabase(){ fetch("/clear").then(()=>alert("Database cleared and recording stopped")); }
function startCharting(){ fetch("/chart_start").then(()=>alert("Charting started")); }
function stopCharting(){ fetch("/chart_stop").then(()=>alert("Charting stopped")); }
function clearChart(){ temps.length=0; clickedIndex=-1; fetch("/chart_clear").then(()=>alert("Chart cleared")); }


// ===== Initialize =====
window.onload=()=>{ tempInterval=setInterval(fetchTemp,currentInterval); };
</script>


  </body></html>
  )rawliteral";
  return html;
}