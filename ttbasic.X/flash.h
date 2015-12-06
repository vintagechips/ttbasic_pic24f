/*
	Filename: flash.h
	Description: flash memory read/write API
*/

void flash_write(unsigned char*);
void flash_read(unsigned char*);
unsigned char bootflag(void);
