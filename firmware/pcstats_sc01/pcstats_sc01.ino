/*
 * PC DASHBOARD - WT32-SC01 (classic ESP32 + ST7796 480x320, LovyanGFX).
 * Clean 4-page UI (tap screen to change page). Fed by pcstats.py over USB serial.
 *   1/4 CPU+RAM+NET+UPTIME   2/4 GPU   3/4 DISK   4/4 SYSTEM
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel; lgfx::Bus_SPI _bus; lgfx::Light_PWM _light; lgfx::Touch_FT5x06 _touch;
public:
  LGFX(void) {
    { auto c = _bus.config(); c.spi_host = HSPI_HOST; c.spi_mode = 0; c.freq_write = 55000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = -1; c.pin_dc = 21; _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config(); c.pin_cs = 15; c.pin_rst = 22; c.pin_busy = -1;
      c.memory_width = 320; c.memory_height = 480; c.panel_width = 320; c.panel_height = 480;
      c.offset_rotation = 0; c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
      c.readable = true; c.invert = false; c.rgb_order = false; c.dlen_16bit = false; c.bus_shared = true; _panel.config(c); }
    { auto c = _light.config(); c.pin_bl = 23; c.freq = 44100; c.pwm_channel = 7; _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config(); c.x_min = 0; c.x_max = 319; c.y_min = 0; c.y_max = 479; c.pin_int = -1; c.bus_shared = true;
      c.offset_rotation = 0; c.i2c_port = 1; c.i2c_addr = 0x38; c.pin_sda = 18; c.pin_scl = 19; c.freq = 400000;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
LGFX tft;

#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
const uint16_t BG=RGB(9,12,20), TITLE=RGB(28,66,170), TRACK=RGB(28,34,52), FILL=RGB(46,104,224),
               CYAN=RGB(64,206,236), WHITE=RGB(235,240,248), GREY=RGB(120,135,155),
               GREEN=RGB(70,230,140), YELL=RGB(255,205,70), RED=RGB(248,70,70), TEAL=RGB(90,190,210),
               MAG=RGB(255,120,255);

#define F_TITLE &fonts::FreeSansBold12pt7b
#define F_LABEL &fonts::FreeSansBold12pt7b
#define F_BIG   &fonts::FreeSansBold18pt7b
#define F_SMALL &fonts::FreeSans9pt7b
#define F_PG    &fonts::Font2

int cpu=0, ram=0, gpu=0, gtemp=0, ctemp=0, disk=0, cores=0, upt=0;
float ru=0, rt=0, gu=0, gtot=0, dmb=0, umb=0; int du=0, dtt=0;
String host="PC", buf;
int page=1; bool haveData=false; bool wasTouch=false;

#define GW 452
uint8_t cpuH[GW], gpuH[GW]; int hCount=0, hHead=0;
void pushHist(int c,int g){ cpuH[hHead]=constrain(c,0,100); gpuH[hHead]=constrain(g,0,100); hHead=(hHead+1)%GW; if(hCount<GW)hCount++; }

uint16_t loadColor(int p){ return p<60?GREEN : p<85?YELL : RED; }
bool alertOn(){ return cpu>=90 || ram>=90 || gtemp>=82 || disk>=97; }

void uptimeStr(char* b, size_t n){
  int d=upt/86400, h=(upt%86400)/3600, m=(upt%3600)/60, s=upt%60;
  if(d>0) snprintf(b,n,"%dd %02d:%02d",d,h,m); else snprintf(b,n,"%02d:%02d:%02d",h,m,s);
}

void titleBar(const char* t){
  tft.fillRect(0,0,480,44,TITLE);
  tft.setFont(F_TITLE); tft.setTextColor(WHITE,TITLE);
  tft.setTextDatum(textdatum_t::middle_left); tft.drawString(t,14,23);
}
void drawAlert(){
  tft.fillRect(330,0,150,44,TITLE);
  bool a=alertOn(); uint16_t c=a?RED:GREEN; const char* s=a?"ALERT":"OK";
  tft.setFont(F_SMALL); tft.setTextColor(c,TITLE);
  tft.setTextDatum(textdatum_t::middle_right); int tw=tft.textWidth(s);
  tft.drawString(s,466,23); tft.fillCircle(466-tw-14,22,6,c);
}
void pageNum(){
  char s[8]; snprintf(s,sizeof(s),"%d/5",page);
  tft.setFont(F_PG); tft.setTextColor(GREY,BG);
  tft.setTextDatum(textdatum_t::bottom_right); tft.drawString(s,470,316);
}
void bar(int x,int y,int w,int h,int pct){
  tft.fillRoundRect(x,y,w,h,4,TRACK);
  int fw=w*constrain(pct,0,100)/100; if(fw>6) tft.fillRoundRect(x,y,fw,h,4,FILL);
}
// label (left) + value% (right) + bar below
void barRow(int y,const char* label,int pct){
  tft.setFont(F_LABEL); tft.setTextColor(WHITE,BG);
  tft.setTextDatum(textdatum_t::middle_left); tft.drawString(label,16,y);
  char v[8]; snprintf(v,sizeof(v),"%d%%",pct);
  tft.fillRect(360,y-14,104,28,BG);
  tft.setTextColor(CYAN,BG); tft.setTextDatum(textdatum_t::middle_right); tft.drawString(v,464,y);
  bar(16,y+16,448,16,pct);
}
// small cyan label + big value (+ optional unit)
void bigField(int x,int y,const char* label,const char* val,const char* unit,uint16_t vc,int clrW){
  tft.setFont(F_SMALL); tft.setTextColor(TEAL,BG);
  tft.setTextDatum(textdatum_t::top_left); tft.drawString(label,x,y);
  tft.fillRect(x,y+18,clrW,32,BG);
  tft.setFont(F_BIG); tft.setTextColor(vc,BG);
  tft.setTextDatum(textdatum_t::top_left); tft.drawString(val,x,y+20);
  if(unit && unit[0]){ int nw=tft.textWidth(val); tft.setFont(F_SMALL); tft.setTextColor(GREY,BG); tft.drawString(unit,x+nw+8,y+32); }
}

void drawGraph(int cx,int cy,int cw,int ch,const char* label,uint16_t accent,uint8_t* hist){
  const uint16_t pbg=RGB(14,18,28);
  tft.fillRoundRect(cx,cy,cw,ch,6,pbg); tft.drawRoundRect(cx,cy,cw,ch,6,RGB(40,52,70));
  tft.setFont(F_SMALL); tft.setTextColor(accent,pbg);
  tft.setTextDatum(textdatum_t::top_left); tft.drawString(label,cx+10,cy+6);
  int gx=cx+8, gy=cy+28, gw=GW, gh=ch-36;
  tft.fillRect(gx,gy,gw,gh,RGB(10,13,20));
  for(int p=25;p<100;p+=25){ int yy=gy+gh-(gh*p/100); tft.drawFastHLine(gx,yy,gw,RGB(30,40,54)); }
  uint16_t dim=(accent>>1)&0x7BEF;
  int n=hCount, start=(hHead-n+GW)%GW, x0=gx+(GW-n), px=0,py=0;
  for(int j=0;j<n;j++){ int val=hist[(start+j)%GW]; int x=x0+j; int yv=gy+gh-(gh*val/100);
    tft.drawFastVLine(x,yv+1,gy+gh-yv-1,dim);
    if(j>0) tft.drawLine(px,py,x,yv,accent); px=x; py=yv; }
}

// ---------- pages ----------
void pageStatic(int p){
  tft.fillScreen(BG);
  const char* t = p==1?"PC DASHBOARD" : p==2?"GPU" : p==3?"DISK" : p==4?"SYSTEM" : "LIVE GRAPHS";
  titleBar(t); pageNum();
}
void updatePage(int p){
  char a[24], b[24];
  drawAlert();
  if(p==1){
    barRow(74,"CPU",cpu);
    barRow(126,"RAM",ram);
    snprintf(a,sizeof(a),"%.1f",dmb); bigField(16,166,"DOWNLOAD",a,"KB/s",WHITE,210);
    snprintf(b,sizeof(b),"%.1f",umb); bigField(250,166,"UPLOAD",b,"KB/s",WHITE,210);
    char u[16]; uptimeStr(u,sizeof(u)); bigField(16,238,"UPTIME",u,"",CYAN,300);
    char ct[10]; if(ctemp>0) snprintf(ct,sizeof(ct),"%dC",ctemp); else snprintf(ct,sizeof(ct),"N/A");
    char tp[32]; snprintf(tp,sizeof(tp),"CPU %s    GPU %dC",ct,gtemp);
    tft.fillRect(0,296,330,22,BG);
    tft.setFont(F_SMALL); tft.setTextColor((ctemp>=85||gtemp>=85)?RED:TEAL,BG);
    tft.setTextDatum(textdatum_t::top_left); tft.drawString(tp,16,298);
  } else if(p==2){
    barRow(74,"GPU LOAD",gpu);
    snprintf(a,sizeof(a),"%d C",gtemp); bigField(16,126,"TEMP",a,"",loadColor(gtemp>100?100:gtemp),200);
    snprintf(b,sizeof(b),"%.1f / %.1f GB",gu,gtot); bigField(250,126,"VRAM",b,"",WHITE,220);
    int vp = gtot>0 ? (int)(gu/gtot*100) : 0;
    tft.setFont(F_SMALL); tft.setTextColor(TEAL,BG); tft.setTextDatum(textdatum_t::top_left); tft.drawString("VRAM USED",16,206);
    bar(16,228,448,16,vp);
  } else if(p==3){
    barRow(74,"DISK ACTIVE",disk);
    snprintf(a,sizeof(a),"%d / %d GB",du,dtt); bigField(16,126,"SPACE USED (C:)",a,"",WHITE,300);
    int sp = dtt>0 ? (int)((long)du*100/dtt) : 0;
    tft.setFont(F_SMALL); tft.setTextColor(TEAL,BG); tft.setTextDatum(textdatum_t::top_left); tft.drawString("CAPACITY",16,196);
    bar(16,218,448,16,sp);
    char spv[8]; snprintf(spv,sizeof(spv),"%d%%",sp);
    tft.fillRect(360,190,104,24,BG); tft.setTextColor(CYAN,BG); tft.setTextDatum(textdatum_t::top_right); tft.drawString(spv,464,196);
  } else if(p==4){
    tft.fillRect(0,56,480,250,BG);
    tft.setTextDatum(textdatum_t::top_left);
    struct { const char* k; char v[24]; } rows[5];
    snprintf(rows[0].v,24,"%s",host.c_str()); rows[0].k="HOST";
    snprintf(rows[1].v,24,"%d threads",cores); rows[1].k="CPU CORES";
    snprintf(rows[2].v,24,"%.1f GB",rt); rows[2].k="TOTAL RAM";
    snprintf(rows[3].v,24,"%d GB",dtt); rows[3].k="DISK (C:)";
    char u[16]; uptimeStr(u,sizeof(u)); snprintf(rows[4].v,24,"%s",u); rows[4].k="UPTIME";
    int y=70;
    for(int i=0;i<5;i++){
      tft.setFont(F_SMALL); tft.setTextColor(TEAL,BG); tft.drawString(rows[i].k,16,y);
      tft.setFont(F_LABEL); tft.setTextColor(WHITE,BG); tft.drawString(rows[i].v,180,y-2);
      y+=46;
    }
  } else if(p==5){
    char l1[28], l2[28], c1[10];
    if(ctemp>0) snprintf(c1,sizeof(c1),"%dC",ctemp); else snprintf(c1,sizeof(c1),"N/A");
    snprintf(l1,sizeof(l1),"CPU  %d%%   %s",cpu,c1);
    snprintf(l2,sizeof(l2),"GPU  %d%%   %dC",gpu,gtemp);
    drawGraph(6,50,468,122,l1,CYAN,cpuH);
    drawGraph(6,180,468,122,l2,MAG,gpuH);
  }
}

String getVal(const String& l, const char* k){
  String key=String(k)+":"; int i=l.indexOf(key); if(i<0) return "";
  i+=key.length(); int j=l.indexOf(';',i); if(j<0) j=l.length(); return l.substring(i,j);
}
void parseLine(const String& l){
  if(l.indexOf("CPU:")<0) return;
  cpu=getVal(l,"CPU").toInt(); ram=getVal(l,"RAM").toInt();
  ru=getVal(l,"RU").toFloat(); rt=getVal(l,"RT").toFloat();
  gpu=getVal(l,"GPU").toInt(); gu=getVal(l,"GU").toFloat(); gtot=getVal(l,"GT").toFloat(); gtemp=getVal(l,"GTP").toInt();
  ctemp=getVal(l,"CTP").toInt();
  disk=getVal(l,"DSK").toInt(); du=getVal(l,"DU").toInt(); dtt=getVal(l,"DT").toInt();
  dmb=getVal(l,"DKB").toFloat(); umb=getVal(l,"UKB").toFloat();
  upt=getVal(l,"UPT").toInt(); cores=getVal(l,"CORES").toInt();
  String h=getVal(l,"HOST"); if(h.length()) host=h;
  pushHist(cpu,gpu);
  if(!haveData){ haveData=true; pageStatic(page); }
  updatePage(page);
}

void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.setBrightness(210);
  pageStatic(1);
  tft.setFont(F_LABEL); tft.setTextColor(GREY,BG); tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("Waiting for PC...",240,150);
  tft.setFont(F_SMALL); tft.drawString("run pcstats.py  -  tap to change page",240,185);
}

void loop(){
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\n'){ parseLine(buf); buf=""; }
    else if(c!='\r' && buf.length()<340) buf+=c;
  }
  int32_t tx,ty;
  if(tft.getTouch(&tx,&ty)){
    if(!wasTouch){ wasTouch=true; page=(page%5)+1; pageStatic(page); if(haveData) updatePage(page); }
  } else wasTouch=false;
}
