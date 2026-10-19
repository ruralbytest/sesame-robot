#include "Audio.h"
#include <driver/i2s_tdm.h>
#include <math.h>
#include "src/esp_codec_dev/include/esp_codec_dev.h"
#include "src/esp_codec_dev/include/esp_codec_dev_defaults.h"

i2c_master_bus_handle_t sharedBus=nullptr;
static i2c_master_dev_handle_t touch=nullptr;
static esp_codec_dev_handle_t speaker=nullptr,microphone=nullptr;
static QueueHandle_t notes=nullptr;
std::atomic<float> micLevel{0};
std::atomic<uint32_t> audioBlocks{0},audioErrors{0},soundUntil{0};
std::atomic<bool> soundEnabled{true};
bool micReady=false,speakerReady=false;

void chirp(int kind){if(notes&&speakerReady&&soundEnabled.load())xQueueOverwrite(notes,&kind);}

static void audioTask(void*) {
  int16_t in[512],out[512]; // 256 stereo samples: 16 ms at 16 kHz
  int sound=0,pos=0,total=0;float phase=0;
  for(;;){
    int request=0;
    if(xQueueReceive(notes,&request,0)==pdTRUE){sound=request;pos=0;total=(sound==3?11200:5600);soundUntil.store(millis()+total/16+350);}
    if(micReady){
      if(esp_codec_dev_read(microphone,in,sizeof(in))==ESP_OK){
        // Remove DC separately for both channels; use the louder microphone.
        float mean[2]={0,0},sum[2]={0,0};
        for(int i=0;i<512;i++)mean[i&1]+=in[i]/256.f;
        for(int i=0;i<512;i++)sum[i&1]+=fabsf(in[i]-mean[i&1]);
        float level=fmaxf(sum[0],sum[1])/256.f/32768.f;
        micLevel.store(level);audioBlocks.fetch_add(1);
      }else{audioErrors.fetch_add(1);vTaskDelay(pdMS_TO_TICKS(10));}
    }
    for(int i=0;i<256;i++){
      float sample=0;
      if(pos<total && soundEnabled.load()){
        float t=pos/16000.f,u=pos/(float)total;
        float f=sound==3?180+65*sinf(t*40):((sound==2?520:700)+380*sinf(u*3.14159f));
        float envelope=fminf(1.f,u*18)*fminf(1.f,(1-u)*12);
        // Quiet, original synthesized pet calls with a soft envelope.
        phase+=6.2831853f*f/16000; if(phase>6.2831853f)phase-=6.2831853f;
        sample=(sinf(phase)+.18f*sinf(phase*2))*envelope*8000;
        pos++;
      }
      out[i*2]=out[i*2+1]=(int16_t)sample;
    }
    if(speakerReady){if(esp_codec_dev_write(speaker,out,sizeof(out))!=ESP_OK){audioErrors.fetch_add(1);vTaskDelay(pdMS_TO_TICKS(10));}}
    if(!micReady&&!speakerReady)vTaskDelay(pdMS_TO_TICKS(30));
  }
}

bool readTouch(int &x,int &y){
  if(!touch)return false;uint8_t reg=2,b[5]={};
  if(i2c_master_transmit_receive(touch,&reg,1,b,5,20)!=ESP_OK)return false;
  if(!(b[0]&15)||(b[1]>>6)==1)return false;
  // Panel MADCTL rotates 180 degrees; match its visible coordinates.
  x=465-(((b[1]&15)<<8)|b[2]);y=465-(((b[3]&15)<<8)|b[4]);
  return x>=0&&x<466&&y>=0&&y<466;
}

