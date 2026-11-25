#include <HTTPClient.h>
#include <esp_timer.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <FS.h>
#include "esp_camera.h"

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#include "deployment_config.h"
const uint16_t HTTPS_PORT=443;
const uint32_t FRAME_INTERVAL_MS=1000;
const uint16_t MAX_LOCAL_CLIPS=100;
const uint8_t MAX_SD_PERCENT=80;
const uint8_t FLASH_PIN=4;
uint32_t flashDuration=120;
bool flashEnabled=false, flashForClips=false;
String lowLightMode="auto", exposureMode="auto"; int gainCeiling=16, exposureValue=800, sensorGain=4;
esp_timer_handle_t flashSafetyTimer=nullptr;
uint16_t imageIndex=0, imageTotal=1;

WebServer server(80);
Preferences prefs;
String ssid, pass;
bool staConnected=false, sdReady=false;
uint32_t sdMountAttempts=0, sdMountedKHz=0; bool sdProbeOk=false; String sdError="Not mounted", sdProbePath="";
bool clipRecording=false, clipUploading=false;
uint32_t configuredSeconds=10, clipStartedAt=0, nextFrameAt=0, nextStatusAt=0;
String configuredSize="VGA"; uint8_t configuredQuality=12; uint8_t burstCount=5; uint32_t burstInterval=500, clipInterval=1000;
uint16_t clipFrames=0, clipUploaded=0, clipTotal=0, clipCount=0;
uint32_t nextCommandPoll=0,nextConfigPoll=3000;
String activeClip="", lastClip="", lastResult="Ready", operationId="", operationKind="clip";

