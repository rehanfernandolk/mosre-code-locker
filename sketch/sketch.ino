#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>

// ---- Pins ----
const int PIN_RESET_SW = 2;  // slide switch: password set/reset
const int PIN_CODE_BTN = 3;  // blue C (Morse)
const int PIN_CLEAR_BTN = 4; // clear current Morse input
const int PIN_LED_R    = 6;
const int PIN_LED_G    = 7;
const int PIN_LED_B    = 8;
const int PIN_BUZZER   = 9;
const int PIN_SERVO    = 10;

const int SERVO_LOCKED   = 0;
const int SERVO_UNLOCKED = 90;

// Reset flow phases (while switch is ON)
enum ResetPhase : uint8_t {
  PHASE_OFF = 0,     // normal lock mode (switch off)
  PHASE_VERIFY,      // must enter old password first
  PHASE_SET          // old password OK — enter new password
};

// ---- OLED ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Servo lockServo;

// ---- Morse timing (ms) ----
const unsigned long DOT_MAX_MS     = 250;
const unsigned long LETTER_GAP_MS  = 700;
const unsigned long MESSAGE_END_MS = 1800;
const unsigned long DEBOUNCE_MS    = 30;

// Default password Morse for "AA"
String storedPassword = ".- .-";

// ---- State ----
ResetPhase resetPhase = PHASE_OFF;
bool resetSwLast = HIGH;       // INPUT_PULLUP: LOW = connected = ON
unsigned long resetDebounceAt = 0;

bool codeBtnLast = HIGH;
unsigned long codeDebounceAt = 0;

bool clearBtnLast = HIGH;
unsigned long clearDebounceAt = 0;

bool codePressed = false;
unsigned long pressStartMs = 0;
unsigned long lastReleaseMs = 0;
bool waitingForGap = false;

String inputMorse = "";
bool awaitingEval = false;
bool showingResult = false;
unsigned long resultUntilMs = 0;

void setup() {
  pinMode(PIN_RESET_SW, INPUT_PULLUP);
  pinMode(PIN_CODE_BTN, INPUT_PULLUP);
  pinMode(PIN_CLEAR_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  lockServo.attach(PIN_SERVO);
  lockServo.write(SERVO_LOCKED);

  noTone(PIN_BUZZER);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  resetPhase = PHASE_OFF;
  setLedYellow();
  showPrompt();
}

void loop() {
  unsigned long now = millis();

  handleResetSwitch(now);

  if (showingResult) {
    if (now >= resultUntilMs) {
      showingResult = false;
      resetMorseInput();
      lockServo.write(SERVO_LOCKED);
      resumeAfterResult();
    }
    return;
  }

  handleCodeButton(now);
  handleClearButton(now);

  // Auto-check Morse when locking or verifying old password (not while typing new pwd)
  if (resetPhase == PHASE_OFF || resetPhase == PHASE_VERIFY) {
    checkMessageEnd(now);
  }
}

void resumeAfterResult() {
  if (resetPhase == PHASE_VERIFY) {
    setLedBlue();
    showVerifyPrompt();
  } else if (resetPhase == PHASE_SET) {
    setLedBlue();
    showSetPrompt();
  } else {
    setLedYellow();
    showPrompt();
  }
}

void handleResetSwitch(unsigned long now) {
  bool reading = digitalRead(PIN_RESET_SW);

  if (reading != resetSwLast) {
    resetDebounceAt = now;
  }

  if ((now - resetDebounceAt) > DEBOUNCE_MS) {
    static bool stable = HIGH;
    if (reading != stable) {
      stable = reading;
      bool switchOn = (stable == LOW);

      if (switchOn && resetPhase == PHASE_OFF) {
        beginResetFlow();
      } else if (!switchOn && resetPhase != PHASE_OFF) {
        endResetFlow();
      }
    }
  }

  resetSwLast = reading;
}

void beginResetFlow() {
  showingResult = false;
  resetMorseInput();
  lockServo.write(SERVO_LOCKED);
  resetPhase = PHASE_VERIFY;
  setLedBlue();
  showVerifyPrompt();
}

void endResetFlow() {
  // Only save a new password if old was verified and something was entered
  if (resetPhase == PHASE_SET) {
    while (inputMorse.length() > 0 && inputMorse.charAt(inputMorse.length() - 1) == ' ') {
      inputMorse.remove(inputMorse.length() - 1);
    }
    if (inputMorse.length() > 0) {
      storedPassword = inputMorse;
      tone(PIN_BUZZER, 800, 150);
      delay(160);
      noTone(PIN_BUZZER);
    }
  }

  resetPhase = PHASE_OFF;
  showingResult = false;
  resetMorseInput();
  setLedYellow();
  showPrompt();
}

void setLedYellow() {
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, LOW);
}