bool initPeripherals(){
  pinMode(18,OUTPUT);digitalWrite(18,HIGH); // codec LDO (official BSP)
  pinMode(46,OUTPUT);digitalWrite(46,LOW); // PA off until codec is ready
  i2c_master_bus_config_t bus={};bus.i2c_port=I2C_NUM_0;bus.sda_io_num=GPIO_NUM_47;bus.scl_io_num=GPIO_NUM_48;
  bus.clk_source=I2C_CLK_SRC_DEFAULT;bus.glitch_ignore_cnt=7;bus.flags.enable_internal_pullup=true;
  if(i2c_new_master_bus(&bus,&sharedBus)!=ESP_OK)return false;
  pinMode(16,OUTPUT);digitalWrite(16,HIGH);delay(20);digitalWrite(16,LOW);delay(20);digitalWrite(16,HIGH);delay(150);
  if(i2c_master_probe(sharedBus,0x15,30)==ESP_OK){
    i2c_device_config_t t={};t.dev_addr_length=I2C_ADDR_BIT_LEN_7;t.device_address=0x15;t.scl_speed_hz=400000;
    i2c_master_bus_add_device(sharedBus,&t,&touch);
  }
  bool hasSpk=i2c_master_probe(sharedBus,0x18,30)==ESP_OK;
  bool hasMic=i2c_master_probe(sharedBus,0x40,30)==ESP_OK;
  Serial.printf("I2C touch=%d speaker=%d microphone=%d\n",touch!=nullptr,hasSpk,hasMic);
  if(!hasSpk&&!hasMic)return touch!=nullptr;
  i2s_chan_handle_t tx=nullptr,rx=nullptr;
  i2s_chan_config_t ch=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);ch.auto_clear=true;
  if(i2s_new_channel(&ch,&tx,&rx)!=ESP_OK)return touch!=nullptr;
  i2s_tdm_config_t cfg={};cfg.clk_cfg=I2S_TDM_CLK_DEFAULT_CONFIG(16000);
  cfg.slot_cfg=I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO,(i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0|I2S_TDM_SLOT1));cfg.slot_cfg.total_slot=2;
  cfg.gpio_cfg.mclk=GPIO_NUM_38;cfg.gpio_cfg.bclk=GPIO_NUM_39;cfg.gpio_cfg.ws=GPIO_NUM_40;cfg.gpio_cfg.dout=GPIO_NUM_41;cfg.gpio_cfg.din=GPIO_NUM_42;
  if(i2s_channel_init_tdm_mode(tx,&cfg)!=ESP_OK||i2s_channel_init_tdm_mode(rx,&cfg)!=ESP_OK)return touch!=nullptr;
  i2s_channel_enable(tx);i2s_channel_enable(rx);
  audio_codec_i2s_cfg_t dataCfg={};dataCfg.port=0;dataCfg.tx_handle=tx;dataCfg.rx_handle=rx;
  auto data=audio_codec_new_i2s_data(&dataCfg);
  auto gpio=audio_codec_new_gpio();
  audio_codec_i2c_cfg_t ctl={};ctl.port=0;ctl.bus_handle=sharedBus;
  esp_codec_dev_sample_info_t fs={};fs.sample_rate=16000;fs.channel=2;fs.bits_per_sample=16;
  if(hasSpk){
    ctl.addr=ES8311_CODEC_DEFAULT_ADDR;
    es8311_codec_cfg_t c={};c.ctrl_if=audio_codec_new_i2c_ctrl(&ctl);c.gpio_if=gpio;c.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC;c.pa_pin=46;c.use_mclk=true;c.hw_gain.pa_gain=6;
    auto codec=es8311_codec_new(&c);
    if(codec&&data){esp_codec_dev_cfg_t d={};d.dev_type=ESP_CODEC_DEV_TYPE_OUT;d.codec_if=codec;d.data_if=data;speaker=esp_codec_dev_new(&d);}
    speakerReady=speaker&&esp_codec_dev_open(speaker,&fs)==ESP_OK;
    if(speakerReady){esp_codec_dev_set_out_vol(speaker,80);esp_codec_dev_set_out_mute(speaker,false);}
  }
  if(hasMic){
    ctl.addr=ES7210_CODEC_DEFAULT_ADDR;
    es7210_codec_cfg_t c={};c.ctrl_if=audio_codec_new_i2c_ctrl(&ctl);c.mic_selected=ES7120_SEL_MIC1|ES7120_SEL_MIC2|ES7120_SEL_MIC3|ES7120_SEL_MIC4;
    auto codec=es7210_codec_new(&c);
    if(codec&&data){esp_codec_dev_cfg_t d={};d.dev_type=ESP_CODEC_DEV_TYPE_IN;d.codec_if=codec;d.data_if=data;microphone=esp_codec_dev_new(&d);}
    micReady=microphone&&esp_codec_dev_open(microphone,&fs)==ESP_OK;
    if(micReady)esp_codec_dev_set_in_gain(microphone,24);
  }
  notes=xQueueCreate(1,sizeof(int));
  if(notes&&(micReady||speakerReady))xTaskCreatePinnedToCore(audioTask,"pet_audio",6144,nullptr,2,nullptr,0);
  Serial.printf("Audio mic=%d speaker=%d\n",micReady,speakerReady);
  return touch!=nullptr;
}
