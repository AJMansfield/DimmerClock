#define INCLUDE_PRINTF
#define DEBUG
#define QUIT_ON_WARN
#include <Arduino.h>
#include <MemoryFree.h>

#include <Wire.h>
// note, I2C addresses:
// LCD is at 0x3f
// RTC is at 0x68
// ROM is at 0x50


// ======================== LCD SETUP ========================
#include <LiquidCrystal_PCF8574.h> // should work with any LCD library
LiquidCrystal_PCF8574 lcd(0x3f);
#define LCD_CHARS   16
#define LCD_LINES    2
void make_char(char c, uint8_t* image){ //need this function to pass to LcdEffects
  lcd.createChar(c, image); // so it knows how to make custom characters
}

#include <LcdEffects.h>
#include <Effects.h>
LcdEffects<0> fx(make_char);
void highlight(char& c){
  fx.applyEffect(c, effect::underline);
}

void setupLCD(){
  lcd.begin(LCD_CHARS, LCD_LINES);
  lcd.setBacklight(255);
  lcd.clear();
}

// =================== TRIAC DIMMER SETUP =======================
#include <TriacDimmer.h>

void setupDimmer(){
  TriacDimmer::begin();
}

// ========================== RTC SETUP ==========================
#include <MD_DS1307.h>
void setupRTC(){
  RTC.readTime();
  set_system_time(mk_gmtime(&RTC.time));
}
void serviceRTC(){ // could theoretically be switched to an interrupt, but that would require 
  static unsigned long last_sync = 0;
  if (Serial.available()) {
    processSyncMessage();
    last_sync = millis();
  } else if(millis() - last_sync > 1000){ // tick every second
    RTC.readTime();
    set_system_time(mk_gmtime(&RTC.time));
    last_sync += 1000;
  }
}

#define TIME_HEADER  'T'   // Header tag for serial time sync message
void processSyncMessage() {

  if(Serial.find(TIME_HEADER)) {
    time_t pctime = Serial.parseInt();
    time_t mctime = pctime - UNIX_OFFSET;
    gmtime_r(&mctime, &RTC.time);
    RTC.writeTime();
    set_system_time(mctime);
    
  }
}

// ========================== ALARM SETUP =========================



// ==================== EEPROM SETTINGS SETUP ======================
#include <extEEPROM.h>
extEEPROM eep(kbits_32, 1, 32);

#include <time.h>
#include <util/eu_dst.h>
#include <util/usa_dst.h>

struct setting_t {
  struct id_t {
    const char signature[8] = {'C','L','O','C','K','C','F','G'};
    const uint16_t v_major = 0xFFFD;
    const uint16_t v_minor = 0xFFFE;
    const uint32_t length = sizeof(setting_t);
  };
  const id_t id;
  
  int8_t timezone;
  size_t dst;

  uint16_t screen_timeout = 10;
};

setting_t setting;

volatile bool saveFlag = false;
volatile bool reloadFlag = false;
void serviceState(){
  if(saveFlag){
    saveFlag = false;
    save();
  }
  if(reloadFlag){
    reloadFlag = false;
    reload();
  }
}
void save(){
  #ifdef DEBUG
  Serial.print(F("Saving..."));
  #endif
  
  eep.begin(twiClock100kHz);
  eep.write(0, (byte*)&setting, sizeof(setting));
  
  #ifdef DEBUG
  Serial.println("ok!");
  #endif
  
  update();
}

void reload(){
  #ifdef DEBUG
  Serial.print(F("Loading..."));
  #endif
  
  setting_t::id_t id;
  eep.begin(twiClock100kHz);
  eep.read(0, (byte*)(&id), sizeof(setting_t::id_t));
  if(memcmp(&id.signature, &setting.id.signature, 8) != 0){
    #ifdef DEBUG
    Serial.printf(F("error: expected file header '%8s': got '%8s'!\n"),
        setting.id.signature, id.signature);
    #endif
    return;
  }
  if(id.v_major != setting.id.v_major || setting.id.v_minor > id.v_minor ){
    #ifdef DEBUG
    Serial.printf(F("error: current version (%4x:%4x) is incompatible with version (%4x:%4x)!\n"),
        setting.id.v_major, setting.id.v_minor, id.v_major, id.v_minor);
    #endif
    return;
  }
  if(setting.id.v_minor != id.v_minor ){
    #ifdef DEBUG
    Serial.printf(F("loading from compatible older version (%4x:%4x) ..."),
        id.v_major, id.v_minor);
    #endif
    #ifdef QUIT_ON_WARN
    return;
    #endif
    
  }
  if(setting.id.length != id.length ){
    #ifdef DEBUG
    Serial.printf(F("warning: different lengths (old %d, new %d) ..."),
        setting.id.length, id.length);
    #endif
    #ifdef QUIT_ON_WARN
    return;
    #endif
  }
  eep.read(0, (byte*)&setting, id.length);

  #ifdef DEBUG
  Serial.println("ok!");
  #endif
  
  update();
}

