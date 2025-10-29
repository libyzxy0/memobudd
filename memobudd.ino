#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>
#include <debounce.h>
#include <LITTLEFS.h>
#include <ArduinoJson.h>

#define FORMAT_LITTLEFS_IF_FAILED false

String wifi_ssid;
String wifi_password;
String wifi_ap_ssid;
String wifi_ap_password;

/* BUTTON PINS */
#define UP_BTN 4
#define ENTER_BTN 16
#define DOWN_BTN 17

/* BUZZER AND LED PINS */
#define BUZZER 5
#define LED_BUILTIN 2

/* DISPLAY SIZE */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

DebouncedButton up_button;
DebouncedButton down_button;
DebouncedButton enter_button;
const unsigned long debounceDelay = 50;

const int WAKEUP_PIN = 4;

/* MENU STATES */
int current_menu_page = 0;
int current_menu_index = 0;
int selected_page = 0;

const char* menu_items[] = {
  "QUICK NOTES",
  "POMODORO TIMER",
  "FLASH CARDS",
  "REMINDERS",
  "BIBLE VERSE",
  "MEMOBUDD QUOTES",
  "SETTINGS"
};

const int menu_length = sizeof(menu_items) / sizeof(menu_items[0]);

/* POMODORO STATES */
bool pomodoro_break = false;
bool pomodoro_paused = false;
bool pomodoro_started = false;
int pomodoro_cycle = 0;
int pomodoro_seconds = 0;
int pomodoro_minutes = 0;
int pomodoro_selected = 0;
int current_pomodoro_index = 0;
const long pomodoro_timer_interval = 1000;
unsigned long previous_pomodoro_millis = 0;

typedef struct {
    int work_time;
    int break_time;
    int long_break_time;
    char label;
} PomodoroPreset;

PomodoroPreset pomodoro_presets[] = {
    {25, 5, 15, 'S'},
    {15, 3, 5, 'L'},
    {40, 10, 30, 'F'}
};
const int pomodoro_options_length = sizeof(pomodoro_presets) / sizeof(pomodoro_presets[0]);

/* DISPLAY OBJECT */
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ========== START UTILS ========== */
void centered_x(const char* text, int y, int textSize = 1) {
  display.setTextSize(textSize);
  int16_t x1,
  y1;
  uint16_t w,
  h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void back_menu() {
  selected_page = 0;
}

void exit_screen(char* pagename) {
  display.clearDisplay();
  display.fillRect(26, 5 - 2, 74, 11, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  centered_x("CLICK ENTER", 5, 1);
  display.setTextColor(SH110X_WHITE);

  centered_x("EXIT", 20, 3);
  centered_x(pagename, 53, 1);
  display.display();

  if(debounce(enter_button)) {
    back_menu();
    current_pomodoro_index = 0;
  }
}

void set_pomodoro_timer(int minutes, int seconds) {
  pomodoro_minutes = minutes;
  pomodoro_seconds = seconds;
  if (pomodoro_seconds >= 60) {
    pomodoro_minutes += pomodoro_seconds / 60;
    pomodoro_seconds %= 60;
  }
}

void fs_write_file(fs::FS &fs, const char * path, const char * data){
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to write file.");
    return;
  }
  if(file.print(data)){
    Serial.println("File written");
  } else {
    Serial.println("Failed to sulat.");
  }
  file.close();
}

void fs_delete_file(fs::FS &fs, const char * path){
  if(fs.remove(path)){
    Serial.println("File deleted");
  } else {
    Serial.println("Failed to delete.");
  }
}

String fs_read_file(fs::FS &fs, const char * path) {
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("Failed to read file.");
    return "";
  }
  String data = "";
  while (file.available()) {
    data += (char)file.read();
  }
  file.close();
  return data;
}

void set_config(const char* ssid, const char* password, const char* ap_ssid, const char* ap_password) {
  DynamicJsonDocument newConf(256);
    newConf["wifi_ssid"] = String(ssid);
    newConf["wifi_password"] = String(password);
    newConf["wifi_ap_ssid"] = String(ap_ssid);
    newConf["wifi_ap_password"] = String(ap_password);
    
    char output[256];
    serializeJson(newConf, output);
    fs_write_file(LITTLEFS, "/config.json", output);
    
    wifi_ssid = String(ssid);
    wifi_password = String(password);
    wifi_ap_ssid = String(ap_ssid);
    wifi_ap_password = String(ap_password);
}

/* ========== END UTILS ========== */

/* ========== SCREENS ========== */
void (*screens[])() = {
  menu_screen,
  quick_notes_screen,
  pomodoro_timer_screen,
  flashcard_screen,
  reminders_screen,
  bible_verse_screen,
  quotes_screen,
  settings_screen
};

