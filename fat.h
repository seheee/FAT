/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : fat.h                                                            */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : FAT File System header                                           */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#ifndef _FAT_H_
#define _FAT_H_

#include "common.h"
#include "disk.h"
#include "clusterlist.h"

#define FAT12					0
#define FAT16					1
#define FAT32					2

#define MAX_SECTOR_SIZE			512
#define MAX_NAME_LENGTH			256
#define MAX_ENTRY_NAME_LENGTH	11

#define ATTR_READ_ONLY			0x01
#define ATTR_HIDDEN				0x02
#define ATTR_SYSTEM				0x04
#define ATTR_VOLUME_ID			0x08
#define ATTR_DIRECTORY			0x10
#define ATTR_ARCHIVE			0x20
#define ATTR_LONG_NAME			ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID

#define VOLUME_LABEL			"FAT BY SKM "
#define DIR_ENTRY_FREE			0xE5
#define DIR_ENTRY_NO_MORE		0x00
#define DIR_ENTRY_OVERWRITE		1

#define SHUT_BIT_MASK16			0x8000
#define ERR_BIT_MASK16			0x4000

#define SHUT_BIT_MASK32			0x08000000
#define ERR_BIT_MASK32			0x04000000

#define EOC12					0x0FF8
#define EOC16					0xFFF8
#define EOC32					0x0FFFFFF8
#define FREE_CLUSTER			0x00

#define MS_EOC12				0x0FFF
#define MS_EOC16				0xFFFF
#define MS_EOC32				0x0FFFFFFF

#define SET_FIRST_CLUSTER( a, b )	{ ( a ).firstClusterHI = ( b ) >> 16; ( a ).firstClusterLO = ( WORD )( ( b ) & 0xFFFF ); }
#define GET_FIRST_CLUSTER( a )		( ( ( ( DWORD )( a ).firstClusterHI ) << 16 ) | ( a ).firstClusterLO )
//#define IS_POINT_ROOT_ENTRY( a )	( ( a ).attribute & ATTR_VOLUME_ID )
#define IS_POINT_ROOT_ENTRY( a )	( ( ( a ).attribute & ATTR_VOLUME_ID ) || ( ( ( a ).attribute & ATTR_DIRECTORY ) && ( ( a ).firstClusterLO == 0 ) ) || ( a ).name[0] == 32 )

/* FAT structures are written based on MS Hardware White Paper */
#ifdef _WIN32
#pragma pack(push,fatstructures)
#endif
#pragma pack(1)

// FAT_BOOTSECTOR
typedef struct
{
	BYTE	driveNumber;
	BYTE	reserved1;
	BYTE	bootSignature;
	DWORD	volumeID;
	BYTE	volumeLabel[11];
	BYTE	filesystemType[8];
} FAT_BOOTSECTOR;


// FAT_BPB
typedef struct
{
	BYTE	jmpBoot[3];
	BYTE	OEMName[8];

	UINT16	bytesPerSector; 
	UINT8	sectorsPerCluster; // cluster의 크기를 위한 정보
	UINT16	reservedSectorCount; // disk에서 0번부터 몇번 sector까지 메타데이터를 위한 sector로 사용할 것인지 나타냄.
								 // FAT16은 1개, FAT32는 32개의 sector.
	UINT8	numberOfFATs; // 해당 볼륨에 존재하는 FAT영역의 개수
	UINT16	rootEntryCount; // 루트 디렉터리가 수용하는 엔트리(파일, 디렉터리) 개수
	UINT16	totalSectors; // 볼륨의 총 sector 개수

	BYTE	media; // 저장장치의 종류

	UINT16	FATSize16;
	UINT16	sectorsPerTrack;
	UINT16	numberOfHeads;
	UINT32	hiddenSectors;
	UINT32	totalSectors32;

	union
	{
		FAT_BOOTSECTOR bs; // 드라이브, 볼륨 정보

		struct
		{
			// FAT32를 위한 추가적인 항목
			UINT32	FATSize32;
			WORD	extFlags;
			WORD	FSVersion;
			UINT32	rootCluster; // 루트디렉터리의 cluster값
			WORD	FSInfo; // FSInfo 구조체의 위치정보
			UINT16	backupBootSectors;
			BYTE	reserved[12];
			FAT_BOOTSECTOR bs;
		} BPB32;

		// boot record의 크기는 1sector(512byte)
		char padding[512 - 36];
	};
} FAT_BPB;


