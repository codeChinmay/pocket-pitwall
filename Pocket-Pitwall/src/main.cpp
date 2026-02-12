#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <vector>
#include <map>
#include <algorithm>

// --- CONFIG ---
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASS";
const char* SERVER_IP = "IP";
const int SERVER_PORT = 8000;

// Colors
#define C_TRACK   0x3186 
#define C_PIT     0x7BEF 
#define C_BG      0x0000 
#define C_TEXT    0xFFFF 
#define C_ACCENT  0xFD20 

M5Canvas canvas(&M5Cardputer.Display);
M5Canvas bgSprite(&M5Cardputer.Display);
WebSocketsClient webSocket;

// --- PHYSICS ENGINE ---
struct Car {
    float currentX, currentY;
    int targetX, targetY;
    float stepX, stepY;
    int pos;
    bool active;
};
std::map<int, Car> cars;

// Data Models
struct Driver { int id; String name; uint16_t color; };
struct TrackPoint { float x, y; TrackPoint(float _x, float _y) : x(_x), y(_y) {} };
struct SessionMetadata { String winnerName; String fastestLapDriver; };
std::vector<Driver> drivers;
std::vector<TrackPoint> trackPath;
std::vector<TrackPoint> pitPath;
SessionMetadata meta;

float minX, maxX, minY, maxY, scale;
int offX, offY;

enum AppState { MENU_SESSIONS, LOADING, DASHBOARD, REPLAY };
AppState currentState = LOADING;
String gameTimeStr = "00:00";
String statusMsg = "Init...";
unsigned long lastDrawTime = 0;

// NETWORK HELPERS
uint16_t hexTo565(String hexStr) {
    long number = strtol(hexStr.c_str(), NULL, 16);
    return M5Cardputer.Display.color565((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
}

bool fetchJSON(String endpoint, JsonDocument& doc) {
    HTTPClient http;
    String url = String("http://") + SERVER_IP + ":" + SERVER_PORT + endpoint;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) { deserializeJson(doc, http.getString()); http.end(); return true; }
    http.end(); return false;
}

// LOGIC
void worldToScreen(float wx, float wy, int &sx, int &sy) {
    sx = offX + (wx - minX) * scale;
    sy = 135 - (offY + (wy - minY) * scale);
}

void bakeTrack() {
    bgSprite.fillScreen(C_BG);
    if (pitPath.size() > 1) for (size_t i=0; i<pitPath.size()-1; i++) {
        int x1,y1,x2,y2; worldToScreen(pitPath[i].x, pitPath[i].y, x1, y1); worldToScreen(pitPath[i+1].x, pitPath[i+1].y, x2, y2);
        bgSprite.drawLine(x1, y1, x2, y2, C_PIT);
    }
    if (trackPath.size() > 1) for (size_t i=0; i<trackPath.size()-1; i++) {
        int x1,y1,x2,y2; worldToScreen(trackPath[i].x, trackPath[i].y, x1, y1); worldToScreen(trackPath[i+1].x, trackPath[i+1].y, x2, y2);
        bgSprite.drawLine(x1, y1, x2, y2, C_TRACK);
    }
    bgSprite.fillRect(180, 0, 60, 135, 0x10A2);
    bgSprite.drawFastVLine(180, 0, 135, C_ACCENT);
}

void loadSessionData() {
    statusMsg = "Fetching Drivers...";
    JsonDocument doc;
    if (fetchJSON("/session/9523/drivers", doc)) {
        drivers.clear();
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject d : arr) {
            Driver newD; newD.id = d["driver_number"]; newD.name = d["name_acronym"].as<String>(); newD.color = hexTo565(d["team_colour"].as<String>());
            drivers.push_back(newD);
        }
    }

    statusMsg = "Building Track...";
    if (fetchJSON("/session/9523/track_layout", doc)) {
        trackPath.clear(); pitPath.clear();
        minX = doc["bounds"]["min_x"]; maxX = doc["bounds"]["max_x"]; minY = doc["bounds"]["min_y"]; maxY = doc["bounds"]["max_y"];
        float mapW = maxX - minX; float mapH = maxY - minY;
        float scaleX = (180.0 - 20) / mapW; float scaleY = (135.0 - 20) / mapH;
        scale = (scaleX < scaleY) ? scaleX : scaleY;
        offX = (180 - (mapW * scale)) / 2; offY = (135 - (mapH * scale)) / 2;

        for (JsonObject p : doc["track_path"].as<JsonArray>()) trackPath.push_back(TrackPoint(p["x"], p["y"]));
        for (JsonObject p : doc["pit_path"].as<JsonArray>()) pitPath.push_back(TrackPoint(p["x"], p["y"]));
        bakeTrack();
    }
    
    if (fetchJSON("/session/9523/race_metadata", doc)) {
         JsonObject fl = doc["fastest_lap"];
         int fId = fl["driver_number"];
         for(auto& d : drivers) if(d.id == fId) meta.fastestLapDriver = d.name;
    }

    statusMsg = "Connecting Stream...";
    webSocket.begin(SERVER_IP, SERVER_PORT, "/ws/9523");
    
    webSocket.onEvent([](WStype_t type, uint8_t * payload, size_t length) {
        if (type == WStype_TEXT) {
            String msg = (char*)payload;
            int pipe = msg.indexOf('|');
            if (pipe == -1) return;
            
            // Time Parsing
            int ms = msg.substring(0, pipe).toInt();
            int totalSec = ms / 1000;
            char tBuf[16];
            sprintf(tBuf, "%d:%02d:%02d", totalSec/3600, (totalSec%3600)/60, totalSec%60);
            gameTimeStr = String(tBuf);
            
            int start = pipe + 1;
            while(start < msg.length()) {
                int end = msg.indexOf('|', start);
                if(end == -1) end = msg.length();
                String chunk = msg.substring(start, end);
                int c1 = chunk.indexOf(','); int c2 = chunk.indexOf(',', c1+1); int c3 = chunk.indexOf(',', c2+1);
                
                if(c3 != -1) {
                    int id = chunk.substring(0, c1).toInt();
                    int tx = chunk.substring(c1+1, c2).toInt();
                    int ty = chunk.substring(c2+1, c3).toInt();
                    int tpos = chunk.substring(c3+1).toInt();
                    
                    if (cars.find(id) == cars.end() || cars[id].currentX == 0) {
                        cars[id].currentX = tx; cars[id].currentY = ty;
                    }
                    
                    cars[id].targetX = tx;
                    cars[id].targetY = ty;
                    cars[id].pos = tpos;
                    cars[id].active = true;
                    
                    // SMOOTHNESS FIX: Divide by MORE frames than expected
                    // Server sends every 100ms (3 frames at 30fps)
                    // We divide by 6.0 (200ms). The car moves slower, but NEVER stops.
                    // When the next packet arrives, we seamlessly update the target.
                    cars[id].stepX = (tx - cars[id].currentX) / 6.0;
                    cars[id].stepY = (ty - cars[id].currentY) / 6.0;
                }
                start = end + 1;
            }
        }
    });
    currentState = REPLAY;
}

