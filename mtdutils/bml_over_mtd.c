/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>

#include "cutils/log.h"

#include <mtd/mtd-user.h>

#include "mtdutils.h"

typedef struct BmlOverMtdReadContext {
	const MtdPartition *partition;
	char *buffer;
	size_t consumed;
	int fd;
} BmlOverMtdReadContext;

typedef struct BmlOverMtdWriteContext {
	const MtdPartition *partition;
	char *buffer;
	size_t stored;
	int fd;

	off_t* bad_block_offsets;
	int bad_block_alloc;
	int bad_block_count;
} BmlOverMtdWriteContext;


static BmlOverMtdReadContext *bml_over_mtd_read_partition(const MtdPartition *partition)
{
	BmlOverMtdReadContext *ctx = (BmlOverMtdReadContext*) malloc(sizeof(BmlOverMtdReadContext));
	if (ctx == NULL) return NULL;

	ctx->buffer = malloc(partition->erase_size);
	if (ctx->buffer == NULL) {
		free(ctx);
		return NULL;
	}

	char mtddevname[32];
	sprintf(mtddevname, "/dev/mtd/mtd%d", partition->device_index);
	ctx->fd = open(mtddevname, O_RDONLY);
	if (ctx->fd < 0) {
		free(ctx);
		free(ctx->buffer);
		return NULL;
	}

	ctx->partition = partition;
	ctx->consumed = partition->erase_size;
	return ctx;
}

static void bml_over_mtd_read_close(BmlOverMtdReadContext *ctx)
{
	close(ctx->fd);
	free(ctx->buffer);
	free(ctx);
}

static BmlOverMtdWriteContext *bml_over_mtd_write_partition(const MtdPartition *partition)
{
	BmlOverMtdWriteContext *ctx = (BmlOverMtdWriteContext*) malloc(sizeof(BmlOverMtdWriteContext));
	if (ctx == NULL) return NULL;

	ctx->bad_block_offsets = NULL;
	ctx->bad_block_alloc = 0;
	ctx->bad_block_count = 0;

	ctx->buffer = malloc(partition->erase_size);
	if (ctx->buffer == NULL) {
		free(ctx);
		return NULL;
	}

	char mtddevname[32];
	sprintf(mtddevname, "/dev/mtd/mtd%d", partition->device_index);
	ctx->fd = open(mtddevname, O_RDWR);
	if (ctx->fd < 0) {
		free(ctx->buffer);
		free(ctx);
		return NULL;
	}

	ctx->partition = partition;
	ctx->stored = 0;
	return ctx;
}

static int bml_over_mtd_write_close(BmlOverMtdWriteContext *ctx)
{
	int r = 0;
	if (close(ctx->fd)) r = -1;
	free(ctx->bad_block_offsets);
	free(ctx->buffer);
	free(ctx);
	return r;
}


#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "bml_over_mtd"

#define BLOCK_SIZE    2048
#define SPARE_SIZE    (BLOCK_SIZE >> 5)

#define EXIT_CODE_BAD_BLOCKS 15

static int die(const char *msg, ...) {
	int err = errno;
	va_list args;
	va_start(args, msg);
	char buf[1024];
	vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);

	if (err != 0) {
		strlcat(buf, ": ", sizeof(buf));
		strlcat(buf, strerror(err), sizeof(buf));
	}

	fprintf(stderr, "%s\n", buf);
	return 1;
}

static unsigned short* CreateEmptyBlockMapping(const MtdPartition* pSrcPart)
{
	size_t srcTotal, srcErase, srcWrite;
	if (mtd_partition_info(pSrcPart, &srcTotal, &srcErase, &srcWrite) != 0)
	{
		fprintf(stderr, "Failed to access partition.\n");
		return NULL;
	}

	int numSrcBlocks = srcTotal/srcErase;

	unsigned short* pMapping = malloc(numSrcBlocks * sizeof(unsigned short));
	if (pMapping == NULL)
	{
		fprintf(stderr, "Failed to allocate block mapping memory.\n");
		return NULL;
	}
	memset(pMapping, 0xFF, numSrcBlocks * sizeof(unsigned short));
	return pMapping;
}

