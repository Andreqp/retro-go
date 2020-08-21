/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** map001.c
**
** Mapper 1 interface
** New implementation by ducalex
**
*/

#include <nofrendo.h>
#include <nes_mmc.h>
#include <nes_ppu.h>
#include <string.h>

static uint8 regs[4];
static uint8 bitcount = 0;
static uint8 latch = 0;

// Shouldn't that be packed? (It wasn't packed in SNSS...)
typedef struct
{
   uint8 regs[4];
   uint8 bitcount;
   uint8 latch;
} mapper1Data;

static void update_mirror()
{
   switch (regs[0] & 3)
   {
      case 0: ppu_setmirroring(PPU_MIRROR_SCR0); break;
      case 1: ppu_setmirroring(PPU_MIRROR_SCR1); break;
      case 2: ppu_setmirroring(PPU_MIRROR_VERT); break;
      case 3: ppu_setmirroring(PPU_MIRROR_HORI); break;
   }
}

static void update_chr()
{
   if (regs[0] & 0x10)
   {
      mmc_bankvrom(4, 0x0000, regs[1]);
      mmc_bankvrom(4, 0x1000, regs[2]);
   }
   else
   {
      mmc_bankvrom(8, 0x0000, regs[1] >> 1);
   }
}

static void update_prg()
{
   int prg_reg = regs[3] & 0xF;
   int mode = (regs[0] >> 2) & 3;

   if (mmc_getinfo()->rom_banks == 0x20)
      prg_reg += regs[1] & 0x10;

   // switch 32 KB at $8000, ignoring low bit of bank number
   if (0 == mode || 1 == mode)
   {
      mmc_bankrom(32, 0x8000, prg_reg >> 1);
   }
   // fix first bank at $8000 and switch 16 KB bank at $C000
   else if (mode == 2)
   {
      mmc_bankrom(16, 0x8000, 0);
      mmc_bankrom(16, 0xC000, prg_reg);
   }
   // fix last bank at $C000 and switch 16 KB bank at $8000
   else if (mode == 3)
   {
      mmc_bankrom(16, 0x8000, prg_reg);
      mmc_bankrom(16, 0xC000, -1);
   }
}

static void map1_write(uint32 address, uint8 value)
{
   // MESSAGE_INFO("MMC1 write: $%02X to $%04X\n", value, address);

   // Reset
   if (value & 0x80)
   {
      regs[0] |= 0x0C;
      bitcount = 0;
      latch = 0;
      update_prg();
      return;
   }

   // Serial data in
   latch |= ((value & 1) << bitcount++);

   /* 5 bit registers */
   if (5 != bitcount)
      return;

   // Only matters on fifth write
   int regnum = (address >> 13) & 0x3;

   regs[regnum] = latch;
   bitcount = 0;
   latch = 0;

   switch (regnum)
   {
      // Register 0: Control
      case 0: update_mirror(); update_prg(); update_chr(); break;

      // Register 1: CHR bank 0
      case 1: update_chr(); update_prg(); break;

      // Register 2: CHR bank 1
      case 2: update_chr(); break;

      // Register 3: PRG bank
      case 3: update_prg(); break;
   }
}

static void map1_init(void)
{
   bitcount = 0;
   latch = 0;

   regs[0] = 0x1F;
   regs[1] = 0x00;
   regs[2] = 0x00;
   regs[3] = 0x00;

   update_mirror();
   update_chr();
   update_prg();
}

static void map1_getstate(void *state)
{
   ((mapper1Data*)state)->regs[0] = regs[0];
   ((mapper1Data*)state)->regs[1] = regs[1];
   ((mapper1Data*)state)->regs[2] = regs[2];
   ((mapper1Data*)state)->regs[3] = regs[3];
   ((mapper1Data*)state)->latch = latch;
   ((mapper1Data*)state)->bitcount = bitcount;
}

static void map1_setstate(void *state)
{
   regs[0]  = ((mapper1Data*)state)->regs[0];
   regs[1]  = ((mapper1Data*)state)->regs[1];
   regs[2]  = ((mapper1Data*)state)->regs[2];
   regs[3]  = ((mapper1Data*)state)->regs[3];
   latch    = ((mapper1Data*)state)->latch;
   bitcount = ((mapper1Data*)state)->bitcount;
}

static mem_write_handler_t map1_memwrite[] =
{
   { 0x8000, 0xFFFF, map1_write },
   LAST_MEMORY_HANDLER
};

mapintf_t map1_intf =
{
   1, /* mapper number */
   "MMC1", /* mapper name */
   map1_init, /* init routine */
   NULL, /* vblank callback */
   NULL, /* hblank callback */
   map1_getstate, /* get state (snss) */
   map1_setstate, /* set state (snss) */
   NULL, /* memory read structure */
   map1_memwrite, /* memory write structure */
   NULL /* external sound device */
};
