/*
 * Copyright (C) 2023 SUSE Software Solutions GmbH
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License
 */
#include <stddef.h>
#include <string.h>

#include "superblocks.h"
#include "crc32.h"

/*
 * Metadata version.
 */
#define DMZ_META_VER	2

/*
 * DM target version
 */
#define DMZ_DM_VER	3

/*
 * On-disk super block magic.
 */
#define DMZ_MAGIC	((((unsigned int)('D')) << 24) | \
			 (((unsigned int)('Z')) << 16) | \
			 (((unsigned int)('B')) <<  8) | \
			 ((unsigned int)('D')))

/*
 * Maximum label and uuid length.
 */
#define DMZ_LABEL_LEN	32
#define DMZ_UUID_LEN	16

#define DMZ_BLOCK_SIZE 4096

/*
 * On disk super block.
 * This uses a full 4KB block. This block is followed on disk
 * by the chunk mapping table to zones and the bitmap blocks
 * indicating block validity.
 * The overall resulting metadat format is:
 *    (1) Super block (1 block)
 *    (2) Chunk mapping table (nr_map_blocks)
 *    (3) Bitmap blocks (nr_bitmap_blocks)
 * with all blocks stored in consecutive random zones starting
 * from the first random zone found on disk.
 */
struct dm_zoned_super {

	/* Magic number */
	__le32		magic;			/*   4 */

	/* Metadata version number */
	__le32		version;		/*   8 */

	/* Generation number */
	__le64		gen;			/*  16 */

	/* This block number */
	__le64		sb_block;		/*  24 */

	/* The number of metadata blocks, including this super block */
	__le32		nr_meta_blocks;		/*  28 */

	/* The number of sequential zones reserved for reclaim */
	__le32		nr_reserved_seq;	/*  32 */

	/* The number of entries in the mapping table */
	__le32		nr_chunks;		/*  36 */

	/* The number of blocks used for the chunk mapping table */
	__le32		nr_map_blocks;		/*  40 */

	/* The number of blocks used for the block bitmaps */
	__le32		nr_bitmap_blocks;	/*  44 */

	/* Checksum */
	__le32		crc;			/*  48 */

	/* Fields added by Metadata version 2 */
	/* DM-Zoned label */
	__u8		dmz_label[DMZ_LABEL_LEN]; /*  80 */

	/* DM-Zoned UUID */
	__u8		dmz_uuid[DMZ_UUID_LEN];	/*  96 */

	/* Device UUID */
	__u8		dev_uuid[DMZ_UUID_LEN];	/*  112 */

	/* Padding to full 512B sector */
	__u8		reserved[400];		/* 512 */

} __attribute__ ((packed));

/*
 * For super block checksum (CRC32)
 */
#define CRCPOLY_LE 0xedb88320

static uint32_t dm_zoned_crc32(uint32_t crc, const void *buf, size_t length)
{
        unsigned char *p = (unsigned char *)buf;
	int i;

	while (length--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_LE : 0);
	}

	return crc;
}

static int dm_zoned_verify_csum(blkid_probe pr, const struct dm_zoned_super *sb)
{
	uint32_t expected = le32_to_cpu(sb->crc);
	uint32_t crc;

	sb->crc = 0;
	crc = dm_zoned_crc32(sb->gen, (unsigned char *)sb,
				      DMZ_BLOCK_SIZE);
	return blkid_probe_verify_csum(pr, crc, expected);
}

static int probe_dm_zoned(blkid_probe pr,
		const struct blkid_idmag *mag  __attribute__((__unused__)))
{
	const struct dm_zoned_super *sb;

	sb = (struct dm_zoned_super *)
		blkid_probe_get_buffer(pr, 0,
				       sizeof(struct dm_zoned_super));
	if (!sb)
		return errno ? -errno : 1;

	if (le32_to_cpu(sb->magic) != DMZ_MAGIC)
		return 1;

	if (!dm_zoned_verify_csum(pr, sb))
		return 1;

	if (sb->dmz_label[0])
		blkid_probe_set_label(pr, (unsigned char *) sb->dmz_label,
				      sizeof(sb->dmz_label));

	blkid_probe_set_uuid(pr, sb->dmz_uuid);
	blkid_probe_set_version(pr, sb->version);

	return 0;
}

const struct blkid_idinfo dm_zoned_idinfo =
{
	.name           = "dmz",
	.usage          = BLKID_USAGE_RAID,
	.probefunc      = probe_dm_zoned,
	.magics         =
        {
		{
			.magic = (char *)DMZ_MAGIC,
			.len = 4,
		},
		{ NULL }
	}
};
