#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <adc0.h>
#include "clock.h"
#include "wait.h"
#include "uart0.h"
#include "tm4c123gh6pm.h"
#include "graphics_lcd.h"

#define FLATHEAD      (*((volatile uint32_t *)(0x42000000 + (0x400243FC-0x40000000)*32 + 5*4)))
#define GREEN_LED    (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 3*4)))
#define RED_LED      (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 1*4)))
#define BLUE_LED     (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 2*4)))
#define PUSH_BUTTON  (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 4*4)))
#define DATA      (*((volatile uint32_t *)(0x42000000 + (0x400063FC-0x40000000)*32 + 6*4)))
#define PD_CLK    (*((volatile uint32_t *)(0x42000000 + (0x400063FC-0x40000000)*32 + 4*4)))

// PortC masks
#define DATA_MASK 64
#define PD_CLK_MASK 16

// PortD masks
#define FREQ_IN_MASK 1

// PortF masks
#define PWM_MASK 2
#define BLUE_LED_MASK 4
#define FLATHEAD_MASK 32
#define RED_LED_MASK 2
#define GREEN_LED_MASK 8
#define PUSH_BUTTON_MASK 16
#define AIN3_MASK 8

#define MAX_CHARS 80
#define MAX_FIELDS 5

uint32_t breathingCount = 0;
bool pulseActive = false;
uint32_t frequency = 0;
uint32_t pulseTime = 0;
uint32_t breathTime = 0;
uint32_t breathCountUp = 0;
uint32_t breathCountDown = 0;
int count = 0;
uint32_t valuePrevious;
bool breathDown = true;
bool breathUp = false;
bool breathActive = true;
double sec;
double bPerSec;
int x[5] = {0, 0, 0, 0, 0};
uint32_t sum = 0;
uint32_t avg = 0;
uint8_t index = 0;
float bPerMin;
float brPerMin;
int pulseMin = 0;
int pulseMax = 300;
int breathMin = 5;
int breathMax = 100;
int count1 = 0;

uint16_t raw1;
uint16_t raw2;
int16_t diff;

