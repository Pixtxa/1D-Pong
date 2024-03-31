/*
  "1D Pong"

  Its a 1D Pong Game on 44 WERMA MC35 Beacons

  based on 1D Pong by FlyingAngel - 18.4.2020
  Pixtxa - 31.03.2024
*/

/*
Arduino ESP32-S2 D1mini settings:
Board: "ESP32S2 Dev Module"
USB CDC On Boot: "Enabled"
CPU Frequency: "240MHz (WiFi)'
Core Debug Level: "None"
USB DFU On Boot: "Disabled"
Erase All Flash Before Sketch Upload: "Disabled"
Flash Frequency: "80MHz"
Flash Mode: "QIO"
Flash Size: "4MB (32Mb)"
JTAG Adapter: "Disabled"
USB Firmware MSC On Boot: "Disabled"
Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
PSRAM: "Disabled"
Upload Mode: "Internal USB"
Upload Speed: "921600"
*/

//FOTA
const char *wifi_ssid = "vspace.one";
const char *wifi_password = "12345678";
const char *hostname = "1D-Pong";
const char *soft_ap_password = "letmeinletmeinletmein";
const char *update_pw = "GetThisUpdateDoneRightNow1";

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// LEDs
#define FASTLED_INTERNAL  // Disable version number message in FastLED library (looks like an error)
#include <FastLED.h>
#include "FastLED_RGBW.h"  //https://www.partsnotincluded.com/fastled-rgbw-neopixels-sk6812/

#include "SPI.h"

enum {
  COLOR_BLACK = 0,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_ORANGE,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE
};

byte rainbow_color[] = {
  COLOR_RED,
  COLOR_ORANGE,
  COLOR_GREEN,
  COLOR_CYAN,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_WHITE,
  COLOR_BLACK
};

// *********************************
// Hardware settings
// *********************************
// Buttons
byte player_btn_pin[] = { 33, 15 };  // pins for buttons

// Shift registers
#define NUM_PIXELS 44  // number of werma mc35 leds
#define D_IN 0
#define CLK 35
#define D_OUT 37
#define LATCH 39
#define OE 40

// LEDs
#define DATA_PIN 18
#define NUM_LEDS ((1 * 4 + NUM_PIXELS * 3 + 1 * 4) / 4)   //1x RGBW, 44x GRB, 1x RGBW in RGBW space
byte player_btn_led[] = { 0, (1 + NUM_PIXELS * 3 / 4) };  // LEDs for buttons


// *********************************
// default game settings
// *********************************
#define CONFIG_VERSION 1                          // Change to delete locally saved settings
byte bg_color = COLOR_BLACK;                      // background color
byte ball_color = COLOR_WHITE;                    // ball color
byte goal_color = COLOR_GREEN;                    // color of the hitting goal
byte player_color[] = { COLOR_BLUE, COLOR_RED };  // colors of the players
byte beep_time = 2;                               // time to beep on each hit
byte pcb_brightness = 10;                         // brightness of debug leds
int game_speed_min = 50;                          // min game-speed
int game_speed_max = 15;                          // max game-speed
int game_speed_step = 2;                          // fasten up when change direction
int ball_speed_max = 9;                           // max ball-speed
int ball_boost_0 = 25;                            // superboost on last position
int ball_boost_1 = 15;                            // boost on forelast position
int win_rounds = 5;                               // x winning rounds for winning game
int end_zone_size = 6;                            // size endzone

// *********************************
// Definition System-Variablen
// *********************************

enum {
  PLAYER_1 = 0,
  PLAYER_2
};

unsigned long previous_button_millis = 0;  // time of last button-press
unsigned long last_hit_millis[2] = { 0 };  // time of last button-press
int player_button_pressed[2];              // ball-position where button was pressed; „-1“ button not pressed
int previous_button_pos = -1;              // position of last button-press
byte previous_button;                      // last Button-press
byte player_score[2] = { 0 };              // actual Score
byte player_start;                         // who starts game
int game_speed;                            // actual game-speed
int ball_dir = 1;                          // direction, ball is moving (+/- 1)
int ball_pos;                              // actual ball-position
int ball_speed = 50;                       // actual ball-speed (higher = slower)

CRGBW leds[NUM_LEDS];
CRGB *leds_rgb = (CRGB *)&leds[0];
CRGB *leds_pcb = (CRGB *)&leds[1];

