/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : disksim.c                                                        */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Disk simulator                                                   */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include <stdlib.h>
#include <memory.h>
#include "fat.h"
#include "disk.h"
#include "disksim.h"

typedef struct
{
	char*	address;
} DISK_MEMORY;

int disksim_read( DISK_OPERATIONS* this, SECTOR sector, void* data );
int disksim_write( DISK_OPERATIONS* this, SECTOR sector, const void* data );

int disksim_init( SECTOR numberOfSectors, unsigned int bytesPerSector, DISK_OPERATIONS* disk ) // 초기화
{
	if( disk == NULL ) // 디스크 공간x
		return -1;

	// 디스크 메모리를 할당받았을 때 그 메모리를 가리키는 주소를 저장하기 위한 공간을 할당받음
	// malloc의 결과로 할당된 메모리 공간의 void pointer를 return함
	// 디스크 공간을 가리키는 포인터를 디스크구조체가 가지고있게 됨
	disk->pdata = malloc( sizeof( DISK_MEMORY ) ); 
	
	// 메모리 공간이 부족할 경우 null이 return됨
	if( disk->pdata == NULL )
	{
		// 해제해줌
		disksim_uninit( disk );
		return -1;
	}

	// main에서 요청한 disk 크기만큼 할당해서 아까 할당받은 공간의 주소변수에 연결
	( ( DISK_MEMORY* )disk->pdata )->address = ( char* )malloc( bytesPerSector * numberOfSectors );
	
	if( disk->pdata == NULL )
	{
		disksim_uninit( disk );
		return -1;
	}

	// main에서 사용할 DISK_OPERATIONS 구조체에 디스크 특정 함수를 등록해주고, 디스크 크기도 등록함
	disk->read_sector	= disksim_read;
	disk->write_sector	= disksim_write;
	disk->numberOfSectors	= numberOfSectors;
	disk->bytesPerSector	= bytesPerSector;

	return 0;
}

// 동적 할당받은 disk->pdata 해제
void disksim_uninit( DISK_OPERATIONS* this )
{
	if( this )
	{
		if( this->pdata )
			free( this->pdata );
	}
}

// disk의 sector위치에 있는 내용을 sector 크기만큼 요청받은 data주소에 복사
int disksim_read( DISK_OPERATIONS* this, SECTOR sector, void* data )
{
	char* disk = ( ( DISK_MEMORY* )this->pdata )->address; // 처리할 섹터의 데이터 주소

	if( sector < 0 || sector >= this->numberOfSectors )
		return -1;

	//disk의 데이터를 data에 복사(sector크기만큼)
	memcpy( data, &disk[sector * this->bytesPerSector], this->bytesPerSector ); 

	return 0;
}

// 요청한 data주소에 있는 내용을 disk의 sector 위치에 복사
int disksim_write( DISK_OPERATIONS* this, SECTOR sector, const void* data )
{
	char* disk = ( ( DISK_MEMORY* )this->pdata )->address; // 처리할 디스크 주소

	// 섹터 번호가 0보다 작거나 지정된 크기보다 크면 에러
	if( sector < 0 || sector >= this->numberOfSectors )
		return -1;

	// 해당 섹터 주소에 data가 가리키는 곳부터 섹터크기만큼을 복사해줌
	memcpy( &disk[sector * this->bytesPerSector], data, this->bytesPerSector ); // data를 디스크에 쓰기

	return 0;
}

