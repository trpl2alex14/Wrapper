#include <Arduino.h>
#include <LiquidCrystal.h>

enum Menu_State {
    MS_START = 0,
    MS_READY = 1,
    MS_ABORT = 2,
    MS_CALIBRATION = 3,
    MS_HOME = 4,
    MS_WRAPPING = 5,
    MS_SETTING = 10,
    MS_SAVED = 11
};

class Menu {
public:
    Menu(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);

    void init();

    void setState(Menu_State state);
    void showValue(float value, String name);

    void tick();

    void processed(long time);
    void compleate();

private:
    LiquidCrystal* _lcd;

    Menu_State _state = MS_START;
    Menu_State _activeState = MS_START;

    bool _processed = false;
    int _step = 0;
    bool _setTime = false;
    int _stepDelay = 0;
    long _delay;

    String _strValue = "";
    bool _update = false;

    void showStep();
};

Menu::Menu(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    Menu::_lcd = new LiquidCrystal(rs, enable, d0, d1, d2, d3);
}

void Menu::setState(Menu_State state)
{
    _state = state;
	_update = true;
}

void Menu::init()
{
    _lcd->begin(16, 2);
    _lcd->setCursor(2, 0);
    _lcd->print("System start");
}

void Menu::processed(long time = 0)
{
    _step = 0;
    _processed = true;

    _setTime = (bool)time;
    _stepDelay = !time ? 500 : time / 14;
	_delay = 0;
}

void Menu::compleate()
{
    _step = 0;
    _processed = false;
    _lcd->setCursor(0, 1);
    _lcd->print("                ");
}

void Menu::showStep()
{
    if (millis() - _delay < (unsigned)_stepDelay) {
        return;
    }
    _delay = millis();

    if (_step == 0) {
        _lcd->setCursor(0, 1);
        _lcd->print("[              ]");
    }

    _lcd->setCursor(_step + 1, 1);
    _lcd->print("=");
    _step++;

    if (_step > 14) {
        compleate();
        _processed = !_setTime;
    }
}

void Menu::showValue(float value, String name = "val")
{
    _strValue = name + ": " + String(value, 1);
    _update = true;
}

void Menu::tick()
{
    if (_update) {
        _lcd->clear();
    }

    if (_processed) {
        showStep();
    }

    if (_activeState == _state && !_update) {
        return;
    }

    _update = false;
    _activeState = _state;

    switch (_activeState) {
    case MS_CALIBRATION:
        _lcd->setCursor(3, 0);
        _lcd->print("Calibration");
        processed();
        break;
    case MS_READY:
        _lcd->setCursor(3, 0);
        _lcd->print("Completed!");
		compleate();
        break;
    case MS_ABORT:
        _lcd->setCursor(4, 0);
        _lcd->print("!Aborted!");
        compleate();
        break;
    case MS_SETTING:
        _lcd->setCursor(4, 0);
        _lcd->print("Settings");
        _lcd->setCursor(0, 1);
        _lcd->print(_strValue);
        break;
    case MS_SAVED:
        _lcd->setCursor(6, 0);
        _lcd->print("Saved");
        break;
    case MS_HOME:
        _lcd->setCursor(3, 0);
        _lcd->print("Move home");
        break;
    case MS_WRAPPING:
        _lcd->setCursor(3, 0);
        _lcd->print("-Wrapping-");
        break;
    default:
        break;
    }
}
