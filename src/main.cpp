#define DEBUG

#include <Arduino.h>
#include <EEPROM.h>
#include <GyverStepper.h>
#include <LedKeypad.h>
#include <LiquidCrystal.h>
#include <menu.h>
#include <message.h>

#define TURNS_LENGTH 8
#define STEPPS 200
#define MAXSPEED (6 * STEPPS)

#define CALIBRATION_SPEED (MAXSPEED)

#define AKEY_PERIOD 100

#define INP_PIN 3 // int0
#define STOP_ITR 1

LedKeypad<AKEY_PERIOD> keypad(A0);  //14 pin

Menu menu(8, 9, 4, 5, 6, 7);

GStepper<STEPPER2WIRE> stepper1(STEPPS, 13, 12, 11); // moving
GStepper<STEPPER2WIRE> stepper2(STEPPS, 15, 16, 17);

enum PR_State {
    INIT,
    CALIBRATION,
    MOVE_HOME,
    HOME,
    WRAP,
    READY,
    P_SET_TURNS,
    P_SET_LEFT,
    P_SET_RIGHT
};

enum WR_State {
    MOVE_START,
    START,
    WRAP_START,
    MOVE_END,
    END,
    WRAP_END,
    COMPLEATE
};

struct Options {
    char init = 'i';
    long endPos = 40;
    long startPos = 40;
    double needTurns = 27.5;
};

const int turnLength = TURNS_LENGTH;
const double stepsInMM = STEPPS / TURNS_LENGTH;

int fixTurns = 10;
int fixStepps = 100;

double needTurns = 27;
long startPos = 40;
long endPos = 40;
long maxPos = 0;

double setValue = 0;
String nameValue = "";

volatile bool isStopIsr = false;
volatile uint32_t debounce;
volatile uint32_t debounceReverse;

PR_State state = INIT;
WR_State wrapState = MOVE_START;

void processWrapping();
void processCalibration();
void enableIsr();
void stopStepperIsr();

void runSteppers(long steps, float turns, bool dir = true)
{
    float timeMax = max(steps, turns * STEPPS) / MAXSPEED;

    float speed1 = steps / timeMax;
    float speed2 = turns * STEPPS / timeMax;

    stepper1.setRunMode(FOLLOW_POS);
    stepper2.setRunMode(FOLLOW_POS);

    stepper1.setMaxSpeed(speed1);
    stepper2.setMaxSpeed(speed2);

    stepper1.setTarget((dir ? 1 : -1) * steps, RELATIVE);
    stepper2.setTarget(turns * STEPPS, RELATIVE);

    if (timeMax > 1) {
        menu.processed((int)timeMax * 1000);
    }
}

void startWrapping()
{
    if (state != HOME) {
        showStateMsg(M_WRAPPING_ERROR);

        return;
    }

    showStateMsg(M_WRAPPING);

    menu.setState(MS_WRAPPING);

    state = WRAP;
    wrapState = MOVE_START;

    float turns = startPos / turnLength;
    runSteppers(startPos * STEPPS / 10, turns);
}

void processWrapping()
{
    if (state != WRAP) {
        return;
    }

    stepper1.tick();
    stepper2.tick();

    if (stepper1.getState() || stepper2.getState()) {
        return;
    }

    switch (wrapState) {
    case MOVE_START:
        wrapState = START;
        break;
    case START:
        wrapState = WRAP_START;
        runSteppers(fixStepps, fixTurns, false);
        break;
    case WRAP_START:
        wrapState = MOVE_END;
        runSteppers(maxPos + fixStepps - (endPos + startPos) * stepsInMM, needTurns);
        break;
    case MOVE_END:
        wrapState = END;
        break;
    case END:
        wrapState = WRAP_END;
        runSteppers(fixStepps, fixTurns, false);
        break;
    case WRAP_END:
        wrapState = COMPLEATE;
        break;
    case COMPLEATE:
        state = READY;
        showStateMsg(M_WRAPPING_COMPLEATE);
        menu.setState(MS_READY);
        break;
    }
}

void startHome()
{
    if (state != READY) {
        showStateMsg(M_HOME_ERROR);
        return;
    }

    state = MOVE_HOME;
    stepper1.setRunMode(FOLLOW_POS);
    stepper1.setMaxSpeed(MAXSPEED);
    stepper1.setTarget(0);

    double time = stepper1.getCurrent() * 1000 / MAXSPEED;
    showStateMsg(M_HOME);
    menu.setState(MS_HOME);
    menu.processed(time);
}

void processHome()
{
    if (state != MOVE_HOME) {
        return;
    }

    if (!stepper1.tick()) {
        state = HOME;
        showStateMsg(M_READY);
        menu.setState(MS_READY);
    }
}