static const unsigned short* CreateBlockMapping(const MtdPartition* pSrcPart, int srcPartStartBlock,
		const MtdPartition *pReservoirPart, int reservoirPartStartBlock)
{
	size_t srcTotal, srcErase, srcWrite;
	if (mtd_partition_info(pSrcPart, &srcTotal, &srcErase, &srcWrite) != 0)
	{
		fprintf(stderr, "Failed to access partition.\n");
		return NULL;
	}

	int numSrcBlocks = srcTotal/srcErase;

	unsigned short* pMapping = malloc(numSrcBlocks * sizeof(unsigned short));
	if (pMapping == NULL)
	{
		fprintf(stderr, "Failed to allocate block mapping memory.\n");
		return NULL;
	}
	memset(pMapping, 0xFF, numSrcBlocks * sizeof(unsigned short));

	size_t total, erase, write;
	if (mtd_partition_info(pReservoirPart, &total, &erase, &write) != 0)
	{
		fprintf(stderr, "Failed to access reservoir partition.\n");
		free(pMapping);
		return NULL;
	}

	if (erase != srcErase || write != srcWrite)
	{
		fprintf(stderr, "Source partition and reservoir partition differ in size properties.\n");
		free(pMapping);
		return NULL;
	}

	printf("Partition info: Total %d, Erase %d, write %d\n", total, erase, write);

	BmlOverMtdReadContext *readctx = bml_over_mtd_read_partition(pReservoirPart);
	if (readctx == NULL)
	{
		fprintf(stderr, "Failed to open reservoir partition for reading.\n");
		free(pMapping);
		return NULL;
	}

	if (total < erase || total > INT_MAX)
	{
		fprintf(stderr, "Unsuitable reservoir partition properties.\n");
		free(pMapping);
		bml_over_mtd_read_close(readctx);
		return NULL;
	}

	int foundMappingTable = 0;

	int currOffset = total; //Offset *behind* the last byte
	while (currOffset > 0)
	{
		currOffset -= erase;
		loff_t pos = lseek64(readctx->fd, currOffset, SEEK_SET);
		int mgbb = ioctl(readctx->fd, MEMGETBADBLOCK, &pos);
		if (mgbb != 0)
		{
			printf("Bad block %d in reservoir area, skipping.\n", currOffset/erase);
			continue;
		}
		ssize_t readBytes = read(readctx->fd, readctx->buffer, erase);
		if (readBytes != (ssize_t)erase)
		{
			fprintf(stderr, "Failed to read good block in reservoir area (%s).\n",
					strerror(errno));
			free(pMapping);
			bml_over_mtd_read_close(readctx);
			return NULL;
		}
		if (readBytes >= 0x2000)
		{
			char* buf = readctx->buffer;
			if (buf[0]=='U' && buf[1]=='P' && buf[2]=='C' && buf[3]=='H')
			{
				printf ("Found mapping block mark at 0x%x (block %d).\n", currOffset, currOffset/erase);

				unsigned short* mappings = (unsigned short*) &buf[0x1000];
				if (mappings[0]==0 && mappings[1]==0xffff)
				{
					printf("Found start of mapping table.\n");
					foundMappingTable = 1;
					//Skip first entry (dummy)
					unsigned short* mappingEntry = mappings + 2;
					while (mappingEntry - mappings < 100
							&& mappingEntry[0] != 0xffff)
					{
						unsigned short rawSrcBlk = mappingEntry[0];
						unsigned short rawDstBlk = mappingEntry[1];

						printf("Found raw block mapping %d -> %d\n", rawSrcBlk,
								rawDstBlk);

						unsigned int srcAbsoluteStartAddress = srcPartStartBlock * erase;
						unsigned int resAbsoluteStartAddress = reservoirPartStartBlock * erase;

						int reservoirLastBlock = reservoirPartStartBlock + numSrcBlocks - 1;
						if (rawDstBlk < reservoirPartStartBlock
								|| rawDstBlk*erase >= resAbsoluteStartAddress+currOffset)
						{
							fprintf(stderr, "Mapped block not within reasonable reservoir area.\n");
							foundMappingTable = 0;
							break;
						}

						int srcLastBlock = srcPartStartBlock + numSrcBlocks - 1;
						if (rawSrcBlk >= srcPartStartBlock && rawSrcBlk <= srcLastBlock)
						{

							unsigned short relSrcBlk = rawSrcBlk - srcPartStartBlock;
							unsigned short relDstBlk = rawDstBlk - reservoirPartStartBlock;
							printf("Partition relative block mapping %d -> %d\n",relSrcBlk, relDstBlk);

							printf("Absolute mapped start addresses 0x%x -> 0x%x\n",
									srcAbsoluteStartAddress+relSrcBlk*erase,
									resAbsoluteStartAddress+relDstBlk*erase);
							printf("Partition relative mapped start addresses 0x%x -> 0x%x\n",
									relSrcBlk*erase, relDstBlk*erase);

							//Set mapping entry. For duplicate entries, later entries replace former ones.
							//*Assumption*: Bad blocks in reservoir area will not be mapped themselves in
							//the mapping table. User partition blocks will not be mapped to bad blocks
							//(only) in the reservoir area. This has to be confirmed on a wider range of
							//devices.
							pMapping[relSrcBlk] = relDstBlk;

						}
						mappingEntry+=2;
					}
					break; //We found the mapping table, no need to search further
				}


			}
		}

	}
	bml_over_mtd_read_close(readctx);

	if (foundMappingTable == 0)
	{
		fprintf(stderr, "Cannot find mapping table in reservoir partition.\n");
		free(pMapping);
		return NULL;
	}

	//Consistency and validity check
	int mappingValid = 1;
	readctx = bml_over_mtd_read_partition(pSrcPart);
	if (readctx == NULL)
	{
		fprintf(stderr, "Cannot open source partition for reading.\n");
		free(pMapping);
		return NULL;
	}
	int currBlock = 0;
	for (;currBlock < numSrcBlocks; ++currBlock)
	{
		loff_t pos = lseek64(readctx->fd, currBlock*erase, SEEK_SET);
		int mgbb = ioctl(readctx->fd, MEMGETBADBLOCK, &pos);
		if (mgbb == 0)
		{
			if (pMapping[currBlock]!=0xffff)
			{
				fprintf(stderr, "Consistency error: Good block has mapping entry %d -> %d\n", currBlock, pMapping[currBlock]);
				mappingValid = 0;
			}
		} else
		{
			//Bad block!
			if (pMapping[currBlock]==0xffff)
			{
				fprintf(stderr, "Consistency error: Bad block has no mapping entry \n");
				mappingValid = 0;
			} else
			{
				BmlOverMtdReadContext* reservoirReadCtx = bml_over_mtd_read_partition(pReservoirPart);
				if (reservoirReadCtx == 0)
				{
					fprintf(stderr, "Reservoir partition cannot be opened for reading in consistency check.\n");
					mappingValid = 0;
				} else
				{
					pos = lseek64(reservoirReadCtx->fd, pMapping[currBlock]*erase, SEEK_SET);
					mgbb = ioctl(reservoirReadCtx->fd, MEMGETBADBLOCK, &pos);
					if (mgbb == 0)
					{
						printf("Bad block has properly mapped reservoir block %d -> %d\n",currBlock, pMapping[currBlock]);
					}
					else
					{
						fprintf(stderr, "Consistency error: Mapped block is bad, too. (%d -> %d)\n",currBlock, pMapping[currBlock]);
						mappingValid = 0;
					}

				}
				bml_over_mtd_read_close(reservoirReadCtx);
			}

		}

	}
	bml_over_mtd_read_close(readctx);


	if (!mappingValid)
	{
		free(pMapping);
		return NULL;
	}

	return pMapping;
}

