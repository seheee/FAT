/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : fat_shell.c                                                      */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Adaption layer between FAT File System and shell                 */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "fat_shell.h"

#define FSOPRS_TO_FATFS( a )		( FAT_FILESYSTEM* )a->pdata

typedef struct
{
	union
	{
		WORD	halfCluster[2];
		DWORD	fullCluster;
	};
	BYTE	attribute;
} PRIVATE_FAT_ENTRY;

char* my_strncpy( char* dest, const char* src, int length )
{
	while( *src && *src != 0x20 && length-- > 0 )
		*dest++ = *src++;

	return dest;
}

int my_strnicmp( const char* str1, const char* str2, int length )
{
	char	c1, c2;

	while( ( ( *str1 && *str1 != 0x20 ) || ( *str2 && *str2 != 0x20 ) ) && length-- > 0 )
	{
		c1 = toupper( *str1 );
		c2 = toupper( *str2 );

		if( c1 > c2 )
			return -1;
		else if( c1 < c2 )
			return 1;

		str1++;
		str2++;
	}

	return 0;
}

// fat_node의 정보를 shell_entry에 적재
/* 사후조건 : 전달인자로 주어진 shell_entry는 shell level directory entry를 가지고
			동시에 fat directory entry를 버퍼에 보관한다.
			-> 즉, shell level에서는 언제든 양쪽 entry에 접근이 가능함*/
int fat_entry_to_shell_entry( const FAT_NODE* fat_entry, SHELL_ENTRY* shell_entry )
{
	FAT_NODE* entry = ( FAT_NODE* )shell_entry->pdata;
	BYTE*	str;

	// shell_entry를 0으로 setting
	memset( shell_entry, 0, sizeof( SHELL_ENTRY ) );

	/* create_root()에서 디렉터리를 생성해서 root sector에 write할 때, 디렉터리의 첫 번째 entry에
		name = VOLUME_LABEL로, attribute = ATTR_VOLUME_ID로 설정하고,
		그 다음 entry의 name에 DIR_ENTRY_NO_MORE(0x00)을 넣어 디렉터리 엔트리가 더이상 없음을 표시했음*/

	// 루트 디렉터리가 아닌 모든 파일인 경우
	if( fat_entry->entry.attribute != ATTR_VOLUME_ID )
	{
		str = shell_entry->name;
		// my_strncpy함수로 FAT_DIR_ENTRY의 name을 복사
		str = my_strncpy( str, fat_entry->entry.name, 8 );
		if( fat_entry->entry.name[8] != 0x20 )
		{
			str = my_strncpy( str, ".", 1 );
			str = my_strncpy( str, &fat_entry->entry.name[8], 3 );
		}
	}

	// root거나 directory이면 entry정보를 directory로 setting
	if( fat_entry->entry.attribute & ATTR_DIRECTORY ||
		fat_entry->entry.attribute & ATTR_VOLUME_ID )
		shell_entry->isDirectory = 1;
	else
		shell_entry->size = fat_entry->entry.fileSize;

	// shell_entry의 pdata버퍼에 fat_entry를 복사해줌
	*entry = *fat_entry;

	return FAT_SUCCESS;
}

int shell_entry_to_fat_entry( const SHELL_ENTRY* shell_entry, FAT_NODE* fat_entry )
{
	FAT_NODE* entry = ( FAT_NODE* )shell_entry->pdata;

	*fat_entry = *entry;

	return FAT_SUCCESS;
}

// 생성
int	fs_create( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name, SHELL_ENTRY* retEntry )
{
	FAT_NODE	FATParent;
	FAT_NODE	FATEntry;
	int				result;

	// SHELL_ENTRY에 있는 FAT_NODE정보 얻음(parent -> FATParent)
	shell_entry_to_fat_entry( parent, &FATParent );

	result = fat_create( &FATParent, name, &FATEntry );

	// FAT_NODE의 정보를 SHELL_ENTRY에 넣음
	fat_entry_to_shell_entry( &FATEntry, retEntry );

	return result;
}

int fs_remove( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	file;

	shell_entry_to_fat_entry( parent, &FATParent );
	fat_lookup( &FATParent, name, &file );

	return fat_remove( &file );
}

int	fs_read( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, unsigned long offset, unsigned long length, char* buffer )
{
	FAT_NODE	FATEntry;

	shell_entry_to_fat_entry( entry, &FATEntry );

	return fat_read( &FATEntry, offset, length, buffer );
}

int	fs_write( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, unsigned long offset, unsigned long length, const char* buffer )
{
	FAT_NODE	FATEntry;

	shell_entry_to_fat_entry( entry, &FATEntry );

	return fat_write( &FATEntry, offset, length, buffer );
}

