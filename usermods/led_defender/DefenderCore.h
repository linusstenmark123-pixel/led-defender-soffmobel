#pragma once
// LED Defender 1.0 - game logic independent from WLED / Arduino.
#include <stdint.h>
#include <stddef.h>
#include <math.h>

struct DefenderCore {
  static constexpr int FIRST=40, LAST=190, COUNT=151, TOTAL=12;
  enum Phase : uint8_t { READY=0, PLAYING=1, WON=2, LOST=3 };
  struct Sprite { float x; uint8_t color; bool alive; };
  static constexpr unsigned MAX_ENEMIES=16, MAX_SHOTS=12;
  Phase phase=READY;
  Sprite enemies[MAX_ENEMIES]{};
  Sprite shots[MAX_SHOTS]{};
  uint32_t rng=0x12C1BACC, lastMs=0, shotMs=0;
  uint8_t lives=3;
  int32_t score=0;
  uint8_t combo=0, spawned=0;
  float spawnWait=0;
  bool recordPending=false;
  float blinkTime=0;

  uint32_t rand32() {
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
    return rng;
  }
  void start(uint32_t ms) {
    phase=PLAYING; lives=3; score=0; combo=0; spawned=0;
    spawnWait=0; lastMs=ms; shotMs=ms-300; recordPending=false; blinkTime=0;
    for (auto &e:enemies)e.alive=false;
    for (auto &p:shots)p.alive=false;
    rng^=ms|1;
  }
  void stop() {phase=READY;}
  bool shoot(uint8_t color,uint32_t ms) {
    if(phase!=PLAYING || color>1 || ms-shotMs<220)return false;
    for(auto &p:shots) if(!p.alive){p={149.0f,color,true};shotMs=ms;return true;}
    return false;
  }
  size_t enemiesAlive() const {size_t n=0;for(const auto &e:enemies)if(e.alive)n++;return n;}
  void tick(uint32_t ms) {
    if(phase!=PLAYING)return;
    float dt=(ms-lastMs)*0.001f;
    lastMs=ms;
    if(dt<=0)return;
    if(dt>0.08f)dt=0.08f;
    spawnWait+=dt;
    if(spawned<TOTAL && spawnWait>=(spawned==0?0.1f:2.1f)) {
      spawnWait=0;
      for(auto &e:enemies)if(!e.alive){e={0.0f,(uint8_t)(rand32()&1),true};spawned++;break;}
    }
    for(auto &e:enemies)if(e.alive)e.x+=6.6f*dt;
    for(auto &p:shots)if(p.alive){
      const float oldX=p.x;
      p.x-=125.0f*dt;
      // Nearest collision to turret wins, regardless of color.
      Sprite *hit=nullptr;
      for(auto &e:enemies)if(e.alive&&e.x<=oldX+0.55f&&e.x>=p.x-0.55f) {
        if(!hit||e.x>hit->x)hit=&e;
      }
      if(hit){
        p.alive=false;
        if(hit->color==p.color){
          hit->alive=false;combo++;
          score+=combo>=10?200:combo>=5?150:100;
          blinkTime=0.12f;
        } else combo=0;
      } else if(p.x<0)p.alive=false;
    }
    for(auto &e:enemies)if(e.alive&&e.x>=150.0f){
      e.alive=false;
      if(lives) lives--;
      combo=0;
      blinkTime=0.35f;
      if(!lives){phase=LOST;recordPending=true;return;}
    }
    if(blinkTime>0)blinkTime-=dt;
    if(spawned==TOTAL && enemiesAlive()==0) {
      score+=250+100*lives;phase=WON;recordPending=true;
    }
  }
};
