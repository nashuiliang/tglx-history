
/******************************************************************************
 *
 * Module Name: amlresrc.h - AML resource descriptors
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef __AMLRESRC_H
#define __AMLRESRC_H


#define ASL_RESNAME_ADDRESS                     "_ADR"
#define ASL_RESNAME_ALIGNMENT                   "_ALN"
#define ASL_RESNAME_ADDRESSSPACE                "_ASI"
#define ASL_RESNAME_BASEADDRESS                 "_BAS"
#define ASL_RESNAME_BUSMASTER                   "_BM_"  /* Master(1), Slave(0) */
#define ASL_RESNAME_DECODE                      "_DEC"
#define ASL_RESNAME_DMA                         "_DMA"
#define ASL_RESNAME_DMATYPE                     "_TYP"  /* Compatible(0), A(1), B(2), F(3) */
#define ASL_RESNAME_GRANULARITY                 "_GRA"
#define ASL_RESNAME_INTERRUPT                   "_INT"
#define ASL_RESNAME_INTERRUPTLEVEL              "_LL_"  /* Active_lo(1), Active_hi(0) */
#define ASL_RESNAME_INTERRUPTSHARE              "_SHR"  /* Shareable(1), No_share(0) */
#define ASL_RESNAME_INTERRUPTTYPE               "_HE_"  /* Edge(1), Level(0) */
#define ASL_RESNAME_LENGTH                      "_LEN"
#define ASL_RESNAME_MEMATTRIBUTES               "_MTP"  /* Memory(0), Reserved(1), ACPI(2), NVS(3) */
#define ASL_RESNAME_MEMTYPE                     "_MEM"  /* Non_cache(0), Cacheable(1) Cache+combine(2), Cache+prefetch(3) */
#define ASL_RESNAME_MAXADDR                     "_MAX"
#define ASL_RESNAME_MINADDR                     "_MIN"
#define ASL_RESNAME_MAXTYPE                     "_MAF"
#define ASL_RESNAME_MINTYPE                     "_MIF"
#define ASL_RESNAME_REGISTERBITOFFSET           "_RBO"
#define ASL_RESNAME_REGISTERBITWIDTH            "_RBW"
#define ASL_RESNAME_RANGETYPE                   "_RNG"
#define ASL_RESNAME_READWRITETYPE               "_RW_"  /* Read_only(0), Writeable (1) */
#define ASL_RESNAME_TRANSLATION                 "_TRA"
#define ASL_RESNAME_TRANSTYPE                   "_TRS"  /* Sparse(1), Dense(0) */
#define ASL_RESNAME_TYPE                        "_TTP"  /* Translation(1), Static (0) */
#define ASL_RESNAME_XFERTYPE                    "_SIZ"  /* 8(0), 8_and16(1), 16(2) */


/* Default sizes for "small" resource descriptors */

#define ASL_RDESC_IRQ_SIZE                      0x02
#define ASL_RDESC_DMA_SIZE                      0x02
#define ASL_RDESC_ST_DEPEND_SIZE                0x00
#define ASL_RDESC_END_DEPEND_SIZE               0x00
#define ASL_RDESC_IO_SIZE                       0x07
#define ASL_RDESC_FIXED_IO_SIZE                 0x03
#define ASL_RDESC_END_TAG_SIZE                  0x01


typedef struct asl_resource_node
{
	u32                         buffer_length;
	void                        *buffer;
	struct asl_resource_node    *next;

} asl_resource_node;


/*
 * Resource descriptors defined in the ACPI specification.
 *
 * Alignment must be BYTE because these descriptors
 * are used to overlay the AML byte stream.
 */
#pragma pack(1)

typedef struct asl_irq_format_desc
{
	u8                          descriptor_type;
	u16                         irq_mask;
	u8                          flags;

} asl_irq_format_desc;


typedef struct asl_irq_noflags_desc
{
	u8                          descriptor_type;
	u16                         irq_mask;

} asl_irq_noflags_desc;


typedef struct asl_dma_format_desc
{
	u8                          descriptor_type;
	u8                          dma_channel_mask;
	u8                          flags;

} asl_dma_format_desc;