framesize_t configuredFrameSize(){if(configuredSize=="QQVGA")return FRAMESIZE_QQVGA;if(configuredSize=="QVGA")return FRAMESIZE_QVGA;if(configuredSize=="SVGA")return FRAMESIZE_SVGA;return FRAMESIZE_VGA;}
// JSON scalar extraction: stop at the value, never scan unrelated booleans.
String jsonScalar(const String &body,const char *key){
 String needle=String(char(34))+key+char(34);int p=body.indexOf(needle);
 while(p>=0){int value=body.indexOf(':',p)+1;while(value<body.length() && isspace(body[value]))value++;
 if(body[value]!='{' && body[value]!='['){p=value;break;}p=body.indexOf(needle,value);}
 if(p<0 || p>=body.length())return "";
 if(body[p]==34){int e=body.indexOf(char(34),p+1);return body.substring(p+1,e);}
 int e=p;while(e<body.length() && body[e]!=',' && body[e]!='}' && !isspace(body[e]))e++;
 return body.substring(p,e);
}
void flashOff(){digitalWrite(FLASH_PIN,LOW);if(flashSafetyTimer)esp_timer_stop(flashSafetyTimer);}
void flashSafetyOff(void*){digitalWrite(FLASH_PIN,LOW);}
// OV2640 hardware readback: bank 1 SENSOR, bank 0 DSP.
// The driver status cache alone is NOT proof of the hardware exposure.
void logSensorRegisters(const char *stage){
 sensor_t *sensor=esp_camera_sensor_get();if(!sensor || sensor->id.PID!=0x26)return;
 int pid=sensor->get_reg(sensor,0x10A,0xFF),com8=sensor->get_reg(sensor,0x113,0xFF);
 int r04=sensor->get_reg(sensor,0x104,0xFF),aec=sensor->get_reg(sensor,0x110,0xFF),r45=sensor->get_reg(sensor,0x145,0xFF);
 int gain=sensor->get_reg(sensor,0x100,0xFF),clk=sensor->get_reg(sensor,0x111,0xFF);
 int ctrl0=sensor->get_reg(sensor,0x0C2,0xFF),pclk=sensor->get_reg(sensor,0x0D3,0xFF);
 int actual=((r45&0x3F)<<10)|((aec&0xFF)<<2)|(r04&3);
 Serial.printf("SENSOR_DIAG {\"stage\":\"%s\",\"ms\":%lu,\"pid\":%d,\"requestedExposure\":%d,\"requestedMode\":\"%s\",\"requestedGainIndex\":%d,\"actualExposure\":%d,\"aecEnabled\":%d,\"agcEnabled\":%d,\"aec2Enabled\":%d,\"rawCOM8\":%d,\"rawREG04\":%d,\"rawAEC\":%d,\"rawREG45\":%d,\"rawGAIN\":%d,\"rawCTRL0\":%d,\"rawCLKRC\":%d,\"rawPCLK\":%d,\"xclkHz\":%lu}\n",stage,millis(),pid,exposureValue,exposureMode.c_str(),sensorGain,actual,(com8&1)!=0,(com8&4)!=0,(ctrl0&0x40)==0,com8,r04,aec,r45,gain,ctrl0,clk,pclk,(unsigned long)sensor->xclk_freq_hz);
}
void applyCameraConfig(){
 sensor_t *sensor=esp_camera_sensor_get();if(!sensor)return;
 framesize_t size=configuredFrameSize();if(sensor->status.framesize!=size)sensor->set_framesize(sensor,size);
 sensor->set_quality(sensor,configuredQuality);
 sensor->set_exposure_ctrl(sensor,1);sensor->set_gain_ctrl(sensor,1);
 sensor->set_gainceiling(sensor,gainCeiling>=32?GAINCEILING_32X:gainCeiling>=16?GAINCEILING_16X:gainCeiling>=8?GAINCEILING_8X:gainCeiling>=4?GAINCEILING_4X:GAINCEILING_2X);
 sensor->set_aec2(sensor,1);sensor->set_ae_level(sensor,lowLightMode=="night"?1:0);
 sensor->set_whitebal(sensor,1);sensor->set_awb_gain(sensor,1);
 if(exposureMode=="manual"){
 sensor->set_aec2(sensor,0);sensor->set_exposure_ctrl(sensor,0);sensor->set_gain_ctrl(sensor,0);
 sensor->set_aec_value(sensor,exposureValue);sensor->set_agc_gain(sensor,sensorGain);
 }
}
void applyConfigBody(const String &body){
 String v=jsonScalar(body,"frameSize");if(v.length())configuredSize=v;
 v=jsonScalar(body,"jpegQuality");if(v.length())configuredQuality=constrain(v.toInt(),10,30);
 v=jsonScalar(body,"burstCount");if(v.length())burstCount=constrain(v.toInt(),1,10);
 v=jsonScalar(body,"burstIntervalMs");if(v.length())burstInterval=constrain(v.toInt(),200,5000);
 v=jsonScalar(body,"clipIntervalMs");if(v.length())clipInterval=constrain(v.toInt(),200,5000);
 v=jsonScalar(body,"clipDurationSeconds");if(v.length())configuredSeconds=constrain(v.toInt(),10,60);
 v=jsonScalar(body,"flashEnabled");if(v.length())flashEnabled=v=="true";
 v=jsonScalar(body,"flashForClips");if(v.length())flashForClips=v=="true";
 v=jsonScalar(body,"flashDurationMs");if(v.length())flashDuration=constrain(v.toInt(),50,500);
 v=jsonScalar(body,"lowLightMode");if(v.length())lowLightMode=v;
 v=jsonScalar(body,"gainCeiling");if(v.length())gainCeiling=constrain(v.toInt(),2,32);
 v=jsonScalar(body,"exposureMode");if(v.length())exposureMode=v;
 v=jsonScalar(body,"exposureValue");if(v.length())exposureValue=constrain(v.toInt(),100,1200);
 v=jsonScalar(body,"sensorGain");if(v.length())sensorGain=constrain(v.toInt(),0,30);
 applyCameraConfig();
 logSensorRegisters("configured");
 Serial.printf("CONFIG ms=%lu size=%s gain=%d mode=%s flash=%d clipFlash=%d\n",millis(),configuredSize.c_str(),gainCeiling,lowLightMode.c_str(),flashEnabled,flashForClips);
}
void beginHttp(HTTPClient &http,WiFiClientSecure &client,const String &path){
 client.setInsecure();client.setHandshakeTimeout(4);
 http.begin(client,String("https://")+HOST+path);http.setTimeout(4000);http.setConnectTimeout(4000);http.setReuse(false);http.useHTTP10(true);
}
camera_fb_t *captureFreshFrame(){
 bool lit=flashEnabled && (operationKind!="clip" || flashForClips);
 logSensorRegisters("before-capture");
 uint32_t started=millis();
 if(lit){pinMode(FLASH_PIN,OUTPUT);digitalWrite(FLASH_PIN,HIGH);if(flashSafetyTimer)esp_timer_start_once(flashSafetyTimer,1500000);Serial.printf("FLASH_ON ms=%lu\n",millis());delay(flashDuration);}
 // With one framebuffer a queued frame can predate the LED. Return it,
 // then drain complete exposures while illuminated for AEC/AWB convergence.
 for(int i=0;i<4;i++){camera_fb_t *discard=esp_camera_fb_get();if(!discard){flashOff();return nullptr;}esp_camera_fb_return(discard);}
 camera_fb_t *fb=esp_camera_fb_get();
 flashOff();
 logSensorRegisters("after-capture");
 sensor_t *sensor=esp_camera_sensor_get();
 Serial.printf("FRAME_READY ms=%lu capture_ms=%lu flash=%d bytes=%u aec=%d gain=%d\n",millis(),millis()-started,lit,fb?(unsigned)fb->len:0,sensor?sensor->status.aec_value:0,sensor?sensor->status.agc_gain:0);
 return fb;
}

