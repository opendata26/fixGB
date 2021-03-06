/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include "mem.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "input.h"
#include "mbc.h"

static uint8_t Ext_Mem[0x20000];
static uint8_t Main_Mem[0x8000];
static uint8_t High_Mem[0x80];
static uint8_t gbs_prevValReads[8];
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
static uint8_t cgbMainBank;
static bool cgbDmaActive;
static uint16_t cgbDmaSrc;
static uint16_t cgbDmaDst;
static uint8_t cgbDmaLen;
static uint8_t memDmaClock;
static bool cgbDmaHBlankMode;
static bool timerRegEnable = false;

static bool emuSaveEnabled = false;

//from main.c
extern bool allowCgbRegs;
extern uint8_t *emuGBROM;

//from mbc.c
extern uint16_t cBank;
extern uint16_t extBank;
extern uint16_t bankMask;
extern uint16_t extMask;
extern uint16_t extTotalMask;
extern size_t extTotalSize;

//from cpu.c
extern bool cpuCgbSpeed;
extern bool cpuDoStopSwitch;

//from ppu.c
extern uint8_t ppuCgbBank;

extern bool extMemUsed;
extern bool bankUsed;
extern bool extSelect;

static void memLoadSave();

static void memSetBankVal()
{
	bankUsed = true;
	switch(emuGBROM[0x148])
	{
		case 0:
			printf("32KB ROM allowed\n");
			bankMask = 1;
			break;
		case 1:
			printf("64KB ROM allowed\n");
			bankMask = 3;
			break;
		case 2:
			printf("128KB ROM allowed\n");
			bankMask = 7;
			break;
		case 3:
			printf("256KB ROM allowed\n");
			bankMask = 15;
			break;
		case 4:
			printf("512KB ROM allowed\n");
			bankMask = 31;
			break;
		case 5:
			printf("1MB ROM allowed\n");
			bankMask = 63;
			break;
		case 6:
			printf("2MB ROM allowed\n");
			bankMask = 127;
			break;
		case 7:
			printf("4MB ROM allowed\n");
			bankMask = 255;
			break;
		case 8:
			printf("8MB ROM allowed\n");
			bankMask = 511;
			break;
		case 0x52:
			printf("1.1MB ROM allowed\n");
			bankMask = 71;
			break;
		case 0x53:
			printf("1.2MB ROM allowed\n");
			bankMask = 79;
			break;
		case 0x54:
			printf("1.5MB ROM allowed\n");
			bankMask = 95;
			break;
		default:
			printf("Unknown ROM Size, allowing 32KB ROM\n");
			bankMask = 1;
			break;
	}
}

