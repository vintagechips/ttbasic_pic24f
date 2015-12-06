/*
	Filename: flash.c
	Description: flash memory read/write API
*/

#include <libpic30.h>

int __attribute__((space(prog),aligned(1024))) storage[512];

void flash_write(unsigned char *buf){
	int i;
	_prog_addressT addr;

	_init_prog_address(addr, storage);
	_erase_flash(addr);
	for(i = 0; i < 1024; i += 128){
		_write_flash16(addr + i, (int*)&buf[i]);
	}
}

void flash_read(unsigned char *buf){
	_prog_addressT	addr;

	_init_prog_address(addr, storage);
	(void)_memcpy_p2d16(buf, addr, 1024);
}

unsigned char bootflag(){
	unsigned char flag;
	_prog_addressT addr;

	_init_prog_address(addr, storage);
	(void)_memcpy_p2d16(&flag, addr + 1023, 1);
	return flag;
}