// FAT_FSINFO
typedef struct
{
	DWORD	leadSignature; // FSInfo sector라는 것을 표시
	BYTE	reserved1[480];
	DWORD	structSignature; // FSInfo sector라는 것을 표시
	UINT32	freeCount; // 볼륨에서 사용되고 있지 않은 cluster의 개수
	UINT32	nextFree; // empty cluster의 위치를 빠르게 얻기 위한 항목
	BYTE	reserved2[12];
	DWORD	trailSignature; // FSInfo sector라는 것을 표시
} FAT_FSINFO;


// FAT_DIR_ENTRY
typedef struct
{
	BYTE	name[11];
	BYTE	attribute; // 이 entry의 용도(파일, 디렉터리 등)
	BYTE	NTReserved;

	BYTE	createdTimeThen;
	WORD	createdTime;
	WORD	createdDate;

	WORD	lastAccessDate;

	WORD	firstClusterHI; // 첫 번째 cluster 번호의 상위 2byte, FAT32에서 참조

	WORD	writeTime;
	WORD	writeData;

	WORD	firstClusterLO; // 첫 번째 cluster 번호의 하위 2byte

	UINT32	fileSize;
} FAT_DIR_ENTRY;

#ifdef _WIN32
#pragma pack(pop, fatstructures)
#else
#pragma pack()
#endif


// FAT_FILESYSTEM
// disk 정보
typedef struct
{
	BYTE			FATType;
	DWORD			FATSize;
	DWORD			EOCMark;
	FAT_BPB			bpb;
	CLUSTER_LIST	freeClusterList;
	DISK_OPERATIONS*	disk;

	union
	{
		FAT_FSINFO	info32;
		struct
		{
			UINT32	freeCount;
			UINT32	nextFree;
		} info;
	};
} FAT_FILESYSTEM;


// FAT_FILETIME
typedef struct
{
	WORD	year;
} FAT_FILETIME;


// FAT_ENTRY_LOCATION
// 디렉터리 엔트리 위치정보
typedef struct
{
	UINT32	cluster; // 해당 dir_entry가 위치해있는 cluster
	UINT32	sector; // 해당 cluster에서의 sector offset
	INT32	number; // 해당 sector에서의 dir_entry offset
} FAT_ENTRY_LOCATION;


// FAT_NODE
typedef struct
{
	FAT_FILESYSTEM*		fs; 
	FAT_DIR_ENTRY		entry; 
	FAT_ENTRY_LOCATION	location;
} FAT_NODE;

typedef int ( *FAT_NODE_ADD )( void*, FAT_NODE* );

void fat_umount( FAT_FILESYSTEM* fs );
int fat_read_superblock( FAT_FILESYSTEM* fs, FAT_NODE* root );
int fat_read_dir( FAT_NODE* dir, FAT_NODE_ADD adder, void* list );
int fat_mkdir( const FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry );
int fat_rmdir( FAT_NODE* node );
int fat_lookup( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry );
int fat_create( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry );
int fat_read( FAT_NODE* file, unsigned long offset, unsigned long length, char* buffer );
int fat_write( FAT_NODE* file, unsigned long offset, unsigned long length, const char* buffer );
int fat_remove( FAT_NODE* file );
int fat_df( FAT_FILESYSTEM* fs, UINT32* totalSectors, UINT32* usedSectors );

#endif