static SHELL_FILE_OPERATIONS g_file =
{
	fs_create,
	fs_remove,
	fs_read,
	fs_write
};

int fs_stat( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, unsigned int* totalSectors, unsigned int* usedSectors )
{
	FAT_NODE	entry;

	return fat_df( FSOPRS_TO_FATFS( fsOprs ), totalSectors, usedSectors );
}

int adder( void* list, FAT_NODE* entry )
{
	SHELL_ENTRY_LIST*	entryList = ( SHELL_ENTRY_LIST* )list;
	SHELL_ENTRY			newEntry;

	fat_entry_to_shell_entry( entry, &newEntry );

	add_entry_list( entryList, &newEntry );

	return FAT_SUCCESS;
}

// directory 읽는 함수 호출
int fs_read_dir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY_LIST* list )
{
	FAT_NODE	entry;

	if( list->count )
		// list 전부 free 해주는 함수
		release_entry_list( list );

	// shell_entry를 fat_node로 연결
	shell_entry_to_fat_entry( parent, &entry );

	// 디렉터리 안에 있는 모든 entry 읽는 함수
	// 그 fat_node에서(mkdir한 현재디렉터리) 엔트리 전부 읽어옴
	fat_read_dir( &entry, adder, list );

	return FAT_SUCCESS;
}

// 해당 디렉터리가 존재하는지 확인하는 함수
// parent : currentDirectory
int is_exist( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	SHELL_ENTRY_LIST		list;
	SHELL_ENTRY_LIST_ITEM*	current;

	// SHELL_ENTRY_LIST list를 0으로 초기화
	init_entry_list( &list ); 

	// directory 읽는 함수 (초기화한 list 넘겨줌)
	// 함수 수행 후 list에 directory entry 담겨있음
	fs_read_dir( disk, fsOprs, parent, &list );
	current = list.first;

	// list 모든 노드들 돌면서 요청한 이름이 이미 있는지 확인
	while( current )				/* is directory already exist? */
	{
		if( my_strnicmp( current->entry.name, name, 12 ) == 0 )
		{
			// 있으면 free해주고 에러 리턴
			release_entry_list( &list );
			return FAT_ERROR;		/* the directory is already exist */
		}

		current = current->next;
	}

	// list 모두 순회하고 없으면 list free하고 성공 리턴
	release_entry_list( &list );
	return FAT_SUCCESS;
}

// mkdir
// g_fsOprs.mkdir( &g_disk, &g_fsOprs, &g_currentDir, argv[1], &entry );
int fs_mkdir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name, SHELL_ENTRY* retEntry )
{
	FAT_NODE		FATParent; // 생성할 디렉터리의 상위 디렉터리
	FAT_NODE		FATEntry;  // 생성할 디렉터리
	int					result;
	
	// 해당 디렉터리가 존재하는지 확인하는 함수
	if( is_exist( disk, fsOprs, parent, name ) )
		return FAT_ERROR;

	shell_entry_to_fat_entry( parent, &FATParent );

	result = fat_mkdir( &FATParent, name, &FATEntry );

	fat_entry_to_shell_entry( &FATEntry, retEntry );

	return result;
}

int fs_rmdir( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	dir;

	shell_entry_to_fat_entry( parent, &FATParent );
	fat_lookup( &FATParent, name, &dir );

	return fat_rmdir( &dir );
}

// fat_lookup 호출
int fs_lookup( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, const SHELL_ENTRY* parent, SHELL_ENTRY* entry, const char* name )
{
	FAT_NODE	FATParent;
	FAT_NODE	FATEntry;
	int				result;

	shell_entry_to_fat_entry( parent, &FATParent );

	// prent에 해당name이 있는지
	result = fat_lookup( &FATParent, name, &FATEntry );

	fat_entry_to_shell_entry( &FATEntry, entry );

	return result;
}

static SHELL_FS_OPERATIONS	g_fsOprs =
{
	fs_read_dir,
	fs_stat,
	fs_mkdir,
	fs_rmdir,
	fs_lookup,
	&g_file,
	NULL
};

