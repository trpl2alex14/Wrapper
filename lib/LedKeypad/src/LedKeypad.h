#ifndef _LedKeypad_h
#define _LedKeypad_h
#include <Arduino.h>

enum Keypad_Buttons {
    KB_NONE = 0,
    KB_RIGHT = 1,
    KB_UP = 2,
    KB_DOWN = 3,
    KB_LEFT = 4,
    KB_SELECT = 5
};

template <uint8_t PERIOD>
class LedKeypad {
public:
    LedKeypad(int pin) { _pin = pin; }

    Keypad_Buttons readKeypadButtons()
    {
        int readKey = analogRead(_pin);
        if (readKey > 1000)
            return KB_NONE;
        if (readKey < 50)
            return KB_RIGHT;
        if (readKey < 250)
            return KB_UP;
        if (readKey < 450)
            return KB_DOWN;
        if (readKey < 650)
            return KB_LEFT;
        if (readKey < 850)
            return KB_SELECT;

        return KB_NONE;
    }

    Keypad_Buttons action()
    {
        return _lastKeyPress;
    }

    Keypad_Buttons press()
    {
        return _keyPress;
    }

    void attach(void (*handler)())
    {
        _cb = *handler;
    }

    bool tick()
    {
        bool state = isAction();
        if (_cb && state) {
            _cb();
        }

        return state;
    }

protected:
    bool isAction()
    {
        Keypad_Buttons tmpBtn;
        if (millis() - _keyDebounce > (unsigned)_period) {
            _keyDebounce = millis();
            tmpBtn = readKeypadButtons();

            if (_keyPress && !tmpBtn) {
                _lastKeyPress = _keyPress;
                _keyPress = KB_NONE;
                return true;
            }

            _keyPress = tmpBtn;
        }

        return false;
    }

private:
    int _pin;
    int _keyDebounce = 0;
    Keypad_Buttons _keyPress = KB_NONE;
    Keypad_Buttons _lastKeyPress = KB_NONE;
    int _period = PERIOD;
    void (*_cb)() = nullptr;
};

#endif