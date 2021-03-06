/******************************************************************************
******************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include <LPC8xx.h>
#include <globals.h>

static bool next = false;
static uint16_t servo_pulse;

static uint16_t gearbox_servo_counter;
static bool gearbox_servo_active;

static SERVO_ENDPOINTS_T servo_setup_endpoint;
SERVO_ENDPOINTS_T servo_output_endpoint;


// ****************************************************************************
static bool servo_output_disabled(void)
{
    if (config.flags.gearbox_servo_output) {
        return false;
    }

    if (config.flags.steering_wheel_servo_output) {
        return false;
    }

    return true;
}


// ****************************************************************************
static void activate_gearbox_servo(void)
{
    gearbox_servo_active = true;
    gearbox_servo_counter = config.gearbox_servo_active_time;
    LPC_SCT->OUT[1].SET = (1 << 0);        // Re-enable event 0 to set CTOUT_1
}


// ****************************************************************************
void init_servo_output(void) {
    if (servo_output_disabled()) {
        return;
    }

    LPC_SCT->CONFIG |= (1 << 18);           // Auto-limit on counter H
    LPC_SCT->CTRL_H |= (1 << 3) |           // Clear the counter H
                       (11 << 5);           // PRE_H[12:5] = 12-1 (SCTimer H clock 1 MHz)
    LPC_SCT->MATCHREL[0].H = 20000 - 1;     // 20 ms per overflow (50 Hz)
    LPC_SCT->MATCHREL[4].H = 1500;          // Servo pulse 1.5 ms intially

    LPC_SCT->EVENT[0].STATE = 0xFFFF;       // Event 0 happens in all states
    LPC_SCT->EVENT[0].CTRL = (0 << 0) |     // Match register 0
                             (1 << 4) |     // Select H counter
                             (0x1 << 12);   // Match condition only

    LPC_SCT->EVENT[4].STATE = 0xFFFF;       // Event 4 happens in all states
    LPC_SCT->EVENT[4].CTRL = (4 << 0) |     // Match register 4
                             (1 << 4) |     // Select H counter
                             (0x1 << 12);   // Match condition only

    // We've chosen CTOUT_1 because CTOUT_0 resides in PINASSIGN6, which
    // changing may affect CTIN_1..3 that we need.
    // CTOUT_1 is in PINASSIGN7, where no other function is needed for our
    // application.
    LPC_SCT->OUT[1].SET = (1 << 0);        // Event 0 will set CTOUT_1
    LPC_SCT->OUT[1].CLR = (1 << 4);        // Event 4 will clear CTOUT_1

    // CTOUT_1 = PIO0_12
    LPC_SWM->PINASSIGN7 = 0xffffff0c;

    LPC_SCT->CTRL_H &= ~(1 << 2);          // Start the SCTimer H


    global_flags.gear = GEAR_1;
    activate_gearbox_servo();
}


// ****************************************************************************
void gearbox_action(uint8_t ch3_clicks)
{
    if (!config.flags.gearbox_servo_output) {
        return;
    }

    // 2-speed gearbox: one click is Gear 1; two clicks is Gear 2
    if (config.number_of_gears == 2) {
        if (ch3_clicks == 1) {
            global_flags.gear = GEAR_1;
        }
        else {
            global_flags.gear = GEAR_2;
        }
    }

    // 3-speed gearbox: one click switches gear up, two clicks gear down
    else {
        if (ch3_clicks == 1) {
            if (global_flags.gear == GEAR_1) {
                global_flags.gear = GEAR_2;
            }
            else {
                global_flags.gear = GEAR_3;
            }
        }
        else {
            if (global_flags.gear == GEAR_3) {
                global_flags.gear = GEAR_2;
            }
            else {
                global_flags.gear = GEAR_1;
            }
        }
    }

    // FIXME: we need to ensure this is active for one mainloop!
    global_flags.gear_changed = true;
    activate_gearbox_servo();
}


// ****************************************************************************
void servo_output_setup_action(uint8_t ch3_clicks)
{
    if (servo_output_disabled()) {
        return;
    }

    if (global_flags.servo_output_setup == SERVO_OUTPUT_SETUP_OFF) {
        servo_output_endpoint.left = 900;
        servo_output_endpoint.centre = 1500;
        servo_output_endpoint.right = 2100;
        global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_LEFT;
    }
    else {
        if (ch3_clicks == 1) {
            // 1 click: next setup step
            next = true;
            //global_flags.steering_wheel_servo_setup_mode = STEERING_WHEEL_SERVO_SETUP_MODE_NEXT;
        }
        else {
            // More than 1 click: cancel setup
            global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_OFF;
            load_persistent_storage();
        }
    }
}


/******************************************************************************

    This function calculates:

          (right - centre) * abs(steering)
          -------------------------------- + centre
                    100

    To ease calculation we first do right - centre, then calculate its absolute
    value but store the sign. After multiplication and division using the
    absolute value we re-apply the sign, then add centre.

    Note: this function is needed by Process_servo_setup, so it can't be
    removed e.g. if only a gearbox servo is used.

******************************************************************************/
static void calculate_servo_pulse(void)
{
    if (channel[ST].normalized < 0) {
        servo_pulse = servo_output_endpoint.centre -
            (((servo_output_endpoint.centre - servo_output_endpoint.left) *
                channel[ST].absolute) / 100);
    }
    else {
        servo_pulse = servo_output_endpoint.centre +
            (((servo_output_endpoint.right - servo_output_endpoint.centre) *
                channel[ST].absolute) / 100);
    }
}


