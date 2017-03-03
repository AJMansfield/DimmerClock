#define INCLUDE_PRINTF

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

// ================== RTC CLOCK FUNCTION SETUP ==================
#include <Time.h>
#include <TimeAlarms.h>
#include <DS1307RTC.h>
#include <ConvertTime.h>

void setupRTC(){
  setSyncProvider(RTC.get);   // the function to get the time from the RTC
}

#define TIME_HEADER  "T" 
void processSyncMessage() {
  unsigned long pctime;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if(Serial.find(TIME_HEADER)) {
     pctime = Serial.parseInt();
     if( pctime >= DEFAULT_TIME) { // check the integer is a valid time (greater than Jan 1 2013)
       RTC.set(pctime);
       setTime(pctime); // Sync Arduino clock to the time received on the serial port
     }
  }
}

#define FADE_TIME 3600 //seconds to fade in, 600 = 10 minutes
unsigned int fade_a_counter = 0;
unsigned int fade_b_counter = 0;
bool fade_a_cancel = false;
bool fade_b_cancel = false;

void fade_a_in(){
  Serial.println("fading in A");
  fade_a_counter = 0;
  fade_a_cancel = false;
  Alarm.timerOnce(1, fade_a_in_helper);
}
void fade_a_in_helper(){
  fade_a_counter++;
  if(fade_a_cancel){
    return;
  }
  if(fade_a_counter >= FADE_TIME){
    TriacDimmer::setBrightness(9, 1);
    return;
  }
  TriacDimmer::setBrightness(9, (float) fade_a_counter / FADE_TIME);
  Alarm.timerOnce(1, fade_a_in_helper);
}
void fade_b_in(){
  Serial.println("fading in B");
  fade_b_counter = 0;
  fade_b_cancel = false;
  Alarm.timerOnce(1, fade_b_in_helper);
}
void fade_b_in_helper(){
  fade_b_counter++;
  if(fade_b_cancel){
    return;
  }
  if(fade_b_counter >= FADE_TIME){
    TriacDimmer::setBrightness(10, 1);
    return;
  }
  TriacDimmer::setBrightness(10, (float) fade_b_counter / FADE_TIME);
  Alarm.timerOnce(1, fade_b_in_helper);
}
void turn_a_off(){
  fade_a_cancel = true;
  TriacDimmer::setBrightness(9, 0);
}
void turn_b_off(){
  fade_b_cancel = true;
  TriacDimmer::setBrightness(10, 0);
}
void turn_off(){
  turn_a_off();
  turn_b_off();
}

void setupAlarms(){
  Alarm.alarmRepeat(0,0,0, fade_a_in);
  Alarm.alarmRepeat(0,0,0, fade_b_in);
  Alarm.alarmRepeat(0,0,0, turn_a_off);
  Alarm.alarmRepeat(0,0,0, turn_b_off);
  Alarm.disable(0);
  Alarm.disable(1);
  Alarm.disable(2);
  Alarm.disable(3);
}

// ===================== EEPROM SETUP ======================
#include <extEEPROM.h>
extEEPROM eep(kbits_32, 1, 32);

#include <time.h>
#include <util/eu_dst.h>
#include <util/usa_dst.h>

struct setting_t {
  struct id_t {
    const char signature[8] = "CLOCKCFG";
    const uint32_t version = ~ 0x00000000;
    const uint32_t length = sizeof(setting_t);
  };
  const id_t id;
  time_t alarm[4];
  int8_t timezone;
  size_t dst;
};

setting_t setting;

void save(){
  eep.begin(twiClock100kHz);
  eep.write(0, (byte*)&setting, sizeof(setting));

  update();
}

void reload(){
  setting_t::id_t id;
  eep.begin(twiClock100kHz);
  eep.read(0, (byte*)(&id), sizeof(setting_t::id_t));
  if(memcmp(&id, &setting.id, sizeof(setting_t::id_t)) != 0){
    Serial.printf("Bad settings file:\nneed:%-8s v. %d, <%d>\nhave:%-8s v. %d, <%d>",
        setting.id.signature, setting.id.version, setting.id.length, id.signature, id.version, id.length);
    return;
  }
  
  eep.read(0, (byte*)&setting, sizeof(setting));

  update();
}