static void memSetExtVal()
{
	extMemUsed = true;
	switch(emuGBROM[0x149])
	{
		case 0:
			printf("No RAM allowed\n");
			extTotalSize = 0;
			extTotalMask = 0;
			extMask = 0;
		case 1:
			printf("2KB RAM allowed\n");
			extTotalSize = 0x800;
			extTotalMask = 0x7FF;
			extMask = 1;
			break;
		case 2:
			printf("8KB RAM allowed\n");
			extTotalSize = 0x2000;
			extTotalMask = 0x1FFF;
			extMask = 1;
			break;
		case 3:
			printf("32KB RAM allowed\n");
			extTotalSize = 0x8000;
			extTotalMask = 0x1FFF;
			extMask = 3;
			break;
		case 4:
			printf("128KB RAM allowed\n");
			extTotalSize = 0x20000;
			extTotalMask = 0x1FFF;
			extMask = 15;
			break;
		case 5:
			printf("64KB RAM allowed\n");
			extTotalSize = 0x10000;
			extTotalMask = 0x1FFF;
			extMask = 7;
			break;
		default:
			printf("Unknwon RAM Size, allowing 8KB RAM\n");
			extTotalMask = 0x1FFF;
			extMask = 1;
			break;
	}
}
static uint8_t curGBS = 0;
extern uint8_t gbsTracksTotal;
bool memInit(bool romcheck, bool gbs)
{
	if(romcheck)
	{
		if(gbs)
		{
			printf("GBS Mode\n");
			mbcInit(MBC_TYPE_GBS);
			bankUsed = true;
			extMemUsed = true;
			printf("8KB RAM allowed\n");
			extTotalSize = 0x2000;
			extTotalMask = 0x1FFF;
			extMask = 1;
			memset(gbs_prevValReads,0,8);
		}
		else
		{
			switch(emuGBROM[0x147])
			{
				case 0x00:
					printf("ROM Only\n");
					mbcInit(MBC_TYPE_NONE);
					bankUsed = false;
					extMemUsed = false;
					break;
				case 0x01:
					printf("ROM Only (MBC1)\n");
					mbcInit(MBC_TYPE_1);
					memSetBankVal();
					extMemUsed = false;
					break;
				case 0x02:
					printf("ROM and RAM (without save) (MBC1)\n");
					mbcInit(MBC_TYPE_1);
					memSetBankVal();
					memSetExtVal();
					break;
				case 0x03:
					printf("ROM and RAM (with save) (MBC1)\n");
					mbcInit(MBC_TYPE_1);
					memSetBankVal();
					memSetExtVal();
					memLoadSave();
					break;
				case 0x0F:
					//TODO: RTC Support
				case 0x11:
					printf("ROM Only (MBC3)\n");
					mbcInit(MBC_TYPE_3);
					memSetBankVal();
					extMemUsed = false;
					break;
				case 0x12:
					printf("ROM and RAM (without save) (MBC3)\n");
					mbcInit(MBC_TYPE_3);
					memSetBankVal();
					memSetExtVal();
					break;
				case 0x10:
					//TODO: RTC Support
				case 0x13:
					printf("ROM and RAM (with save) (MBC3)\n");
					mbcInit(MBC_TYPE_3);
					memSetBankVal();
					memSetExtVal();
					memLoadSave();
					break;
				case 0x19:
				case 0x1C:
					printf("ROM Only (MBC5)\n");
					mbcInit(MBC_TYPE_5);
					memSetBankVal();
					extMemUsed = false;
					break;
				case 0x1A:
				case 0x1D:
					printf("ROM and RAM (without save) (MBC5)\n");
					mbcInit(MBC_TYPE_5);
					memSetBankVal();
					memSetExtVal();
					break;
				case 0x1B:
				case 0x1E:
					printf("ROM and RAM (with save) (MBC5)\n");
					mbcInit(MBC_TYPE_5);
					memSetBankVal();
					memSetExtVal();
					memLoadSave();
					break;
				default:
					printf("Unsupported Type %02x!\n", emuGBROM[0x147]);
					return false;
			}
		}
	}
	memset(Main_Mem,0,0x8000);
	memset(High_Mem,0,0x80);
	memLastVal = 0;
	irqEnableReg = 0;
	irqFlagsReg = 0;
	divRegVal = 0;
	divRegClock = 1;
	timerReg = 0;
	timerRegVal = 0;
	timerResetVal = 0;
	timerRegClock = 1;
	timerRegTimer = 64; //262144 / 64 = 4096
	cgbMainBank = 1;
	cgbDmaActive = false;
	cgbDmaSrc = 0;
	cgbDmaDst = 0;
	cgbDmaLen = 0;
	memDmaClock = 1;
	cgbDmaHBlankMode = false;
	timerRegEnable = false;
	return true;
}

void memStartGBS()
{
	curGBS = 1;
	printf("Track %i/%i         ", curGBS, gbsTracksTotal);
	cpuLoadGBS(curGBS-1);
}