// 마운트
// 코드적으로 보면 파일 operation등록해주고, super block을 읽고 cluster list를 초기화함
int fs_mount( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs, SHELL_ENTRY* root )
{
	FAT_FILESYSTEM* fat;
	FAT_NODE	fat_entry;
	int		result;
	char	FATTypes[][8] = { "FAT12", "FAT16", "FAT32" };
	char	volumeLabel[12] = { 0, };

	// fsOprs 연결
	*fsOprs = g_fsOprs;
	
	// FAT_FILESYSTEM만큼 할당받음	
	fsOprs->pdata = malloc( sizeof( FAT_FILESYSTEM ) );
	
	// #define FSOPRS_TO_FATFS(a)	(FAT_FILESYSTEM*)(a->pdata)
	// 그래서 아래 코드에서 fat이 fsOprs의 pdata(->FAT_FILESYSTEM공간)을 가리킴
	fat = FSOPRS_TO_FATFS( fsOprs );

	// 그 할당받은 메모리를 0으로 초기화
	ZeroMemory( fat, sizeof( FAT_FILESYSTEM ) );

	// 그 할당받은 메모리에 disk operations 등록
	fat->disk = disk;

	// fat_entry에 root디렉터리 정보 저장됨
	result = fat_read_superblock( fat, &fat_entry ); //fat.h --> FAT 시스템 호출

	if( result == FAT_SUCCESS )
	{	
		// FATType에 따라 출력
		if( fat->FATType == 2)
			memcpy ( volumeLabel, fat->bpb.BPB32.bs.volumeLabel, 11 );
		else
			memcpy ( volumeLabel, fat->bpb.bs.volumeLabel, 11 );

		printf( "FAT type               : %s\n", FATTypes[fat->FATType] );
		printf( "volume label           : %s\n", volumeLabel );
		printf( "bytes per sector       : %d\n", fat->bpb.bytesPerSector );
		printf( "sectors per cluster    : %d\n", fat->bpb.sectorsPerCluster );
		printf( "number of FATs         : %d\n", fat->bpb.numberOfFATs );
		printf( "root entry count       : %d\n", fat->bpb.rootEntryCount );
		printf( "total sectors          : %u\n", ( fat->bpb.totalSectors ? fat->bpb.totalSectors : fat->bpb.totalSectors32 ) );
		printf( "\n" );
	}
	//fat_shell.h --> root 디렉터리 정보 shell형태로 변경
	fat_entry_to_shell_entry( &fat_entry, root ); 
	return result;
}


// 동적할당받은 영역 해제
void fs_umount( DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs )
{
	if( fsOprs && fsOprs->pdata )
	{
		// fat_mount가면 free_cluster_list 해제하는 함수 있음
		fat_umount( FSOPRS_TO_FATFS( fsOprs ) );

		// FILE_SYSTEM 메모리 영역 해제
		free( fsOprs->pdata );
		fsOprs->pdata = 0;
	}
}

// FAT 타입 설정
// FAT 파일 시스템 수준에서의 formatting을 수행하는 모듈
// 최종 목적은 해당 디스크에 적합한 혹은 사용자가 요청한 타입의 파일시스템에 맞는
// formatting 함수인 fat_format()을 호출하는 것
int fs_format( DISK_OPERATIONS* disk, void* param ) 
{
	unsigned char FATType;
	char*	FATTypeString[3] = { "FAT12", "FAT16", "FAT32" };
	char*	paramStr = ( char* )param;
	int		i;

	// 파라미터가 있을경우(사용자가 직접적으로 타입을 요청했을 경우)
	if( param )
	{
		// FAT타입 문자열 지정해놓은 것과 일치하면 해당 인덱스로
		for( i = 0; i < 3; i++ )
		{
			if( my_strnicmp( paramStr, FATTypeString[i], 100 ) == 0 )
			{
				FATType = i;
				break;
			}
		}

		// 해당하는 문자열이 없어서 for문 다 돌고도 못빠져나왔을경우
		if( i == 3 )
		{
			PRINTF( "Unknown FAT type\n" );
			return -1;
		}
	}
	// 파라미터가 없을경우
	// disksim_init을 통해 얻은 디스크 크기 정보로 타입 결정
	else
	{
		// DISK_OPERATIONS 구조체에 있는 섹터개수 보고 fat타입 자동으로 지정
		// 섹터의 수는 shell.c에서 매크로로 정의되어 있음
		// (#define NUMBER_OF_SECTORS	4096)
		if( disk->numberOfSectors <= 8400 )
			FATType = 0;
		else if( disk->numberOfSectors <= 66600 )
			FATType = 1;
		else
			FATType = 2;
	}

	printf( "formatting as a %s\n", FATTypeString[FATType] );
	return fat_format( disk, FATType ); // BPB 등 초기화
}

// 파일시스템 이름, 마운트, 언마운트, 포맷하는 함수 등록되어있는 구조체
static SHELL_FILESYSTEM g_fat = 
{
	"FAT",
	fs_mount,
	fs_umount,
	fs_format
};

// main에서 전달받은 구조체에 위에서 만든 g_fat구조체를 등록해줌
void shell_register_filesystem( SHELL_FILESYSTEM* fs )
{
	*fs = g_fat;
}
