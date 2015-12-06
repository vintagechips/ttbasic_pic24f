/*
	Filename: led.c
	Description: LED API
*/

#include <xc.h>

void led_init(){
	TRISB &= 0b1000111111111111;//set RB12-RB14 as output
	AD1PCFG |= 0b0001110000000000;//set AN10-AN12 as digital

    LATBbits.LATB12 = 1;// off
    LATBbits.LATB13 = 1;// off
    LATBbits.LATB14 = 1;// off
}

void led_write(short no, unsigned char sw){
    switch(no){
        case 0:
            LATBbits.LATB12 = sw;
            break;
        case 1:
            LATBbits.LATB13 = sw;
            break;
        case 2:
            LATBbits.LATB14 = sw;
            break;
        default:
            LATBbits.LATB12 = sw;
            LATBbits.LATB13 = sw;
            LATBbits.LATB14 = sw;
            break;
    }
}
