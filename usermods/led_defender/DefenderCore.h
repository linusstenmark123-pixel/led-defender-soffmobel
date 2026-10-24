#pragma once
// LED Defender 2.0. No Arduino dependency: the game runs entirely on the ESP32.
#include <stdint.h>
#include <stddef.h>
#include <math.h>

struct DefenderCore {
  static constexpr int LED_COUNT = 291;
  static constexpr int FIRST = 40, LAST = 190;
  static constexpr int MAX_ENEMIES = 30, MAX_SHOTS = 16, MAX_BLASTS = 12;
  enum Phase : uint8_t { READY=0, PLAYING=1, WON=2, LOST=3 };
  enum EnemyKind : uint8_t { NORMAL=0, FAST=1, ARMOR=2, SPLITTER=3, ELITE=4 };
  enum PortalKind : uint8_t { TIMED=0, BREAKABLE=1 };
  struct Enemy {
    float x=0, warpCooldown=0;
    int8_t direction=1;
    uint8_t color=0, kind=NORMAL, hp=1;
    bool alive=false;
  };
  struct Shot {
    float x=0;
    int8_t direction=-1;
    uint8_t color=0;
    bool alive=false;
  };
  struct Portal {
    bool active=false;
    uint8_t kind=TIMED, count=0, hp=0;
    int8_t direction=1;
    float entry=0, exit=0, remaining=0, duration=0;
    int barStart=0;
  };
  struct Blast {float x=0, time=0;uint8_t color=0;bool alive=false;};
  Phase phase=READY;
  uint8_t map=0; // map 0 = LEDs 40..190, map 1 = LEDs 0..290
  uint8_t lives=3, combo=0, bestCombo=0, spawned=0, total=12, level=1;
  int32_t score=0;
  bool recordPending=false, waveCleared=false;
  uint32_t rng=0x12C1BACC, lastMs=0, shotMs=0;
  float spawnWait=0, pauseRemaining=0, flashTime=0;
  Enemy enemies[MAX_ENEMIES]{};
  Shot shots[MAX_SHOTS]{};
  Portal portals[2]{};
  Blast blasts[MAX_BLASTS]{};