void startCalibration()
{
    if (state != INIT && state != HOME && state != READY) {
        showStateMsg(M_CALIBRATION_ERROR);
        return;
    }

    menu.setState(MS_CALIBRATION);
    showStateMsg(M_CALIBRATION);

    stepper1.stop();
    stepper2.stop();

    maxPos = 0;
    isStopIsr = false;
    state = CALIBRATION;

    stepper1.setRunMode(KEEP_SPEED);
    stepper1.setSpeed(-1 * CALIBRATION_SPEED);
}

void processCalibration()
{
    if (state != CALIBRATION) {
        return;
    }

    stepper1.tick();

    if (!isStopIsr) {
        return;
    }

    detachInterrupt(STOP_ITR);

    if (!maxPos) {
        stepper1.setCurrent(-100);
        stepper1.setSpeed(CALIBRATION_SPEED);

        maxPos = 1;
        debounceReverse = millis();
        showStateMsg(M_CALIBRATION_START);
    } else if (maxPos == 2) {
        maxPos = stepper1.getCurrent() - 100;

        stepper1.setRunMode(FOLLOW_POS);
        stepper1.setTarget(-100, RELATIVE);

        debounceReverse = millis();
        showStateMsg(M_CALIBRATION_END, String(maxPos));
    }

    if (millis() - debounceReverse > 500) {
        if (maxPos == 1) {
            maxPos = 2;
        }

        if (maxPos > 2) {
            state = READY;
            menu.setState(MS_READY);
        }

        isStopIsr = false;
        enableIsr();
    }
}

void enableIsr()
{
    attachInterrupt(STOP_ITR, stopStepperIsr, CHANGE);
}

void stopStepperIsr()
{
    if (isStopIsr) {
        return;
    }

    if (millis() - debounce >= 1000 && digitalRead(INP_PIN)) {
        stepper1.stop();
        stepper2.stop();
        debounce = millis();
        isStopIsr = true;
    }
}

void abortStepper()
{
    stepper1.stop();
    stepper2.stop();
    state = INIT;

    menu.setState(MS_ABORT);
}

void saveOptions()
{
    Options buf;

    EEPROM.get(0, buf);
    if (buf.endPos == endPos && buf.needTurns == needTurns && buf.startPos == startPos) {
        return;
    }

    buf.needTurns = needTurns;
    buf.startPos = startPos;
    buf.endPos = endPos;

    EEPROM.put(0, buf);
    showStateMsg(M_OPTIONS_S);
}

void LoadOptions()
{
    Options buf;
    EEPROM.get(0, buf);

    if (buf.init != 'i') {
        saveOptions();
        return;
    }

    needTurns = buf.needTurns;
    startPos = buf.startPos;
    endPos = buf.endPos;
    showStateMsg(M_OPTIONS_L, "turns: " + String(needTurns, 2) + "\nleft: " + String(startPos) + "\nright: " + String(endPos));
}

void keyAction()
{
    switch (keypad.action()) {
    case KB_RIGHT:
        if (state == INIT) {
            startCalibration();
        } else if (state == READY) {
            startHome();
        } else if (state == HOME) {
            startWrapping();
        } else if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue += 0.25;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_LEFT:
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue -= 0.25;
            setValue = setValue < 0 ? 0 : setValue;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_UP:
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue++;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_DOWN:
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue--;
            setValue = setValue < 0 ? 0 : setValue;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_SELECT:
        if (state == CALIBRATION) {
            abortStepper();
        } else if (state == READY || state == HOME || state == INIT) {
            state = P_SET_TURNS;
            setValue = needTurns;
            nameValue = "TURNS";
            menu.setState(MS_SETTING);
        } else if (state == P_SET_TURNS) {
            state = P_SET_LEFT;
            needTurns = setValue;
            setValue = startPos;
            nameValue = "LEFT";
        } else if (state == P_SET_LEFT) {
            state = P_SET_RIGHT;
            startPos = setValue;
            setValue = endPos;
            nameValue = "RIGHT";
        } else if (state == P_SET_RIGHT) {
            state = INIT;
            endPos = setValue;
            nameValue = "";
            saveOptions();
            menu.setState(MS_SAVED);
        }

        if (nameValue) {
            menu.showValue(setValue, nameValue);
        }
        break;
    default:
        break;
    }
}

void setup()
{
    Serial.begin(9600);

    LoadOptions();

    pinMode(INP_PIN, INPUT_PULLUP);
    enableIsr();

    stepper1.disable();
    stepper1.autoPower(true);
    stepper1.setRunMode(FOLLOW_POS);
    stepper1.setAcceleration(0);
    stepper1.setMaxSpeed(MAXSPEED);

    stepper2.disable();
    stepper2.autoPower(true);
    stepper2.setRunMode(FOLLOW_POS);
    stepper2.setAcceleration(0);
    stepper2.setMaxSpeed(MAXSPEED);

    keypad.attach(keyAction);
    showStateMsg(M_BOOT);

    menu.init();
}

void loop()
{
    processCalibration();
    processHome();
    processWrapping();

    keypad.tick();
    menu.tick();
}