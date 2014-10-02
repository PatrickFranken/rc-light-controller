/******************************************************************************


    Light scenarios to consider

    - Always-on of certain LEDs
    - Dynamic discover of light_mode maximum value
    - Combined tail / brake lights
    - Combined tail / brake / indicator lights (XR311, Sawback)
    - Hazard and indicators
    - High priority situations (in order or priority)
        - Startup
        - Setup of steering, throttle reverse
        - Setup of the output servo centre, left, right
        - Winch active
    - Events
        - Can trigger a sequence
        - Gear change
        -   Different sequence depending on gear value
        -   Different sequence depending on state of roof lights?
        - Any other?
    - Programmabe sequences
        - Store difference with previous state
        - Steps can have a delay
        - Run once or always
        - Events can trigger a sequence, while another sequence is running
          and needs to continue after the event sequence finished.
        - How to map sequences and non-sequences?
        - Automatic fading between two steps?
        - Resolution 20ms
        - Bogdan's idea regarding flame simulation, depending on throttle

    - Consider single and multi-color LEDs
    - Consider half brightness is not half LED current
    - Simulation of incadescent bulbs
    - Simulation of weak ground connection
    - Support weird, unforeseen combinations, like combined reverse/indicators
      on the Lancia Fulvia



    Combined tail / brake / indicator:

                             BLINKFLAG
                          on          off
     --------------------------------------
     Tail + Brake off     half        off
     Tail                 half        off
     Brake                full        off
     Tail + Brake         full        half

    Best is to pre-calculate this, as well as the indicators.


    Flags for static lights:
    - light_mode (max 16)
    - brakeing
    - reversing
    - indicator left
    - indicator left
    - hazard
    - blink flag
    - blink left (includes hazard, modulated with blink_flag)
    - blink right (includes hazard,  modulated with blink_flag)
    - brake / tail / blink left half (XR311)
    - brake / tail / blink right half (XR311)
    - brake / tail / blink left full (XR311)
    - brake / tail / blink right full (XR311)


            Tail | Brake | Ind | Blink      RESULT
            0      0       0     0          0
            0      0       0     1          0
            0      0       1     0          0
            0      0       1     1          half
            0      1       0     0          full
            0      1       0     1          full
            0      1       1     0          0
            0      1       1     1          full
            1      0       0     0          half
            1      0       0     1          half
            1      0       1     0          0
            1      0       1     1          half
            1      1       0     0          full
            1      1       0     1          full
            1      1       1     0          half
            1      1       1     1          full





******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <LPC8xx.h>

#include <globals.h>
#include <uart0.h>

static uint16_t light_mode;
static uint8_t tlc5940_light_data[16];

/*
SPI configuration:
    Configuration: CPOL = 0, CPHA = 0,
    We can send 6 bit frame lengths, so no need to pack light data!
    TXRDY indicates when we can put the next data into txbuf
    Use SSEL function to de-assert XLAT while sending new data
*/

#define GSCLK LPC_GPIO_PORT->W0[1]
#define BLANK LPC_GPIO_PORT->W0[6]
#define XLAT LPC_GPIO_PORT->W0[2]
#define SCK LPC_GPIO_PORT->W0[3]
#define SIN LPC_GPIO_PORT->W0[7]

#define LED_BRIGHTNESS_CONST_A        (0.08f)                       /* Set Point LED brightness equation: a * b ^ (brightness_level + c) ...                      */
#define LED_BRIGHTNESS_CONST_B        (1.75f)                       /* Constants have been set for the equations to produce distinctive brightness levels         */
#define LED_BRIGHTNESS_CONST_C        (2.00f)
#define LED_BRIGHTNESS_EQUATION(level) (LED_BRIGHTNESS_CONST_A * pow(LED_BRIGHTNESS_CONST_B, level + LED_BRIGHTNESS_CONST_C))



static void send_light_data_to_tlc5940(void)
{
    volatile int i;

    // Wait for MSTIDLE
    while (!(LPC_SPI0->STAT & (1u << 8)));

    for (i = 15; i >= 0; i--) {
        // Wait for TXRDY
        while (!(LPC_SPI0->STAT & (1u << 1)));

        LPC_SPI0->TXDAT = tlc5940_light_data[i];
    }

    // Force END OF TRANSFER
    LPC_SPI0->STAT = (1u << 7);
}


void init_lights(void)
{
    BLANK = 1;
    GSCLK = 0;
    XLAT = 0;

    LPC_GPIO_PORT->DIR0 |=
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 6) | (1u << 7);

    // Use 2 MHz SPI clock. 16 bytes take about 50 us to transmit.
    LPC_SPI0->DIV = (__SYSTEM_CLOCK / 2000000) - 1;

    LPC_SPI0->CFG = (1u << 0) |          // Enable SPI0
                    (1u << 2) |          // Master mode
                    (0 << 3) |          // LSB First mode disabled
                    (0 << 4) |          // CPHA = 0
                    (0 << 5) |          // CPOL = 0
                    (0 << 8);           // SPOL = 0

    LPC_SPI0->TXCTRL = (1u << 21) |      // set EOF
                       (1u << 22) |      // RXIGNORE, otherwise SPI hangs until
                                        //   we read the data register
                       ((6 - 1) << 24); // 6 bit frames

    // We use the SSEL function for XLAT: low during the transmission, high
    // during the idle periood.
    LPC_SWM->PINASSIGN3 = 0x03ffffff;   // PIO0_3 is SCK
    LPC_SWM->PINASSIGN4 = 0xff02ff07;   // PIO0_2 is XLAT (SSEL) PIO0_3 is SIN (MOSI)

    send_light_data_to_tlc5940();

    BLANK = 0;
    GSCLK = 1;
}


void next_light_sequence(void)
{
	;
}


void more_lights(void)
{
    // Switch light mode up (Parking, Low Beam, Fog, High Beam)
    light_mode = (uint16_t)(light_mode << 1);
    light_mode |= 1;
    light_mode &= config.light_mode_mask;
}


void less_lights(void)
{
    // Switch light mode down (Parking, Low Beam, Fog, High Beam)
    light_mode >>= 1;
    light_mode &= config.light_mode_mask;
}


void toggle_lights(void)
{
    if (light_mode == config.light_mode_mask) {
        light_mode = 0;
    }
    else {
        light_mode = config.light_mode_mask;
    }
}


void process_lights(void)
{
    static uint16_t old_light_mode = 0xffff;

    if (light_mode != old_light_mode) {
        old_light_mode = light_mode;
        uart0_send_cstring("light_mode ");
        uart0_send_uint32(light_mode);
        uart0_send_linefeed();
    }

    if (global_flags.systick) {
        if (global_flags.braking) {
            tlc5940_light_data[0] = 63;
        }
        else {
            tlc5940_light_data[0] = 0;
        }
        send_light_data_to_tlc5940();
    }
}