void update(){
  set_zone(setting.timezone * ONE_HOUR);
  switch(setting.dst){
    case 0: set_dst(nullptr); break;
    case 1: set_dst(usa_dst); break;
    case 2: set_dst(eu_dst); break;
  }
}



// ======================= MENU DEFINITIONS ============================
#include <Adjuster.h>
#include <AdjustmentBase.h>
#include <Adjustment.h>
#include <ChoiceAdjustment.h>
#include <TimeAdjustment.h>
#include <ExitAdjustment.h>
#include <PickAdjustment.h>


const char* adj2_names[] = {
  "Daylight Time",
  "Time Zone",
  "Screen Timeout",
  "Back",
};

const char* dst_names[] = {
  "None",
  "USA",
  "Europe"
};

const char* dst_fmt = "DST: %-6s";
const char* off_fmt = "Offset: GMT%+d";
const char* to_fmt = "Timeout: %4d s";
ChoiceAdjustment adj_dst(&setting.dst, dst_names, sizeof(dst_names)/sizeof(const char*), dst_fmt, "", highlight);
Adjuster<int8_t> adjr_tz(&setting.timezone, -11, 13, 1, true);
Adjustment<int8_t> adj_tz(&adjr_tz, off_fmt, "", highlight);
Adjuster<uint16_t> adjr_to(&setting.screen_timeout, 1, 1000, 1);
Adjustment<uint16_t> adj_to(&adjr_to, to_fmt, "", highlight);
ExitAdjustment<EXIT_SAVE> adj_exit;

AdjustmentBase* adj2[] = {
  &adj_dst,
  &adj_tz,
  &adj_to,
  &adj_exit
};

PickAdjustment menu2(adj2, adj2_names, sizeof(adj2)/sizeof(AdjustmentBase*), LCD_CHARS);

const char* adj_names[] = {
  "Alarm 1",
  "Settings",
  "Back",
};
const char* time_fmt = "  %I:%M %p";
const char* time_fx = "  00 11";
time_t t0;
TimeAdjustment<false,false,false,true,true,false>
  adj_a0(&t0, time_fmt, time_fx, highlight);
//  adj_a1(&setting.alarm[1], time_fmt, time_edit, highlight),
//  adj_a2(&setting.alarm[2], time_fmt, time_edit, highlight),
//  adj_a3(&setting.alarm[3], time_fmt, time_edit, highlight);
AdjustmentBase* adj[] = {
  &adj_a0,
//  &adj_a1,
//  &adj_a2,
//  &adj_a3,
  &menu2,
  &adj_exit
};

PickAdjustment root(adj, adj_names, sizeof(adj)/sizeof(AdjustmentBase*), LCD_CHARS);

#include <TimeLUT.h>

TimeLUT schedule;

void setupSchedule(){
  schedule.insert(2000, 5);
  schedule.insert(3000, 10);
  schedule.insert(7000, 30);
  schedule.insert(8000, 35);
  schedule.insert(9000, 40);
  schedule.insert(4000, 15);
  schedule.insert(1000, 5);
  schedule.insert(5000, 20);
  schedule.insert(6000, 25);
  schedule.insert(10000, 45);

  for(size_t i = 0; i < schedule.size; i++){
    Serial.printf("(%p, %p)\n", &schedule.table[i].x, &schedule.table[i].y);
  }

  //time_t randm = rand();
  //Serial.printf("(%d, %2d)\n", randm, schedule.lookup(randm));
}