bool initCamera(){
 camera_config_t c={}; c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
 c.pin_d0=Y2_GPIO_NUM;c.pin_d1=Y3_GPIO_NUM;c.pin_d2=Y4_GPIO_NUM;c.pin_d3=Y5_GPIO_NUM;c.pin_d4=Y6_GPIO_NUM;c.pin_d5=Y7_GPIO_NUM;c.pin_d6=Y8_GPIO_NUM;c.pin_d7=Y9_GPIO_NUM;
 c.pin_xclk=XCLK_GPIO_NUM;c.pin_pclk=PCLK_GPIO_NUM;c.pin_vsync=VSYNC_GPIO_NUM;c.pin_href=HREF_GPIO_NUM;c.pin_sccb_sda=SIOD_GPIO_NUM;c.pin_sccb_scl=SIOC_GPIO_NUM;c.pin_pwdn=PWDN_GPIO_NUM;c.pin_reset=RESET_GPIO_NUM;
 c.xclk_freq_hz=10000000;c.pixel_format=PIXFORMAT_JPEG;c.frame_size=FRAMESIZE_QVGA;c.jpeg_quality=14;c.fb_count=1;
 if(psramFound()){c.frame_size=FRAMESIZE_SVGA;c.jpeg_quality=12;c.fb_count=1;c.grab_mode=CAMERA_GRAB_LATEST;}
 return esp_camera_init(&c)==ESP_OK;
}

uint16_t countClips(){
 if(!sdReady)return 0; File root=SD_MMC.open("/clips"); if(!root)return 0; uint16_t n=0; File f=root.openNextFile();
 while(f){if(f.isDirectory())n++;f=root.openNextFile();}return n;
}


uint64_t sdUsedPercent(){ return sdReady && SD_MMC.totalBytes() ? (SD_MMC.usedBytes()*100ULL/SD_MMC.totalBytes()) : 0; }
String oldestClip(){ File root=SD_MMC.open("/clips"); if(!root)return ""; String best=""; File f=root.openNextFile(); while(f){String n=String(f.name()); bool d=f.isDirectory(); f.close(); if(d && n.startsWith("clip-") && (best.length()==0 || n<best))best=n; f=root.openNextFile();} root.close(); return best; }
bool deleteClipDir(const String &name){ if(!sdReady || clipRecording || clipUploading || name==activeClip)return false; String base="/clips/"+name; File dir=SD_MMC.open(base); if(!dir || !dir.isDirectory())return false; File item=dir.openNextFile(); while(item){String path=String(item.path()); bool nested=item.isDirectory(); item.close(); if(nested || !SD_MMC.remove(path))return false; item=dir.openNextFile();} dir.close(); bool ok=SD_MMC.rmdir(base); if(ok){clipCount=countClips();Serial.print("SD_PRUNE removed=");Serial.print(name);Serial.print(" clips=");Serial.println(clipCount);} return ok; }
uint16_t pruneOldClips(uint16_t target){uint16_t removed=0; while(sdReady && clipCount>=target && !clipRecording && !clipUploading){String n=oldestClip(); if(!n.length() || !deleteClipDir(n))break; removed++;} while(sdReady && sdUsedPercent()>=MAX_SD_PERCENT && !clipRecording && !clipUploading){String n=oldestClip(); if(!n.length() || !deleteClipDir(n))break; removed++;} return removed; }