uint8_t memGet8(uint16_t addr)
{
	uint8_t val = memLastVal;
	//printf("memGet8 %04x\n", addr);
	if(addr < 0x4000)
		val = emuGBROM[addr];
	else if(addr < 0x8000)
		val = bankUsed?(emuGBROM[(cBank<<14)+(addr&0x3FFF)]):emuGBROM[addr];
	else if(addr >= 0x8000 && addr < 0xA000)
		val = ppuGet8(addr);
	else if(addr >= 0xA000 && addr < 0xC000 && extMemUsed)
		val = Ext_Mem[((extBank<<13)+(addr&0x1FFF))&extTotalMask];
	else if(addr >= 0xC000 && addr < 0xFE00)
	{
		if(!allowCgbRegs)
			val = Main_Mem[addr&0x1FFF];
		else
		{
			if(addr < 0xD000)
				val = Main_Mem[addr&0xFFF];
			else
				val = Main_Mem[(cgbMainBank<<12)|(addr&0xFFF)];
		}
	}
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
		val = irqFlagsReg|0xE0;
		//printf("memGet8 %04x %02x\n", addr, val);
	}
	else if(addr >= 0xFF10 && addr < 0xFF40)
		val = apuGet8(addr&0xFF);
	else if(addr >= 0xFF40 && addr < 0xFF4C)
		val = ppuGet8(addr);
	else if(addr >= 0xFF4D && addr < 0xFF80)
	{
		if(allowCgbRegs)
		{
			if(addr == 0xFF4D)
				val = (cpuDoStopSwitch | (cpuCgbSpeed<<7));
			else if(addr == 0xFF4F)
				val = ppuCgbBank;
			else if(addr == 0xFF51)
				val = cgbDmaSrc>>8;
			else if(addr == 0xFF52)
				val = (cgbDmaSrc&0xFF);
			else if(addr == 0xFF53)
				val = cgbDmaDst>>8;
			else if(addr == 0xFF54)
				val = (cgbDmaDst&0xFF);
			else if(addr == 0xFF55)
			{
				val = cgbDmaLen-1;
				//bit 7 = 1 means NOT active
				if(!cgbDmaActive)
					val |= 0x80;
			}
			else if(addr >= 0xFF68 && addr < 0xFF6C)
				val = ppuGet8(addr);
			else if(addr == 0xFF70)
				val = cgbMainBank;
		}
	}
	else if(addr >= 0xFF80 && addr < 0xFFFF)
		val = High_Mem[addr&0x7F];
	else if(addr == 0xFFFF)
		val = irqEnableReg|0xE0;
	memLastVal = val;
	return val;
}
extern uint32_t cpu_oam_dma;
extern bool cpu_odd_cycle;
void memSet8(uint16_t addr, uint8_t val)
{
	if(addr < 0x8000)
		mbcSet8(addr, val);
	if(addr >= 0x8000 && addr < 0xA000)
		ppuSet8(addr, val);
	else if(addr >= 0xA000 && addr < 0xC000 && extMemUsed)
		Ext_Mem[((extBank<<13)+(addr&0x1FFF))&extTotalMask] = val;
	else if(addr >= 0xC000 && addr < 0xFE00)
	{
		if(!allowCgbRegs)
			Main_Mem[addr&0x1FFF] = val;
		else
		{
			if(addr < 0xD000)
				Main_Mem[addr&0xFFF] = val;
			else
				Main_Mem[(cgbMainBank<<12)|(addr&0xFFF)] = val;
		}
	}
	else if(addr >= 0xFE00 && addr < 0xFEA0)
		ppuSet8(addr, val);
	else if(addr == 0xFF00)
		inputSet8(val);
	else if(addr == 0xFF04)
		divRegVal = 0; //writing any val resets to 0
	else if(addr == 0xFF05)
		timerRegVal = val;
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
		irqFlagsReg = val&0x1F;
	}
	else if(addr >= 0xFF10 && addr < 0xFF40)
		apuSet8(addr&0xFF, val);
	else if(addr >= 0xFF40 && addr < 0xFF4C)
		ppuSet8(addr, val);
	else if(addr >= 0xFF4D && addr < 0xFF80)
	{
		if(allowCgbRegs)
		{
			if(addr == 0xFF4D)
				cpuDoStopSwitch = !!(val&1);
			else if(addr == 0xFF4F)
				ppuCgbBank = (val&1);
			else if(addr == 0xFF51)
				cgbDmaSrc = (cgbDmaSrc&0x00FF)|(val<<8);
			else if(addr == 0xFF52)
				cgbDmaSrc = (cgbDmaSrc&0xFF00)|(val&~0xF);
			else if(addr == 0xFF53)
				cgbDmaDst = (cgbDmaDst&0x00FF)|(val<<8)|0x8000;
			else if(addr == 0xFF54)
				cgbDmaDst = (cgbDmaDst&0xFF00)|(val&~0xF);
			else if(addr == 0xFF55)
			{
				//disabling ongoing HBlank DMA when disabling HBlank mode
				if(cgbDmaActive && cgbDmaHBlankMode && !(val&0x80))
					cgbDmaActive = false;
				else //enable DMA in all other cases
				{
					cgbDmaActive = true;
					cgbDmaLen = (val&0x7F)+1;
					cgbDmaHBlankMode = !!(val&0x80);
					//trigger immediately
					memDmaClock = 16;
					memDmaClockTimers();
				}
			}
			else if(addr >= 0xFF68 && addr < 0xFF6C)
				ppuSet8(addr,val);
			else if(addr == 0xFF70)
			{
				cgbMainBank = (val&7);
				if(cgbMainBank == 0)
					cgbMainBank = 1;
			}
		}
	}
	else if(addr >= 0xFF80 && addr < 0xFFFF)
		High_Mem[addr&0x7F] = val;
	else if(addr == 0xFFFF)
	{
		//printf("memSet8 %04x %02x\n", addr, val);
		irqEnableReg = val&0x1F;
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

#define DEBUG_MEM_DUMP 0

void memDumpMainMem()
{
	#if DEBUG_MEM_DUMP
	FILE *f = fopen("MainMem.bin","wb");
	if(f)
	{
		fwrite(Main_Mem,1,allowCgbRegs?0x8000:0x2000,f);
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

extern char *emuSaveName;
void memLoadSave()
{
	if(emuSaveName && extTotalSize)
	{
		emuSaveEnabled = true;
		FILE *save = fopen(emuSaveName, "rb");
		if(save)
		{
			fseek(save,0,SEEK_END);
			size_t saveSize = ftell(save);
			if(saveSize == extTotalSize)
			{
				rewind(save);
				fread(Ext_Mem,1,saveSize,save);
			}
			else
				printf("Save file ignored\n");
			fclose(save);
		}
	}
}

void memSaveGame()
{
	if(emuSaveName && extMask && emuSaveEnabled)
	{
		FILE *save = fopen(emuSaveName, "wb");
		if(save)
		{
			fwrite(Ext_Mem,1,extTotalSize,save);
			fclose(save);
		}
	}
}

extern bool gbEmuGBSPlayback;
extern bool gbsTimerMode;
extern uint8_t inValReads[8];

//clocked at 262144 Hz (or 2x that in CGB Mode)
void memClockTimers()
{
	if(gbEmuGBSPlayback)
	{
		if(inValReads[BUTTON_RIGHT] && !gbs_prevValReads[BUTTON_RIGHT])
		{
			gbs_prevValReads[BUTTON_RIGHT] = inValReads[BUTTON_RIGHT];
			curGBS++;
			if(curGBS > gbsTracksTotal)
				curGBS = 1;
			printf("\rTrack %i/%i         ", curGBS, gbsTracksTotal);
			cpuLoadGBS(curGBS-1);
		}
		else if(!inValReads[BUTTON_RIGHT])
			gbs_prevValReads[BUTTON_RIGHT] = 0;
		
		if(inValReads[BUTTON_LEFT] && !gbs_prevValReads[BUTTON_LEFT])
		{
			gbs_prevValReads[BUTTON_LEFT] = inValReads[BUTTON_LEFT];
			curGBS--;
			if(curGBS < 1)
				curGBS = gbsTracksTotal;
			printf("\rTrack %i/%i         ", curGBS, gbsTracksTotal);
			cpuLoadGBS(curGBS-1);
		}
		else if(!inValReads[BUTTON_LEFT])
			gbs_prevValReads[BUTTON_LEFT] = 0;
	}

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
			if(!gbEmuGBSPlayback)
				irqFlagsReg |= 4;
			else if(gbsTimerMode)
				cpuPlayGBS();
		}
		timerRegClock = 1;
	}
	else
		timerRegClock++;
}

extern bool cpuDmaHalt;

//clocked at 131072 Hz
void memDmaClockTimers()
{
	if(memDmaClock >= 16)
	{
		cpuDmaHalt = false;
		if(!cgbDmaActive)
			return;
		//printf("%04x %04x %02x\n", cgbDmaSrc, cgbDmaDst, cgbDmaLen);
		if(cgbDmaLen && ((cgbDmaSrc < 0x8000) || (cgbDmaSrc >= 0xA000 && cgbDmaSrc < 0xE000)) && (cgbDmaDst >= 0x8000 && cgbDmaDst < 0xA000))
		{
			if(!cgbDmaHBlankMode || (cgbDmaHBlankMode && ppuInHBlank()))
			{
				uint8_t i;
				for(i = 0; i < 0x10; i++)
					memSet8(cgbDmaDst+i, memGet8(cgbDmaSrc+i));
				cgbDmaLen--;
				if(cgbDmaLen == 0)
					cgbDmaActive = false;
				cgbDmaSrc += 0x10;
				cgbDmaDst += 0x10;
				cpuDmaHalt = true;
			}
		}
		else
			cgbDmaActive = false;
		memDmaClock = 1;
	}
	else
		memDmaClock++;
}