static void ReleaseBlockMapping(const unsigned short* blockMapping)
{
	free((void*)blockMapping);
}

static int dump_bml_partition(const MtdPartition* pSrcPart, const MtdPartition* pReservoirPart,
		const unsigned short* blockMapping, const char* filename)
{
	int fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0)
	{
		fprintf(stderr, "error opening %s", filename);
		return -1;
	}
	BmlOverMtdReadContext* pSrcRead = bml_over_mtd_read_partition(pSrcPart);
	if (pSrcRead == NULL)
	{
		close(fd);
		fprintf(stderr, "dump_bml_partition: Error opening src part for reading.\n");
		return -1;
	}

	BmlOverMtdReadContext* pResRead = bml_over_mtd_read_partition(pReservoirPart);
	if (pResRead == NULL)
	{
		close(fd);
		bml_over_mtd_read_close(pSrcRead);
		fprintf(stderr, "dump_bml_partition: Error opening reservoir part for reading.\n");
		return -1;
	}


	int numBlocks = pSrcPart->size / pSrcPart->erase_size;
	int currblock = 0;
	for (;currblock < numBlocks; ++currblock)
	{
		int srcFd = -1;
		if (blockMapping[currblock] == 0xffff)
		{
			//Good block, use src partition
			srcFd = pSrcRead->fd;
			if (lseek64(pSrcRead->fd, currblock*pSrcPart->erase_size, SEEK_SET)==-1)
			{
				close(fd);
				bml_over_mtd_read_close(pSrcRead);
				bml_over_mtd_read_close(pResRead);
				fprintf(stderr, "dump_bml_partition: lseek in src partition failed\n");
				return -1;
			}
		} else
		{
			//Bad block, use mapped block in reservoir partition
			srcFd = pResRead->fd;
			if (lseek64(pResRead->fd, blockMapping[currblock]*pSrcPart->erase_size, SEEK_SET)==-1)
			{
				close(fd);
				bml_over_mtd_read_close(pSrcRead);
				bml_over_mtd_read_close(pResRead);
				fprintf(stderr, "dump_bml_partition: lseek in reservoir partition failed\n");
				return -1;
			}
		}
		size_t blockBytesRead = 0;
		while (blockBytesRead < pSrcPart->erase_size)
		{
			ssize_t len = read(srcFd, pSrcRead->buffer + blockBytesRead,
					pSrcPart->erase_size - blockBytesRead);
			if (len <= 0)
			{
				close(fd);
				bml_over_mtd_read_close(pSrcRead);
				bml_over_mtd_read_close(pResRead);
				fprintf(stderr, "dump_bml_partition: reading partition failed\n");
				return -1;
			}
			blockBytesRead += len;
		}

		size_t blockBytesWritten = 0;
		while (blockBytesWritten < pSrcPart->erase_size)
		{
			ssize_t len = write(fd, pSrcRead->buffer + blockBytesWritten,
					pSrcPart->erase_size - blockBytesWritten);
			if (len <= 0)
			{
				close(fd);
				bml_over_mtd_read_close(pSrcRead);
				bml_over_mtd_read_close(pResRead);
				fprintf(stderr, "dump_bml_partition: writing partition dump file failed\n");
				return -1;
			}
			blockBytesWritten += len;
		}

	}

	bml_over_mtd_read_close(pSrcRead);
	bml_over_mtd_read_close(pResRead);

	if (close(fd)) {
		unlink(filename);
		printf("error closing %s", filename);
		return -1;
	}

	return 0;
}