byte werma[NUM_PIXELS] = { 0 };

// Set Werma MC35 and neopixels
void SetLeds() {
  digitalWrite(LATCH, LOW);
  for (uint8_t i = 0; i < NUM_PIXELS; i++) {
    if (i % 2 == 0) {
      SPI.transfer((werma[NUM_PIXELS - 1 - i] & 0x0F) << 4 | werma[NUM_PIXELS - 2 - i] & 0x0F);
    }
    leds_pcb[i] = CRGB(((werma[i] & COLOR_GREEN) > 0) * pcb_brightness, ((werma[i] & COLOR_RED) > 0) * pcb_brightness, ((werma[i] & COLOR_BLUE) > 0) * pcb_brightness);
  }
  digitalWrite(LATCH, HIGH);
  FastLED.show();
}

// Set RGBW leds in button
void SetButton(byte button, byte color, byte brightness) {
  // Use white for glow after hit
  byte white = 0;
  if (last_hit_millis[button] + 511 > millis()) {
    white = ((last_hit_millis[button] + 511) - millis()) / 2;
  }

  // Set RGB to color
  switch (color) {
    case COLOR_RED:
      leds[player_btn_led[button]] = CRGBW(255 * brightness / 255, 0, 0, white);
      break;
    case COLOR_GREEN:
      leds[player_btn_led[button]] = CRGBW(0, 255 * brightness / 255, 0, white);
      break;
    case COLOR_BLUE:
      leds[player_btn_led[button]] = CRGBW(0, 0, 255 * brightness / 255, white);
      break;
    case COLOR_ORANGE:
      leds[player_btn_led[button]] = CRGBW(207 * brightness / 255, 47 * brightness / 255, 0, white);
      break;
    case COLOR_CYAN:
      leds[player_btn_led[button]] = CRGBW(0, 127 * brightness / 255, 127 * brightness / 255, white);
      break;
    case COLOR_MAGENTA:
      leds[player_btn_led[button]] = CRGBW(127 * brightness / 255, 0, 127 * brightness / 255, white);
      break;
    case COLOR_WHITE:
      leds[player_btn_led[button]] = CRGBW(85 * brightness / 255, 85 * brightness / 255, 85 * brightness / 255, white);
      break;
    case COLOR_BLACK:
    default:
      leds[player_btn_led[button]] = CRGBW(0, 0, 0, white);
      break;
  }
}

// *********************************
// Setup
// *********************************
void setup() {
  // disable outputs
  digitalWrite(OE, HIGH);
  pinMode(OE, OUTPUT);

  // init Buttons
  pinMode(player_btn_pin[PLAYER_1], INPUT_PULLUP);
  pinMode(player_btn_pin[PLAYER_2], INPUT_PULLUP);

  // init WS2812B
  FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds_rgb, getRGBWsize(NUM_LEDS));
  FastLED.setBrightness(255);

  // init SPI
  pinMode(LATCH, OUTPUT);
  SPI.begin(CLK, D_IN, D_OUT, LATCH);
  //  SPI.setClockDivider(SPI_CLOCK_DIV2);//divide the clock by 2 => 8 MHz
  SPI.setClockDivider(SPI_CLOCK_DIV128);  //divide the clock by 128 => 125 kHz
  //SPI.setDataMode(SPI_MODE0);

  // FOTA
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAP(hostname, soft_ap_password);
  WiFi.setHostname(hostname);
  WiFi.begin(wifi_ssid, wifi_password);

  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword(update_pw);
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else  // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      memset(werma, COLOR_BLACK, NUM_PIXELS);
      SetButton(PLAYER_1, COLOR_BLACK, 0);
      SetButton(PLAYER_2, COLOR_BLACK, 0);
      SetLeds();
    })
    .onEnd([]() {
      memset(werma, COLOR_GREEN, NUM_PIXELS);
      SetLeds();
      delay(1000);
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      memset(werma, COLOR_BLACK, NUM_PIXELS);
      memset(werma, COLOR_WHITE, (progress / (total / NUM_PIXELS)));
      SetLeds();
    })
    .onError([](ota_error_t error) {
      if (error == OTA_AUTH_ERROR) memset(werma, COLOR_MAGENTA, NUM_PIXELS);
      else if (error == OTA_BEGIN_ERROR)memset(werma, COLOR_RED, NUM_PIXELS);
      else if (error == OTA_CONNECT_ERROR) memset(werma, COLOR_BLUE, NUM_PIXELS);
      else if (error == OTA_RECEIVE_ERROR) memset(werma, COLOR_CYAN, NUM_PIXELS);
      else if (error == OTA_END_ERROR) memset(werma, COLOR_ORANGE, NUM_PIXELS);
      SetLeds();
    });
  ArduinoOTA.begin();

  // deactivate beacons
  memset(werma, 0, NUM_PIXELS);
  SetLeds();
  digitalWrite(OE, LOW);  // enable outputs
}