// Never format on mount failure. Retry only while no capture is running.
bool initSD(){
 if(sdReady && SD_MMC.cardType()!=CARD_NONE)return true;
 sdReady=false;sdProbeOk=false;sdMountedKHz=0;
 const int clocks[]={20000,10000,4000,400};
 for(unsigned i=0;i<sizeof(clocks)/sizeof(clocks[0]);i++){
  SD_MMC.end();delay(150);sdMountAttempts++;
  Serial.printf("SD_MOUNT_ATTEMPT attempt=%lu khz=%d format=0\n",(unsigned long)sdMountAttempts,clocks[i]);
  if(!SD_MMC.begin("/sdcard",true,false,clocks[i],8)){sdError="Mount failed (format disabled)";continue;}
  if(SD_MMC.cardType()==CARD_NONE){sdError="No card detected";SD_MMC.end();continue;}
  sdReady=true;sdMountedKHz=clocks[i];sdError="";
  if(!SD_MMC.exists("/clips") && !SD_MMC.mkdir("/clips")){sdError="Mounted but cannot create clips directory";}
  clipCount=countClips();
  Serial.printf("SD_OK MB=%llu used=%llu clips=%u khz=%lu\n",SD_MMC.cardSize()/1024/1024,SD_MMC.usedBytes()/1024/1024,(unsigned)clipCount,(unsigned long)sdMountedKHz);return true;
 }
 Serial.printf("SD_UNAVAILABLE attempts=%lu error=%s\n",(unsigned long)sdMountAttempts,sdError.c_str());return false;
}
bool probeSD(){
 sdProbeOk=false;if(!sdReady){sdError="Not mounted";return false;}
 if(!SD_MMC.exists("/diagnostics") && !SD_MMC.mkdir("/diagnostics")){sdError="Cannot create diagnostics directory";return false;}
 String path;
 for(int i=0;i<4;i++){path="/diagnostics/probe-"+String(esp_random(),HEX)+".txt";if(!SD_MMC.exists(path))break;path="";}
 if(!path.length()){sdError="Cannot allocate unique probe name";return false;}
 const char *payload="CAM_SD_WRITE_READ_PROBE_V1\n";size_t len=strlen(payload);
 File f=SD_MMC.open(path,FILE_WRITE);if(!f){sdError="Probe open failed";return false;}
 sdProbePath=path;size_t written=f.write((const uint8_t*)payload,len);f.flush();f.close();
 if(written!=len){sdError="Probe short write";return false;}
 File r=SD_MMC.open(path,FILE_READ);if(!r){sdError="Probe reopen failed";return false;}
 char contents[64]={};size_t size=r.size(),read=r.readBytes(contents,len);r.close();
 if(size!=len || read!=len || memcmp(contents,payload,len)!=0){sdError="Probe read verification failed";return false;}
 sdProbeOk=true;sdError="";Serial.printf("SD_PROBE_OK path=%s bytes=%u\n",path.c_str(),(unsigned)len);return true;
}

bool getDuration(){
 if(!staConnected)return false;WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/config");http.addHeader("X-Web-Token",WEB_TOKEN);
 int code=http.GET();String body=code==200?http.getString():"";http.end();if(code!=200)return false;applyConfigBody(body);return true;
}
bool saveFrame(const String &path){
 camera_fb_t *fb=captureFreshFrame();if(!fb)return false;File file=SD_MMC.open(path,FILE_WRITE);bool ok=false;
 if(file){ok=file.write(fb->buf,fb->len)==fb->len;file.close();}esp_camera_fb_return(fb);return ok;
}

