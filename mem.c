/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "input.h"

static uint8_t Main_Mem[0x2000];
static uint8_t High_Mem[0x80];
static uint8_t memLastVal;
static uint8_t irqEnableReg;
static uint8_t irqFlagsReg;

static uint8_t divRegVal;
static uint8_t divRegClock;
static uint8_t timerReg;
static uint8_t timerRegVal;
static uint8_t timerResetVal;
static uint8_t timerRegClock;
static uint8_t timerRegTimer;
static uint8_t cBank;
static uint32_t bankMask;
static bool timerRegEnable;

extern uint8_t *emuGBROM;

void memInit()
{
	bankMask = (0x8000<<(emuGBROM[0x148]&0xF))-1;
	memset(Main_Mem,0,0x2000);
	memset(High_Mem,0,0x80);
	memLastVal = 0;
	irqEnableReg = 0;
	irqFlagsReg = 0;
	cBank = 1;
	divRegVal = 0;
	divRegClock = 1;
	timerReg = 0;
	timerRegVal = 0;
	timerResetVal = 0;
	timerRegClock = 1;
	timerRegTimer = 64; //262144 / 64 = 4096
	timerRegEnable = false;
}

uint8_t memGet8(uint16_t addr)
{
	uint8_t val = memLastVal;
	//printf("memGet8 %04x\n", addr);
	if(addr < 0x4000)
		val = emuGBROM[addr];
	else if(addr < 0x8000)
		val = emuGBROM[((cBank<<14)+(addr&0x3FFF))&bankMask];
	else if(addr >= 0x8000 && addr < 0xA000)
		val = ppuGet8(addr);
	else if(addr >= 0xC000 && addr < 0xFE00)
		val = Main_Mem[addr&0x1FFF];
	else if(addr >= 0xFE00 && addr < 0xFEA0)
		val = ppuGet8(addr);
	else if(addr == 0xFF00)
		val = inputGet8();
	else if(addr == 0xFF04)
		val = divRegVal;
	else if(addr == 0xFF05)
		val = timerRegVal;
	else if(addr == 0xFF06)
		val = timerResetVal;
	else if(addr == 0xFF07)
		val = timerReg;
	else if(addr == 0xFF0F)
	{
		val = irqFlagsReg;
		//printf("memGet8 %04x %02x\n", addr, val);
	}
	else if(addr >= 0xFF10 && addr < 0xFF40)
		val = apuGet8(addr&0xFF);
	else if(addr >= 0xFF40 && addr < 0xFF70)
		val = ppuGet8(addr);
	else if(addr >= 0xFF80 && addr < 0xFFFF)
		val = High_Mem[addr&0x7F];
	else if(addr == 0xFFFF)
		val = irqEnableReg;
	memLastVal = val;
	return val;
}
extern uint32_t cpu_oam_dma;
extern bool cpu_odd_cycle;
void memSet8(uint16_t addr, uint8_t val)
{
	if(addr >= 0x2000 && addr < 0x4000)
	{
		//printf("%02x\n",val);
		cBank = val;
		if(cBank == 0)
			cBank = 1;
	}
	if(addr >= 0x8000 && addr < 0xA000)
		ppuSet8(addr, val);
	else if(addr >= 0xC000 && addr < 0xFE00)
		Main_Mem[addr&0x1FFF] = val;
	else if(addr >= 0xFE00 && addr < 0xFEA0)
		ppuSet8(addr, val);
	else if(addr == 0xFF00)
		inputSet8(val);
	else if(addr == 0xFF04)
		divRegVal = 0; //writing any val resets to 0
	else if(addr == 0xFF05)
		timerRegVal = 0; //not sure
	else if(addr == 0xFF06)
		timerResetVal = val;
	else if(addr == 0xFF07)
	{
		//if(val != 0)
		//	printf("memSet8 %04x %02x\n", addr, val);
		timerReg = val; //for readback
		timerRegEnable = ((val&4)!=0);
		if((val&3)==0) //0 for 4096 Hz
			timerRegTimer = 64; //262144 / 64 = 4096
		else if((val&3)==1) //1 for 262144 Hz
			timerRegTimer = 1; //262144 / 1 = 262144
		else if((val&3)==2) //2 for 65536 Hz
			timerRegTimer = 4; //262144 / 4 = 65536
		else if((val&3)==3) //3 for 16384 Hz
			timerRegTimer = 16; //262144 / 16 = 16384
	}
	else if(addr == 0xFF0F)
	{
		//printf("memSet8 %04x %02x\n", addr, val);
		irqFlagsReg = val;
	}
	else if(addr >= 0xFF10 && addr < 0xFF40)
		apuSet8(addr&0xFF, val);
	else if(addr >= 0xFF40 && addr < 0xFF70)
		ppuSet8(addr, val);
	else if(addr >= 0xFF80 && addr < 0xFFFF)
		High_Mem[addr&0x7F] = val;
	else if(addr == 0xFFFF)
	{
		//printf("memSet8 %04x %02x\n", addr, val);
		irqEnableReg = val;
	}
	memLastVal = val;
}

uint8_t memGetCurIrqList()
{
	return (irqEnableReg & irqFlagsReg);
}

void memClearCurIrqList(uint8_t num)
{
	irqFlagsReg &= ~num;
}

void memEnableVBlankIrq()
{
	irqFlagsReg |= 1;
}

void memEnableStatIrq()
{
	irqFlagsReg |= 2;
}

#define DEBUG_MEM_DUMP 1

void memDumpMainMem()
{
	#if DEBUG_MEM_DUMP
	FILE *f = fopen("MainMem.bin","wb");
	if(f)
	{
		fwrite(Main_Mem,1,0x2000,f);
		fclose(f);
	}
	f = fopen("HighMem.bin","wb");
	if(f)
	{
		fwrite(High_Mem,1,0x80,f);
		fclose(f);
	}
	ppuDumpMem();
	#endif
}

//clocked at 262144 Hz
void memClockTimers()
{
	//clocked at 16384 Hz (262144 / 16 = 16384)
	if(divRegClock == 16)
	{
		divRegVal++;
		divRegClock = 1;
	}
	else
		divRegClock++;

	if(!timerRegEnable)
		return;

	//clocked at specified rate
	if(timerRegClock == timerRegTimer)
	{
		timerRegVal++;
		if(timerRegVal == 0) //set on overflow
		{
			//printf("Timer interrupt\n");
			timerRegVal = timerResetVal;
			irqFlagsReg |= 4;
		}
		timerRegClock = 1;
	}
	else
		timerRegClock++;
}