typedef struct asl_start_dependent_desc
{
	u8                          descriptor_type;
	u8                          flags;

} asl_start_dependent_desc;


typedef struct asl_start_dependent_noprio_desc
{
	u8                          descriptor_type;

} asl_start_dependent_noprio_desc;


typedef struct asl_end_dependent_desc
{
	u8                          descriptor_type;

} asl_end_dependent_desc;


typedef struct asl_io_port_desc
{
	u8                          descriptor_type;
	u8                          information;
	u16                         address_min;
	u16                         address_max;
	u8                          alignment;
	u8                          length;

} asl_io_port_desc;


typedef struct asl_fixed_io_port_desc
{
	u8                          descriptor_type;
	u16                         base_address;
	u8                          length;

} asl_fixed_io_port_desc;


typedef struct asl_small_vendor_desc
{
	u8                          descriptor_type;
	u8                          vendor_defined[7];

} asl_small_vendor_desc;


typedef struct asl_end_tag_desc
{
	u8                          descriptor_type;
	u8                          checksum;

} asl_end_tag_desc;


/* LARGE descriptors */

typedef struct asl_memory_24_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          information;
	u16                         address_min;
	u16                         address_max;
	u16                         alignment;
	u16                         range_length;

} asl_memory_24_desc;


typedef struct asl_large_vendor_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          vendor_defined[1];

} asl_large_vendor_desc;


typedef struct asl_memory_32_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          information;
	u32                         address_min;
	u32                         address_max;
	u32                         alignment;
	u32                         range_length;

} asl_memory_32_desc;


typedef struct asl_fixed_memory_32_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          information;
	u32                         base_address;
	u32                         range_length;

} asl_fixed_memory_32_desc;


typedef struct asl_qword_address_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          resource_type;
	u8                          flags;
	u8                          specific_flags;
	u64                         granularity;
	u64                         address_min;
	u64                         address_max;
	u64                         translation_offset;
	u64                         address_length;
	u8                          optional_fields[2];

} asl_qword_address_desc;


typedef struct asl_dword_address_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          resource_type;
	u8                          flags;
	u8                          specific_flags;
	u32                         granularity;
	u32                         address_min;
	u32                         address_max;
	u32                         translation_offset;
	u32                         address_length;
	u8                          optional_fields[2];

} asl_dword_address_desc;


typedef struct asl_word_address_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          resource_type;
	u8                          flags;
	u8                          specific_flags;
	u16                         granularity;
	u16                         address_min;
	u16                         address_max;
	u16                         translation_offset;
	u16                         address_length;
	u8                          optional_fields[2];

} asl_word_address_desc;


typedef struct asl_extended_xrupt_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          flags;
	u8                          table_length;
	u32                         interrupt_number[1];
	/* Res_source_index, Res_source optional fields follow */

} asl_extended_xrupt_desc;


typedef struct asl_general_register_desc
{
	u8                          descriptor_type;
	u16                         length;
	u8                          address_space_id;
	u8                          bit_width;
	u8                          bit_offset;
	u8                          reserved;
	u64                         address;

} asl_general_register_desc;

/* restore default alignment */

#pragma pack()

/* Union of all resource descriptors, sow we can allocate the worst case */

typedef union asl_resource_desc
{
	asl_irq_format_desc         irq;
	asl_dma_format_desc         dma;
	asl_start_dependent_desc    std;
	asl_end_dependent_desc      end;
	asl_io_port_desc            iop;
	asl_fixed_io_port_desc      fio;
	asl_small_vendor_desc       smv;
	asl_end_tag_desc            et;

	asl_memory_24_desc          M24;
	asl_large_vendor_desc       lgv;
	asl_memory_32_desc          M32;
	asl_fixed_memory_32_desc    F32;
	asl_qword_address_desc      qas;
	asl_dword_address_desc      das;
	asl_word_address_desc       was;
	asl_extended_xrupt_desc     exx;
	asl_general_register_desc   grg;
	u32                         U32_item;
	u16                         U16_item;
	u8                          U8item;

} asl_resource_desc;


#endif