static ssize_t bml_over_mtd_write_block(int fd, ssize_t erase_size, char* data)
{
	off_t pos = lseek(fd, 0, SEEK_CUR);
	if (pos == (off_t) -1) return -1;

	ssize_t size = erase_size;
	loff_t bpos = pos;
	int ret = ioctl(fd, MEMGETBADBLOCK, &bpos);
	if (ret != 0 && !(ret == -1 && errno == EOPNOTSUPP)) {
		fprintf(stderr,
				"Mapping failure: Trying to write bad block at 0x%08lx (ret %d errno %d)\n",
				pos, ret, errno);
		return -1;
	}

	struct erase_info_user erase_info;
	erase_info.start = pos;
	erase_info.length = size;
	int retry;
	for (retry = 0; retry < 2; ++retry) {
		if (ioctl(fd, MEMERASE, &erase_info) < 0) {
			fprintf(stderr, "mtd: erase failure at 0x%08lx (%s)\n",
					pos, strerror(errno));
			continue;
		}
		if (lseek(fd, pos, SEEK_SET) != pos ||
				write(fd, data, size) != size) {
			fprintf(stderr, "mtd: write error at 0x%08lx (%s)\n",
					pos, strerror(errno));
		}

		char verify[size];
		if (lseek(fd, pos, SEEK_SET) != pos ||
				read(fd, verify, size) != size) {
			fprintf(stderr, "mtd: re-read error at 0x%08lx (%s)\n",
					pos, strerror(errno));
			continue;
		}
		if (memcmp(data, verify, size) != 0) {
			fprintf(stderr, "mtd: verification error at 0x%08lx (%s)\n",
					pos, strerror(errno));
			continue;
		}

		if (retry > 0) {
			fprintf(stderr, "mtd: wrote block after %d retries\n", retry);
		}
		fprintf(stderr, "mtd: successfully wrote block at %llx\n", pos);
		return size;  // Success!
	}


	fprintf(stderr, "mtd: Block at %llx could not be properly written.\n", pos);
	// Ran out of space on the device
	errno = ENOSPC;
	return -1;
}