void update(){
  set_zone(setting.dst * ONE_HOUR);
  switch(setting.dst){
    case 0: set_dst(nullptr); break;
    case 1: set_dst(usa_dst); break;
    case 2: set_dst(eu_dst); break;
  }
  for(uint8_t i = 0; i < 4; i++){
    Alarm.write(i, convertToTimeLib(setting.alarm[i]) % ONE_DAY);
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


const char* const adj2_names[] = {
  "Daylight Time",
  "Time Zone",
  "Back",
};

const char* const dst_names[] = {
  "None",
  "USA",
  "Europe"
};

const char* const dst_fmt = "DST: %-6s";
const char* const off_fmt = "Offset: GMT%+d";

ChoiceAdjustment adj_dst(&setting.dst, dst_names, 3, dst_fmt, "", highlight);
Adjuster<int8_t> adjr_tz(&setting.timezone, -11, 13, 1, true);
Adjustment<int8_t> adj_tz(&adjr_tz, off_fmt, "", highlight);
ExitAdjustment<EXIT_SAVE> adj_exit;
AdjustmentBase* adj2[] = {
  &adj_dst,
  &adj_tz,
  &adj_exit
};

PickAdjustment menu2(adj2, adj2_names, 3, LCD_CHARS);

const char* const adj_names[] = {
  "Alarm 1",
  "Alarm 2",
  "Alarm 3",
  "Alarm 4",
  "Settings",
  "Back",
};
const char* const time_fmt = "  %I:%M %p";
const char* const time_edit = "  00 11";
TimeAdjustment<false,false,false,true,true,false>
  adj_a0(&setting.alarm[0], time_fmt, time_edit, highlight),
  adj_a1(&setting.alarm[1], time_fmt, time_edit, highlight),
  adj_a2(&setting.alarm[2], time_fmt, time_edit, highlight),
  adj_a3(&setting.alarm[3], time_fmt, time_edit, highlight);
AdjustmentBase* adj[] = {
  &adj_a0,
  &adj_a1,
  &adj_a2,
  &adj_a3,
  &menu2,
  &adj_exit
};

PickAdjustment root(adj, adj_names, 6, LCD_CHARS);



// ============================ INPUT DEVICE CONFIG ==============================
#include <ClickEncoder.h>
ClickEncoder encoder(7, 6, 5, 4);

ISR(TIMER2_COMPA_vect) {
  serviceEncoder();
}

bool in_menu = false;
void serviceEncoder(){  
  encoder.service();
  int8_t v = encoder.getValue();
  ClickEncoder::Button b = encoder.getButton();
  
  if(!in_menu && (b == ClickEncoder::Open || b == ClickEncoder::Released)  && v == 0){
    return;
  }
  if(!in_menu){
    in_menu = true;
    return;
  }
  
  exit_t ev = EXIT;
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Pressed:
        break;
      case ClickEncoder::Held:
        ev = root.action(ACT_CTXT);
        break;
      case ClickEncoder::Released:
        break;
      case ClickEncoder::Clicked:
        ev = root.action(ACT_ENTER);
        break;
      case ClickEncoder::DoubleClicked:
        ev = root.action(ACT_BACK);
        break;
    }
  }
  if(v != 0){
    ev = root.action(ACT_CHANGE, v);
  }

  switch(ev){
  case EXIT_SAVE:
    in_menu = false;
  case NOEXIT_SAVE:
    save();
    break;
  case EXIT_CANCEL:
    in_menu = false;
  case NOEXIT_CANCEL:
    reload();
    break;
  case EXIT:
    //in_menu = false;
  case NOEXIT:
    break;
  }
}

void setupInput(){
  encoder.setAccelerationEnabled(false);
  
  TIMSK2 = 0; // disable interrupts
  TIFR2 = 0xff; // clear flags
  TCCR2A = _BV(WGM21);// CTC OCRA mode
  TCCR2B = _BV(CS22); // /64 prescaler
  OCR2A = 250; // 1ms intervals
  TIMSK2 |= _BV(OCIE2A); // enable OCRA interrupt
}




void setup() {
  Serial.begin(115200);
  setupLCD();
  setupDimmer();
  setupRTC();
  setupAlarms();
  setupInput();
  reload();

  if(timeStatus()!= timeSet) 
     Serial.println(F("Unable to sync with the RTC"));
  else
     Serial.println(F("Time Synced!"));
}

void loop() {
  
  if (Serial.available()) {
    processSyncMessage();
  }
  
  char buf[LCD_LINES*LCD_CHARS+1];
  memset(buf, 0x20, LCD_LINES*LCD_CHARS+1);

  if(in_menu){
    lcd.setBacklight(255);
    root.full_string(buf, LCD_LINES*LCD_CHARS);
  } else {
    time_t t = convertToTimeH(now());
    strftime(buf, LCD_LINES*LCD_CHARS+1, "   %I:%M:%S %p  %a %b %d, %Y", localtime(&t));
  }

//  Serial.printf(F("now: %d\n"), now());
//  Serial.printf(F("now: %d\n"), convertToTimeH(now()));
//  time_t t = convertToTimeH(now());
//  strftime(buf, 9, "%H:%M:%S", localtime(&t));
//  Serial.println(buf);
  
  
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
  
  Serial.printf(F("free: %d\n"), freeMemory());
  Alarm.delay(10);
}


void digitalClockDisplay(Print& p){
  // digital clock display of the time
  p.print(hour());
  printDigits(p, minute());
  printDigits(p, second());
  p.print(" ");
  p.print(day());
  p.print(" ");
  p.print(month());
  p.print(" ");
  p.print(year()); 
  p.println(); 
}

void printDigits(Print& p, int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  p.print(":");
  if(digits < 10)
    p.print('0');
  p.print(digits);
}