bool uploadFrame(const String &clip,const String &name){
 if(!staConnected)return false;File f=SD_MMC.open("/clips/"+clip+"/"+name,FILE_READ);if(!f)return false;
 WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/clips/"+clip+"/"+name);
 http.addHeader("X-Camera-Token",CAMERA_TOKEN);http.addHeader("X-Operation-Id",operationId);http.addHeader("Content-Type","image/jpeg");
 int code=http.sendRequest("POST",&f,f.size());f.close();http.end();return code==201;
}
bool pollRemoteCommand(){
 if(!staConnected || clipRecording || clipUploading || (int32_t)(millis()-nextCommandPoll)<0)return false;
 nextCommandPoll=millis()+1000;WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/capture-command");http.addHeader("X-Camera-Token",CAMERA_TOKEN);
 uint32_t at=millis();int code=http.GET();String body=code==200?http.getString():"";http.end();
 String command=jsonScalar(body,"command");if(code!=200 || !command.length())return false;
 operationId=jsonScalar(body,"operationId");applyConfigBody(body);
 Serial.printf("COMMAND_RECEIVED ms=%lu http_ms=%lu id=%s cmd=%s\n",millis(),millis()-at,operationId.c_str(),command.c_str());
 if(command=="capture-image"){operationKind="image";captureImageCommand();return true;}
 if(command=="capture-burst"){operationKind="burst";captureBurstCommand();return true;}
 if(command=="capture-clip"){operationKind="clip";startClip();return true;}return false;
}
bool uploadCurrentImage(){
 camera_fb_t *fb=captureFreshFrame();if(!fb)return false;
 publishStatus("uploading","Fotografia tomada; enviando imagen",imageIndex,imageTotal);
 WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/upload");
 http.addHeader("Content-Type","image/jpeg");http.addHeader("X-Camera-Token",CAMERA_TOKEN);http.addHeader("X-Operation-Id",operationId);
 http.addHeader("X-Capture-Millis",String(millis()));
 int code=http.POST(fb->buf,fb->len);http.end();esp_camera_fb_return(fb);
 Serial.printf("UPLOAD_ACK ms=%lu code=%d id=%s\n",millis(),code,operationId.c_str());return code==201;
}
void captureImageCommand(){
 imageIndex=0;imageTotal=1;bool ok=uploadCurrentImage();lastResult=ok?"Imagen recibida en galeria":"Fallo de captura o subida";
 if(!ok)publishStatus("error",lastResult,0,1); // Server confirms completion atomically with the JPEG.
}
void captureBurstCommand(){
 imageTotal=burstCount;uint16_t saved=0,success=0;String dir="/burst-"+operationId;
 if(!sdReady && !initSD()){publishStatus("error","MicroSD no montada; recuperacion sin formateo fallida",0,imageTotal);return;}
  if(SD_MMC.exists(dir) || !SD_MMC.mkdir(dir)){sdError="Cannot create unique burst directory";publishStatus("error","MicroSD montada pero no permite crear directorio de rafaga",0,imageTotal);return;}
 uint32_t due=millis();
 for(imageIndex=0;imageIndex<imageTotal;imageIndex++){
  if((int32_t)(due-millis())>0)delay(due-millis());due=millis()+burstInterval;
  if(!saveFrame(dir+"/"+String(imageIndex)+".jpg"))break;saved++;
 }
 if(saved!=imageTotal){publishStatus("error","Captura de rafaga incompleta",0,imageTotal);return;}
 publishStatus("uploading","Rafaga capturada; enviando imagenes",0,imageTotal);
 for(imageIndex=0;imageIndex<imageTotal;imageIndex++){
  String path=dir+"/"+String(imageIndex)+".jpg";File file=SD_MMC.open(path,FILE_READ);if(!file)break;
  WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/upload");
  http.addHeader("Content-Type","image/jpeg");http.addHeader("X-Camera-Token",CAMERA_TOKEN);http.addHeader("X-Operation-Id",operationId);
  int code=http.sendRequest("POST",&file,file.size());http.end();file.close();if(code!=201)break;
  success++;SD_MMC.remove(path);Serial.printf("BURST_UPLOAD ms=%lu frame=%u\n",millis(),success);
 }
 if(success==imageTotal)SD_MMC.rmdir(dir);else publishStatus("error","Subida de rafaga incompleta; originales conservados en SD",success,imageTotal);
}
void publishStatus(const String &state,const String &message,uint16_t current,uint16_t total){
 if(!staConnected)return;char q=34;String body=String("{")+q+"operationId"+q+":"+q+operationId+q+","+q+"kind"+q+":"+q+operationKind+q+","+q+"state"+q+":"+q+state+q+","+q+"message"+q+":"+q+message+q+","+q+"current"+q+":"+String(current)+","+q+"total"+q+":"+String(total)+"}";
 for(int attempt=0;attempt<3;attempt++){
  WiFiClientSecure client;HTTPClient http;beginHttp(http,client,"/api/camera-status");http.addHeader("Content-Type","application/json");http.addHeader("X-Camera-Token",CAMERA_TOKEN);
  int code=http.POST(body);http.end();Serial.printf("STATUS_ACK ms=%lu state=%s code=%d\n",millis(),state.c_str(),code);
  if(code==200 || code==409 || (state!="complete" && state!="error"))break;delay(250);
 }
}
void startClip(){
 if(clipRecording || clipUploading){publishStatus("error","Camara ocupada",0,0);return;}
  if(!sdReady && !initSD()){publishStatus("error","MicroSD no montada; recuperacion sin formateo fallida",0,0);return;}
 pruneOldClips(MAX_LOCAL_CLIPS);activeClip="clip-"+String((uint64_t(esp_random())<<32)|millis());
 if(!SD_MMC.mkdir("/clips/"+activeClip)){publishStatus("error","No se pudo crear el clip en SD",0,0);return;}
 clipFrames=0;clipUploaded=0;clipTotal=0;
 publishStatus("capturing","Grabando clip en microSD",0,0);
 clipStartedAt=millis();nextFrameAt=clipStartedAt;clipRecording=true;lastResult="Recording";
 Serial.printf("CLIP_START ms=%lu id=%s seconds=%u interval=%lu\n",millis(),activeClip.c_str(),(unsigned)configuredSeconds,clipInterval);
}
void finishClip(){
 clipRecording=false;lastClip=activeClip;clipTotal=clipFrames;clipCount=countClips();
 File m=SD_MMC.open("/clips/"+activeClip+"/manifest.txt",FILE_WRITE);if(m){m.printf("duration=%u\nframes=%u\ninterval=%lu\n",(unsigned)configuredSeconds,(unsigned)clipFrames,clipInterval);m.close();}
 Serial.printf("CLIP_FINISH ms=%lu id=%s frames=%u elapsed=%lu\n",millis(),activeClip.c_str(),(unsigned)clipFrames,millis()-clipStartedAt);
 if(!clipFrames){publishStatus("error","El clip no contiene fotogramas",0,0);return;}
 publishStatus("uploading","Clip grabado; enviando fotogramas",0,clipTotal);clipUploading=staConnected;lastResult="Uploading clip";
 if(!clipUploading)publishStatus("error","Sin WiFi; clip conservado en SD",0,clipTotal);
}
void tickClip(){
 if(clipRecording){
  uint32_t now=millis();
  if(now-clipStartedAt>=configuredSeconds*1000UL){finishClip();}
  else if((int32_t)(now-nextFrameAt)>=0){
   // Never catch up missed timestamps with a rush of delayed exposures.
   nextFrameAt=now+clipInterval;char name[16];snprintf(name,sizeof(name),"f%03u.jpg",(unsigned)(clipFrames+1));
   if(saveFrame("/clips/"+activeClip+"/"+String(name)))clipFrames++;
   else{clipRecording=false;flashOff();publishStatus("error","Fallo al guardar un fotograma; originales conservados en SD",0,0);}
  }
 }
 if(clipUploading && clipUploaded<clipTotal){
  char name[16];snprintf(name,sizeof(name),"f%03u.jpg",(unsigned)(clipUploaded+1));bool sent=false;
  for(uint8_t attempt=0;attempt<3 && !sent;attempt++){sent=uploadFrame(lastClip,String(name));if(!sent)delay(300);}
  if(sent){clipUploaded++;Serial.printf("CLIP_FRAME_ACK ms=%lu current=%u total=%u\n",millis(),clipUploaded,clipTotal);}
  else{clipUploading=false;lastResult="Upload failed; original on SD";publishStatus("error","Fallo de subida tras 3 intentos; original conservado en SD",clipUploaded,clipTotal);}
 }
 if(clipUploading && clipUploaded>=clipTotal){
  publishStatus("complete","Clip disponible en galeria",clipUploaded,clipTotal);clipUploading=false;lastResult="Clip available in gallery";
  Serial.printf("CLIP_UPLOAD_DONE ms=%lu id=%s frames=%u\n",millis(),lastClip.c_str(),(unsigned)clipUploaded);
 }
}