// ============================ INPUT DEVICE CONFIG ==============================
#include <Rotary.h>
//#include <ClickButton.h>
#include <ClickButton.h>


Rotary encoder(7, 6);
ClickButton button(5);


bool in_menu = false;
unsigned long last_act = 0;
ISR(TIMER2_COMPA_vect) { //service encoder
  button.service();
  ClickButton::Button b = button.getValue();
  
  unsigned char dir = encoder.process();
  int8_t v = 0;// = encoder.process();
  switch(dir){
    case DIR_NONE: v = 0; break;
    case DIR_CW: v = 1; break;
    case DIR_CCW: v = -1; break;
    default: assert(false); break;
  }

  if(!(b == ClickButton::Open && v == 0) && millis() - last_act > setting.screen_timeout*1000){
    last_act = millis();
    return;
  }

  exit_t ev = E_NONE;
  
  switch (b) {
    case ClickButton::Open:
      if(v != 0) {
        ev = root.action(ACT_CHANGE, v);
      } else if(in_menu){
        ev = root.action(ACT_NONE);
      }
      break;
    case ClickButton::Pressed:
      break;
    case ClickButton::Held:
      if(in_menu){
        ev = root.action(ACT_CTXT);
      } else {
        ev = root.action(ACT_BEGIN);
      }
      break;
    case ClickButton::Clicked:
      if(in_menu){
        ev = root.action(ACT_ENTER);
      } else {
        ev = root.action(ACT_BEGIN);
      }
      break;
    case ClickButton::DoubleClicked:
      if(in_menu){
        ev = root.action(ACT_BACK);
      } else {
        ev = root.action(ACT_BEGIN);
      }
      break;
    case ClickButton::Released:
      break;
    case ClickButton::Closed:
      break;
  }
  
  switch(ev){
  case EXIT_SAVE:
    in_menu = false;
    saveFlag = true;
    break;
  case NOEXIT_SAVE:
    in_menu = true;
    saveFlag = true;
    last_act = millis();
    break;
  case EXIT_CANCEL:
    in_menu = false;
    reloadFlag = true;
    break;
  case NOEXIT_CANCEL:
    in_menu = true;
    reloadFlag = true;
    last_act = millis();
    break;
  case EXIT:
    in_menu = false;
    break;
  case NOEXIT:
    in_menu = true;
    last_act = millis();
    break;
  case E_NONE:
    break;
  }
}

void setupInput(){
  
  TIMSK2 = 0; // disable interrupts
  TIFR2 = 0xff; // clear flags
  TCCR2A = _BV(WGM21);// CTC OCRA mode
  TCCR2B = _BV(CS22); // /64 prescaler
  OCR2A = 250; // 1ms intervals
  TIMSK2 |= _BV(OCIE2A); // enable OCRA interrupt
//         
}




void setup() {
  #ifdef DEBUG
  //Serial.begin(115200);
  Serial.begin(9600);
  #endif
  setupLCD();
  setupRTC();
  setupSchedule();
  setupInput();
  setupDimmer();
  reload();
}

void loop() {
  char buf[LCD_LINES*LCD_CHARS+1];
  memset(buf, 0x20, LCD_LINES*LCD_CHARS+1);
  serviceState();
  serviceRTC();

  if(millis() - last_act > setting.screen_timeout * 1000){
    lcd.setBacklight(0);
  } else {
    lcd.setBacklight(255);
  }
  if(in_menu){
    root.full_string(buf, LCD_LINES*LCD_CHARS);
  } else {
    time_t t = time(nullptr);
    strftime(buf, LCD_LINES*LCD_CHARS+1, "   %I:%M:%S %p  %a %b %d, %Y", localtime(&t));
  }
  
  
  for(size_t i = 0; i < LCD_LINES*LCD_CHARS; i++){
    if(buf[i] == 0)
      buf[i] = ' ';
  }
  
  for(size_t i = 0; i < LCD_LINES; i++){
    char buf2[LCD_CHARS+1];
    memcpy(buf2, buf+i*LCD_CHARS, LCD_CHARS+1);
    
    lcd.setCursor(0,i); 
    lcd.write(buf2, LCD_CHARS);
  }
  
  #ifdef DEBUG
  //Serial.printf(F("free: %d\n"), freeMemory());
  #endif
  delay(10);
}

