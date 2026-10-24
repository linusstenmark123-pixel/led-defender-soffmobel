// LED Defender 1.1 - native game, WLED 16.0.1 ESP32 (unverified hardware build).
// Important: Does NOT change current WLED state, segments, colors, presets or K4.
// Draws an overlay ONLY while playing. Stop => immediate normal WLED output.
#include "wled.h"
#include <Preferences.h>
#include "DefenderCore.h"
#include "page_gz.h"

class LedDefenderMod : public Usermod {
  DefenderCore game;
  uint32_t frameMs = 0, lastClientMs = 0;
  uint32_t highscore = 0;
  bool initialized = false;
  static constexpr uint32_t CLIENT_TIMEOUT_MS = 15000;

  void saveHighscore() {
    if (!game.recordPending) return;
    game.recordPending = false;
    if (game.score <= (int32_t)highscore) return;
    highscore = game.score;
    Preferences prefs;
    if (prefs.begin("leddefender", false)) {
      prefs.putUInt("best", highscore);
      prefs.end();
    }
  }

public:
  void setup() override {
    Preferences prefs;
    if (prefs.begin("leddefender", true)) {
      highscore = prefs.getUInt("best", 0);
      prefs.end();
    }
    // Embedded controller: http://<WLED-IP>/defender
    server.on("/defender", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncWebServerResponse *response = request->beginResponse_P(
        200, "text/html; charset=utf-8", LEDDEFENDER_PAGE_GZ, LEDDEFENDER_PAGE_GZ_LEN);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-store");
      request->send(response);
    });
    initialized = true;
  }

  void loop() override {
    if (!initialized) return;
    uint32_t ms = millis();
    if (game.phase == DefenderCore::PLAYING) {
      // If phone is closed/disconnected, stop automatically; never leave light locked in game.
      if ((uint32_t)(ms - lastClientMs) > CLIENT_TIMEOUT_MS) {
        game.stop();
        strip.trigger();
      } else {
        game.tick(ms);
        if (game.phase != DefenderCore::PLAYING) strip.trigger();
        if ((uint32_t)(ms - frameMs) >= 25) {
          frameMs = ms;
          strip.trigger();  // WLED LED service owns strip.show().
        }
      }
    }
    saveHighscore();
  }

  // Called by WLED just before LED refresh, after all segment effects and layers.
  void handleOverlayDraw() override {
    if (game.phase != DefenderCore::PLAYING) return;
    // The furniture is configured with exactly 291 LEDs, indices 0..290.
    for (unsigned i = 0; i < 291; ++i) strip.setPixelColor(i, 0);
    for (unsigned i = DefenderCore::FIRST; i <= DefenderCore::LAST; i += 10)
      strip.setPixelColor(i, 0x030811);
    const uint32_t colors[2] = {0x17D8FF, 0xFFD050};
    for (const auto &enemy : game.enemies) if (enemy.alive) {
      const int pixel = DefenderCore::FIRST + (int)roundf(enemy.x);
      if (pixel >= DefenderCore::FIRST && pixel < DefenderCore::LAST) {
        strip.setPixelColor(pixel, colors[enemy.color]);
        if (pixel > DefenderCore::FIRST)
          strip.setPixelColor(pixel - 1, enemy.color ? 0x46310A : 0x05323B);
      }
    }
    for (const auto &bullet : game.shots) if (bullet.alive) {
      const int pixel = DefenderCore::FIRST + (int)roundf(bullet.x);
      if (pixel >= DefenderCore::FIRST && pixel < DefenderCore::LAST)
        strip.setPixelColor(pixel, colors[bullet.color]);
    }
    strip.setPixelColor(DefenderCore::LAST, game.blinkTime > 0 ? 0xFF173D : 0xC43A66);
  }

  void addToJsonState(JsonObject &root) override {
    JsonObject state = root["leddefender"];
    if (state.isNull()) state = root.createNestedObject("leddefender");
    state["phase"] = (int)game.phase;
    state["score"] = game.score;
    state["best"] = highscore;
    state["lives"] = game.lives;
    state["combo"] = game.combo;
    state["spawned"] = game.spawned;
    state["total"] = DefenderCore::TOTAL;
    JsonArray enemies = state.createNestedArray("enemies");
    for (const auto &e : game.enemies) if (e.alive) {
      JsonArray item = enemies.createNestedArray();
      item.add((int)roundf(e.x)); item.add(e.color);
    }
    JsonArray shots = state.createNestedArray("shots");
    for (const auto &s : game.shots) if (s.alive) {
      JsonArray item = shots.createNestedArray();
      item.add((int)roundf(s.x)); item.add(s.color);
    }
  }

  void readFromJsonState(JsonObject &root) override {
    if (!initialized) return;
    JsonObject state = root["leddefender"];
    if (state.isNull()) return;
    const char *cmd = state["cmd"] | "";
    if (!strcmp(cmd, "start")) {
      // Game only operates when WLED is already powered on; no K4 or light changes.
      if (bri == 0) return;
      game.start(millis());
      lastClientMs = millis();
      strip.trigger();
    } else if (!strcmp(cmd, "stop")) {
      game.stop();
      strip.trigger();
    } else if (!strcmp(cmd, "shoot")) {
      lastClientMs = millis();
      game.shoot((uint8_t)(state["color"] | 0), millis());
    } else if (!strcmp(cmd, "ping")) {
      lastClientMs = millis();
    }
  }
};

static LedDefenderMod ledDefender;
REGISTER_USERMOD(ledDefender);
