#include <FastLED.h>
#include <usbh_midi.h>

void debugPrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsprintf(buf, fmt, args);
    va_end(args);
    Serial.print(buf);
}

namespace fl {

    template<class Config = void>
    class MIDIController : public USBDeviceConfig {
    public:
        class Delegate {
        public:
            virtual void OnNoteOn(uint8_t note, uint8_t velocity) = 0;
            virtual void OnNoteOff(uint8_t note, uint8_t velocity) = 0;
            virtual void OnControlModeChanged(uint8_t param1, uint8_t param2) = 0;
        };

        MIDIController(USB *usb) : midi_(usb) {
            usb->RegisterDeviceClass(this);
        }

        void SetDelegate(Delegate *delegate) {
            delegate_ = delegate;
        }

        virtual uint8_t Poll() override {
            uint8_t buffer[MIDI_EVENT_PACKET_SIZE];
            do {
                uint16_t size = 0;
                if (midi_.RecvData(&size, buffer) == 0) {
                    ProcessPackets(buffer, size);
                    if (size < MIDI_EVENT_PACKET_SIZE) {
                        break;
                    }
                } else {
                    break;
                }
            } while (1);
            
            return 0;
        }

    private:
        void ProcessPackets(uint8_t *buffer, uint16_t size) {
            for (uint16_t i = 0; i < size; i += 4) {
                const auto data1 = buffer[i];
                const auto data2 = buffer[i + 1];
                const auto data3 = buffer[i + 2];
                const auto data4 = buffer[i + 3];

                debugPrintf("Received packet: %02X %02X %02X\n", data2, data3, data4);

                switch (data2 >> 4) {
                    case 0x09:
                        delegate_->OnNoteOn(data3, data4);
                        break;
                    case 0x08:
                        delegate_->OnNoteOff(data3, data4);
                        break;
                    case 0x0b:
                        delegate_->OnControlModeChanged(data3, data4);
                        break;
                }
            }
        }

        USBH_MIDI midi_;
        Delegate *delegate_;
    };

    template<class Config>
    class LEDController {
    public:
        LEDController() {
            memset(lightValues_, 0, sizeof(lightValues_));

            LEDS.addLeds<WS2812B, Config::ledPin, GRB>(leds_, Config::ledCount);
            SetBrightness(0x3f);
        }

        inline void SetBrightness(uint8_t scale) {
            LEDS.setBrightness(scale);
            dirty_ = true;
        }

        void SetLight(uint16_t n, bool on) {
            lightValues_[n] = on;
            UpdateLight(n);
        }

        void SetColorScheme(int scheme) {
            colorScheme_ = scheme;
            for (int16_t i = 0; i < Config::ledCount; ++i) {
                UpdateLight(i);
            }
        }

        void UpdateIfNeeded() {
            if (dirty_) {
                LEDS.show();
                dirty_ = false;
            }
        }

    private:
        void UpdateLight(uint16_t n) {
            if (lightValues_[n]) {
                leds_[n] = GetLightColor(n);
            } else {
                leds_[n] = CRGB::Black;
            }
            dirty_ = true;
        }

        CRGB GetLightColor(uint16_t n) const {
            if (colorScheme_ == 0) {
                return CHSV(0xc0 * n / Config::ledCount, 0xff, 0xff);
            } else {
                return CHSV(0x00, 0xff, 0xff);
            }
        }

        bool lightValues_[Config::ledCount];
        CRGB leds_[Config::ledCount];
        int colorScheme_ = 0;
        bool dirty_ = true;
    };

}


// The upper and lower bounds of the piano range.
uint8_t nlower = 0x15;
uint8_t nupper = 0x6c;

using DefaultMIDIController = fl::MIDIController<>;

class App : public DefaultMIDIController::Delegate {
    struct LEDConfig {
        constexpr static auto ledPin = 7;
        constexpr static auto ledCount = 176;
    };

public:
    App(DefaultMIDIController *midiController) : midiController_(midiController) {
        midiController_->SetDelegate(this);
    }

    void Update() {
        ledController_.UpdateIfNeeded();
    }

    virtual void OnNoteOn(uint8_t note, uint8_t velocity) override {
        auto idx = ledIndexFromNote(note);
        ledController_.SetLight(idx, true);
        ledController_.SetLight(idx + 1, true);
    }

    virtual void OnNoteOff(uint8_t note, uint8_t velocity) override {
        auto idx = ledIndexFromNote(note);
        ledController_.SetLight(idx, false);
        ledController_.SetLight(idx + 1, false);
    }

    virtual void OnControlModeChanged(uint8_t param1, uint8_t param2) override {
        if (param1 == 0x43) {
            ledController_.SetColorScheme(1);
        }
    }

private:
    inline uint16_t ledIndexFromNote(uint8_t note) const {
        return (note - nlower) * 2;
    }

    DefaultMIDIController *midiController_;
    fl::LEDController<LEDConfig> ledController_;
};

USB usb;
DefaultMIDIController midiController(&usb);
App app(&midiController);

void setup()
{
    Serial.begin(115200);

    // Initialize the USB device.
    if (usb.Init() == -1)
        while (1);

    delay(200);
}

void loop()
{
    usb.Task();
    app.Update();
}
