/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <boot.h>
#include <pgtable.h>
#include <util.h>
#include <logmsg.h>

/**
 * @pre mbi != NULL && mb2_tag_mmap != NULL
 */
static void mb2_mmap_to_mbi(struct acrn_multiboot_info *mbi, struct multiboot2_tag_mmap *mb2_tag_mmap)
{
	uint32_t i;

	/* multiboot2 mmap tag header occupied 16 bytes */
	mbi->mi_mmap_entries = (mb2_tag_mmap->size - 16U) / sizeof(struct multiboot2_mmap_entry);
	if (mbi->mi_mmap_entries > E820_MAX_ENTRIES) {
		pr_err("Too many E820 entries %d\n", mbi->mi_mmap_entries);
		mbi->mi_mmap_entries = E820_MAX_ENTRIES;
	}
	for (i = 0U; i < mbi->mi_mmap_entries; i++) {
		mbi->mi_mmap_entry[i].baseaddr = mb2_tag_mmap->entries[i].addr;
		mbi->mi_mmap_entry[i].length = mb2_tag_mmap->entries[i].len;
		mbi->mi_mmap_entry[i].type = mb2_tag_mmap->entries[i].type;
	}
	mbi->mi_flags |= MULTIBOOT_INFO_HAS_MMAP;
}

/**
 * @pre mbi != NULL && mb2_tag_mods != NULL
 */
static void mb2_mods_to_mbi(struct acrn_multiboot_info *mbi,
			uint32_t mbi_mod_idx, struct multiboot2_tag_module *mb2_tag_mods)
{
	if (mbi_mod_idx >= MAX_MODULE_COUNT) {
		pr_err("unhandled multiboot2 module: 0x%x", mb2_tag_mods->mod_start);
	} else {
		mbi->mi_mods[mbi_mod_idx].mm_mod_start = mb2_tag_mods->mod_start;
		mbi->mi_mods[mbi_mod_idx].mm_mod_end = mb2_tag_mods->mod_end;
		mbi->mi_mods[mbi_mod_idx].mm_string = (uint32_t)(uint64_t)mb2_tag_mods->cmdline;
		mbi->mi_mods_count = mbi_mod_idx + 1U;
	}
	mbi->mi_flags |= MULTIBOOT_INFO_HAS_MODS;
}

/**
 * @pre mbi != NULL && mb2_tag_efi64 != 0
 */
static void mb2_efi64_to_mbi(struct acrn_multiboot_info *mbi, struct multiboot2_tag_efi64 *mb2_tag_efi64)
{
	mbi->mi_efi_info.efi_systab = (uint32_t)(uint64_t)mb2_tag_efi64->pointer;
	mbi->mi_efi_info.efi_loader_signature = (uint32_t)(uint64_t)efiloader_sig;
	mbi->mi_flags |= MULTIBOOT_INFO_HAS_EFI64;
}

/**
 * @pre mbi != NULL && mb2_tag_efimmap != 0
 */
static int32_t mb2_efimmap_to_mbi(struct acrn_multiboot_info *mbi, struct multiboot2_tag_efi_mmap *mb2_tag_efimmap)
{
	int32_t ret = 0;

	mbi->mi_efi_info.efi_memdesc_size = mb2_tag_efimmap->descr_size;
	mbi->mi_efi_info.efi_memdesc_version = mb2_tag_efimmap->descr_vers;
	mbi->mi_efi_info.efi_memmap = (uint32_t)(uint64_t)mb2_tag_efimmap->efi_mmap;
	mbi->mi_efi_info.efi_memmap_size = mb2_tag_efimmap->size - 16U;
	mbi->mi_efi_info.efi_memmap_hi = (uint32_t)(((uint64_t)mb2_tag_efimmap->efi_mmap) >> 32U);
	if (mbi->mi_efi_info.efi_memmap_hi != 0U) {
		pr_err("the efi mmap address should be less than 4G!");
		ret = -EINVAL;
	} else {
		mbi->mi_flags |= MULTIBOOT_INFO_HAS_EFI64;
	}
	return ret;
}

/**
 * @pre mbi != NULL && mb2_info != NULL
 */
int32_t multiboot2_to_acrn_mbi(struct acrn_multiboot_info *mbi, void *mb2_info)
{
	int32_t ret = 0;
	struct multiboot2_tag *mb2_tag, *mb2_tag_end;
	uint32_t mb2_info_size = *(uint32_t *)mb2_info;
	uint32_t mod_idx = 0U;

	/* The start part of multiboot2 info: total mbi size (4 bytes), reserved (4 bytes) */
	mb2_tag = (struct multiboot2_tag *)((uint8_t *)mb2_info + 8U);
	mb2_tag_end = (struct multiboot2_tag *)((uint8_t *)mb2_info + mb2_info_size);

	while ((mb2_tag->type != MULTIBOOT2_TAG_TYPE_END) && (mb2_tag < mb2_tag_end)) {
		if (mb2_tag->size == 0U) {
			pr_err("the multiboot2 tag size should not be 0!");
			ret = -EINVAL;
			break;
		}

		switch (mb2_tag->type) {
		case MULTIBOOT2_TAG_TYPE_MMAP:
			mb2_mmap_to_mbi(mbi, (struct multiboot2_tag_mmap *)mb2_tag);
			break;
		case MULTIBOOT2_TAG_TYPE_MODULE:
			mb2_mods_to_mbi(mbi, mod_idx, (struct multiboot2_tag_module *)mb2_tag);
			mod_idx++;
			break;
		case MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME:
			mbi->mi_loader_name = ((struct multiboot2_tag_string *)mb2_tag)->string;
			break;
		case MULTIBOOT2_TAG_TYPE_ACPI_NEW:
			mbi->mi_acpi_rsdp = ((struct multiboot2_tag_new_acpi *)mb2_tag)->rsdp;
			break;
		case MULTIBOOT2_TAG_TYPE_EFI64:
			mb2_efi64_to_mbi(mbi, (struct multiboot2_tag_efi64 *)mb2_tag);
			break;
		case MULTIBOOT2_TAG_TYPE_EFI_MMAP:
			ret = mb2_efimmap_to_mbi(mbi, (struct multiboot2_tag_efi_mmap *)mb2_tag);
			break;
		default:
			if (mb2_tag->type <= MULTIBOOT2_TAG_TYPE_LOAD_BASE_ADDR) {
				pr_warn("unhandled multiboot2 tag type: %d", mb2_tag->type);
			} else {
				pr_err("unknown multiboot2 tag type: %d", mb2_tag->type);
				ret = -EINVAL;
			}
			break;
		}
		if (ret != 0) {
			pr_err("multiboot2 info format error!");
			break;
		}
		/*
		 * tag->size is not including padding whearas each tag
		 * start at 8-bytes aligned address.
		 */
		mb2_tag = (struct multiboot2_tag *)((uint8_t *)mb2_tag
				+ ((mb2_tag->size + (MULTIBOOT2_INFO_ALIGN - 1U)) & ~(MULTIBOOT2_INFO_ALIGN - 1U)));
	}
	if ((mbi->mi_flags & (MULTIBOOT_INFO_HAS_EFI64 | MULTIBOOT_INFO_HAS_EFI_MMAP)) == 0U) {
		pr_err("no multiboot2 uefi info found!");
	}
	return ret;
}
