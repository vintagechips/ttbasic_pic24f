/*	Filename: sci.c
	Description: UART API
*/

#include <xc.h>

#define BUF_SIZE 64

struct {
	char buf[BUF_SIZE];
	unsigned char wp;
	unsigned char rp;
	unsigned char dc;
} RX_BUF;

void sci2_init(void){
	TRISBbits.TRISB10 = 0; // TX
	TRISBbits.TRISB11 = 1; // RX

	RPOR5bits.RP10R = 5; // 5 = U2TX
	RPINR19bits.U2RXR = 11; // 11 = RP11

	U2BRG = 103; // 9600bps
	U2MODE = 0b1000100000000000;
	U2STA =  0b0000010000000000;

	IPC7bits.U2RXIP = 4;
	IFS1bits.U2RXIF = 0;
	IEC1bits.U2RXIE = 1;
}

void putch2(char c){
	while(U2STAbits.UTXBF);
	U2TXREG = c;
}

void __attribute__((interrupt, no_auto_psv, shadow))
_U2RXInterrupt(void){
	char c;

	IFS1bits.U2RXIF = 0;
	c = U2RXREG;

	if(RX_BUF.dc == BUF_SIZE) return;

	RX_BUF.buf[RX_BUF.wp] = c;
	RX_BUF.wp++;
	RX_BUF.wp &= (BUF_SIZE - 1);
	RX_BUF.dc++;
}

unsigned char kbhit2(void){
	return(RX_BUF.dc);
}

char getch2(void){
	char c;

	while(!kbhit2());
	IEC1bits.U2RXIE = 0; //disable
	c = RX_BUF.buf[RX_BUF.rp];
	RX_BUF.rp++;
	RX_BUF.rp &= (BUF_SIZE - 1);
	RX_BUF.dc --;
	IEC1bits.U2RXIE = 1; //enable
	return(c);
}
