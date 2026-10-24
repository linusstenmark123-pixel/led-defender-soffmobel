// LED Defender 2.0: native WLED overlay, no modification of presets, segments or K4.
// Uses same known-good WLED v0.16.0.1 usermod integration as LED Defender v1.1.
#include "wled.h"
#include <Preferences.h>
#include "DefenderCore.h"
#include "page_gz.h"

class LedDefenderMod : public Usermod {
  DefenderCore game;
  uint32_t frameMs=0,lastClientMs=0;
  uint32_t highscore[2]={0,0};
  uint16_t highestLevel[2]={1,1};
  uint8_t highestCombo[2]={0,0};
  bool initialized=false;
  static constexpr uint32_t CLIENT_TIMEOUT_MS=15000;

  void saveRecords() {
    if(!game.recordPending)return;
    game.recordPending=false;
    const int m=game.map;
    bool changed=false;
    if(game.score>(int32_t)highscore[m]){highscore[m]=game.score;changed=true;}
    if(game.level>highestLevel[m]){highestLevel[m]=game.level;changed=true;}
    if(game.bestCombo>highestCombo[m]){highestCombo[m]=game.bestCombo;changed=true;}
    if(!changed)return;
    Preferences p;
    if(p.begin("leddefender",false)){
      p.putUInt(m?"best2":"best",highscore[m]);
      p.putUShort(m?"level2":"level1",highestLevel[m]);
      p.putUChar(m?"combo2":"combo1",highestCombo[m]);
      p.end();
    }
  }
  static uint32_t color(uint8_t i) {
    static constexpr uint32_t c[5]={0x17D8FF,0xFFD050,0xB579FF,0x2EE6A6,0xFF4C64};
    return c[i<5?i:4];
  }
  void dot(int x,uint32_t c){if(x>=0 && x<DefenderCore::LED_COUNT)strip.setPixelColor(x,c);}

public:
  void setup() override {
    Preferences p;
    if(p.begin("leddefender",true)) {
      highscore[0]=p.getUInt("best",0); // v1.1 compatible
      highscore[1]=p.getUInt("best2",0);
      highestLevel[0]=p.getUShort("level1",1);
      highestLevel[1]=p.getUShort("level2",1);
      highestCombo[0]=p.getUChar("combo1",0);
      highestCombo[1]=p.getUChar("combo2",0);
      p.end();
    }
    server.on("/defender",HTTP_GET,[](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response=request->beginResponse_P(
        200,"text/html; charset=utf-8",LEDDEFENDER_PAGE_GZ,LEDDEFENDER_PAGE_GZ_LEN);
      response->addHeader("Content-Encoding","gzip");
      response->addHeader("Cache-Control","no-store");
      request->send(response);
    });
    initialized=true;
  }
  void loop() override {
    if(!initialized)return;
    const uint32_t ms=millis();
    if(game.phase==DefenderCore::PLAYING) {
      if((uint32_t)(ms-lastClientMs)>CLIENT_TIMEOUT_MS){
        game.stop();strip.trigger();
      } else {
        game.tick(ms);
        if(game.phase!=DefenderCore::PLAYING)strip.trigger();
        if((uint32_t)(ms-frameMs)>=25){frameMs=ms;strip.trigger();}
      }
    }
    saveRecords();
  }
  void handleOverlayDraw() override {
    if(game.phase!=DefenderCore::PLAYING)return;
    // WLED remains in control of GPIO16 and WS2815's output timing.
    // The game temporarily covers all 291 LEDs; effects resume when game stops.
    for(int i=0;i<DefenderCore::LED_COUNT;++i)dot(i,0);
    const int l=game.left(),r=game.right();
    for(int i=l;i<=r;i+=10)dot(i,0x030811);
    const bool map2=game.map==1;
    const int home=(int)game.base();
    dot(home,game.flashTime>0?0xFF173D:0xB63865);
    if(map2){dot(home-1,0x401323);dot(home+1,0x401323);}

    // Portal openings are black with luminous rims; bridge is the health/timer bar.
    for(const auto &p:game.portals)if(p.active) {
      const uint32_t c=p.kind==DefenderCore::TIMED?0xA05AFF:0xFF8A28;
      const int entry=(int)roundf(p.entry),exit=(int)roundf(p.exit);
      dot(entry-1,c);dot(entry,0);dot(entry+1,c);
      dot(exit-1,c);dot(exit,0);dot(exit+1,c);
      int filled=p.kind==DefenderCore::BREAKABLE?p.hp:
        int(ceilf(float(p.count)*p.remaining/p.duration));
      if(filled>p.count)filled=p.count;
      for(int i=0;i<p.count;++i)dot(p.barStart+i,i<filled?c:0);
    }

    // Trails, enemies, shots, and impact flashes drawn on top of portals.
    for(const auto &e:game.enemies)if(e.alive) {
      int x=(int)roundf(e.x);
      uint32_t trail=e.color==0?0x063644:e.color==1?0x493511:
        e.color==2?0x2A1643:0x08402D;
      dot(x-e.direction,trail);
      if(e.kind==DefenderCore::ARMOR||e.kind==DefenderCore::ELITE){
        dot(x+e.direction,e.hp>=2?0xFFFFFF:0x553535);
      }
      if(e.kind==DefenderCore::SPLITTER){dot(x-e.direction*2,0x6D26A3);}
      dot(x,color(e.color));
    }
    for(const auto &s:game.shots)if(s.alive){
      int x=(int)roundf(s.x);
      dot(x-s.direction,0x333333);
      dot(x,color(s.color));
    }
    for(const auto &b:game.blasts)if(b.alive){
      int x=(int)roundf(b.x);
      dot(x,color(b.color));
      if(b.time>0.17f){dot(x-1,0x555555);dot(x+1,0x555555);}
    }
    if(game.waveCleared){dot(home,0x42FF9D);dot(home-1,0x0A5030);}
  }