// ****************************************************************************
void process_servo_output(void)
{
    if (servo_output_disabled()) {
        return;
    }

    // --------------------------------
    // Servo output setup is active
    if (global_flags.servo_output_setup != SERVO_OUTPUT_SETUP_OFF) {
        calculate_servo_pulse();
        if (next) {
            next = false;

            switch (global_flags.servo_output_setup) {
                case SERVO_OUTPUT_SETUP_LEFT:
                    servo_setup_endpoint.left = servo_pulse;
                    global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_CENTRE;
                    break;

                case SERVO_OUTPUT_SETUP_CENTRE:
                    servo_setup_endpoint.centre = servo_pulse;

                    // In case we are dealing with a 2-speed gearbox we only
                    // configure left and center endpoint
                    if (config.flags.gearbox_servo_output &&
                            config.number_of_gears == 2) {
                        servo_output_endpoint.centre = servo_setup_endpoint.centre;
                        servo_output_endpoint.left = servo_setup_endpoint.left;
                        write_persistent_storage();

                        global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_OFF;
                    }
                    else {
                        global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_RIGHT;
                    }
                    break;

                case SERVO_OUTPUT_SETUP_RIGHT:
                    servo_setup_endpoint.right = servo_pulse;

                    servo_output_endpoint.right = servo_setup_endpoint.right;
                    servo_output_endpoint.centre = servo_setup_endpoint.centre;
                    servo_output_endpoint.left = servo_setup_endpoint.left;
                    write_persistent_storage();

                    global_flags.servo_output_setup = SERVO_OUTPUT_SETUP_OFF;
                    break;

                default:
                    break;
            }
        }
    }

    // --------------------------------
    // Gearbox servo is active
    else if (config.flags.gearbox_servo_output) {

        if (config.gearbox_servo_idle_time && global_flags.systick) {
            if (gearbox_servo_counter) {
                --gearbox_servo_counter;
            }
            else {
                if (gearbox_servo_active) {
                    // Turn off the setting of CTOUT_1, so no pulse will be
                    // generated. However, clearing of CTOUT_1 is still active
                    // through event 0, so if a pulse is currently active
                    // it will be nicely terminated, and from the next period
                    // onwards the pulses will cease.
                    LPC_SCT->OUT[1].SET = 0;
                    gearbox_servo_counter = config.gearbox_servo_idle_time;
                    gearbox_servo_active = false;
                }
                else {
                    activate_gearbox_servo();
                }
            }
        }

        if (global_flags.gear == GEAR_1) {
            servo_pulse = servo_output_endpoint.left;
        }
        else if (global_flags.gear == GEAR_2) {
            servo_pulse = servo_output_endpoint.centre;
        }
        else if (global_flags.gear == GEAR_3) {
            servo_pulse = servo_output_endpoint.right;
        }
    }

    // --------------------------------
    // Steering wheel servo output is avtive
    else if (config.flags.steering_wheel_servo_output) {
        calculate_servo_pulse();
    }


    // Put the servo pulse duration in milliseconds into the match register
    // to output the pulse of the given duration.
    LPC_SCT->MATCHREL[4].H = servo_pulse;
}


