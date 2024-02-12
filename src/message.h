#include <Arduino.h>

enum MSG_name {
    M_BOOT,
    M_WRAPPING_ERROR,
    M_WRAPPING,
    M_WRAPPING_COMPLEATE,
    M_HOME_ERROR,
    M_HOME,
    M_READY,
    M_CALIBRATION_ERROR,
    M_CALIBRATION,
    M_CALIBRATION_START,
    M_CALIBRATION_END,
    M_OPTIONS_L,
    M_OPTIONS_S
};

void showStateMsg(MSG_name msg, String str = "")
{
#ifdef DEBUG
    switch (msg) {
    case M_WRAPPING_ERROR:
        Serial.println("He is not at home");
        break;
    case M_WRAPPING:
        Serial.println("Wrapping in the process");
        break;
    case M_WRAPPING_COMPLEATE:
        Serial.println("Wrapping compleate");
        break;
    case M_HOME_ERROR:
        Serial.println("Cannot state ready");
        break;
    case M_HOME:
        Serial.println("Moving home");
        break;
    case M_READY:
        Serial.println("Ready to work");
        break;
    case M_CALIBRATION_ERROR:
        Serial.println("Cannot be calibrated in the wrapping process");
        break;
    case M_CALIBRATION:
        Serial.println("Start calibration");
        break;
    case M_CALIBRATION_START:
        Serial.println("Set start position");
        break;
    case M_CALIBRATION_END:
        Serial.println("Set end position: " + str);
        break;
    case M_BOOT:
        Serial.println("System start");
        break;
    case M_OPTIONS_L:
        Serial.println("Options load:");
        Serial.println(str);        
        break;
    case M_OPTIONS_S:
        Serial.println("Options saved");        
        break;        
    default:
        Serial.println(str);
        break;
    }
#endif
}