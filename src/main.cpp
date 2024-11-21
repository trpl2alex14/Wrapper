#define DEBUG

#include <Arduino.h>
#include <EEPROM.h>
#include <GyverStepper.h>
#include <LedKeypad.h>
#include <LiquidCrystal.h>
#include <menu.h>
#include <message.h>

#define TURNS_LENGTH 60.0f //60mm or 8mm
#define STEPPS 800.0f
#define DELTA_POS 10.0f //10mm 
#define MAXSPEED (2 * STEPPS)

#define CALIBRATION_SPEED (3 * MAXSPEED)

#define AKEY_PERIOD 150

#define INP_PIN 3 // int0
#define STOP_ITR 1
#define START_KEY A5

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
double fixStepps = 4.0f / TURNS_LENGTH * STEPPS;

double needTurns = 27;
long startPos = 40;
long endPos = 40;
long maxPos = 0;

double ratioTurns = 0.35;  // 35% / 65% turns
double calcTurns = 0;

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

void runSteppers(long steps, double turns, bool dir = true)
{
    double timeMax = max(steps, turns * STEPPS) / MAXSPEED;

    double speed1 = steps / timeMax;
    double speed2 = turns * STEPPS / timeMax;

    stepper1.setRunMode(FOLLOW_POS);
    stepper2.setRunMode(FOLLOW_POS);

    stepper1.setMaxSpeed(speed1);
    stepper2.setMaxSpeed(speed2);

    stepper1.setTarget((dir ? 1 : -1) * steps, RELATIVE);
    stepper2.setTarget((turns - 0.2) * STEPPS, RELATIVE);

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
    stepper1.autoPower(false);
    stepper2.autoPower(false);     

    state = WRAP;
    wrapState = MOVE_START;

    calcTurns = maxPos + fixStepps - (endPos + startPos) * stepsInMM;
    float turns = startPos / 10;
    runSteppers(startPos * stepsInMM, turns);
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
        runSteppers( calcTurns * ratioTurns, needTurns / 2);
        break;
    case MOVE_END:
        wrapState = END;
        runSteppers( calcTurns * (1 - ratioTurns), needTurns / 2);
        break;
    case END:
        wrapState = WRAP_END;
        runSteppers(fixStepps, fixTurns, false);
        break;
    case WRAP_END:
        wrapState = COMPLEATE;
        break;
    case COMPLEATE:
        stepper1.enable();
        stepper2.enable();
        state = READY;
        showStateMsg(M_WRAPPING_COMPLEATE);
        menu.setState(MS_READY, MS_RWORK, 2000);
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
    stepper2.disable();    
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
        menu.setState(MS_READY, MS_RWORK, 2000);
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
    stepper1.autoPower(false);
    stepper2.autoPower(false);
    stepper1.disable(); 
    stepper2.disable(); 

    maxPos = 0;
    isStopIsr = false;
    state = CALIBRATION;
    enableIsr();

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
        stepper1.setCurrent(-1 * DELTA_POS * stepsInMM);
        stepper1.setSpeed(CALIBRATION_SPEED);

        maxPos = 1;
        debounceReverse = millis();
        showStateMsg(M_CALIBRATION_START);
    } else if (maxPos == 2) {
        maxPos = stepper1.getCurrent() - DELTA_POS * stepsInMM;

        stepper1.setRunMode(FOLLOW_POS);
        stepper1.setMaxSpeed(CALIBRATION_SPEED);
        stepper1.setTarget(maxPos / 2);

        debounceReverse = millis();
        showStateMsg(M_CALIBRATION_END, String(maxPos));
    }

    if (millis() - debounceReverse > 500) {
        if (maxPos == 1) {
            maxPos = 2;
        }      

        if (maxPos > 2) {          
            state = READY;
            menu.setState(MS_READY, MS_RWORK, 2000);
        }

        isStopIsr = false;
        enableIsr();
    }
}

void enableIsr()
{
    attachInterrupt(STOP_ITR, stopStepperIsr, LOW);
}

void abortStepper()
{
    stepper1.stop();
    stepper2.stop();
    stepper1.disable();
    stepper2.disable();
    state = INIT;

    menu.setState(MS_ABORT, MS_NCALIBRATION, 2000);
}

void stopStepperIsr()
{
    if (isStopIsr) {
        return;
    }

    if (millis() - debounce >= 1000 && !digitalRead(INP_PIN)) {
        stepper1.stop();
        stepper2.stop();
        debounce = millis();
        isStopIsr = true;
        if(state != CALIBRATION){
            abortStepper();   
        }
    }
}

void saveOptions()
{
    Options buf;

    /*EEPROM.get(0, buf);
    if (buf.endPos == endPos && buf.needTurns == needTurns && buf.startPos == startPos) {
        return;
    }*/

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


bool startPress()
{
    if (state == INIT) {
        startCalibration();
    } else if (state == READY) {
        startHome();
    } else if (state == HOME) {
        startWrapping();
    }else {
        return false;
    }

    return true;
}

void keyAction()
{
    switch (keypad.action()) {
    case KB_RIGHT:
        if(!digitalRead(INP_PIN)){
            break;
        }
        if (!startPress() && (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT)) {
            setValue += 0.1;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_LEFT:
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue -= 0.1;
            setValue = setValue < 0 ? 0 : setValue;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_UP:
        if (state == INIT && digitalRead(INP_PIN)) {            
            stepper1.setTarget(stepsInMM, RELATIVE);      
        }    
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue++;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_DOWN:
        if (state == INIT && digitalRead(INP_PIN)) {            
            stepper1.setTarget(-1 * stepsInMM * 0.3, RELATIVE);               
        }  
        if (state == P_SET_TURNS || state == P_SET_LEFT || state == P_SET_RIGHT) {
            setValue--;
            setValue = setValue < 0 ? 0 : setValue;
            menu.showValue(setValue, nameValue);
        }
        break;
    case KB_SELECT:
        if (state == CALIBRATION || state == WRAP ||state == MOVE_HOME || state == HOME) {
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
            menu.setState(MS_SAVED, MS_NCALIBRATION, 2000);
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
    pinMode(START_KEY, INPUT_PULLUP);
    enableIsr();
    
    stepper1.autoPower(false);
    stepper1.setRunMode(FOLLOW_POS);
    stepper1.setAcceleration(0);
    stepper1.setMaxSpeed(MAXSPEED);
    stepper1.reverse(false);
    stepper1.disable();
    
    stepper2.autoPower(false);
    stepper2.setRunMode(FOLLOW_POS);
    stepper2.setAcceleration(0);
    stepper2.setMaxSpeed(MAXSPEED);
    stepper2.disable();

    keypad.attach(keyAction);    
    showStateMsg(M_BOOT);

    menu.init();
}

void loop()
{
    processCalibration();
    processHome();
    processWrapping();

    static unsigned long pushStartBtnTime;
    if (!digitalRead(START_KEY) && pushStartBtnTime == 0) {
        pushStartBtnTime = millis();
    } else if (digitalRead(START_KEY) && pushStartBtnTime > 0 && millis() - pushStartBtnTime > 150) {
        pushStartBtnTime = 0;
        startPress();   
    }

    keypad.tick();    
    menu.tick();
    stepper1.tick(); 
}