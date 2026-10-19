#include "Aurora.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "src/esp_lcd_sh8601.h"
#include "Scene.h"
#include "Audio.h"
#include "ControlPage.h"

static Scene scene;
static WebServer server(80);
static esp_lcd_panel_handle_t panel=nullptr;
static esp_lcd_panel_io_handle_t panelIo=nullptr;
static SemaphoreHandle_t flushDone=nullptr;
static uint16_t* dmaBuf=nullptr;
static constexpr int ROWS=24;
static bool touchReady=false,showcase=false,reactEnabled=true,apReady=false;
static uint32_t lastBody=0,bodyLines=0,uartBytes=0,touchCount=0,soundReactions=0;
static String bodyFace="idle",bodyMode="loop";
static Mood bodyMood=CURIOUS,manualMood=CURIOUS;
// Body moves arrive as "M:<command>" lines. A move stays on screen for at
// least MOVE_HOLD_MS, so one-shot poses like rest or stand are still seen.
static Move bodyMove=NO_MOVE,manualMove=NO_MOVE;
static uint32_t bodyMoveAt=0,moveClearAt=0,lastMoveLine=0;
static constexpr uint32_t MOVE_HOLD_MS=3000;
static uint32_t manualUntil=0,nextFrame=0,lastAudioReaction=0,showStart=0;
static float noiseFloor=.002f,energy=0,framesPerSecond=0;
static uint32_t frames=0,fpsAt=0;
// Touch can drive the body: double-tap sends the next trick, hold sends stop,
// and any touch while the body moves sends stop. Only one-shot poses are used,
// so the robot never walks off from a touch. Switch off on the web page/USB.
static bool bodyControl=true;
static const char* const TRICKS[]={"wave","bow","dance","cute","point","shrug"};
static uint8_t nextTrick=0;
static uint32_t commandSentAt=0,commandsSent=0;

