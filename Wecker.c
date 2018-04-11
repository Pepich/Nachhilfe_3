#include <stdlib.h>
#include <avr/io.h>
#include <util/setbaud.h>
#include <util/delay.h>

// Wird von util/delay.h verwendet
#define F_CPU = 16000000

// Quality of life. Ich bin true und false gewöhnt :P
#define true  1
#define false 0

// Buttons angeschlossen an PB0-PB3 (hardware, hier als define, sodass es einfacher geändert werden kann falls sich die Hardware ändert)
#define btn_ok     1<<PB0
#define btn_cancel 1<<PB1
#define btn_up     1<<PB2
#define btn_down   1<<PB3

/* 7 segmentanzeige:
    
   disp_0:      disp_1:    ...

    00000        00000      
   1     6      1     6     
   1     6      1     6   
   1     6      1     6   
    22222        22222  
   3     5      3     5  
   3     5      3     5  
   3     5      3     5   
    44444  dp    44444  dp

*/
#define seg_0  1<<PC0
#define seg_1  1<<PC1
#define seg_2  1<<PC2
#define seg_3  1<<PC3
#define seg_4  1<<PC4
#define seg_5  1<<PC5
#define seg_6  1<<PC6
#define seg_dp 1<<PC7 // Benutzt als punkt zwischen stunde.minute.sekunde - blinkt im normalbetrieb jede Sekunde, dauerhaft an wenn zeit eingestellt wird

#define disp_hour_tens   1<<PD0
#define disp_hour_ones   1<<PD1
#define disp_minute_tens 1<<PD2
#define disp_minute_ones 1<<PD3
#define disp_second_tens 1<<PD4
#define disp_second_ones 1<<PD5

#define s_settime_hours    0
#define s_settime_minutes  1
#define s_settime_seconds  2
#define s_setalarm_hours   4
#define s_setalarm_minutes 5
#define s_setalarm_seconds 6
#define s_normal           7
#define s_alarm            8

int status;
/* Dezimalpunkt = 7
 0: 0, 1, 3, 4, 5, 6
 1: 5, 6
 2: 0, 2, 3, 4, 6
 3: 0, 2, 4, 5, 6
 4: 1, 2, 5, 6
 5: 0, 1, 2, 4, 5
 6: 0, 1, 2, 3, 4, 5
 7: 0, 5, 6
 8: 0, 1, 2, 3, 4, 5, 6
 9: 0, 1, 2, 4, 5, 6
*/
int[] number_map = {0b01111011, 0b01100000, 0b01011101, 0b01110101, 0b01100110, 0b00110111, 0b00111111, 0b01100001, 0b01111111, 0b01110111};

typedef struct {
    int hour;
    int minute;
    int second;
} time;

time uhrzeit;
time wecker;

typedef struct {
    int hour_tens;
    int hour_ones;
    int minutes_tens;
    int minutes_ones;
    int second_tens;
    int second_ones;
} bcd_time;

bcd_time output;

/** int Init(void)
 *  Initialisierung der CPU Register (GPIOs, Timer/Counter)
 */
int init(void)
{
    // Buttons = eingänge
    DDRB &= ~btn_ok;
    // DDRB ...

    // Der Einfachheit halber: Verwende interne pullups (Verfügbarkeit CPU Abhängig, ich gehe hier von einem ATMega328p aus)
    PORTB |= btn_ok;
    // PORTB ...

    // Variablen initialisieren
    uhrzeit = {0, 0, 0};
    // wecker = ...
    // output = ...
    status = s_settime;
}


/** int getButton(void)
 *  Gibt den momentanen Status der Eingabebuttons zurück. Zu verwenden mit pressed(), oder mit den definitionen von btn_ok_pin, etc.
 *  Jeder button wird nur EINMAL als gedrückt angegeben, wird ein button gehalten und diese Funktion erneut aufgerufen, so wird er als "nicht gedrückt" zurückgegeben.
 */
int getButton(void)
{
    static int old_button = 0;
    // Simuliere atomares Verhalten (wenn sich PINB ändert während wir diese Funktion ausführen, sollen für die Rückgabe und für "old_button" die gleichen Bedingungen sein)
    int pinb = PINB;
    int result = pinb & (btn_ok | btn_cancel /* ... */ );
    result &= ~old_button;
    old_button = pinb;
    return result;
}