/* ========== MENU SCREEN START ========== */
void menu_screen() {
  display.clearDisplay();
  
  if(selected_page != 0) return;
  
  if(!digitalRead(ENTER_BTN)) {
    if(!digitalRead(UP_BTN)) {
      ESP.restart();
      return;
    }
    if(!digitalRead(DOWN_BTN)) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("Turning off...");
      display.display();
      delay(1000);
      display.clearDisplay();
      display.display();
      delay(1000);
      esp_deep_sleep_start();
      return;
    }
  }
  
  
  
  if(debounce(up_button)) {
    if (current_menu_index > 0) {
      current_menu_index--;
    } else if (current_menu_page > 0) {
      current_menu_page--;
      current_menu_index = 3;
    }
  }
  if(debounce(down_button)) {
    int startIndex = current_menu_page * 4;
    int endIndex = min(startIndex + 4, menu_length);
    if (current_menu_index < 3 && startIndex + current_menu_index + 1 < menu_length) {
      current_menu_index++;
    } else if (endIndex < menu_length) {
      current_menu_page++;
      current_menu_index = 0;
    }
  }
  if(debounce(enter_button)) {
    int selectedIndex = current_menu_page * 4 + current_menu_index;
    selected_page = selectedIndex + 1;
  }

  int startIndex = current_menu_page * 4;
  int endIndex = min(startIndex + 4, menu_length);
  display.clearDisplay();
  display.setCursor(0, 1);
  display.print("MEMOBUDD MENU");
  display.fillRect(114, 0, 20, 9, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(116, 1);
  display.print("V1");
  display.setTextColor(SH110X_WHITE);

  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  for (int i = startIndex; i < endIndex; i++) {
    int yPos = 15 + (i - startIndex) * 13;
    if (i == startIndex + current_menu_index) {
      display.fillRect(0, yPos - 2, SCREEN_WIDTH, 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
      display.setCursor(4, yPos);
      display.print(menu_items[i]);
      display.setTextColor(SH110X_WHITE);
    } else {
      display.setCursor(4, yPos);
      display.print(menu_items[i]);
    }
  }
  display.display();
}
/* ========== MENU SCREEN END ========== */

/* ========== QUICK NOTES SCREEN START ========== */
void quick_notes_screen() {
  display.clearDisplay();
  if(selected_page != 1) return;
  
  if(debounce(up_button)) {}
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {
    back_menu();
  }


  display.setCursor(0, 0);
  display.print("Quick Notes");
  display.setCursor(0, 12);
  display.print("Press ENTER to go back");
  display.display();
}
/* ========== QUICK NOTES SCREEN END ========== */

/* ========== POMODORO TIMER SCREEN START ========== */
void pomodoro_timer_screen() {
  if(pomodoro_started && !pomodoro_paused) {
    unsigned long current_millis = millis();
    if(current_millis - previous_pomodoro_millis >= pomodoro_timer_interval) {
      previous_pomodoro_millis = current_millis;
      pomodoro_seconds--;
      if(pomodoro_seconds < 0) {
        pomodoro_seconds = 59;
        pomodoro_minutes--;
        if(pomodoro_minutes < 0) {
          /* COUNTDOWN ENDS */
          pomodoro_minutes = 0;
          pomodoro_seconds = 0;
          if(pomodoro_break) {
            set_pomodoro_timer(pomodoro_presets[current_pomodoro_index].work_time, 00);
            pomodoro_break = false;
            pomodoro_cycle++;
            digitalWrite(BUZZER, HIGH);
            delay(1000);
            digitalWrite(BUZZER, LOW);
          } else {
            if(pomodoro_cycle != 0 && pomodoro_cycle % 4 == 0) {
              set_pomodoro_timer(pomodoro_presets[current_pomodoro_index].long_break_time, 00);
              pomodoro_break = true;
              digitalWrite(BUZZER, HIGH);
              delay(500);
              digitalWrite(BUZZER, LOW);
              delay(100);
              digitalWrite(BUZZER, HIGH);
              delay(500);
              digitalWrite(BUZZER, LOW);
            } else {
              set_pomodoro_timer(pomodoro_presets[current_pomodoro_index].break_time, 00);
              pomodoro_break = true;
              
              digitalWrite(BUZZER, HIGH);
              delay(50);
              digitalWrite(BUZZER, LOW);
              delay(50);
              digitalWrite(BUZZER, HIGH);
              delay(50);
              digitalWrite(BUZZER, LOW);
              delay(50);
              digitalWrite(BUZZER, HIGH);
              delay(50);
              digitalWrite(BUZZER, LOW);
            }
          }
        }
      }
    }
  }

  if(debounce(up_button)) {
    if(pomodoro_started) {
      set_pomodoro_timer(pomodoro_presets[current_pomodoro_index].work_time, 00);
      pomodoro_cycle = 0;
      pomodoro_break = false;
      return;
    }
    if(current_pomodoro_index > 0) {
      current_pomodoro_index--;
    }
  }

  if(debounce(down_button)) {
    if(pomodoro_started) {
      pomodoro_started = false;
      return;
    }
    if(current_pomodoro_index < pomodoro_options_length) {
      current_pomodoro_index++;
    } else {
      pomodoro_started = false;
    }
  }

  if(current_pomodoro_index == pomodoro_options_length) {
    exit_screen("POMODORO TIMER");
    return;
  }

  if(debounce(enter_button)) {
    if(!pomodoro_started) {
      set_pomodoro_timer(pomodoro_presets[current_pomodoro_index].work_time, 00);
    } else {
      pomodoro_paused = !pomodoro_paused;
      digitalWrite(BUZZER, HIGH);
      delay(50);
      digitalWrite(BUZZER, LOW);
    }
    pomodoro_started = true;
    pomodoro_selected = current_pomodoro_index;
  }

  if(selected_page != 2) return;

  display.clearDisplay();

  display.setCursor(0, 1);
  display.print("POMODORO TIMER ");
  display.print(pomodoro_presets[current_pomodoro_index].label);

  if(pomodoro_started) {
    display.fillRect(119, 0, 9, 9, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(121, 1);
    display.print(pomodoro_cycle);
    display.setTextColor(SH110X_WHITE);

    display.setTextSize(3);
    display.setCursor(20, 22);
    if(pomodoro_minutes < 10) display.print("0");
    display.print(pomodoro_minutes);
    display.print(":");
    if(pomodoro_seconds < 10) display.print("0");
    display.print(pomodoro_seconds);

    display.setTextSize(1);
    display.setTextColor(SH110X_BLACK);

    if(pomodoro_paused) {
      display.fillRect(97, 56 - 1, 35, 9, SH110X_WHITE);
      display.setCursor(98, 56);
      display.print("PAUSE");
    } else if (pomodoro_break) {
      display.fillRect(97, 56 - 1, 35, 9, SH110X_WHITE);
      display.setCursor(98, 56);
      display.print("BREAK");
    } else {
      display.fillRect(103, 56 - 1, 30, 9, SH110X_WHITE);
      display.setCursor(104, 56);
      display.print("WORK");
    }

    display.setTextColor(SH110X_WHITE);

    display.setCursor(0, 56);
    display.print(pomodoro_cycle != 0 && pomodoro_cycle % 4 == 0 ? "LB": "SB");
    display.setTextColor(SH110X_WHITE);

    display.display();
  } else {
    display.setTextSize(2);
    display.setCursor(pomodoro_presets[current_pomodoro_index].work_time >= 10 ? 31: 38, 22);
    display.print(String(pomodoro_presets[current_pomodoro_index].work_time));
    display.setCursor(pomodoro_presets[current_pomodoro_index].break_time >= 10 ? 77: 84, 22);
    display.print(String(pomodoro_presets[current_pomodoro_index].break_time));
    display.fillRect(25, 43 - 1, 34, 10, SH110X_WHITE);
    display.fillRect(71, 43 - 1, 36, 10, SH110X_WHITE);
    display.setTextSize(1);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(30, 43);
    display.print("WORK");
    display.setCursor(74, 43);
    display.print("BREAK");
    display.setTextColor(SH110X_WHITE);
    display.display();
  }
}
/* ========== POMODORO TIMER SCREEN END ========== */

/* ========== FLSHCARD SCREEN START ========== */
void flashcard_screen() {
  display.clearDisplay();
  if(selected_page != 3) return;
  
  if(debounce(up_button)) {}
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {
    back_menu();
  }

  display.setCursor(0, 0);
  display.print("FLASHCARD");
  display.setCursor(0, 12);
  display.print("Press ENTER to go back");
  display.display();
}
/* ========== FLASHCARD SCREEN END ========== */

/* ========== REMINDERS SCREEN START ========== */
void reminders_screen() {
  display.clearDisplay();
  if(selected_page != 4) return;
  
  if(debounce(up_button)) {}
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {
    back_menu();
  }

  display.setCursor(0, 0);
  display.print("Reminders");
  display.setCursor(0, 12);
  display.print("Press ENTER to go back");
  display.display();
}
/* ========== REMINDERS SCREEN END ========== */

/* ========== BIBLE VERSE SCREEN START ========== */
void bible_verse_screen() {
  display.clearDisplay();

  if(selected_page != 5) return;
  
  if(debounce(up_button)) {}
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {
    back_menu();
  }

  display.setCursor(0, 0);
  display.print("Bible Verse");
  display.setCursor(0, 12);
  display.print("Press ENTER to go back");
  display.display();
} 
/* ========== BIBLE VERSE SCREEN END ========== */

/* ========== QUOTES SCREEN START ========== */
void quotes_screen() {
  display.clearDisplay();

  if(selected_page != 6) return;
  
  if(debounce(up_button)) {}
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {
    back_menu();
  }

  display.setCursor(0, 0);
  display.print("HALUUU");
  display.display();
}
/* ========== QUOTES SCREEN END ========== */

void settings_screen() {
  display.clearDisplay();
  if(selected_page != 7) return;
  
  if(debounce(up_button)) {
    back_menu();
  }
  
  if(debounce(down_button)) {}
  if(debounce(enter_button)) {}

  String config = fs_read_file(LITTLEFS, "/config.json");

  display.setCursor(0, 0);
  display.print("Configure");
  display.setCursor(0, 12);
  display.print(config);
  display.display();
}

/* ========== SETUP AND LOOP FUNCTION BELOW MAIN ========== */
void setup() {
  Wire.begin();
  Serial.begin(115200);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(ENTER_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  /* WAKE UP BUTTON */
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, LOW);

  initDebouncedButton(up_button, UP_BTN, debounceDelay);
  initDebouncedButton(down_button, DOWN_BTN, debounceDelay);
  initDebouncedButton(enter_button, ENTER_BTN, debounceDelay);

  if (!display.begin(0x3C, true)) {
    for (;;);
  }
  
  if(!LITTLEFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed!");
    return;
  }

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  
  /* INITIALIZE CONFIGURATIONS */
  String config = fs_read_file(LITTLEFS, "/config.json");
  
  if(config == "") {
    Serial.println("Failed to read configurations");
    display.print("Failed to read configurations.");
    return;
  } 
  
  if (config != "") {
    DynamicJsonDocument conf(512);
    DeserializationError error = deserializeJson(conf, config);
    if(error) {
      Serial.println("Failed to read config file.");
      return;
    }
    wifi_ssid = conf["wifi_ssid"].as<String>();
    wifi_password = conf["wifi_password"].as<String>();
    wifi_ap_ssid = conf["wifi_ap_ssid"].as<String>();
    wifi_ap_password = conf["wifi_ap_password"].as<String>();
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(wifi_ssid);
  display.print(":");
  display.print(wifi_password);
  display.display();

  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  display.setCursor(0, 12);
  display.print("Connecting to WiFi...");
  display.display();
  int attempts = 0;

  display.fillRect(10, 38 - 1, SCREEN_WIDTH - 20, 1, SH110X_WHITE);
  display.fillRect(10, 38 + 10, SCREEN_WIDTH - 20, 1, SH110X_WHITE);
  display.fillRect(10, 38, 1, 10, SH110X_WHITE);
  display.fillRect(SCREEN_WIDTH - 11, 38, 1, 10, SH110X_WHITE);

  while (WiFi.status() != WL_CONNECTED && attempts < 5) {
    delay(500);
    Serial.print(".");
    display.fillRect(10, 38, attempts * ((SCREEN_WIDTH - 20) / 4), 10, SH110X_WHITE);
    display.display();
    attempts++;
  }

  display.clearDisplay();
  String creds = String(wifi_ssid) + ":" + String(wifi_password);
  centered_x(creds.c_str(), 48, 1);
  display.display();
  
  if(attempts == 5 && WiFi.status() != WL_CONNECTED) {
    display.setCursor(0, 12);
    centered_x("Failed to connect!", 22, 1);
    display.display();
    delay(1000);
  } else {
    centered_x("CONNECTED", 20, 2);
    display.display();
  }

  display.setTextSize(1);
  display.clearDisplay();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP().toString());
    ArduinoOTA.setHostname("libyzyx0_esp32");
    ArduinoOTA.onStart([]() {
      Serial.println("Started!");
      display.clearDisplay();
      centered_x("OTA_MODE", 20, 2);
      centered_x("Uploading sketch...", 48, 1);
      display.display();
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("Done!");
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.println("Error!");
    });
    ArduinoOTA.begin();
  } else {
    Serial.println("WiFi connection failed");
    esp_wifi_stop();
  }
}

void loop() {
  screens[selected_page]();
  ArduinoOTA.handle();
}