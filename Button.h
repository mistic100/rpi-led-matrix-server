#include <wiringPi.h>

#define BTN_IDLE 0
#define BTN_SINGLE 1
#define BTN_SUSTAIN 2
#define BTN_RELEASE 3

#define BUTTON_DELAY 300
#define BUTTON_SUSTAIN_DELAY 1000
#define BUTTON_SUSTAIN_INTERVAL 50

class Button {

private:
    uint8_t pin;

    unsigned long pressTime = 0;
    uint8_t state = BTN_IDLE;
    unsigned long sustainCount = 0;
    bool isDouble = false;

    std::function<void(void)> user_onSinglePress;
    std::function<void(void)> user_onLongPress;
    std::function<void(void)> user_onDoublePress;
    std::function<void(bool last, unsigned long ellapsed)> user_onSustain;

    long getTime() {
        return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    }

public:
    Button(uint8_t _pin) {
        pin = _pin;
        pinMode(pin, INPUT);
        pullUpDnControl(pin, PUD_UP);
    }

    void onSinglePress(std::function<void(void)> fn) {
        user_onSinglePress = fn;
    }

    void onLongPress(std::function<void(void)> fn) {
        user_onLongPress = fn;
    }

    void onDoublePress(std::function<void(void)> fn) {
        user_onDoublePress = fn;
    }

    void onSustain(std::function<void(bool last, unsigned long ellapsed)> fn) {
        user_onSustain = fn;
    }

    void handle() {
        bool pressed = digitalRead(pin) == 0;

        if (pressed) {
            switch (state) {
                case BTN_RELEASE:
                    isDouble = true;
                case BTN_IDLE:
                    pressTime = getTime();
                    state = BTN_SINGLE;
                    break;

                case BTN_SINGLE:
                    if (getTime() - pressTime > BUTTON_SUSTAIN_DELAY) {
                        sustainCount = 1;
                        if (user_onSustain) {
                            user_onSustain(false, sustainCount * BUTTON_SUSTAIN_INTERVAL);
                        }
                        pressTime = getTime();
                        state = BTN_SUSTAIN;
                    }
                    break;

                case BTN_SUSTAIN:
                    if (getTime() - pressTime > BUTTON_SUSTAIN_INTERVAL) {
                        sustainCount++;
                        if (user_onSustain) {
                            user_onSustain(false, sustainCount * BUTTON_SUSTAIN_INTERVAL);
                        }
                        pressTime = getTime();
                    }
                    break;
            }
        } else {
            switch (state) {
                case BTN_SINGLE:
                    if (getTime() - pressTime < BUTTON_DELAY) {
                        pressTime = getTime();
                        state = BTN_RELEASE;
                    } else {
                        if (user_onLongPress) {
                            user_onLongPress();
                        }
                        state = BTN_IDLE;
                        isDouble = false;
                    }
                    break;

                case BTN_SUSTAIN:
                    sustainCount++;
                    if (user_onSustain) {
                        user_onSustain(true, sustainCount * BUTTON_SUSTAIN_INTERVAL);
                    }
                    state = BTN_IDLE;
                    isDouble = false;
                    break;

                case BTN_RELEASE:
                    if (isDouble) {
                        if (user_onDoublePress) {
                            user_onDoublePress();
                        }
                        state = BTN_IDLE;
                        isDouble = false;
                    } else if (getTime() - pressTime >= BUTTON_DELAY) {
                        if (user_onSinglePress) {
                            user_onSinglePress();
                        }
                        state = BTN_IDLE;
                        isDouble = false;
                    }
                    break;
            }
        }
    }

};
