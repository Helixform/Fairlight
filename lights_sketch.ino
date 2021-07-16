#include <FastLED.h>
#include <usbh_midi.h>

#define LED_PIN 7
#define NUM_LEDS 176

#define MIDI_SERIAL_PORT Serial

#define ENABLE_VELOCITY_FEEDBACK 0

CRGB leds[NUM_LEDS];

USB usb;
USBH_MIDI midi(&usb);

// The upper and lower bounds of the piano range.
uint8_t nlower = 0x15;
uint8_t nupper = 0x6c;

void midipoll();

void debugPrintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsprintf(buf, fmt, args);
    va_end(args);
    Serial.print(buf);
}

void setup()
{
    MIDI_SERIAL_PORT.begin(115200);

    LEDS.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    LEDS.setBrightness(0x3f);

    // Initialize the USB device.
    if (usb.Init() == -1)
        while (1)
            ;

    delay(200);
}

void loop()
{
    usb.Task();

    if (midi)
        midipoll();
}

void midipoll()
{
    uint8_t outBuf[MIDI_EVENT_PACKET_SIZE];
    uint16_t size = 0;

    enum class KeyStatus
    {
        pressed,
        released
    };

    int loop = 0;

    do
    {
        if (midi.RecvData(&size, outBuf) == 0)
        {
            debugPrintf("Packet size: %d bytes on loop %d.\n", size, loop);
            loop++;

            for (uint8_t i = 0; i < size >> 2; i++)
            {
                // Record the state of the key.
                KeyStatus status = KeyStatus::released;
                // The index of the LED.
                uint8_t idx = EOF;
                // The color of the LED.
                CRGB color = CRGB::Black;

                // The exact location of the bytecode data in the data packet.
                int didx = i * 4;
                // Note event.
                uint8_t event = outBuf[didx + 1];
                uint8_t ec = event >> 4;
                if (ec == 0x09)
                    // Key pressed.
                    status = KeyStatus::pressed;
                else if (ec == 0x08)
                    // Key released.
                    status = KeyStatus::released;

                uint8_t note = outBuf[didx + 2];
                idx = (note - nlower) * 2;

                uint8_t velocity = outBuf[didx + 3];
#if ENABLE_VELOCITY_FEEDBACK
                if (status == KeyStatus::pressed && idx != EOF)
                    color = CHSV(0xc0 - (0xc0 * velocity / 0x7f), 0xff, 0xff);
#else
                if (status == KeyStatus::pressed && idx != EOF)
                    color = CHSV(0xc0 * (note - nlower) / 0x58, 0xff, 0xff);
#endif

                // Print MIDI logs via serial port.
                debugPrintf("%02X %02X %02X\n", event, note, velocity);

                leds[idx] = color;
                leds[idx + 1] = color;
            }
        }
    } while (size == MIDI_EVENT_PACKET_SIZE);

    LEDS.show();
}