typedef struct _USER_DATA
{
    char buffer[MAX_CHARS+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

int32_t atoi1(char *str) {
    int n = 0;
    int i;
    for (i = 0; str[i] != '\0'; ++i)
            n = n * 10 + str[i] - '0';
    return n;
}

int32_t strcmp1(char *str1, char *str2) {
    int i = 0;
    while(str1[i] != '\0' || str2[i] != '\0') {
        if(str1[i] != str2[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

void getsUart0(USER_DATA *data) {
    int count = 0;
    char x;
    int i = 1;
    while(i) {
        x = getcUart0();
        if((x == 8 || x == 127) && count > 0) {
            count--;
        }
        else if(x == 13) {
            data->buffer[count] = '\0';
            i = 0;
        }
        else if(x >= 32) {
            data->buffer[count] = x;
            if(count == MAX_CHARS) {
                data->buffer[count] = '\0';
                i = 0;
            }
            count++;
        }
    }
}

void parseFields(USER_DATA *data) {
    int index = 0;
    char temp;
    char delim = 'd';
    data->fieldCount = 0;
    while(data->buffer[index] != '\0') {
        if(delim == 'd') {
            temp = delim;
            if((data->buffer[index] >= 65 && data->buffer[index] <= 90) || (data->buffer[index] >= 97 && data->buffer[index] <= 122)) {
                delim = 'a';
            }
            else if(data->buffer[index] >= 48 && data->buffer[index] <= 57) {
                delim = 'n';
            }
            else
                data->buffer[index] = '\0';
            index++;
        }
        else
            delim = 'd';
        if(temp != delim && delim != 'd' && (data->buffer[index-2] == '\0' || index == 0)) {
            data->fieldPosition[data->fieldCount] = index - 1;
            data->fieldType[data->fieldCount] = delim;
            data->fieldCount++;
        }
    }

}

char* getFieldString(USER_DATA *data, uint8_t fieldNumber) {
    if(data->fieldCount >= fieldNumber) {
        return (data->buffer + data->fieldPosition[fieldNumber]);
    }
    else
        return NULL;
}

int32_t getFieldInteger(USER_DATA *data, uint8_t fieldNumber) {
    if(data->fieldCount >= fieldNumber && data->fieldType[fieldNumber] == 'n') {
        return atoi1(data->buffer + data->fieldPosition[fieldNumber]);
    }
    else
        return 0;
}

bool isCommand(USER_DATA *data, char strCommand[], uint8_t minArguments) {
    if(strcmp1(strCommand, data->buffer) && (data->fieldCount - 1 >= minArguments)) {
        return true;
    }
    else
        return false;
}

void portCIsr()
{
        uint32_t value = 0;
        char str[24];

        int i;
        for(i = 0; i < 24; i++) {
            PD_CLK = 1;
            waitMicrosecond(3);
            PD_CLK = 0;
            value <<= 1;
            value |= DATA;
            _delay_cycles(10);
        }
        PD_CLK = 1;
        waitMicrosecond(3);
        PD_CLK = 0;
        _delay_cycles(10);
        if(valuePrevious < value) {
            breathCountUp++;
            breathCountDown = 0;
        }
        else if(valuePrevious > value) {
            breathCountDown++;
            breathCountUp = 0;
        }

        if(breathCountUp >= 5 && breathDown) {
            breathDown = false;
            breathUp = true;
            breathActive = true;
            brPerMin = (60 / (breathingCount / 10.0));

            snprintf(str, sizeof(str), "Breaths Per Min: %d\n", (int)(brPerMin));
            putsUart0(str);

            if(brPerMin < breathMin || brPerMin > breathMax) {
                BLUE_LED = 1;
                PWM1_0_CMPB_R = 10000;
                //waitMicrosecond(1000000);
                PWM1_0_CMPB_R = 0;
            }
            else {
                BLUE_LED = 0;
                PWM1_0_CMPB_R = 0;
            }
            breathingCount = 0;
            GREEN_LED ^= 1;
            TIMER2_TAILR_R = 40000000 * 5;
        }
        else if(breathCountDown >= 5 && breathUp) {
            breathDown = true;
            breathUp = false;
        }
        valuePrevious = value;
        breathingCount++;
        GPIO_PORTC_ICR_R = DATA_MASK;
}

void enableTimer1()
{
    // Configure Timer 1 as the time base
    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER1_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER1_TAMR_R = TIMER_TAMR_TAMR_PERIOD;          // configure for periodic mode (count down)
    TIMER1_TAILR_R = 40000000;                       // set load value to 40e6 for 1 Hz interrupt rate
    TIMER1_IMR_R = TIMER_IMR_TATOIM;                 // turn-on interrupts
    TIMER1_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    NVIC_EN0_R |= 1 << (INT_TIMER1A-16);             // turn-on interrupt 37 (TIMER1A)
}

void enableTimer2()
{
    // Configure Timer 1 as the time base
    TIMER2_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER2_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER2_TAMR_R = TIMER_TAMR_TAMR_PERIOD;          // configure for periodic mode (count down)
    TIMER2_TAILR_R = 40000000 * 5;                       // set load value to 40e6 for 1 Hz interrupt rate (5 seconds)
    TIMER2_IMR_R = TIMER_IMR_TATOIM;                 // turn-on interrupts
    TIMER2_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    NVIC_EN0_R |= 1 << (INT_TIMER2A-16);             // turn-on interrupt 37 (TIMER1A)
}

void enableTimerMode()
{
    WTIMER2_CTL_R &= ~TIMER_CTL_TAEN;                // turn-off counter before reconfiguring
    WTIMER2_CFG_R = 4;                               // configure as 32-bit counter (A only)
    WTIMER2_TAMR_R = TIMER_TAMR_TACMR | TIMER_TAMR_TAMR_CAP | TIMER_TAMR_TACDIR; // configure for edge time mode, count up
    WTIMER2_CTL_R = TIMER_CTL_TAEVENT_POS;           // measure time from positive edge to positive edge
    WTIMER2_IMR_R = TIMER_IMR_CAEIM;                 // turn-on interrupts
    WTIMER2_TAV_R = 0;                               // zero counter for first period
    WTIMER2_CTL_R |= TIMER_CTL_TAEN;                 // turn-on counter
    NVIC_EN3_R |= (1 << (INT_WTIMER2A-16-96));       // turn-on interrupt 112 (WTIMER2A)
}

void timer1Isr()
{
    int diff;
    char str[24];
    if(!pulseActive)
    {
        raw1 = readAdc0Ss3();
        FLATHEAD = 1;
        waitMicrosecond(10000);
        raw2 = readAdc0Ss3();
        FLATHEAD = 0;
        if(raw2 >= raw1)
        {
            diff = raw2 - raw1;
        }
        else
        {
            diff = raw1 - raw2;
        }
        if(diff > 50) {
            FLATHEAD = 1;
            pulseActive = true;
        }
    }
    else if(pulseActive) {
       raw1 = readAdc0Ss3();
       if(raw1 < 3000) {
           FLATHEAD = 0;
           pulseActive = false;
       }
       WTIMER2_CTL_R |= TIMER_CTL_TAEN;                 // turn-on counter
       NVIC_EN3_R |= 1 << (INT_WTIMER2A-16-96);         // turn-on interrupt 112 (WTIMER2A)
    }
    sum -= x[index];
    sum += pulseTime / 40;
    x[index] = pulseTime / 40;
    index = (index + 1) % 5;
    avg = sum / 5;

    sec = avg / (1e6);
    bPerSec = 1 / sec;
    bPerMin = bPerSec * 60;
    count1++;
    snprintf(str, sizeof(str), "Beats Per Min: %d\n", (int)(bPerMin));
            putsUart0(str);
    TIMER1_ICR_R = TIMER_ICR_TATOCINT;
}

void timer2Isr() {
    brPerMin = 0;
    breathActive = false;
    TIMER2_ICR_R = TIMER_ICR_TATOCINT;
}

void wideTimer2Isr()
{
    GREEN_LED ^= 1;
    if(pulseActive) {
        pulseTime = WTIMER2_TAV_R;                        // read counter input
        WTIMER2_TAV_R = 0;                           // zero counter for next edge

//        sum -= x[index];
//        sum += pulseTime / 40;
//        x[index] = pulseTime / 40;
//        index = (index + 1) % 5;
//        avg = sum / 5;
//
//        sec = avg / (1e6);
//        bPerSec = 1 / sec;
//        bPerMin = bPerSec * 60;
//        count1++;

    }
    if((bPerMin < pulseMin || bPerMin > pulseMax) && count1 > 5 && pulseActive) {
        RED_LED = 1;
        PWM1_0_CMPB_R = 30000;
        waitMicrosecond(1000000);
        PWM1_0_CMPB_R = 0;
        pulseActive = false;
        TIMER1_CTL_R |= TIMER_CTL_TAEN;
        NVIC_EN0_R |= 1 << (INT_TIMER1A-16);
    }
    if(!pulseActive || (bPerMin > pulseMin && bPerMin < pulseMax) && count1 > 5 && pulseActive) {
        RED_LED = 0;
        PWM1_0_CMPA_R = 0;
    }
    WTIMER2_ICR_R = TIMER_ICR_CAECINT;           // clear interrupt flag
}

void initHw()
{
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Enable clocks
    SYSCTL_RCGCWTIMER_R |= SYSCTL_RCGCWTIMER_R2;
    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R1 | SYSCTL_RCGCTIMER_R2;
    SYSCTL_RCGCPWM_R |= SYSCTL_RCGCPWM_R1;
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R5 | SYSCTL_RCGCGPIO_R4 | SYSCTL_RCGCGPIO_R3 | SYSCTL_RCGCGPIO_R2 | SYSCTL_RCGCGPIO_R1;
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    GPIO_PORTF_DIR_R |= GREEN_LED_MASK | BLUE_LED_MASK | RED_LED_MASK;  // bits 1 and 2 are outputs, other pins are inputs
    GPIO_PORTF_DIR_R &= ~PUSH_BUTTON_MASK;               // bit 4 is an input
    GPIO_PORTF_DR2R_R |= GREEN_LED_MASK | BLUE_LED_MASK | RED_LED_MASK; // set drive strength to 2mA (not needed since default configuration -- for clarity)
    GPIO_PORTF_DEN_R |= PUSH_BUTTON_MASK | GREEN_LED_MASK | BLUE_LED_MASK | RED_LED_MASK;
                                                         // enable LEDs and pushbuttons
    GPIO_PORTF_PUR_R |= PUSH_BUTTON_MASK;                // enable internal pull-up for push button

    // Configure SIGNAL_IN for frequency and time measurements
    GPIO_PORTD_AFSEL_R |= FREQ_IN_MASK;              // select alternative functions for SIGNAL_IN pin
    GPIO_PORTD_PCTL_R &= ~GPIO_PCTL_PD0_M;           // map alt fns to SIGNAL_IN
    GPIO_PORTD_PCTL_R |= GPIO_PCTL_PD0_WT2CCP0;
    GPIO_PORTD_DEN_R |= FREQ_IN_MASK;                // enable bit 6 for digital input

    GPIO_PORTE_DIR_R |= FLATHEAD_MASK;   // bits 1 and 3 are outputs, other pins are inputs
    GPIO_PORTE_DR2R_R |= FLATHEAD_MASK;  // set drive strength to 2mA (not needed since default configuration -- for clarity)
    GPIO_PORTE_DEN_R |= FLATHEAD_MASK;

    // Configure AIN3 as an analog input
    GPIO_PORTE_AFSEL_R |= AIN3_MASK;                 // select alternative functions for AN3 (PE0)
    GPIO_PORTE_DEN_R &= ~AIN3_MASK;                  // turn off digital operation on pin PE0
    GPIO_PORTE_AMSEL_R |= AIN3_MASK;                 // turn on analog operation on pin PE0


    GPIO_PORTC_DIR_R |= PD_CLK_MASK;  // bits 1 and 2 are outputs, other pins are inputs
    GPIO_PORTC_DIR_R &= ~DATA_MASK;               // bit 4 is an input
    GPIO_PORTC_DR2R_R |= DATA_MASK | PD_CLK_MASK; // set drive strength to 2mA (not needed since default configuration -- for clarity)

    GPIO_PORTC_IM_R &= ~DATA_MASK;
    GPIO_PORTC_IS_R &= ~DATA_MASK;
    GPIO_PORTC_IBE_R &= ~DATA_MASK;
    GPIO_PORTC_IEV_R &= ~DATA_MASK;
    GPIO_PORTC_RIS_R &= ~DATA_MASK;
    GPIO_PORTC_IM_R |= DATA_MASK;
    NVIC_EN0_R |= 1 << (INT_GPIOC-16);

    GPIO_PORTC_DEN_R |= DATA_MASK | PD_CLK_MASK;

    //pwm stuff

    GPIO_PORTD_DEN_R |= PWM_MASK;
    GPIO_PORTD_AFSEL_R |= PWM_MASK;
    GPIO_PORTD_PCTL_R &= ~(GPIO_PCTL_PD1_M);
    GPIO_PORTD_PCTL_R |= GPIO_PCTL_PD1_M1PWM1;

    SYSCTL_SRPWM_R = SYSCTL_SRPWM_R1;                // reset PWM1 module
    SYSCTL_SRPWM_R = 0;                              // leave reset state
    PWM1_0_CTL_R = 0;                                // turn-off PWM1 generator 0 (drives outs 0 and 1)
    PWM1_0_GENB_R = PWM_0_GENB_ACTCMPBD_ONE | PWM_0_GENB_ACTLOAD_ZERO; // output 0 on PWM1, gen 0A, cmpa
    PWM1_0_LOAD_R = 40000;                            // set frequency to 40 MHz sys clock / 2 / 1024 = 19.53125 kHz
    PWM1_0_CMPB_R = 0;                               // red off (0=always low, 1023=always high)
    PWM1_0_CTL_R = PWM_1_CTL_ENABLE;                 // turn-on PWM1 generator 0
    PWM1_ENABLE_R = PWM_ENABLE_PWM1EN;               // enable outputs
}


int main(void)

{
    initHw();
    initUart0();
    initAdc0Ss3();
    setAdc0Ss3Mux(0);
    setAdc0Ss3Log2AverageCount(2);
    setUart0BaudRate(115200, 40e6);
    enableTimer1();
    enableTimer2();
    enableTimerMode();

    FLATHEAD = 0;

    USER_DATA data;
    char str[32];
    char *str1;
    int argu1 = 0;
    int argu2 = 0;


    while(1) {
         getsUart0(&data);
         parseFields(&data);
         if(isCommand(&data, "pulse", 0)) {
             if(pulseActive) {
                 snprintf(str, sizeof(str), "Beats Per Min: %d\n", (int)(bPerMin));
                 putsUart0(str);
             }
             else if(!pulseActive) {
                 putsUart0("No Pulse Detected\n");
             }
         }
         else if(isCommand(&data, "respiration", 0)) {
              if(breathActive) {
                  snprintf(str, sizeof(str), " Breaths Per Min: %d\n", (int)(brPerMin));
                  putsUart0(str);
              }
              else if(!breathActive) {
                  putsUart0("No Pulse Detected\n");
              }
          }
         else if(isCommand(&data, "alarm", 3)) {
              str1 = getFieldString(&data, 1);
              argu1 = getFieldInteger(&data, 2);
              argu2 = getFieldInteger(&data, 3);
              if(strcmp1(str1, "pulse")) {
                  if(argu1 && argu2) {
                      pulseMin = argu1;
                      pulseMax = argu2;
                  }
                  else
                      putsUart0("Invalid min and max\n");
              }
              if(strcmp1(str1, "respiration")) {
                if(argu1 < argu2) {
                    breathMin = argu1;
                    breathMax = argu2;
                }
                else
                    putsUart0("Invalid min and max\n");
            }
          }
         else if(isCommand(&data, "speaker", 1)) {
              str1 = getFieldString(&data, 1);
              if(strcmp1(str1, "ON"))
                  PWM1_ENABLE_R = PWM_ENABLE_PWM1EN;
              else if(strcmp1(str1, "OFF"))
                  PWM1_ENABLE_R = ~PWM_ENABLE_PWM1EN;
          }
         else {
             putsUart0("Invalid Command\n");
         }
     }
}