enum {
  S_START = 0,
  S_SET_WIN_ROUNDS,
  S_IDLE,
  S_END_TRANSITION,
  S_START_GAME,
  S_INIT_PLAYERS,
  S_GAME_LOOP,
  S_CHECK_SCORE,
  S_BLINK_SCORE,
  S_CHECK_WINNER,
  S_WON
};

int state = S_START;
uint32_t sub_state = 0;
int old_state = -1;
int pos = 0;
unsigned long previous_millis = 0;
unsigned long reset_millis = 0;
unsigned long beep_millis = 0;
int beep_pos = -1;
int lost = 0;

// *********************************
// Loop
// *********************************
void loop() {
  ArduinoOTA.handle();
  if (old_state != state) {
    old_state = state;
    sub_state = 0;
    previous_millis = millis();
  }

  switch (state) {
    case S_START:
      memset(player_score, 0, sizeof(player_score));  // reset all scores
      lost = 0;
      // wait for both buttons to be released
      if (digitalRead(player_btn_pin[PLAYER_1]) == HIGH && digitalRead(player_btn_pin[PLAYER_2]) == HIGH) {
        state = S_IDLE;
      } else {
        memset(werma, COLOR_WHITE, NUM_PIXELS);
        SetButton(PLAYER_1, COLOR_WHITE, 255);
        SetButton(PLAYER_2, COLOR_WHITE, 255);
      }
      if (digitalRead(player_btn_pin[PLAYER_1]) == LOW && digitalRead(player_btn_pin[PLAYER_2]) == LOW) {
        if ((millis() - previous_millis) > 2000) {
          state = S_SET_WIN_ROUNDS;
        }
      } else {
        previous_millis = millis();
      }
      break;
    case S_SET_WIN_ROUNDS:
      win_rounds = 1 + (((millis() - previous_millis) / 300) % (NUM_PIXELS / 2 - end_zone_size));
      if (sub_state != win_rounds) {
        if (sub_state > win_rounds) {
          if (beep_time != 0) {
            beep_time = 0;
          } else {
            beep_time = 2;
          }
        }
        sub_state = win_rounds;
        memset(player_score, win_rounds, sizeof(player_score));
        GeneratePlayField();
        if (beep_time) {
          werma[NUM_PIXELS / 2 + 1 - win_rounds] |= 0b1000;
          werma[NUM_PIXELS / 2 + win_rounds] |= 0b1000;
          SetLeds();
          werma[NUM_PIXELS / 2 + 1 - win_rounds] &= 0b111;
          werma[NUM_PIXELS / 2 + win_rounds] &= 0b111;
          SetLeds();
        }
      }
      if (digitalRead(player_btn_pin[PLAYER_1]) == HIGH || digitalRead(player_btn_pin[PLAYER_2]) == HIGH) {
        delay(200);
        state = S_START;
      }
      break;
    case S_IDLE:
      // idle animation + Reset when all LEDs set to white
      if (previous_millis + 20 < millis()) {
        previous_millis = millis();
        sub_state = sub_state % (7 * (NUM_PIXELS + 2) * 2);
        byte color = rainbow_color[(sub_state / (NUM_PIXELS + 2)) % 7];
        switch (sub_state % ((NUM_PIXELS + 2) * 2)) {
          case 0:
            //beep_pos = 0;
            //beep_millis = millis();
          case (NUM_PIXELS * 2 + 3):
            SetButton(0, color, 255);
            break;

          case 1 ... NUM_PIXELS:
            werma[(sub_state % ((NUM_PIXELS + 2) * 2)) - 1] = color;
            break;

          case (NUM_PIXELS + 2):
            //beep_pos = NUM_PIXELS - 1;
            //beep_millis = millis();
          case (NUM_PIXELS + 1):
            SetButton(1, color, 255);
            // reset scores if idle animation runs too long
            if (color == COLOR_WHITE) {
              memset(player_score, 0, sizeof(player_score));
            }
            break;

          default:
            werma[90 - (sub_state % ((NUM_PIXELS + 2) * 2))] = color;
            break;
        }
        sub_state++;
      }

      // Transition to game view on button press. If no game is running, first move is given to the player who pesses first, except the other player presses the button while the transition is running
      uint8_t transition_leds[2];
      for (int player = PLAYER_1; player <= PLAYER_2; player++) {
        if (((millis() - last_hit_millis[player]) / 5) <= NUM_PIXELS + 2) {
          transition_leds[player] = (millis() - last_hit_millis[player]) / 5;
          SetButton(player, player_color[player], 255);
        } else {
          transition_leds[player] = 0;
          if (digitalRead(player_btn_pin[player]) == LOW) {
            last_hit_millis[player] = millis();
            previous_button = player;
            if ((player_score[PLAYER_1] + player_score[PLAYER_2]) == 0) {
              player_start = player;
            }
          }
        }
      }

      if (transition_leds[PLAYER_1] || transition_leds[PLAYER_2]) {
        if (transition_leds[PLAYER_1] + transition_leds[PLAYER_2] > NUM_PIXELS) {
          state = S_END_TRANSITION;
          break;
        }
        uint8_t backup_werma[NUM_PIXELS];
        memcpy(backup_werma, werma, NUM_PIXELS);
        GeneratePlayField();
        memcpy(&werma[transition_leds[PLAYER_1]], &backup_werma[transition_leds[PLAYER_1]], NUM_PIXELS - transition_leds[PLAYER_1] - transition_leds[PLAYER_2]);
      }
      break;
    case S_END_TRANSITION:
      // Transition between idle animation and game start
      GeneratePlayField();
      SetButton(PLAYER_1, player_color[PLAYER_1], 255);
      SetButton(PLAYER_2, player_color[PLAYER_2], 255);

      //wait for both buttons to be released
      if (digitalRead(player_btn_pin[PLAYER_1]) == HIGH && digitalRead(player_btn_pin[PLAYER_2]) == HIGH) {
        state = S_START_GAME;
      }
      break;
    case S_START_GAME:
      game_speed = game_speed_min;                                       // set starting game speed
      ball_speed = game_speed;                                           // set starting ball speed
      memset(player_button_pressed, -1, sizeof(player_button_pressed));  // clear keypress
      previous_button_pos = -1;
      beep_pos = -1;
      lost = 0;
      state = S_INIT_PLAYERS;
      break;
    case S_INIT_PLAYERS:
      GeneratePlayField();
      if (player_start == PLAYER_1) {
        ball_dir = 1;  // set ball direction
        ball_pos = 0;  // set startposition of ball

        if (digitalRead(player_btn_pin[PLAYER_1]) == LOW) {
          state = S_GAME_LOOP;
          last_hit_millis[PLAYER_1] = millis();
        }

        SetButton(PLAYER_1, player_color[PLAYER_1], 255);
        SetButton(PLAYER_2, player_color[PLAYER_2], 0);
      } else {
        ball_dir = -1;              // set ball direction
        ball_pos = NUM_PIXELS - 1;  // set startposition of ball

        if (digitalRead(player_btn_pin[PLAYER_2]) == LOW) {
          state = S_GAME_LOOP;
          last_hit_millis[PLAYER_2] = millis();
        }

        SetButton(PLAYER_1, player_color[PLAYER_1], 0);
        SetButton(PLAYER_2, player_color[PLAYER_2], 255);
      }
      if (previous_millis + 10000 < millis()) {
        state = S_IDLE;
      }
      break;
    case S_GAME_LOOP:
      if ((millis() - previous_millis > ball_speed) || (sub_state == 0)) {
        previous_millis = millis();

        GeneratePlayField();

        if (sub_state == 0) {
          sub_state++;
          beep_pos = ball_pos;
          beep_millis = millis();
        } else {
          ball_pos += ball_dir;
          if (ball_pos < 0 || ball_pos >= NUM_PIXELS)  // ball left endzone?
          {
            state = S_CHECK_SCORE;
            break;
          }
        }
        werma[ball_pos] = ball_color;  // generate ball
      }

      if (ball_dir == 1) {
        SetButton(PLAYER_1, player_color[PLAYER_1], 64);
        if (player_button_pressed[PLAYER_2] == -1) {
          SetButton(PLAYER_2, player_color[PLAYER_2], 255);
        } else {
          SetButton(PLAYER_2, player_color[PLAYER_2], 64);
          lost = 1;
        }
      } else {
        SetButton(PLAYER_2, player_color[PLAYER_2], 64);
        if (player_button_pressed[PLAYER_1] == -1) {
          SetButton(PLAYER_1, player_color[PLAYER_1], 255);
        } else {
          SetButton(PLAYER_1, player_color[PLAYER_1], 64);
          lost = 1;
        }
      }

      for (int i = PLAYER_1; i <= PLAYER_2; i++) {
        // player pressed button?
        if (player_button_pressed[i] == -1 && digitalRead(player_btn_pin[i]) == LOW && (ball_dir + 1) / 2 == i)
        // (ball_dir + 1) / 2 == i  -->  TRUE, when:
        // ball_dir == -1  AND  i = 0  -->  player 0 is active player
        // ball_dir == +1  AND  i = 1  -->  player 1 is active player
        // only the button-press of the active player is stored
        {
          if ((i==PLAYER_1 && ball_pos < (NUM_PIXELS/2+2)) || (i==PLAYER_2 && ball_pos >= (NUM_PIXELS/2-2))) //limit players range to half the field + 2 pixels
          {
            player_button_pressed[i] = ball_pos;  //store position of pressed button
            previous_button_pos = ball_pos;
            previous_button = i;
            previous_button_millis = millis();  // store time when button was pressed
            last_hit_millis[i] = millis();
            beep_pos = ball_pos;
            beep_millis = millis();
          }
        }
      }

      // fix positions of keypress for testing
      // if (ball_pos == 3) player_button_pressed[PLAYER_1] = 3;
      // if (ball_pos == 59) player_button_pressed[PLAYER_2] = 59;

      if (ball_dir == -1 && player_button_pressed[PLAYER_1] <= end_zone_size - 1 && player_button_pressed[PLAYER_1] != -1) {
        ChangeDirection();
      }

      if (ball_dir == +1 && player_button_pressed[PLAYER_2] >= NUM_PIXELS - end_zone_size) {
        ChangeDirection();
      }
      break;
    case S_CHECK_SCORE:
      SetButton(PLAYER_1, player_color[PLAYER_1], 64);
      SetButton(PLAYER_2, player_color[PLAYER_2], 64);
      previous_button_pos = -1;  // clear last ball-position at button-press

      // check who made score
      if (ball_pos >= NUM_PIXELS) {
        player_score[PLAYER_1] += 1;  // new score for player 1

        GeneratePlayField();  // show new score full bright
        pos = NUM_PIXELS / 2 - player_score[PLAYER_1];

        player_start = PLAYER_1;  // define next player to start (player, who made the point)
      } else {
        player_score[PLAYER_2] += 1;  // new score for player 2

        GeneratePlayField();  // show new score full bright
        pos = NUM_PIXELS / 2 - 1 + player_score[PLAYER_2];

        player_start = PLAYER_2;  // define next player to start (player, who made the point)
      }

      GeneratePlayField();  // show new score full bright

      state = S_BLINK_SCORE;
      break;
    case S_BLINK_SCORE:
      switch (sub_state) {
        case 0:
        case 2:
        case 4:
          werma[pos] = player_color[player_start];
          SetButton(player_start, player_color[player_start], 255);
          previous_button_pos = pos;
          static int ch = -1;
          if (ch != sub_state) {
            ch = sub_state;
            beep_pos = pos;
            beep_millis = millis();
          }
          break;
        case 1:
        case 3:
          werma[pos] = bg_color;
        case 5 ... 7:
          SetButton(PLAYER_1, player_color[PLAYER_1], 64);
          SetButton(PLAYER_2, player_color[PLAYER_2], 64);
          break;
        default:
          state = S_CHECK_WINNER;
          break;
      }

      if (previous_millis + 250 < millis()) {
        previous_millis = millis();
        sub_state++;
      }

      break;
    case S_CHECK_WINNER:
      // check if we have a winner
      if (player_score[PLAYER_1] >= win_rounds || player_score[PLAYER_2] >= win_rounds) {  // we have a winner!
        memset(player_score, 0, sizeof(player_score));                                     // reset all scores
        player_start = abs(player_start - 1);                                              // next game starts looser
        state = S_WON;
      } else {
        state = S_START_GAME;
      }
      break;
    case S_WON:
      if (player_start == PLAYER_1) {
        werma[NUM_PIXELS / 2] = bg_color;
      } else {
        werma[NUM_PIXELS / 2 - 1] = bg_color;
      }
      if ((previous_millis + 100 < millis()) || (sub_state == 0)) {
        previous_millis = millis();
        for (int i = 0; i < NUM_PIXELS / 2 - 1; i++) {
          if (player_start == PLAYER_2) {
            werma[i] = rainbow_color[(i + sub_state) / 3 % 7];
          } else {
            werma[NUM_PIXELS - i - 1] = rainbow_color[(i + sub_state) / 3 % 7];
          }
        }
        sub_state++;
      }
      if (sub_state == 100 || (digitalRead(player_btn_pin[1-player_start]) == LOW) && sub_state > 5) {
        state = S_START_GAME;
      }
      break;
  }

  if (digitalRead(player_btn_pin[PLAYER_1]) == LOW && digitalRead(player_btn_pin[PLAYER_2]) == LOW && (state != S_SET_WIN_ROUNDS)) {
    if ((reset_millis + 1000) < millis()) {
      unsigned long end = (millis() - (reset_millis + 1000)) / 45;
      if (end < NUM_PIXELS / 2) {
        for (int i = 0; i < end; i++) {
          werma[i] = COLOR_WHITE;
          werma[NUM_PIXELS - i] = COLOR_WHITE;
        }
        SetButton(PLAYER_1, COLOR_WHITE, 255);
        SetButton(PLAYER_2, COLOR_WHITE, 255);
      } else {
        state = S_START;
      }
    }
  } else {
    reset_millis = millis();
  }

  if (beep_pos != -1 && (beep_millis + beep_time) > millis()) {
    werma[beep_pos] |= 0b1000;
    SetLeds();
    werma[beep_pos] &= 0b111;
  } else {
    SetLeds();
  }
}