String statusJson(){
  char q=34;
  uint32_t elapsed=clipRecording?(millis()-clipStartedAt)/1000:0;
  uint32_t remaining=(clipRecording&&configuredSeconds>elapsed)?configuredSeconds-elapsed:0;
  String j=String("{")+q+"recording"+q+":"+(clipRecording?"true":"false");
  j+=String(",")+q+"uploading"+q+":"+(clipUploading?"true":"false");
  j+=String(",")+q+"duration"+q+":"+String(configuredSeconds);
  j+=String(",")+q+"elapsed"+q+":"+String(elapsed);
  j+=String(",")+q+"remaining"+q+":"+String(remaining);
  j+=String(",")+q+"frames"+q+":"+String(clipFrames);
  j+=String(",")+q+"uploaded"+q+":"+String(clipUploaded);
  j+=String(",")+q+"total"+q+":"+String(clipTotal);
  j+=String(",")+q+"clips"+q+":"+String(clipCount);
  j+=String(",")+q+"result"+q+":"+q+lastResult+q+"}";
  return j;
}

void page(){
 String h="<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>ESP32-CAM</title><style>body{font-family:sans-serif;max-width:40rem;margin:2rem auto;padding:1rem}button{font-size:1rem;padding:.7rem;margin:.2rem}#bar{height:16px;background:#ddd;border-radius:5px;overflow:hidden}#fill{height:100%;width:0;background:#1976d2}</style><h1>ESP32-CAM evidence prototype</h1>";
 h+="<p>Wi-Fi: "+String(staConnected?"connected":"provisioning")+" | microSD: "+String(sdReady?"ready":"not available")+"</p><p id=msg>"+lastResult+"</p>";
 h+="<form action='/capture-upload' method=post><button>Capture and upload</button></form><form action='/capture-clip' method=post><button>Record local clip (microSD)</button></form><form action='/free-space' method=post><button>Free space (delete oldest clips)</button></form>";
 h+="<p>Configured duration: "+String(configuredSeconds)+" seconds | saved clips: "+String(clipCount)+" | last clip: "+String(clipFrames)+" frames</p><div id=bar><div id=fill></div></div><p id=progress></p>";
 h+="<p><a href='"+String(GALLERY_URL)+"'>View evidence gallery</a></p><p><a href='/sd-status'>microSD status</a> | <a href='/capture'>Local JPEG</a> | <a href='/health'>Health</a></p>";
 h+="<script>async function u(){try{let s=await fetch('/clip-status').then(r=>r.json());let p=s.recording?Math.min(100,s.elapsed*100/s.duration):(s.uploading?100:0);fill.style.width=p+'%';progress.textContent=s.recording?'Recording: '+s.elapsed+' / '+s.duration+' seconds | '+s.frames+' frames':s.uploading?'Uploading: '+s.uploaded+' / '+s.total+' frames':s.result;msg.textContent=s.result+' | saved clips: '+s.clips;}catch(e){}}setInterval(u,1000);u()</script>";server.send(200,"text/html",h);
}
void health(){server.send(200,"application/json",statusJson());}
void clipStatus(){server.send(200,"application/json",statusJson());}
String sdReport(){
 return String("{\"ready\":")+(sdReady?"true":"false")+",\"total_mb\":"+(sdReady?String(SD_MMC.cardSize()/1024/1024):"0")+",\"volume_mb\":"+(sdReady?String(SD_MMC.totalBytes()/1024/1024):"0")+",\"used_mb\":"+(sdReady?String(SD_MMC.usedBytes()/1024/1024):"0")+",\"clips\":"+String(clipCount)+",\"mount_attempts\":"+String(sdMountAttempts)+",\"clock_khz\":"+String(sdMountedKHz)+",\"write_read_verified\":"+(sdProbeOk?"true":"false")+",\"probe_path\":\""+sdProbePath+"\",\"error\":\""+sdError+"\",\"format_on_failure\":false}";
}
void sdStatus(){server.send(200,"application/json",sdReport());}
void sdRecover(){
 if(!server.hasHeader("X-Camera-Token") || server.header("X-Camera-Token")!=CAMERA_TOKEN){server.send(401,"application/json","{\"ok\":false}");return;}
 if(clipRecording || clipUploading){server.send(409,"application/json","{\"ok\":false,\"error\":\"Camera busy\"}");return;}
 bool ok=initSD() && probeSD();
 if(!ok && sdReady){SD_MMC.end();sdReady=false;ok=initSD() && probeSD();}
 server.send(ok?200:503,"application/json",sdReport());
}
void capture(){camera_fb_t *fb=esp_camera_fb_get();if(!fb){server.send(503,"text/plain","camera unavailable");return;}server.setContentLength(fb->len);server.send(200,"image/jpeg","");server.client().write(fb->buf,fb->len);esp_camera_fb_return(fb);}
void captureUpload(){camera_fb_t *fb=esp_camera_fb_get();if(!fb){server.send(503,"text/plain","camera unavailable");return;}WiFiClientSecure c;c.setInsecure();c.setTimeout(15000);bool ok=staConnected&&c.connect(HOST,HTTPS_PORT);if(ok){c.printf("POST /api/upload HTTP/1.1\r\nHost: %s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\nX-Camera-Token: %s\r\nConnection: close\r\n\r\n",HOST,fb->len,CAMERA_TOKEN);ok=c.write(fb->buf,fb->len)==fb->len;String status=c.readStringUntil('\n');ok=ok&&status.indexOf(" 201 ")>=0;c.stop();}esp_camera_fb_return(fb);lastResult=ok?"Uploaded securely":"Upload failed";server.send(ok?200:502,"text/html",String("<p>")+lastResult+"</p><p><a href='/'>Back</a></p>");}
void captureClip(){server.send(409,"text/plain","Use the authenticated remote gallery to create a correlated operation");return;startClip();server.send(202,"text/html","<p>Clip started. Return to <a href='/'>status</a>.</p>");}
void flashOnHandler(){pinMode(FLASH_PIN,OUTPUT);digitalWrite(FLASH_PIN,HIGH);esp_timer_stop(flashSafetyTimer);esp_timer_start_once(flashSafetyTimer,1000000);server.send(200,"text/plain","FLASH_ON (1s safety cutoff)");}
void flashOffHandler(){pinMode(FLASH_PIN,OUTPUT);digitalWrite(FLASH_PIN,LOW);server.send(200,"text/plain","FLASH_OFF");}
void freeSpaceHandler(){if(clipRecording||clipUploading){server.send(409,"text/plain","Recording or upload in progress");return;}uint16_t removed=pruneOldClips(MAX_LOCAL_CLIPS-1);lastResult=removed?"Freed "+String(removed)+" old clip(s)":"No clips removed";server.send(200,"text/html",String("<p>")+lastResult+"</p><p><a href='/'>Back</a></p>");}
void configPage(){server.send(200,"text/html","<h1>Configure Wi-Fi</h1><form method=post action=/save>SSID:<input name=s><br>Password:<input type=password name=p><br><button>Save</button></form>");}
void saveConfig(){ssid=server.arg("s");pass=server.arg("p");if(!ssid.length()){server.send(400,"text/plain","SSID required");return;}prefs.begin("camproto",false);prefs.putString("ssid",ssid);prefs.putString("pass",pass);prefs.end();server.send(200,"text/html","Saved. Restarting...");delay(500);ESP.restart();}
void setup(){Serial.begin(115200);delay(250);Serial.println("CAM_EVIDENCE_SYNC_V2");pinMode(FLASH_PIN,OUTPUT);digitalWrite(FLASH_PIN,LOW);esp_timer_create_args_t timerArgs={};timerArgs.callback=&flashSafetyOff;timerArgs.name="flash-safety";esp_timer_create(&timerArgs,&flashSafetyTimer);if(!initCamera()){Serial.println("CAMERA_INIT_FAIL");return;}sdReady=initSD();prefs.begin("camproto",true);ssid=prefs.getString("ssid","");pass=prefs.getString("pass","");prefs.end();WiFi.mode(WIFI_AP_STA);if(ssid.length()){WiFi.begin(ssid.c_str(),pass.c_str());uint32_t until=millis()+25000;while(WiFi.status()!=WL_CONNECTED&&millis()<until)delay(250);staConnected=WiFi.status()==WL_CONNECTED;}if(staConnected){Serial.printf("WIFI_OK IP=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());}else{WiFi.softAP("CAM-PROTOTYPE");}server.on("/",page);server.on("/health",health);server.on("/clip-status",clipStatus);server.on("/sd-status",sdStatus);server.on("/sd-recover",HTTP_POST,sdRecover);const char *sdHeaders[]={"X-Camera-Token"};server.collectHeaders(sdHeaders,1);server.on("/capture",capture);server.on("/capture-upload",HTTP_POST,captureUpload);server.on("/capture-clip",HTTP_POST,captureClip);server.on("/flash-on",HTTP_GET,flashOnHandler);server.on("/flash-off",HTTP_GET,flashOffHandler);server.on("/free-space",HTTP_POST,freeSpaceHandler);server.on("/configure",configPage);server.on("/save",HTTP_POST,saveConfig);pinMode(FLASH_PIN,OUTPUT);flashOff();server.begin();Serial.println("HTTP_READY");}
void loop(){staConnected=WiFi.status()==WL_CONNECTED;server.handleClient();pollRemoteCommand();tickClip();delay(2);}