/** int pressed(int status, int button)
 *  status = Rückgabewert von getButton
 *  button = ???_pin -> wenn ok button abgefragt wird, dann btn_ok_pin
 *  Gibt 0 zurück wenn der button nicht gedrückt ist, sonst gibt button zurück
 */
int pressed(int status, int button)
{
    return status & button;
}

/** int twodigitBCD(int num)
 *  Wandelt eine Zahl 0-99 in zweistellig BCD um. Gibt ~0 zurück, wenn ein Bereichsfehler auftritt.
 *  Format: ttttttttoooooooo
 * t = tens, o = ones
 */
unsigned int twodigitBCD(int num)
{
    if ((num < 0) || (num > 99))
        return ~0;
    // Ziemlich geschummelt, ich weiß, funktioniert aber super :P
    //     Zehnerstelle  | Einerstelle
    return (num/10 << 8) | (num%10);
}

int tens(unsigned int bcd)
{
    return bcd >> 8;
}

int ones(unsigned int bcd)
{
    return (bcd & 255);
}

/** void decToBCD(void)
 *  Bereitet die globale Variable "output" für die Ausgabe vor.
 *  Füllt die BCD-Variablen abhängig vom momentanen Status mit der Uhrzeit, oder dem Alarm.
 */
void decToBCD(void)
{
    switch (status)
    {
        case s_setalarm:
        {
            unsigned int temp_bcd = twodigitBCD(alarm.hours);
            output.hour_tens = tens(temp_bcd);
            output.hour_ones = ones(temp_bcd);
            temp_bcd = twodigitBCD(alarm.minutes);
            // output...
            break;
        }
        case s_settime:
        case s_normal:
        case s_ringing:
        {
            unsigned int temp_bcd = twodigitBCD(uhrzeit.hours);
            // output...
            break;
        }
    }
}

/** void updateDisplay(void)
 *  Gibt die momentane Uhrzeit (oder den Wecker, je nach status) auf einem angeschlossenen 7-seg aus.
*/
void updateDisplay(int status)
{
    // Variable output vorbereiten
    decToBCD();
    // Und display aktualisieren (Multiplex)
    // 
    for (int i = 0; i < 6; i++)
    {
        switch (i)
        {
            case 0:
            {
                PORTC = number_map[output.hour_tens];
                PORTD = disp_hour_tens;
                break;
            }
            case 1:
            {
                PORTC = number_map[output.hour_ones];
                // Blinkender Dezimalpunkt, oder dauerhaft an während des Einstellens
                PORTC |= ((status < 7) || (time.second_ones % 2)) ? 128 : 0;
                PORTD = disp_hour_ones;
                break;
            }
            case 2:
            {
                // ...
            }
            // ...
        }
        // 100 Hz multiplex
        _delay_ms(10);
    }
}

int timer_interrupt(void)
{
   if ((uhrzeit.second += 1) == 60)
   {
       uhrzeit.second = 0;
       if ((uhrzeit.minute += 1 ) == 60)
       {
           uhrzeit.second = 0;
           uhrzeit.hour = (uhrzeit.hour + 1) % 24;
       }
   }
}

int main(void)
{
    if (init() != 0)
    {
        // Fehler (nicht genutzt)
    }
    
    while (true)
    {
        int buttons = getButton;

        switch (status)
        {                
            case s_settime_hours:
            {
                if (pressed(buttons, btn_ok))
                    status++;
                else if (pressed(buttons, btn_cancel))
                    status = s_setalarm_hours;
                else if (pressed(buttons, btn_up))
                    uhrzeit.hours = (uhrzeit.hours + 1) % 24;
                else if (pressed(buttons_))
                    uhrzeit.hours = (uhrzeit.hours + 23) % 24;
                break;
            }
            case s_settime_minutes:
            {
                // ...
                break;
            }
            // s_settime_seconds, s_setalarm...
            case s_normal:
            {
                if ((uhrzeit.hour == alarm.hour) /* && (...) && (...)*/)
                    status = s_alarm;
                else if (pressed(buttons, btn_ok))
                    status = s_settime_hours;
                else if (pressed(buttons, btn_cancel))
                    status = s_setalarm_hours;
                
                break;
            }
            case s_alarm:
            {
                // beep() (nicht implementiert, weil Töne zu generieren ist doof. Müsste aber hier hin, wäre in einer Klausur wahrscheinlich vorgegeben.)
                if (pressed(buttons, btn_up) || pressed(buttons, btn_down))
                    status = s_normal;
                break;
            }
        }
        updateDisplay();
    }
}