void drawSidebar() {
    canvas.setTextSize(1); canvas.setTextColor(C_TEXT);
    canvas.setCursor(185, 5); canvas.print(gameTimeStr);
    
    std::vector<std::pair<int, int>> sortedCars;
    for(auto const& item : cars) if(item.second.active) sortedCars.push_back({item.second.pos, item.first});
    std::sort(sortedCars.begin(), sortedCars.end());
    
    int y = 25;
    int limit = (sortedCars.size() < 6) ? sortedCars.size() : 6;
    for(int i=0; i < limit; i++) {
        int id = sortedCars[i].second;
        String name = "???"; uint16_t col = WHITE;
        for(auto& d : drivers) if(d.id == id) { name = d.name; col = d.color; break; }
        canvas.fillRect(182, y, 3, 10, col);
        canvas.setCursor(188, y+1); canvas.printf("%d.%s", sortedCars[i].first, name.c_str());
        y += 15;
    }
    canvas.setCursor(185, 120); canvas.setTextColor(PURPLE); canvas.print("FL:"); canvas.print(meta.fastestLapDriver);
}

void drawReplay() {
    bgSprite.pushSprite(&canvas, 0, 0);

    for(auto& item : cars) {
        Car& car = item.second;
        if (!car.active || (car.currentX == 0 && car.currentY == 0)) continue;
        
        // PHYSICS: Always move. Never stop to snap.
        // We only teleport if the error is massive (lag spike)
        if (abs(car.targetX - car.currentX) > 500) { 
             car.currentX = car.targetX; 
             car.currentY = car.targetY;
        } else {
             car.currentX += car.stepX; 
             car.currentY += car.stepY;
        }

        int sx, sy;
        worldToScreen(car.currentX, car.currentY, sx, sy);
        if (sx < 0 || sx > 240 || sy < 0 || sy > 135) continue;

        uint16_t c = WHITE;
        for(auto& d : drivers) if(d.id == item.first) { c = d.color; break; }
        canvas.fillCircle(sx, sy, 3, c);
        if (car.pos == 1) canvas.drawCircle(sx, sy, 5, WHITE);
    }
    
    drawSidebar();
    canvas.pushSprite(0, 0);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(240, 135);
    bgSprite.createSprite(240, 135);
    
    canvas.setTextSize(1); canvas.fillScreen(BLACK);
    canvas.setCursor(10, 10); canvas.println("Connecting WiFi..."); canvas.pushSprite(0,0);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    currentState = LOADING;
}

void loop() {
    M5Cardputer.update();
    
    // IMPORTANT: Check network as fast as possible!
    webSocket.loop();
    
    if (currentState == LOADING) {
        canvas.fillScreen(BLACK); canvas.setCursor(10, 60); canvas.setTextColor(C_ACCENT);
        canvas.println(statusMsg); canvas.pushSprite(0,0);
        loadSessionData();
    }
    else if (currentState == REPLAY) {
        // Non-blocking Timer: Only draw every 33ms (30 FPS)
        // This frees up CPU to handle WebSocket packets instantly
        if (millis() - lastDrawTime > 33) {
            drawReplay();
            lastDrawTime = millis();
        }
    }
}