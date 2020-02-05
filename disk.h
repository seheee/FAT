/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : disk.h                                                           */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Disk device header                                               */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#ifndef _DISK_H_
#define _DISK_H_

#include "common.h"

// sector의 크기, 개수
// sector 읽기, 쓰기를 지원하는 함수
// sector로 관리되는 memory 공간
typedef struct DISK_OPERATIONS
{
	int		( *read_sector	)( struct DISK_OPERATIONS*, SECTOR, void* );
	int		( *write_sector	)( struct DISK_OPERATIONS*, SECTOR, const void* );
	SECTOR	numberOfSectors;
	int		bytesPerSector;
	void*	pdata;
} DISK_OPERATIONS;

#endif