void setLedBlue() {
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_G, LOW);
  digitalWrite(PIN_LED_B, HIGH);
}

void setLedGreen() {
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, LOW);
}

void setLedRed() {
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, LOW);
  digitalWrite(PIN_LED_B, LOW);
}

void handleClearButton(unsigned long now) {
  bool reading = digitalRead(PIN_CLEAR_BTN);

  if (reading != clearBtnLast) {
    clearDebounceAt = now;
  }

  if ((now - clearDebounceAt) > DEBOUNCE_MS) {
    static bool stable = HIGH;
    if (reading != stable) {
      stable = reading;
      if (stable == LOW) {
        resetMorseInput();
        resumeAfterResult();
        tone(PIN_BUZZER, 500, 80);
        delay(90);
        noTone(PIN_BUZZER);
      }
    }
  }

  clearBtnLast = reading;
}

void handleCodeButton(unsigned long now) {
  bool reading = digitalRead(PIN_CODE_BTN);

  if (reading != codeBtnLast) {
    codeDebounceAt = now;
  }

  if ((now - codeDebounceAt) > DEBOUNCE_MS) {
    static bool stable = HIGH;
    if (reading != stable) {
      stable = reading;

      if (stable == LOW) {
        codePressed = true;
        pressStartMs = now;
        waitingForGap = false;
        awaitingEval = false;
        tone(PIN_BUZZER, 800);
      } else if (codePressed) {
        noTone(PIN_BUZZER);
        unsigned long held = now - pressStartMs;
        char symbol = (held < DOT_MAX_MS) ? '.' : '-';
        inputMorse += symbol;
        codePressed = false;
        lastReleaseMs = now;
        waitingForGap = true;
        awaitingEval = true;
        showLiveInput();
      }
    }
  }

  codeBtnLast = reading;

  if (waitingForGap && !codePressed && awaitingEval) {
    unsigned long silence = now - lastReleaseMs;
    if (silence >= LETTER_GAP_MS && silence < MESSAGE_END_MS) {
      if (inputMorse.length() > 0 && inputMorse.charAt(inputMorse.length() - 1) != ' ') {
        inputMorse += ' ';
        showLiveInput();
      }
      waitingForGap = false;
    }
  }
}

void checkMessageEnd(unsigned long now) {
  if (!awaitingEval || codePressed) {
    return;
  }

  if ((now - lastReleaseMs) >= MESSAGE_END_MS && inputMorse.length() > 0) {
    awaitingEval = false;
    if (resetPhase == PHASE_VERIFY) {
      evaluateOldPassword();
    } else {
      evaluateAnswer();
    }
  }
}

void evaluateOldPassword() {
  while (inputMorse.length() > 0 && inputMorse.charAt(inputMorse.length() - 1) == ' ') {
    inputMorse.remove(inputMorse.length() - 1);
  }

  float accuracy = morseAccuracy(storedPassword.c_str(), inputMorse.c_str());

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 8);

  if (accuracy >= 75.0) {
    display.println(F("OK!"));
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.println(F("now set new pwd"));
    display.display();
    setLedGreen();
    playHighNote();
    resetMorseInput();
    resetPhase = PHASE_SET;
    showingResult = true;
    resultUntilMs = millis() + 1500;
  } else {
    display.println(F("wrong!"));
    display.setTextSize(1);
    display.setCursor(0, 32);
    display.println(F("enter old pwd"));
    display.setCursor(0, 48);
    display.print(F("acc: "));
    display.print((int)accuracy);
    display.print(F("%"));
    display.display();
    setLedRed();
    playLowNote();
    showingResult = true;
    resultUntilMs = millis() + 2000;
  }
}