static const sh8601_lcd_init_cmd_t initCmds[]={
  {0xFE,(uint8_t[]){0},1,0},{0xC4,(uint8_t[]){0x80},1,0},
  {0x3A,(uint8_t[]){0x55},1,0},{0x35,(uint8_t[]){0},1,0},
  {0x53,(uint8_t[]){0x20},1,0},{0x51,(uint8_t[]){0xFF},1,0},
  {0x36,(uint8_t[]){0xC0},1,0},{0x63,(uint8_t[]){0xFF},1,0},
  {0x2A,(uint8_t[]){0,6,1,0xD7},4,0},{0x2B,(uint8_t[]){0,0,1,0xD1},4,0},
  {0x11,(uint8_t[]){0},0,150}
};
static bool IRAM_ATTR flushed(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*){
  BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(flushDone,&wake);return wake==pdTRUE;
}
static void brightness(int percent){uint8_t b=constrain(percent,0,100)*255/100;ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panelIo,(0x02<<24)|(0x51<<8),&b,1));}
static void pushFrame(){
  for(int y=0;y<466;y+=ROWS){
    int h=std::min(ROWS,466-y),n=466*h;const uint16_t* src=scene.g.p+y*466;
    for(int i=0;i<n;i++)dmaBuf[i]=(src[i]<<8)|(src[i]>>8);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,y,466,y+h,dmaBuf));
    if(xSemaphoreTake(flushDone,pdMS_TO_TICKS(1000))!=pdTRUE){Serial.println("Display transfer timeout");esp_restart();}
  }
}
static void initDisplay(){
  delay(2000);flushDone=xSemaphoreCreateBinary();
  scene.g.p=(uint16_t*)heap_caps_malloc(466*466*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  scene.background=(uint16_t*)heap_caps_malloc(466*466*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  scene.base=(uint16_t*)heap_caps_malloc(466*466*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  dmaBuf=(uint16_t*)heap_caps_malloc(466*ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  if(!scene.g.p||!scene.background||!scene.base||!dmaBuf||!flushDone){Serial.println("ERROR: enable OPI PSRAM in board settings");while(true)delay(1000);}
  spi_bus_config_t bus={};bus.sclk_io_num=14;bus.data0_io_num=9;bus.data1_io_num=10;bus.data2_io_num=11;bus.data3_io_num=12;bus.max_transfer_sz=466*ROWS*2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t io={};io.cs_gpio_num=15;io.dc_gpio_num=-1;io.spi_mode=0;io.pclk_hz=40000000;io.trans_queue_depth=2;io.on_color_trans_done=flushed;io.lcd_cmd_bits=32;io.lcd_param_bits=8;io.flags.quad_mode=true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST,&io,&panelIo));
  sh8601_vendor_config_t vendor={};vendor.init_cmds=initCmds;vendor.init_cmds_size=sizeof(initCmds)/sizeof(initCmds[0]);vendor.flags.use_qspi_interface=1;
  esp_lcd_panel_dev_config_t dev={};dev.reset_gpio_num=13;dev.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;dev.bits_per_pixel=16;dev.vendor_config=&vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(panelIo,&dev,&panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,8,0));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,false));
  scene.prepare();scene.draw(1);pushFrame(); // fully painted before display-on
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));brightness(65);
}
static Mood faceMood(String s){
  s.toLowerCase();s.replace("talk_","");
  if(s=="rest"||s=="sleepy"||s=="dead")return SLEEPY;
  if(s=="party"||s=="dance"||s=="excited"||s=="freaky"||s=="worm")return PARTY;
  if(s=="happy"||s=="wave"||s=="walk"||s=="swim"||s=="shake")return HAPPY;
  if(s=="love"||s=="cute"||s=="bow")return LOVE;
  if(s=="surprised")return SURPRISED;
  if(s=="angry"||s=="brave"||s=="pushup"||s=="crab")return BRAVE;
  if(s=="sad")return SAD;
  if(s=="thinking"||s=="confused"||s=="point"||s=="shrug")return THINKING;
  return CURIOUS;
}
static bool isFuture(uint32_t deadline){return (int32_t)(deadline-millis())>0;}
static void preview(Mood m,uint32_t duration=10000){manualMood=m;manualMove=NO_MOVE;manualUntil=millis()+duration;}
static void previewMove(Move m,uint32_t duration=10000){manualMove=m;manualUntil=millis()+duration;}
static void startBodyMove(Move m){
  moveClearAt=0;
  if(m==bodyMove)return; // keepalive repeat
  bodyMove=m;bodyMoveAt=millis();moveSound(m);
  Serial.printf("Body move: %s\n",moveLabel(m));
}
static void endBodyMove(){
  if(bodyMove!=NO_MOVE&&!moveClearAt){uint32_t held=bodyMoveAt+MOVE_HOLD_MS;moveClearAt=isFuture(held)?held:millis();}
}
static bool bodyLinked(){return lastBody&&millis()-lastBody<8000;}
static bool canDriveBody(){return bodyControl&&bodyLinked();}
// True while the body runs a move, and briefly after we sent one (before its
// "M:" reply arrives), so a quick second touch still counts as "stop".
static bool bodyBusy(){
  return bodyLinked()&&((bodyMove!=NO_MOVE&&!moveClearAt)||(commandSentAt&&millis()-commandSentAt<1500));
}
static void sendBody(const char* cmd){
  Serial1.printf("C:%s\n",cmd);commandsSent++;
  commandSentAt=strcmp(cmd,"stop")?millis():0;
  Serial.printf("To body: C:%s\n",cmd);
}
static String status(){
  char b[700];snprintf(b,sizeof(b),"{\"mood\":\"%s\",\"move\":\"%s\",\"character\":%d,\"fps\":%.1f,\"energy\":%.3f,\"rawMic\":%.5f,\"mic\":%s,\"speaker\":%s,\"touch\":%s,\"link\":%s,\"bodyLines\":%lu,\"uartBytes\":%lu,\"touches\":%lu,\"reactions\":%lu,\"audioBlocks\":%lu,\"audioErrors\":%lu,\"sound\":%s,\"react\":%s,\"show\":%s,\"bodyControl\":%s,\"commandsSent\":%lu,\"heap\":%u}",
    moodName(scene.mood),scene.move!=NO_MOVE?moveLabel(scene.move):"",scene.character,framesPerSecond,energy,micLevel.load(),micReady?"true":"false",speakerReady?"true":"false",touchReady?"true":"false",bodyLinked()?"true":"false",(unsigned long)bodyLines,(unsigned long)uartBytes,(unsigned long)touchCount,(unsigned long)soundReactions,(unsigned long)audioBlocks.load(),(unsigned long)audioErrors.load(),soundEnabled?"true":"false",reactEnabled?"true":"false",showcase?"true":"false",bodyControl?"true":"false",(unsigned long)commandsSent,ESP.getFreeHeap());
  return String(b);
}
static void bodyMessage(String line,bool fromUsb){
  line.trim();
  if(line=="?"&&fromUsb){Serial.println(status());return;}
  if(line=="SNAP"&&fromUsb){Serial.printf("SNAP %u\n",466*466*2);Serial.write((const uint8_t*)scene.g.p,466*466*2);Serial.flush();return;}
  if(line=="SHOW"&&fromUsb){showcase=true;showStart=millis();return;}
  if(line=="FOLLOW"&&fromUsb){showcase=false;manualUntil=0;return;}
  if(line=="CHIRP"&&fromUsb){chirp(1);return;}
  if((line=="BODY:1"||line=="BODY:0")&&fromUsb){bodyControl=line=="BODY:1";Serial.printf("Body control %s\n",bodyControl?"on":"off");return;}
  if(line.startsWith("PET:")&&fromUsb){scene.character=constrain(line.substring(4).toInt(),0,2);return;}
  if(line.startsWith("M:")){
    String name=line.substring(2);name.trim();name.toLowerCase();Move m=moveFromName(name.c_str());
    if(fromUsb){if(m!=NO_MOVE){showcase=false;previewMove(m);moveSound(m);}return;}
    lastBody=lastMoveLine=millis();bodyLines++;
    if(m!=NO_MOVE)startBodyMove(m);else endBodyMove(); // "M:idle" when the body stops
    return;
  }
  if(!line.startsWith("F:"))return;
  int comma=line.indexOf(',');String name=line.substring(2,comma<0?line.length():comma);name.trim();name.toLowerCase();
  String mode=comma<0?"loop":line.substring(comma+1);mode.trim();mode.toLowerCase();
  if(!name.length()||(mode!="loop"&&mode!="once"&&mode!="boomerang"))return;
  if(fromUsb){Mood m=faceMood(name);showcase=false;preview(m);moodSound(m);return;}
  lastBody=millis();bodyLines++;
  if(name!=bodyFace||mode!=bodyMode){
    Mood previous=bodyMood;
    bodyFace=name;bodyMode=mode;bodyMood=faceMood(name);
    // A body without "M:" lines (older firmware): read the move from the face.
    // Its four walking commands all show "walk", so they look the same.
    bool legacyBody=!lastMoveLine||millis()-lastMoveLine>10000;
    if(legacyBody){Move m=moveFromName(name.c_str());if(m!=NO_MOVE)startBodyMove(m);else endBodyMove();}
    // Faces that are not moves (happy, sad, ... sent with "fc") get their mood
    // sound. Curious is the resting face, so it stays silent.
    if(bodyMove==NO_MOVE&&bodyMood!=previous&&bodyMood!=CURIOUS)moodSound(bodyMood);
    // Repeated body keepalives do not restart local animations or pet reactions.
    Serial.printf("Body face: %s (%s)\n",name.c_str(),mode.c_str());
  }
}
struct Reader{
  Stream& s;bool usb;char b[80];uint8_t n=0;bool overflow=false;
  void poll(){int budget=256;while(s.available()&&budget--){char c=s.read();if(!usb)uartBytes++;if(c=='\n'||c=='\r'){if(n&&!overflow){b[n]=0;bodyMessage(String(b),usb);}n=0;overflow=false;}else if(n<sizeof(b)-1)b[n++]=c;else overflow=true;}}
};
static Reader usb{Serial,true},bodyReader{Serial1,false};
static void initWeb(){
  WiFi.mode(WIFI_AP);WiFi.setSleep(false);apReady=WiFi.softAP("Sesame-Aurora","Sesame2026",6,false,2);
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",CONTROL_PAGE);});
  server.on("/status",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",status());});
  server.on("/api",HTTP_POST,[]{
    String a=server.arg("action"),v=server.arg("value"),reply="Done";
    if(a=="character"&&(v=="0"||v=="1"||v=="2")){scene.character=v.toInt();chirp(1);reply="New companion, same robot.";}
    else if(a=="mood"){showcase=false;Mood m=faceMood(v);preview(m);moodSound(m);reply="Mood set for 10 seconds.";}
    else if(a=="move"){Move m=moveFromName(v.c_str());if(m==NO_MOVE){server.send(400,"text/plain","Unknown move");return;}
      showcase=false;previewMove(m);moveSound(m);reply="Showing that move for 10 seconds. The body does not move.";}
    else if(a=="show"){showcase=true;showStart=millis();reply="Showcase is playing. Body motion is unchanged.";}
    else if(a=="follow"){showcase=false;manualUntil=0;reply="Following the body again.";}
    else if(a=="sound"){soundEnabled.store(v=="1");reply=soundEnabled?"Sound on":"Sound muted";}
    else if(a=="react"){reactEnabled=v=="1";reply=reactEnabled?"Sound reactions on":"Sound reactions paused";}
    else if(a=="body"){bodyControl=v=="1";reply=bodyControl?"Double-tap the head for a trick. Touch it to stop.":"Touch reacts on the head only.";}
    else if(a=="chirp"){chirp(1);preview(HAPPY,2000);reply="Hello!";}
    else{server.send(400,"text/plain","Unknown control");return;}
    server.send(200,"text/plain",reply);
  });
  server.onNotFound([]{server.send(404,"text/plain","Open http://192.168.4.1/");});server.begin();
  Serial.printf("WiFi %s: Sesame-Aurora / Sesame2026 / %s\n",apReady?"ready":"failed",WiFi.softAPIP().toString().c_str());
}
static void interactions(){
  static uint32_t lastTouchPoll=0,pressedAt=0,lastSeen=0,lastTapAt=0;static bool down=false,longDone=false,stopTouch=false;
  static int firstX=233,firstY=233,latestX=233,latestY=233;
  uint32_t now=millis();
  if(now-lastTouchPoll>=35){
    lastTouchPoll=now;int x,y;bool pressed=touchReady&&readTouch(x,y);
    if(pressed){
      lastSeen=now;latestX=x;latestY=y;
      if(!down){
        down=true;pressedAt=now;firstX=x;firstY=y;longDone=false;touchCount++;
        // A touch while the body moves is an emergency stop and nothing else.
        stopTouch=bodyControl&&bodyBusy();
        if(stopTouch){sendBody("stop");lastTapAt=0;preview(CURIOUS,2500);chirp(1);}
      }
      scene.gazeX+=(constrain((x-233)/14.f,-10.f,10.f)-scene.gazeX)*.25f;
      scene.gazeY+=(constrain((y-233)/22.f,-6.f,6.f)-scene.gazeY)*.25f;
      // Hold still: calm down. Stops the body and ends a showcase.
      if(!stopTouch&&!longDone&&now-pressedAt>900&&abs(x-firstX)<45&&abs(y-firstY)<45){
        longDone=true;lastTapAt=0;showcase=false;preview(CURIOUS,2500);chirp(3);
        if(canDriveBody())sendBody("stop");
      }
    }else if(down&&now-lastSeen>100){
      down=false;
      if(!longDone&&!stopTouch){
        if(abs(latestX-firstX)>65||abs(latestY-firstY)>65){scene.character=(scene.character+1)%3;chirp(1);lastTapAt=0;}
        else if(lastTapAt&&now-lastTapAt<500){
          // Double-tap: next trick. The first tap already showed love.
          lastTapAt=0;const char* trick=TRICKS[nextTrick];nextTrick=(nextTrick+1)%(sizeof(TRICKS)/sizeof(TRICKS[0]));
          showcase=false;
          // The body's "M:" reply brings the move's expression and sound.
          if(canDriveBody()){manualUntil=0;sendBody(trick);}
          else{Move m=moveFromName(trick);previewMove(m,3000);moveSound(m);}
        }
        else{lastTapAt=now;preview(LOVE,4500);chirp(2);}
      }
    }
  }
  if(!down){float gx=sinf(now*.00047f)*7,gy=sinf(now*.00031f)*3;scene.gazeX+=(gx-scene.gazeX)*.06f;scene.gazeY+=(gy-scene.gazeY)*.06f;}
  float level=micLevel.load();
  if(!isFuture(soundUntil.load()))noiseFloor=noiseFloor*.997f+fminf(level,.05f)*.003f;
  // The mic hears our own speaker; ignore it while a sound plays so the
  // mouth keeps the expression's shape instead of popping open.
  float target=isFuture(soundUntil.load())?0:constrain((level-noiseFloor*.8f)*18.f,0.f,1.f);
  energy=energy*.75f+target*.25f;scene.energy=reactEnabled?energy:0;
  // Servo noise is not a surprise: no mic reactions while the body moves.
  if(reactEnabled&&micReady&&now>8000&&!isFuture(soundUntil.load())&&!bodyBusy()&&now-lastAudioReaction>4500&&level>fmaxf(.025f,noiseFloor*3.5f)){
    lastAudioReaction=now;soundReactions++;preview(SURPRISED,1800);moodSound(SURPRISED);
  }
  static bool bootHeld=false;
  bool boot=digitalRead(0)==LOW;
  if(boot&&!bootHeld){showcase=!showcase;showStart=now;manualUntil=0;chirp(1);}
  bootHeld=boot;
}

void auroraSetup(){
  Serial.begin(115200);Serial1.setRxBufferSize(512);Serial1.begin(115200,SERIAL_8N1,44,43);gpio_pullup_en(GPIO_NUM_44);pinMode(0,INPUT_PULLUP);
  Serial.println("Sesame Aurora v1 starting");
  esp_reset_reason_t reason=esp_reset_reason();initDisplay();
  // Preserve the known working board-specific cold-start reset workaround.
  if(reason==ESP_RST_POWERON||reason==ESP_RST_BROWNOUT){while(millis()<3000)delay(10);Serial.println("Cold start: one warm restart");Serial.flush();esp_restart();}
  touchReady=initPeripherals();initWeb();fpsAt=millis();chirp(1);
  Serial.println("Aurora ready. Tap=love, double-tap=body trick, swipe=pet, hold=calm/stop, BOOT=showcase. USB: ?, SNAP, SHOW, FOLLOW, BODY:0|1, PET:0..2, CHIRP, F:happy, M:crab");
}
void auroraLoop(){
  bodyReader.poll();usb.poll();server.handleClient();
  uint32_t now=millis();
  if((int32_t)(now-nextFrame)>=0){
    nextFrame=now+50;interactions();
    if(moveClearAt&&!isFuture(moveClearAt)){bodyMove=NO_MOVE;moveClearAt=0;}
    static int shownStep=-1;
    if(showcase){
      // All 19 body moves, then the 9 moods, 4.5 s each, each with its sound.
      int step=((now-showStart)/4500)%(MOVE_COUNT+MOOD_COUNT);
      if(step<MOVE_COUNT){scene.move=(Move)step;}else{scene.move=NO_MOVE;scene.mood=(Mood)(step-MOVE_COUNT);}
      if(step!=shownStep){shownStep=step;if(scene.move!=NO_MOVE)moveSound(scene.move);else moodSound(scene.mood);}
    }else{
      shownStep=-1;
      bool manual=isFuture(manualUntil);
      scene.mood=manual?manualMood:bodyMood;scene.move=manual?manualMove:bodyMove;
    }
    scene.linked=lastBody&&now-lastBody<8000;
    scene.draw(now/1000.f);pushFrame();frames++;
    if(now-fpsAt>=2000){framesPerSecond=frames*1000.f/(now-fpsAt);frames=0;fpsAt=now;}
  }
  static uint32_t brightAt=0;if(now-brightAt>2000){brightAt=now;brightness(65);}
  delay(1);
}