static int flash_bml_partition(const MtdPartition* pSrcPart, const MtdPartition* pReservoirPart,
		const unsigned short* blockMapping, const char* filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		fprintf(stderr, "error opening %s", filename);
		return -1;
	}
	BmlOverMtdWriteContext* pSrcWrite = bml_over_mtd_write_partition(pSrcPart);
	if (pSrcWrite == NULL)
	{
		close(fd);
		fprintf(stderr, "flash_bml_partition: Error opening src part for writing.\n");
		return -1;
	}

#ifdef DUMMY_WRITING
	close(pSrcWrite->fd);
	pSrcWrite->fd = open("/sdcard/srcPartWriteDummy.bin", O_WRONLY|O_CREAT|O_TRUNC, 0666);
#endif

	BmlOverMtdWriteContext* pResWrite = bml_over_mtd_write_partition(pReservoirPart);
	if (pResWrite == NULL)
	{
		close(fd);
		bml_over_mtd_write_close(pSrcWrite);
		fprintf(stderr, "flash_bml_partition: Error opening reservoir part for writing.\n");
		return -1;
	}
#ifdef DUMMY_WRITING
	close(pResWrite->fd);
	pResWrite->fd = open("/sdcard/resPartWriteDummy.bin", O_WRONLY|O_CREAT|O_TRUNC, 0666);
#endif

	struct stat fileStat;
	if (fstat(fd, &fileStat) != 0)
	{
		close(fd);
		bml_over_mtd_write_close(pSrcWrite);
		bml_over_mtd_write_close(pResWrite);
		fprintf(stderr, "flash_bml_partition: Failed to stat source file.\n");
		return -1;

	}
	if (fileStat.st_size > pSrcPart->size)
	{
		close(fd);
		bml_over_mtd_write_close(pSrcWrite);
		bml_over_mtd_write_close(pResWrite);
		fprintf(stderr, "flash_bml_partition: Source file too large for target partition.\n");
		return -1;
	}

	int numBlocks = (fileStat.st_size +  pSrcPart->erase_size - 1) / pSrcPart->erase_size;
	int currblock;
	for (currblock = 0 ;currblock < numBlocks; ++currblock)
	{
		memset(pSrcWrite->buffer, 0xFF, pSrcPart->erase_size);
		size_t blockBytesRead = 0;
		while (blockBytesRead < pSrcPart->erase_size)
		{
			ssize_t len = read(fd, pSrcWrite->buffer + blockBytesRead,
					pSrcPart->erase_size - blockBytesRead);
			if (len < 0)
			{
				close(fd);
				bml_over_mtd_write_close(pSrcWrite);
				bml_over_mtd_write_close(pResWrite);
				fprintf(stderr, "flash_bml_partition: read source file failed\n");
				return -1;
			}
			if (len == 0)
			{
				//End of file
				break;
			}

			blockBytesRead += len;
		}



		int srcFd = -1;
		if (blockMapping[currblock] == 0xffff)
		{
			//Good block, use src partition
			srcFd = pSrcWrite->fd;
			if (lseek64(pSrcWrite->fd, currblock*pSrcPart->erase_size, SEEK_SET)==-1)
			{
				close(fd);
				bml_over_mtd_write_close(pSrcWrite);
				bml_over_mtd_write_close(pResWrite);
				fprintf(stderr, "flash_bml_partition: lseek in src partition failed\n");
				return -1;
			}
		} else
		{
			//Bad block, use mapped block in reservoir partition
			srcFd = pResWrite->fd;
			if (lseek64(pResWrite->fd, blockMapping[currblock]*pSrcPart->erase_size, SEEK_SET)==-1)
			{
				close(fd);
				bml_over_mtd_write_close(pSrcWrite);
				bml_over_mtd_write_close(pResWrite);
				fprintf(stderr, "flash_bml_partition: lseek in reservoir partition failed\n");
				return -1;
			}
		}
		size_t blockBytesWritten = 0;
		while (blockBytesWritten < pSrcPart->erase_size)
		{
#ifdef DUMMY_WRITING
			ssize_t len = write(srcFd, pSrcWrite->buffer + blockBytesWritten,
					pSrcPart->erase_size - blockBytesWritten);
#else
			ssize_t len = bml_over_mtd_write_block(srcFd, pSrcPart->erase_size, pSrcWrite->buffer);
#endif
			if (len <= 0)
			{
				close(fd);
				bml_over_mtd_write_close(pSrcWrite);
				bml_over_mtd_write_close(pResWrite);
				fprintf(stderr, "flash_bml_partition: writing to partition failed\n");
				return -1;
			}
			blockBytesWritten += len;
		}


	}

	bml_over_mtd_write_close(pSrcWrite);
	bml_over_mtd_write_close(pResWrite);

	if (close(fd)) {
		printf("error closing %s", filename);
		return -1;
	}

	return 0;
}