void evaluateAnswer() {
  while (inputMorse.length() > 0 && inputMorse.charAt(inputMorse.length() - 1) == ' ') {
    inputMorse.remove(inputMorse.length() - 1);
  }

  float accuracy = morseAccuracy(storedPassword.c_str(), inputMorse.c_str());

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 8);

  if (accuracy >= 75.0) {
    display.println(F("correct!"));
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print(F("acc: "));
    display.print((int)accuracy);
    display.print(F("%"));
    display.display();
    setLedGreen();
    lockServo.write(SERVO_UNLOCKED);
    playHighNote();
  } else {
    display.println(F("wrong!"));
    display.setTextSize(1);
    display.setCursor(0, 32);
    display.println(F("try again!"));
    display.setCursor(0, 48);
    display.print(F("acc: "));
    display.print((int)accuracy);
    display.print(F("%"));
    display.display();
    setLedRed();
    lockServo.write(SERVO_LOCKED);
    playLowNote();
  }

  showingResult = true;
  resultUntilMs = millis() + 2500;
}

float morseAccuracy(const char* expected, const char* actual) {
  int n = strlen(expected);
  int m = strlen(actual);
  if (n == 0 && m == 0) return 100.0;
  if (n == 0 || m == 0) return 0.0;

  const int MAXN = 48;
  if (n > MAXN) n = MAXN;
  if (m > MAXN) m = MAXN;

  static uint8_t prev[MAXN + 1];
  static uint8_t curr[MAXN + 1];

  for (int j = 0; j <= m; j++) {
    prev[j] = j;
  }

  for (int i = 1; i <= n; i++) {
    curr[0] = i;
    for (int j = 1; j <= m; j++) {
      uint8_t cost = (expected[i - 1] == actual[j - 1]) ? 0 : 1;
      uint8_t del = prev[j] + 1;
      uint8_t ins = curr[j - 1] + 1;
      uint8_t sub = prev[j - 1] + cost;
      uint8_t best = del < ins ? del : ins;
      curr[j] = best < sub ? best : sub;
    }
    for (int j = 0; j <= m; j++) {
      prev[j] = curr[j];
    }
  }

  int dist = prev[m];
  int maxlen = n > m ? n : m;
  float accuracy = (1.0f - ((float)dist / (float)maxlen)) * 100.0f;
  if (accuracy < 0) accuracy = 0;
  if (accuracy > 100) accuracy = 100;
  return accuracy;
}

void playHighNote() {
  tone(PIN_BUZZER, 1200, 400);
  delay(420);
  noTone(PIN_BUZZER);
}

void playLowNote() {
  tone(PIN_BUZZER, 220, 500);
  delay(520);
  noTone(PIN_BUZZER);
}

void resetMorseInput() {
  inputMorse = "";
  codePressed = false;
  waitingForGap = false;
  awaitingEval = false;
  noTone(PIN_BUZZER);
}

void showPrompt() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 24);
  display.println(F("code?"));
  display.display();
}

void showVerifyPrompt() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.println(F("old pwd"));
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println(F("enter current Morse"));
  display.display();
}

void showSetPrompt() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 16);
  display.println(F("set pwd"));
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(F("enter Morse..."));
  display.display();
}

void showLiveInput() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (resetPhase == PHASE_VERIFY) {
    display.println(F("old pwd"));
  } else if (resetPhase == PHASE_SET) {
    display.println(F("set pwd"));
  } else {
    display.println(F("code?"));
  }
  display.setCursor(0, 16);
  display.println(F("input:"));
  display.setCursor(0, 32);
  display.println(inputMorse);
  display.display();
}
