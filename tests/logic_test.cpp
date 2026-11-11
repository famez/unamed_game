// Test de la lógica del juego, sin ventana ni OpenGL: se rellena `Controls` a mano, se
// simulan pasos fijos de 1/60 s (determinista) y se comprueba el estado interno.
// Se compila con AddressSanitizer + UBSan. Ejecutar: ctest --test-dir build  (o build/logic_test)
#define private public   // solo para el test: acceso al estado interno
#include "game.h"
#undef private
#include <cstdio>
const float dt = 1.0f/60;
int fails = 0;
#define CHECK(c, ...) do { bool ok_=(c); std::printf("%s ", ok_?"[OK]  ":"[FAIL]"); std::printf(__VA_ARGS__); std::printf("\n"); if(!ok_) ++fails; } while(0)
void run(Game& g, float secs, Controls c = {}) { for (int i=0;i<int(secs*60);++i){ g.update(dt,c); c.jump=c.use=c.throwRock=false; c.selectSlot=-1; } }
Game fresh(){ Game g; g.init(); return g; }
int main(){
  { Game g=fresh(); run(g,0.2f); Controls c; c.jump=true; float maxY=0;
    g.update(dt,c); for(int i=0;i<120;++i){ g.update(dt,{}); if(g.hero_.body.pos.y>maxY) maxY=g.hero_.body.pos.y; }
    CHECK(maxY>1.1f && maxY<1.35f, "salto: altura max %.2f m", maxY);
    CHECK(g.hero_.body.grounded && g.hero_.body.pos.y==0, "salto: aterriza (y=%.2f)", g.hero_.body.pos.y); }
  { Game g=fresh(); g.hero_.body.pos={4,2,4}; run(g,1);
    CHECK(std::fabs(g.hero_.body.pos.y-0.5f)<1e-3f && g.hero_.body.grounded, "cae y aterriza sobre bloque: y=%.2f", g.hero_.body.pos.y);
    Controls c; c.move={1,0,0}; c.aimYaw=kPi*0.5f; run(g,1.0f,c);  // cámara mirando hacia donde va
    CHECK(g.hero_.body.pos.y<0.51f && g.hero_.body.pos.x<4.9f, "bloque de 0.5 m sin saltar: bloqueado (x=%.2f y=%.2f)", g.hero_.body.pos.x, g.hero_.body.pos.y);
    c.jump=true; g.update(dt,c); c.jump=false; run(g,1.0f,c);
    CHECK(std::fabs(g.hero_.body.pos.y-1.0f)<1e-3f, "saltando sube al siguiente bloque: y=%.2f", g.hero_.body.pos.y); }
  { Game g=fresh(); g.hero_.body.pos={-8,0,-2}; Controls c; c.move={0,0,-1}; c.aimYaw=kPi; run(g,0.8f,c);
    CHECK(std::fabs(g.hero_.body.pos.y-1.0f)<1e-3f, "escalinata de 0.25 m se sube andando: y=%.2f z=%.2f", g.hero_.body.pos.y, g.hero_.body.pos.z); }
  { Game g=fresh(); g.hero_.body.pos={0,0,4.9f}; run(g,0.1f); Enemy& e=g.enemies_[0];
    Controls c; c.use=true; c.aimYaw=0; run(g,0.6f,c); 
    CHECK(std::fabs(e.health-(100-8*0.2f))<1e-3f, "punetazo de frente: escudo bloquea, vida %.1f", e.health);
    CHECK(e.body.pos.z>6.05f, "y retrocede igualmente: z %.2f (antes 6.00)", e.body.pos.z); }
  { Game g=fresh(); Enemy& e=g.enemies_[0]; g.hero_.body.pos={0,0,7.1f}; g.hero_.item=Item::Sword; run(g,0.1f);
    Controls c; c.use=true; c.aimYaw=kPi; run(g,0.7f,c);
    CHECK(std::fabs(e.health-75)<1e-3f, "espadazo por la espalda: vida %.1f", e.health);
    CHECK(e.body.pos.z<5.6f, "retroceso del espadazo: z %.2f", e.body.pos.z);
    for(int i=0;i<3;++i){ g.hero_.body.pos={0,0,e.body.pos.z+1.1f}; c.use=true; run(g,0.7f,c);}  
    CHECK(!e.alive() && g.kills_==1, "4 espadazos lo derriban (vida %.0f, derribos %d)", e.health, g.kills_);
    run(g,4.2f); CHECK(e.alive() && e.health==100 && std::fabs(e.body.pos.z-6)<1e-3f, "reaparece con vida llena en su sitio"); }
  { Game g=fresh(); Enemy& e=g.enemies_[3]; g.hero_.body.pos={4,0,0}; g.hero_.item=Item::Pistol; run(g,0.1f);
    Controls c; c.use=true; c.aimYaw=kPi; run(g,0.5f,c);
    CHECK(std::fabs(e.health-88)<1e-3f, "disparo por la espalda a 4 m: vida %.1f", e.health);
    CHECK(g.projectiles_.empty(), "la bala desaparece al impactar"); }
  { Game g=fresh(); g.hero_.item=Item::Rocks; run(g,0.1f); Controls c; c.use=true; c.aimYaw=kPi*0.75f; c.aimPitch=0.35f; run(g,0.1f,c);
    run(g,3.0f); 
    CHECK(g.rocks_==9 && g.projectiles_.size()==1 && g.projectiles_[0].resting, "piedra lanzada: queda en el suelo a %.1f m (piedras %d)", length(g.projectiles_[0].pos), g.rocks_);
    g.hero_.body.pos = g.projectiles_[0].pos; g.hero_.body.pos.y=0; run(g,0.1f);
    CHECK(g.rocks_==10 && g.projectiles_.empty(), "se recoge al pasar por encima (piedras %d)", g.rocks_); }
  { Game g=fresh(); Enemy& e=g.enemies_[1]; g.hero_.body.pos={-5.5f,0,6}; g.hero_.item=Item::Rocks; run(g,0.1f);
    Controls c; c.use=true; c.aimYaw=std::atan2(0.5f,3.0f); c.aimPitch=0.35f; run(g,1.5f,c);
    CHECK(e.health<100, "piedra impacta al enemigo de costado: vida %.0f", e.health); }
  { Game g=fresh(); Enemy& e=g.enemies_[2]; float y0=e.body.pos.y; g.hero_.body.pos={12.5f,2.0f,4}; g.hero_.item=Item::Sword; run(g,0.3f);
    CHECK(std::fabs(g.hero_.body.pos.y-2.0f)<1e-3f, "jugador sobre la plataforma alta");
    Controls c; c.aimYaw=-kPi*0.5f; int hits=0;
    while (e.alive() && e.body.pos.y >= y0 - 0.01f && hits < 6) { g.hero_.body.pos={e.body.pos.x+1.1f,2,4}; c.use=true; run(g,0.7f,c); ++hits; }
    run(g,1.0f);
    CHECK(e.body.pos.y < y0, "golpeado por la espalda cae de la plataforma tras %d espadazos: y %.2f -> %.2f", hits, y0, e.body.pos.y); }
  { Game g=fresh(); Enemy& e=g.enemies_[2]; g.hero_.body.pos={8.6f,2.0f,4}; g.hero_.item=Item::Sword; run(g,0.3f);
    Controls c; c.aimYaw=kPi*0.5f; float x0=e.body.pos.x; g.hero_.body.pos={e.body.pos.x-1.1f,2,4}; c.use=true; run(g,0.7f,c);
    CHECK(e.health > 94 && e.body.pos.x - x0 < 0.8f, "de frente el escudo reduce daño y retroceso: vida %.0f, retrocede %.2f m", e.health, e.body.pos.x - x0); }
  { float d[2]; for (int k=0;k<2;++k){ Game g=fresh(); g.hero_.item=Item::Rocks; run(g,0.1f); Controls c; c.use=true; c.aimYaw=kPi*0.75f; c.aimPitch= k? -0.2f : 0.35f; run(g,0.1f,c); run(g,3.0f); d[k]=length(Vec3{g.projectiles_[0].pos.x,0,g.projectiles_[0].pos.z}); }
    CHECK(d[1] > d[0]*1.5f, "mirando arriba se lanza mas lejos: %.1f m vs %.1f m", d[1], d[0]); }
  { Game g=fresh(); Controls c; c.move={0,0,1}; run(g,1.0f,c); float walk=g.hero_.speed; c.run=true; run(g,1.0f,c);
    CHECK(std::fabs(walk-3.5f)<1e-3f && std::fabs(g.hero_.speed-8.0f)<1e-3f, "velocidades: trote %.1f m/s, sprint %.1f m/s", walk, g.hero_.speed);
    Game g2=fresh(); c.run=false; float t=0; while(g2.hero_.speed<3.49f && t<1){ g2.update(dt,c); t+=dt; }
    CHECK(t < 0.11f, "acelera a velocidad maxima en %.2f s", t);
    Game g3=fresh(); g3.hero_.item=Item::Pistol; c.run=true; c.aim=true; c.aimDir={0,0,1}; run(g3,1.0f,c);
    CHECK(std::fabs(g3.hero_.speed-2.2f)<1e-3f, "apuntando va mas despacio aunque se pulse sprint: %.1f m/s", g3.hero_.speed); }
  { Game g=fresh(); Enemy& e=g.enemies_[2]; g.hero_.body.pos={10.5f,0,-3}; g.hero_.item=Item::Pistol; run(g,0.1f);
    Controls c; c.use=true; c.aimYaw=0; run(g,0.5f,c);
    CHECK(e.health==100, "sin apuntar (desde la cadera) la bala choca con la plataforma: vida %.0f", e.health);
    Vec3 eye{11.0f,2.0f,-4.5f}; Vec3 dir=normalize(Vec3{10.5f,3.0f,4.0f}-eye);
    c.aim=true; c.aimOrigin=eye; c.aimDir=dir; c.use=false; run(g,0.3f,c);
    CHECK(g.aimOnEnemy_, "la mira detecta al enemigo de la plataforma alta (punto %.1f,%.1f,%.1f)", g.aimPoint_.x,g.aimPoint_.y,g.aimPoint_.z);
    c.use=true; run(g,0.5f,c);
    CHECK(std::fabs(e.health-88)<1e-3f, "apuntando, la bala sube hasta la plataforma y acierta: vida %.0f", e.health); }
  { // Fallo 1: enemigo entre la cámara y el personaje
    Game g=fresh(); Enemy& e=g.enemies_[0]; g.hero_.body.pos={0,0,8}; g.hero_.item=Item::Pistol; run(g,0.1f);
    Controls c; c.aim=true; c.aimOrigin={0.5f,2.0f,3.0f}; c.aimDir=normalize(Vec3{0,-0.03f,1}); run(g,0.3f,c);
    CHECK(g.aimPoint_.z > 8.5f && !g.aimOnEnemy_, "enemigo a la espalda: la mira no lo toma (punto z=%.1f)", g.aimPoint_.z);
    c.use=true; run(g,0.6f,c);
    CHECK(e.health==100, "y la bala no sale hacia atras: vida del enemigo %.0f", e.health); }
  { // Fallo 2: pistola alineada con el punto de mira (a la derecha y arriba)
    auto barrelAlign=[](const Game& g){ const Mat4& w=g.hero_.joints[WristR];
      Vec3 barrel=normalize(Vec3{-w.m[4],-w.m[5],-w.m[6]});  // eje -Y de la muñeca
      Vec3 muzzle=transformPoint(w,kPistolMuzzle); return dot(barrel, normalize(g.hero_.aimTarget()-muzzle)); };
    Game g=fresh(); g.hero_.item=Item::Pistol; run(g,0.1f);
    Controls c; c.aim=true; c.aimOrigin={0.55f,2.0f,-1.5f}; c.aimDir=normalize(Vec3{0.15f,0.12f,1}); run(g,0.5f,c);
    float a1=barrelAlign(g);
    CHECK(a1>0.995f, "apuntando: canon alineado con la mira (cos %.4f = %.2f grados)", a1, std::acos(std::min(1.f,a1))*57.3f);
    c.aimDir=normalize(Vec3{-0.4f,-0.1f,1}); run(g,0.5f,c); float a2=barrelAlign(g);
    CHECK(a2>0.995f, "apuntando a la izquierda y abajo: alineado (%.2f grados)", std::acos(std::min(1.f,a2))*57.3f);
    Game h=fresh(); h.hero_.item=Item::Pistol; run(h,0.1f); Controls d; d.use=true; d.aimYaw=0.7f; run(h,0.3f,d); float a3=barrelAlign(h);
    CHECK(a3>0.995f, "disparo desde la cadera: alineado (%.2f grados)", std::acos(std::min(1.f,a3))*57.3f); }
  { // Molotov contra un enemigo de frente: el escudo no protege y acaba ardiendo hasta morir
    Game g=fresh(); Enemy& e=g.enemies_[0]; g.hero_.body.pos={0,0,2.5f}; g.hero_.item=Item::Molotov; run(g,0.1f);
    Controls c; c.use=true; c.aimYaw=0; c.aimPitch=0.35f; run(g,0.6f,c);
    CHECK(g.molotovs_==4, "molotov lanzado (quedan %d)", g.molotovs_);
    run(g,0.5f);
    CHECK(g.fires_.size()==1 && std::fabs(g.fires_[0].pos.y)<1e-3f, "se rompe y deja un fuego en el suelo (%zu fuegos, y=%.2f, %zu lenguas)", g.fires_.size(), g.fires_.empty()?-1.f:g.fires_[0].pos.y, g.fires_.empty()?0:g.fires_[0].tongues.size());
    CHECK(e.health < 85 && e.burnTimer > 0, "explosion de frente no bloqueada por el escudo y arde: vida %.0f", e.health);
    run(g,5.0f);
    CHECK(!e.alive() && g.kills_==1, "muere quemado (vida %.0f, derribos %d)", e.health, g.kills_);
    run(g,2.0f); CHECK(g.fires_.empty(), "el fuego se apaga a los 7 s"); }
  { // Fuego sobre plataforma: arde encima y no flota en el borde
    Game g=fresh(); g.enemies_.clear(); g.igniteAt({10.5f,2.0f,4.0f},{0,1,0});
    const Fire& f=g.fires_[0]; bool allTop=true; for(auto&t:f.tongues) allTop &= std::fabs(t.pos.y-2.0f)<1e-3f;
    CHECK(std::fabs(f.pos.y-2.0f)<1e-3f && allTop && f.tongues.size()==22, "en el centro de la plataforma: y=%.1f, %zu/22 lenguas encima", f.pos.y, f.tongues.size());
    Game h=fresh(); h.enemies_.clear(); h.igniteAt({8.3f,2.0f,4.0f},{0,1,0}); const Fire& f2=h.fires_[0];
    bool top2=true; for(auto&t:f2.tongues) top2 &= std::fabs(t.pos.y-2.0f)<1e-3f;
    CHECK(top2 && f2.tongues.size()<16, "en el borde: solo %zu lenguas, todas sobre la plataforma", f2.tongues.size());
    Game k=fresh(); k.enemies_.clear(); k.igniteAt({10.5f,1.0f,1.5f},{0,0,-1});
    CHECK(std::fabs(k.fires_[0].pos.y)<1e-3f, "contra la pared de la plataforma: el fuego cae al suelo (y=%.2f)", k.fires_[0].pos.y);
    Game m=fresh(); m.enemies_.clear(); m.igniteAt({8.0f,1.0f,4.0f},{-1,0,0});
    CHECK(std::fabs(m.fires_[0].pos.y-1.5f)<1e-3f, "rincon pared/escalon: el fuego queda sobre el escalon (y=%.2f)", m.fires_[0].pos.y); }
  { // Quemadura persiste fuera del fuego y el enemigo de la plataforma alta muere al prenderle fuego
    Game g=fresh(); Enemy& e=g.enemies_[2]; g.igniteAt({10.5f,2.0f,4.0f},{0,1,0}); run(g,1.0f);
    CHECK(e.health < 62 && e.health > 50, "enemigo sobre la plataforma: explosion + 1 s ardiendo: vida %.0f", e.health); }
  { // Objetivo resaltado: el enemigo bajo la mira, y nadie al dejar de apuntar
    Game g=fresh(); g.hero_.item=Item::Pistol; run(g,0.1f);
    Controls c; c.aim=true; c.aimOrigin={0.5f,2.0f,-1.5f}; c.aimDir=normalize(Vec3{0,1.2f,6}-c.aimOrigin); run(g,0.2f,c);
    CHECK(g.aimEnemy_==0, "apuntando al enemigo 0: objetivo = %d", g.aimEnemy_);
    c.aimDir=normalize(Vec3{0,0.3f,1}); run(g,0.2f,c);
    CHECK(g.aimEnemy_==-1, "apuntando al cielo: sin objetivo (%d)", g.aimEnemy_);
    c.aimDir=normalize(Vec3{0,1.2f,6}-c.aimOrigin); run(g,0.2f,c); c.aim=false; run(g,0.1f,c);
    CHECK(g.aimEnemy_==-1, "al soltar el boton derecho se quita la silueta (%d)", g.aimEnemy_); }
  { Game g=fresh(); g.enemies_.clear(); g.igniteAt({8.3f,2.0f,4.0f},{0,1,0}); g.igniteAt({0,0,-2},{0,1,0});
    const Fire& a=g.fires_[0]; const Fire& b=g.fires_[1];
    CHECK(a.onPlatform && std::fabs(a.supportMin.x-8.0f)<1e-3f && !b.onPlatform, "soporte del fuego: plataforma x>=%.1f / suelo", a.supportMin.x); }
  { // Movimiento relativo a la cámara: de lado sin girarse, hacia atrás más lento
    Game g=fresh(); g.enemies_.clear(); g.hero_.body.pos={0,0,-12}; Controls c; c.aimYaw=0; c.move={-1,0,0}; run(g,1.0f,c);
    CHECK(std::fabs(wrapAngle(g.hero_.yaw))<0.01f && g.hero_.body.pos.x < -3.0f, "paso lateral: sigue mirando a la camara (yaw %.2f), x=%.1f", g.hero_.yaw, g.hero_.body.pos.x);
    c.move={0,0,-1}; run(g,1.0f,c);
    CHECK(std::fabs(wrapAngle(g.hero_.yaw))<0.01f && std::fabs(g.hero_.speed-3.5f*0.75f)<1e-3f, "hacia atras: de cara a la camara a %.2f m/s", g.hero_.speed);
    c.aimYaw=1.2f; c.move={}; run(g,0.5f,c);
    CHECK(std::fabs(wrapAngle(g.hero_.yaw-1.2f))<0.01f, "parado tambien gira con la camara (yaw %.2f)", g.hero_.yaw); }
  { // Voltereta: espacio yendo de lado rueda en vez de saltar; de frente salta
    Game g=fresh(); g.enemies_.clear(); g.hero_.body.pos={0,0,-12}; Controls c; c.aimYaw=0; c.move={-1,0,0}; run(g,0.3f,c);
    float x0=g.hero_.body.pos.x; c.jump=true; g.update(dt,c); c.jump=false; float maxY=0; bool rolled=g.hero_.rolling();
    for(int i=0;i<30;++i){ g.update(dt,c); maxY=std::max(maxY,g.hero_.body.pos.y); }
    CHECK(rolled && maxY<0.01f, "espacio de lado: voltereta sin despegar del suelo (y max %.2f)", maxY);
    CHECK(x0-g.hero_.body.pos.x > 3.0f, "la voltereta avanza rapido: %.1f m en 0,5 s", x0-g.hero_.body.pos.x);
    run(g,0.5f,c); CHECK(!g.hero_.rolling() && std::fabs(wrapAngle(g.hero_.yaw))<0.01f, "acaba y vuelve a mirar a la camara");
    c.use=true; g.hero_.item=Item::Sword; c.jump=true; g.update(dt,c); c.jump=c.use=false;
    CHECK(g.hero_.action()==Action::None || !g.hero_.rolling(), "no rueda y ataca a la vez");
    Game h=fresh(); h.enemies_.clear(); Controls d; d.move={0,0,1}; run(h,0.3f,d); d.jump=true; h.update(dt,d); d.jump=false; run(h,0.2f,d);
    CHECK(!h.hero_.rolling() && h.hero_.body.pos.y>0.5f, "espacio hacia delante: salta (y %.2f)", h.hero_.body.pos.y); }
  { // Lanzamiento apuntado: la trayectoria prevista es la real
    for (int k=0;k<2;++k) {
      Game g=fresh(); g.enemies_.clear(); g.hero_.item = k ? Item::Molotov : Item::Rocks; run(g,0.1f);
      Controls c; c.aim=true; c.aimDir=normalize(Vec3{0.3f,-0.1f,1}); c.aimOrigin={0,2,-2}; run(g,0.3f,c);
      CHECK(g.throwArc_.size()>5 && g.throwArcHits_ && g.hero_.throwAimHeld(), "%s: trayectoria prevista de %zu puntos", k?"molotov":"piedra", g.throwArc_.size());
      Vec3 predicted=g.throwArc_.back(); c.use=true; g.update(dt,c); c.use=false; Vec3 landed{}; bool done=false;
      for(int i=0;i<300 && !done;++i){ g.update(dt,c);
        if (k && !g.fires_.empty()) { landed=g.blasts_.empty()?g.fires_[0].pos:g.blasts_[0].pos; done=true; }
        if (!k && !g.projectiles_.empty() && g.projectiles_[0].vel.y>0 && g.projectiles_[0].pos.y<0.2f) { landed=g.projectiles_[0].pos; done=true; } }
      CHECK(done && length(landed-predicted)<0.3f, "%s: cae donde marca la linea (error %.2f m)", k?"molotov":"piedra", length(landed-predicted));
    }
    Game g=fresh(); g.hero_.item=Item::Molotov; g.molotovs_=0; Controls c; c.aim=true; run(g,0.2f,c);
    CHECK(g.throwArc_.empty() && !g.hero_.throwAimHeld(), "sin municion no hay trayectoria"); }
  { // Combos: puños directo-izquierdo-gancho y espada tajo-barrido-estocada
    Game g=fresh(); Enemy& e=g.enemies_[3]; g.hero_.body.pos={4,0,-2.9f}; g.hero_.item=Item::Fists; run(g,0.1f);  // detras del enemigo 3
    Controls c; c.aimYaw=kPi; Action seen[3]; float hp[3];
    for(int k=0;k<3;++k){ g.hero_.body.pos=e.body.pos+Vec3{0,0,1.1f}; c.use=true; g.update(dt,c); c.use=false; seen[k]=g.hero_.action(); run(g,0.32f,c); hp[k]=e.health; }
    CHECK(seen[0]==Action::Punch && seen[1]==Action::Punch2 && seen[2]==Action::Punch3, "combo de punos: %d %d %d", int(seen[0]),int(seen[1]),int(seen[2]));
    CHECK(std::fabs(hp[2]-(100-8-8-15))<1e-3f, "danos 8+8+15 por la espalda: vida %.0f", hp[2]);
    run(g,1.0f); c.use=true; g.update(dt,c);
    CHECK(g.hero_.action()==Action::Punch, "pasada la ventana el combo vuelve a empezar");
    Game h=fresh(); h.hero_.item=Item::Sword; run(h,0.1f); Controls d; d.aimYaw=kPi;
    d.use=true; h.update(dt,d); h.update(dt,d); d.use=false;  // dos clics seguidos: el segundo queda en cola
    run(h,0.48f,d); Action second=h.hero_.action(); run(h,0.5f,d); d.use=true; h.update(dt,d); d.use=false;
    CHECK(second==Action::Slash2 && h.hero_.action()==Action::Slash3, "espada: el clic durante el golpe encadena el barrido y luego la estocada");
    Game m=fresh(); Enemy& e3=m.enemies_[3]; m.hero_.item=Item::Sword; m.hero_.body.pos={4,0,-1.8f}; run(m,0.1f);
    Controls f; f.aimYaw=kPi; for(int k=0;k<3;++k){ m.hero_.body.pos=e3.body.pos+Vec3{0,0,1.6f}; f.use=true; m.update(dt,f); f.use=false; run(m,0.6f,f); }
    CHECK(!e3.alive() || e3.health<=100-25-20-35+1e-3f, "tajo+barrido+estocada por la espalda: 80 de dano (vida %.0f)", e3.health); }
  { // Ataque enemigo, bloqueo con espada y esquiva rodando
    Game g=fresh(); Enemy& e=g.enemies_[0]; g.hero_.body.pos={0,0,4.8f}; g.hero_.item=Item::Fists; Controls c; c.aimYaw=0;
    run(g,0.7f,c);
    CHECK(e.attackTime>=0 || e.attackCooldown>0, "el enemigo ataca al tener al jugador delante");
    run(g,0.3f,c);
    CHECK(std::fabs(g.heroHealth_-85)<1e-3f, "el porrazo quita 15: vida %.0f", g.heroHealth_);
    Game h=fresh(); h.hero_.body.pos={0,0,4.8f}; h.hero_.item=Item::Sword; Controls d; d.aimYaw=0; d.aim=true; run(h,1.0f,d);
    CHECK(h.hero_.blocking() && std::fabs(h.heroHealth_-97)<1e-3f, "bloqueando de frente con la espada: solo 3 de dano (vida %.0f)", h.heroHealth_);
    Game k=fresh(); k.hero_.body.pos={0,0,4.8f}; Controls r; r.aimYaw=0; run(k,0.45f,r);
    r.move={1,0,0}; r.jump=true; k.update(dt,r); r.jump=false; run(k,0.3f,r);
    CHECK(k.heroHealth_==100, "rodando se esquiva el porrazo (vida %.0f)", k.heroHealth_);
    Game n=fresh(); Enemy& e0=n.enemies_[0]; n.hero_.body.pos={0,0,4.9f}; n.hero_.item=Item::Pistol; Controls s; s.aimYaw=0; run(n,0.3f,s);
    bool winding=e0.attackTime>=0; s.use=true; n.update(dt,s); s.use=false; run(n,0.15f,s);
    CHECK(winding && e0.attackTime<0 && n.heroHealth_==100, "un disparo durante la preparacion lo interrumpe (vida %.0f)", n.heroHealth_);
    Game q=fresh(); q.heroHealth_=10; q.hero_.body.pos={0,0,4.8f}; Controls t; t.aimYaw=0; run(q,1.0f,t);
    CHECK(q.heroDeadTimer_>=0 && q.heroHealth_==0, "con poca vida el porrazo lo derriba");
    run(q,3.0f); CHECK(q.heroDeadTimer_<0 && q.heroHealth_==100 && length(q.hero_.body.pos)<0.01f, "reaparece en el inicio con vida llena"); }
  { // Escopeta: 8 perdigones en cono; a bocajarro hace mucho más daño que la pistola
    Game g=fresh(); Enemy& e=g.enemies_[3]; g.hero_.body.pos={4,0,-1.5f}; g.hero_.item=Item::Shotgun; run(g,0.1f);
    Controls c; c.aimYaw=kPi; c.use=true; g.update(dt,c); c.use=false;
    int pellets=0; for(auto&p:g.projectiles_) pellets += p.kind==ProjectileKind::Pellet;
    CHECK(pellets==8 && g.shells_==15, "escopeta: %d perdigones, quedan %d cartuchos", pellets, g.shells_);
    run(g,0.5f);
    CHECK(e.health <= 100-7*5, "a 2,5 m por la espalda entran casi todos: vida %.0f", e.health);
    c.use=true; g.update(dt,c); c.use=false; g.update(dt,c);
    CHECK(g.shells_==15, "no dispara otra vez hasta amartillar (0,8 s)"); }
  { // Granada: rebota, explota a los 2 s y daña en área (también al jugador si está cerca)
    Game g=fresh(); g.hero_.item=Item::Grenade; run(g,0.1f);
    Controls c; c.aim=true; c.aimOrigin={0,2,-2}; c.aimDir=normalize(Vec3{0,-0.05f,1}); run(g,0.2f,c);
    c.use=true; g.update(dt,c); c.use=false; run(g,0.5f,c);
    bool flying=false; for(auto&p:g.projectiles_) flying |= p.kind==ProjectileKind::Grenade;
    CHECK(flying && g.grenades_==3, "granada lanzada (quedan %d)", g.grenades_);
    Enemy& e=g.enemies_[0]; float hp0=e.health;
    run(g,1.0f,c); bool still=false; for(auto&p:g.projectiles_) still |= p.kind==ProjectileKind::Grenade;
    CHECK(still && e.health==hp0, "rebota y aun no ha explotado al 1,5 s");
    run(g,1.0f,c);
    CHECK(g.projectiles_.empty() && !g.blasts_.empty(), "explota al acabarse la mecha");
    CHECK(e.health<hp0, "dana al enemigo cercano: vida %.0f", e.health);
    Game h=fresh(); h.enemies_.clear(); h.explodeGrenade(h.hero_.body.pos+Vec3{0.5f,0.2f,0}); 
    CHECK(h.heroHealth_<80, "a medio metro el jugador tambien se lleva la explosion: vida %.0f", h.heroHealth_); }
  { // Martillo: rompe la guardia y lanza por los aires
    Game g=fresh(); Enemy& e=g.enemies_[0]; g.hero_.body.pos={0,0,4.6f}; g.hero_.item=Item::Hammer; run(g,0.05f);
    Controls c; c.aimYaw=0; c.use=true; g.update(dt,c); c.use=false; float maxY=0;
    for(int i=0;i<60;++i){ g.update(dt,c); maxY=std::max(maxY,e.body.pos.y); }
    CHECK(e.health<=100-40*0.6f+1e-3f && e.health>50, "de frente el escudo solo para el 40%%: vida %.0f", e.health);
    CHECK(maxY>0.5f, "lo levanta del suelo (%.2f m)", maxY);
    Game h=fresh(); h.hero_.item=Item::Hammer; Controls d; d.use=true; h.update(dt,d); d.use=false; h.update(dt,d);
    CHECK(h.hero_.action()==Action::Smash, "el martillo usa su golpe (sin combo de espada)"); }
  { // Inventario: 8 casillas
    Game g=fresh(); Controls c; c.selectSlot=7; g.update(dt,c);
    CHECK(g.hero_.item==Item::Hammer && g.ammo(Item::Shotgun)==16 && g.ammo(Item::Hammer)==-1, "tecla 8 = martillo; municion por objeto"); }
  std::printf("\n%s (%d fallos)\n", fails?"HAY FALLOS":"TODO OK", fails); return fails;
}