static int scan_partition(const MtdPartition* pPart)
{
	BmlOverMtdReadContext* readCtx = bml_over_mtd_read_partition(pPart);
	if (readCtx == NULL)
	{
		fprintf(stderr, "Failed to open partition for reading.\n");
		return -1;
	}

	int numBadBlocks = 0;
	size_t numBlocks = pPart->size / pPart->erase_size;
	size_t currBlock;
	for (currBlock = 0; currBlock < numBlocks; ++currBlock)
	{

		loff_t pos = currBlock * pPart->erase_size;
		int mgbb = ioctl(readCtx->fd, MEMGETBADBLOCK, &pos);
		if (mgbb != 0)
		{
			printf("Bad block %d at 0x%x.\n", currBlock, (unsigned int)pos);
			numBadBlocks++;
		}
	}

	bml_over_mtd_read_close(readCtx);
	if (numBadBlocks == 0)
	{
		printf("No bad blocks.\n");
		return 0;
	}
	return -1 ;
}

int main(int argc, char **argv)
{
	if (argc != 7 && (argc != 3 || (argc == 3 && strcmp(argv[1],"scan"))!=0)
			&& (argc != 6 || (argc == 6 && strcmp(argv[1],"scan"))!=0))
		return die("Usage: %s dump|flash <partition> <partition_start_block> <reservoirpartition> <reservoir_start_block> <file>\n"
				"E.g. %s dump boot 72 reservoir 2004 file.bin\n"
				"Usage: %s scan <partition> [<partition_start_block> <reservoirpartition> <reservoir_start_block>]\n"
				,argv[0], argv[0], argv[0]);
	int num_partitions = mtd_scan_partitions();
	const MtdPartition *pSrcPart = mtd_find_partition_by_name(argv[2]);
	if (pSrcPart == NULL)
		return die("Cannot find partition %s", argv[2]);

	int scanResult = scan_partition(pSrcPart);

	if (argc == 3 && strcmp(argv[1],"scan")==0)
	{
		return (scanResult == 0 ? 0 : EXIT_CODE_BAD_BLOCKS);
	}

	int retVal = 0;
	const MtdPartition* pReservoirPart = mtd_find_partition_by_name(argv[4]);
	if (pReservoirPart == NULL)
		return die("Cannot find partition %s", argv[4]);

	int srcPartStartBlock = atoi(argv[3]);
	int reservoirPartStartBlock = atoi(argv[5]);
	const unsigned short* pMapping = CreateBlockMapping(pSrcPart, srcPartStartBlock,
			pReservoirPart, reservoirPartStartBlock);

	if (pMapping == NULL && scanResult == 0)
	{
		printf("Generating empty block mapping table for error-free partition.\n");
		pMapping = CreateEmptyBlockMapping(pSrcPart);
	}

	if (argc == 6 && strcmp(argv[1],"scan")==0)
	{
		retVal = (scanResult == 0 ? 0 : EXIT_CODE_BAD_BLOCKS);
	}

	if (pMapping == NULL)
		return die("Failed to create block mapping table");

	if (strcmp(argv[1],"dump")==0)
	{
		retVal = dump_bml_partition(pSrcPart, pReservoirPart, pMapping, argv[6]);
		if (retVal == 0)
			printf("Successfully dumped partition to %s\n", argv[6]);
	}

	if (strcmp(argv[1],"flash")==0)
	{
		retVal = flash_bml_partition(pSrcPart, pReservoirPart, pMapping, argv[6]);
		if (retVal == 0)
			printf("Successfully wrote %s to partition\n", argv[6]);

	}


	ReleaseBlockMapping(pMapping);
	return retVal;
}