void ChangeDirection() {
  ball_dir *= -1;
  game_speed -= game_speed_step;
  ball_speed = game_speed;
  if (ball_pos == 0 || ball_pos == NUM_PIXELS - 1)  // triggered on first or last segment
  {
    ball_speed -= ball_boost_0;  // Super-Boost
  }

  if (ball_pos == 1 || ball_pos == NUM_PIXELS - 2)  // triggered on second or forelast segment
  {
    ball_speed -= ball_boost_1;  // Boost
  }

  ball_speed = max(ball_speed, ball_speed_max);                      // limit the maximum ball_speed
  memset(player_button_pressed, -1, sizeof(player_button_pressed));  // clear keypress
}

void GeneratePlayField() {
  for (int i = 0; i < NUM_PIXELS; i++) {
    werma[i] = bg_color;
  }
  GenerateEndZone();  // generate endzone
  GenerateScore();    // generate actual score
  GenerateLastHit();  // generate mark of position of last button-press
}


void GenerateEndZone() {
  for (int i = 0; i < end_zone_size; i++) {
    werma[i] = goal_color;
    werma[NUM_PIXELS - 1 - i] = goal_color;
  }
}

void GenerateScore() {
  int i;

  // Player 0
  for (i = 0; i < player_score[PLAYER_1]; i++) {
    werma[NUM_PIXELS / 2 - 1 - i] = player_color[PLAYER_1];
  }

  // Player 1
  for (i = 0; i < player_score[PLAYER_2]; i++) {
    werma[NUM_PIXELS / 2 + i] = player_color[PLAYER_2];
  }
}

void GenerateLastHit() {
  if (previous_button_pos != -1 && ((previous_button_millis + 500 > millis()) || lost)) {
    werma[previous_button_pos] = player_color[previous_button];
  }
}