  void addToJsonState(JsonObject &root) override {
    JsonObject s=root["leddefender"];
    if(s.isNull())s=root.createNestedObject("leddefender");
    s["phase"]=(int)game.phase;
    s["map"]=game.map;
    s["level"]=game.level;
    s["score"]=game.score;
    s["best"]=highscore[game.map];
    s["best1"]=highscore[0];s["best2"]=highscore[1];
    s["level1"]=highestLevel[0];s["level2"]=highestLevel[1];
    s["bestcombo1"]=highestCombo[0];s["bestcombo2"]=highestCombo[1];
    s["lives"]=game.lives;
    s["combo"]=game.combo;
    s["spawned"]=game.spawned;
    s["total"]=game.total;
    s["colors"]=game.colorsCount();
    s["waveClear"]=game.waveCleared;
    JsonArray enemies=s.createNestedArray("enemies");
    for(const auto &e:game.enemies)if(e.alive){
      JsonArray a=enemies.createNestedArray();
      a.add(int(roundf(e.x)));a.add(e.color);a.add(e.kind);a.add(e.direction);a.add(e.hp);
    }
    JsonArray shots=s.createNestedArray("shots");
    for(const auto &p:game.shots)if(p.alive){
      JsonArray a=shots.createNestedArray();
      a.add(int(roundf(p.x)));a.add(p.color);
    }
    JsonArray portals=s.createNestedArray("portals");
    for(const auto &p:game.portals)if(p.active){
      JsonArray a=portals.createNestedArray();
      a.add((int)p.kind);a.add((int)roundf(p.entry));a.add((int)roundf(p.exit));
      a.add(p.barStart);a.add(p.count);
      a.add(p.kind==DefenderCore::TIMED?
        int(ceilf(p.count*p.remaining/p.duration)):int(p.hp));
    }
  }

  void readFromJsonState(JsonObject &root) override {
    if(!initialized)return;
    JsonObject s=root["leddefender"];
    if(s.isNull())return;
    const char *cmd=s["cmd"]|"";
    if(!strcmp(cmd,"start")){
      // Does not switch K4 or alter any saved WLED state.
      if(bri==0)return;
      game.start((uint8_t)(s["map"]|0),millis());
      lastClientMs=millis();strip.trigger();
    } else if(!strcmp(cmd,"stop")){
      game.stop();strip.trigger();
    } else if(!strcmp(cmd,"shoot")){
      lastClientMs=millis();
      game.shoot((uint8_t)(s["color"]|0),(uint8_t)(s["side"]|0),millis());
    } else if(!strcmp(cmd,"ping")){
      lastClientMs=millis();
    }
  }
};
static LedDefenderMod ledDefender;
REGISTER_USERMOD(ledDefender);
