/*
	Filename: sw.c
	Description: switch API
*/

#include <xc.h>

unsigned int swstat;

void
__attribute__((interrupt, no_auto_psv))
_CNInterrupt(void){
	swstat = PORTB & 0b0000000000000111;
	IFS1bits.CNIF = 0;
}

void delay_us(unsigned int t){
	while(t--){
		Nop(); Nop(); Nop(); Nop();
		Nop(); Nop(); Nop(); Nop();
		Nop(); Nop(); Nop();
	}
}

void delay_ms(unsigned int t){
	while(t--)
		delay_us(1000);
}
void sw_init(){
	TRISB |= 0b0000000000000111;//set RB0-RB2 as input
	AD1PCFG |= 0b0000000000011100;//set AN2-AN4 as digital
	CNEN1 |= 0b0000000001110000;
	CNPU1 |= 0b0000000001110000;
	delay_ms(10);
	swstat = PORTB & 0b0000000000000111;//Dummy read

	IFS1bits.CNIF = 0;
	IEC1bits.CNIE = 1;
}