  uint32_t rand32() {
    rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
    return rng;
  }
  uint8_t colorsCount() const { return level < 4 ? 2 : level < 7 ? 3 : 4; }
  int left() const { return map==0 ? FIRST : 0; }
  int right() const { return map==0 ? LAST : LED_COUNT-1; }
  float base() const { return map==0 ? float(LAST) : 145.0f; }
  float enemySpeed() const {
    float s=6.6f + (float(level)-1.0f)*0.83f;
    return s > 28.0f ? 28.0f : s;
  }
  int enemiesAlive() const {int n=0;for(const auto &e:enemies)if(e.alive)++n;return n;}
  bool portalsActive() const {for(const auto &p:portals)if(p.active)return true;return false;}
  void blast(float x,uint8_t color,float sec=0.25f) {
    for(auto &b:blasts) if(!b.alive) {b.x=x;b.time=sec;b.color=color;b.alive=true;return;}
  }
  void beginWave() {
    spawned=0;
    total=(uint8_t)((12+(int(level)-1)*2)<35?(12+(int(level)-1)*2):35);
    spawnWait=0;
    pauseRemaining=0;
    waveCleared=false;
    for(auto &e:enemies)e.alive=false;
    for(auto &s:shots)s.alive=false;
    for(auto &p:portals)p=Portal{};
    for(auto &b:blasts)b.alive=false;
    if(level<6)return;
    // A portal is always in front of the defenders, but behind the spawn point.
    // The bridge has 5..10 coloured LEDs, independently of teleport distance.
    int count=5+int(level-6)/2;
    if(count>10)count=10;
    int pCount=(level>=16 && map==1)?2:1;
    for(int i=0;i<pCount;i++) {
      Portal &p=portals[i];
      p.active=true;
      p.kind=(level>=9 && ((level%3!=0) || level==9 || i==1))?BREAKABLE:TIMED;
      p.direction=(map==1 && ((level+i)%2))?-1:1;
      float begin=(p.direction>0)?float(left()):float(right());
      float route=(map==0)?150.0f:145.0f;
      p.entry=begin+p.direction*(route*0.36f);
      p.exit=begin+p.direction*(route*0.72f);
      p.count=(uint8_t)count;
      p.hp=(uint8_t)count;
      p.duration=(level<9?9.0f:10.0f)+(float(level-6)*0.7f);
      if(p.duration>28.0f)p.duration=28.0f;
      p.remaining=p.duration;
      float bridgeCenter=(p.entry+p.exit)*0.5f;
      p.barStart=int(roundf(bridgeCenter))-(count/2);
    }
  }
  void start(uint8_t wantedMap,uint32_t ms) {
    map=wantedMap?1:0;
    phase=PLAYING;
    lives=3;score=0;combo=0;bestCombo=0;level=1;
    flashTime=0;recordPending=false;lastMs=ms;shotMs=ms-250;
    rng^=(ms|1u);
    beginWave();
  }
  void stop(){phase=READY;}
  void spawnEnemy(float x,int8_t dir,uint8_t color,uint8_t kind,uint8_t hp) {
    for(auto &e:enemies) if(!e.alive) {
      e.x=x;e.direction=dir;e.color=color;e.kind=kind;e.hp=hp;
      e.warpCooldown=0;e.alive=true;
      return;
    }
  }
  void spawnRandom() {
    int8_t dir=1;
    if(map==1 && (rand32()&1))dir=-1;
    uint8_t kind=NORMAL;
    const uint32_t n=rand32()%100;
    if(level>=10 && n<8)kind=ELITE;
    else if(level>=8 && n<22)kind=SPLITTER;
    else if(level>=7 && n<37)kind=ARMOR;
    else if(level>=5 && n<57)kind=FAST;
    uint8_t c=(uint8_t)(rand32()%colorsCount());
    spawnEnemy(dir==1?(float)left():(float)right(),dir,c,kind,(kind==ARMOR||kind==ELITE)?2:1);
    spawned++;
  }
  bool shoot(uint8_t color,uint8_t side,uint32_t ms) {
    if(phase!=PLAYING || waveCleared || color>=colorsCount() || ms-shotMs<175)return false;
    for(auto &s:shots)if(!s.alive) {
      s.x=base();
      s.direction=map==0?-1:(side==0?-1:1);
      s.color=color;s.alive=true;shotMs=ms;
      return true;
    }
    return false;
  }
  void damageEnemy(Enemy &e) {
    e.hp--;
    if(e.hp>0) {blast(e.x,e.color,0.17f);score+=25;return;}
    e.alive=false;
    ++combo;if(combo>bestCombo)bestCombo=combo;
    int multiplier=(combo>=10)?2:(combo>=5?3:2);
    int basePoints=e.kind==FAST?150:e.kind==ARMOR?200:e.kind==SPLITTER?250:e.kind==ELITE?350:100;
    score+=basePoints*multiplier/2;
    blast(e.x,e.color,0.32f);
    if(e.kind==SPLITTER) {
      // Two fragments move towards the base. One is slightly ahead and one behind.
      float a=e.x+e.direction*6.0f;
      float b=e.x-e.direction*6.0f;
      spawnEnemy(a,e.direction,(uint8_t)(rand32()%colorsCount()),NORMAL,1);
      spawnEnemy(b,e.direction,(uint8_t)(rand32()%colorsCount()),NORMAL,1);
    }
  }
  void hitPortal(Portal &p) {
    if(!p.active||p.kind!=BREAKABLE)return;
    if(p.hp>0)--p.hp;
    score+=25;
    blast(p.exit,3,0.2f);
    if(p.hp==0){p.active=false;score+=300;blast(p.entry,3,0.65f);blast(p.exit,3,0.65f);}
  }
  void tick(uint32_t ms) {
    if(phase!=PLAYING)return;
    float dt=float(uint32_t(ms-lastMs))*0.001f;
    lastMs=ms;
    if(dt<=0)return;
    if(dt>0.08f)dt=0.08f;
    if(flashTime>0)flashTime-=dt;
    for(auto &b:blasts)if(b.alive){b.time-=dt;if(b.time<=0)b.alive=false;}
    if(waveCleared) {
      pauseRemaining-=dt;
      if(pauseRemaining<=0){++level;beginWave();}
      return;
    }
    for(auto &p:portals)if(p.active && p.kind==TIMED) {
      p.remaining-=dt;
      if(p.remaining<=0){p.remaining=0;p.active=false;blast(p.entry,2,0.35f);blast(p.exit,2,0.35f);}
    }
    spawnWait+=dt;
    float interval=2.15f-0.075f*(level-1);
    if(interval<0.65f)interval=0.65f;
    if(spawned<total && spawnWait>=(spawned==0?0.1f:interval)) {
      if(enemiesAlive()<MAX_ENEMIES-3){spawnWait=0;spawnRandom();}
    }
    for(auto &e:enemies)if(e.alive) {
      float step=enemySpeed()*dt*(e.kind==FAST?1.65f:e.kind==ELITE?1.25f:1.0f);
      float before=e.x;
      e.x+=e.direction*step;
      if(e.warpCooldown>0)e.warpCooldown-=dt;
      if(e.warpCooldown<=0) for(auto &p:portals) if(p.active && p.direction==e.direction) {
        if((e.direction==1 && before<p.entry && e.x>=p.entry) ||
           (e.direction==-1 && before>p.entry && e.x<=p.entry)) {
          blast(p.entry,e.color,0.24f);
          e.x=p.exit+e.direction*2.0f;
          e.warpCooldown=1.0f;
          blast(p.exit,e.color,0.24f);
          break;
        }
      }
      if((e.direction==1 && e.x>=base()) || (e.direction==-1 && e.x<=base())) {
        e.alive=false;
        if(lives>0)--lives;
        combo=0;flashTime=0.4f;
        if(!lives){phase=LOST;recordPending=true;return;}
      }
    }
    for(auto &s:shots)if(s.alive) {
      float old=s.x;
      s.x+=s.direction*125.0f*dt;
      float closest=1.0e9f;
      Enemy *target=nullptr;
      Portal *portal=nullptr;
      // Find first object on projectile path (including fast-frame crossing).
      for(auto &e:enemies)if(e.alive) {
        float travel=(e.x-old)*s.direction;
        if(travel>=-0.6f && travel<=fabsf(s.x-old)+0.7f && travel<closest) {
          closest=travel;target=&e;portal=nullptr;
        }
      }
      for(auto &p:portals)if(p.active&&p.kind==BREAKABLE && p.direction==-s.direction) {
        // Player shoots the exit mouth, which is nearer to the base.
        float travel=(p.exit-old)*s.direction;
        if(travel>=-0.7f && travel<=fabsf(s.x-old)+0.7f && travel<closest) {
          closest=travel;portal=&p;target=nullptr;
        }
      }
      if(target||portal) {
        s.alive=false;
        if(target) {if(target->color==s.color)damageEnemy(*target);else{combo=0;blast(target->x,4,0.13f);}}
        else hitPortal(*portal);
      } else if(s.x<left()-1 || s.x>right()+1)s.alive=false;
    }
    if(spawned==total && enemiesAlive()==0) {
      score+=250+100*lives;
      waveCleared=true;pauseRemaining=2.3f;recordPending=true;
      for(auto &p:portals)p.active=false;
    }
  }
};
