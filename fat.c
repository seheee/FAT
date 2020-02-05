/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : fat.c                                                            */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : FAT File System core                                             */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include "fat.h"
#include "clusterlist.h"

#define MIN( a, b )					( ( a ) < ( b ) ? ( a ) : ( b ) )
#define MAX( a, b )					( ( a ) > ( b ) ? ( a ) : ( b ) )
#define NO_MORE_CLUSER()			WARNING( "No more clusters are remained\n" );

unsigned char toupper( unsigned char ch );
int isalpha( unsigned char ch );
int isdigit( unsigned char ch );

/* calculate the 'sectors per cluster' by some conditions */
DWORD get_sector_per_clusterN( DWORD diskTable[][2], UINT64 diskSize, UINT32 bytesPerSector )
{
	int i = 0;

	do
	{
		if( ( ( UINT64 )( diskTable[i][0] * 512 ) ) >= diskSize )
			return diskTable[i][1] / ( bytesPerSector / 512 );
	}
	while( diskTable[i++][0] < 0xFFFFFFFF );

	return 0;
}

DWORD get_sector_per_cluster16( UINT64 diskSize, UINT32 bytesPerSector )
{
	DWORD	diskTableFAT16[][2] =
	{
		{ 8400,			0	},
		{ 32680,		2	},
		{ 262144,		4	},
		{ 524288,		8	},
		{ 1048576,		16	},
		/* The entries after this point are not used unless FAT16 is forced */
		{ 2097152,		32	},
		{ 4194304,		64	},
		{ 0xFFFFFFFF,	0	}
	};

	return get_sector_per_clusterN( diskTableFAT16, diskSize, bytesPerSector );
}

DWORD get_sector_per_cluster32( UINT64 diskSize, UINT32 bytesPerSector )
{
	DWORD	diskTableFAT32[][2] =
	{
		{ 66600,		0	},
		{ 532480,		1	},
		{ 16777216,		8	},
		{ 33554432,		16	},
		{ 67108864,		32	},
		{ 0xFFFFFFFF,	64	}
	};

	return get_sector_per_clusterN( diskTableFAT32, diskSize, bytesPerSector );
}

// 클러스터 하나 당 섹터가 몇개인지
DWORD get_sector_per_cluster( BYTE FATType, UINT64 diskSize, UINT32 bytesPerSector )
{
	// fat 타입에 따라
	switch( FATType )
	{
		case 0:		/* FAT12 */
			return 1;
		case 1:		/* FAT16 */
			return get_sector_per_cluster16( diskSize, bytesPerSector );
		case 2:		/* FAT32 */
			return get_sector_per_cluster32( diskSize, bytesPerSector );
	}

	return 0;
}

// fat크기 구해서 bpb에 setting
/* fills the field FATSize16 and FATSize32 of the FAT_BPB */
void fill_fat_size( FAT_BPB* bpb, BYTE FATType )
{
	UINT32	diskSize = ( bpb->totalSectors32 == 0 ? bpb->totalSectors : bpb->totalSectors32 );
	UINT32	rootDirSectors = ( ( bpb->rootEntryCount * 32 ) + (bpb->bytesPerSector - 1) ) / bpb->bytesPerSector;
	UINT32	tmpVal1 = diskSize - ( bpb->reservedSectorCount + rootDirSectors );
	UINT32	tmpVal2 = ( 256 * bpb->sectorsPerCluster ) + bpb->numberOfFATs;
	UINT32	FATSize;

	if( FATType == FAT32 )
		tmpVal2 = tmpVal2 / 2;

	FATSize = ( tmpVal1 + ( tmpVal2 - 1 ) ) / tmpVal2;

	if( FATType == 32 )
	{
		bpb->FATSize16 = 0;
		bpb->BPB32.FATSize32 = FATSize;
	}
	else
		bpb->FATSize16 = ( WORD )( FATSize & 0xFFFF );
}

// FAT 버전에 따라서 BPB에 디스크의 모든 하드웨어적인 정보 등을 등록
// 파일시스템 버전에 따라서 부트 파라미터 블록의 내용이 달라지기 때문에
// 커널에서 사용자에게 원하는 파일시스템을 입력받고 그에맞는 내용으로 채워서 디스크에 써줌
int fill_bpb( FAT_BPB* bpb, BYTE FATType, SECTOR numberOfSectors, UINT32 bytesPerSector )
{
	QWORD diskSize = numberOfSectors * bytesPerSector; 
	
	/*typedef struct
	{
		BYTE	driveNumber;
		BYTE	reserved1;
		BYTE	bootSignature;
		DWORD	volumeID;
		BYTE	volumeLabel[11];
		BYTE	filesystemType[8];
	} FAT_BOOTSECTOR;*/
	FAT_BOOTSECTOR* bs; // 드라이브, 볼륨 정보
	BYTE	filesystemType[][8] = { "FAT12   ", "FAT16   ", "FAT32   " };
	UINT32	sectorsPerCluster;

	// 2보다 큰건 없음, 에러
	if( FATType > 2 )
		return FAT_ERROR;

	// #define ZeroMemory( a, b )      memset( a, 0, b )
	// void* memset(void*ptr, int value, size_t num); >> ptr(포인터)부터 num(바이트)만큼 value로 채움
	ZeroMemory( bpb, sizeof( FAT_BPB ) ); // bpb부터 FAT_BPB의 크기만큼 0으로 채움 -> FAT_BPB구조체인 bpb가 0으로 채워짐

	// Jump Boot Code = Boot Code로 점프하기 위한 코드
	bpb->jmpBoot[0] = 0xEB; 
	bpb->jmpBoot[1] = 0x00;		/* ?? */
	bpb->jmpBoot[2] = 0x90;	

	// OEM : original equipment manufacturer
	memcpy( bpb->OEMName, "MSWIN4.1", 8 );

	// 타입에 맞는 cluster 당 sector 개수
	sectorsPerCluster			= get_sector_per_cluster( FATType, diskSize, bytesPerSector );
	if( sectorsPerCluster == 0 )
	{
		WARNING( "The number of sector is out of range\n" );
		return -1;
	}

	// 여러가지 디스크의 하드웨어적인 정보를 setting
	bpb->bytesPerSector			= bytesPerSector;
	bpb->sectorsPerCluster		= sectorsPerCluster;
	bpb->reservedSectorCount	= ( FATType == FAT32 ? 32 : 1 );
	bpb->numberOfFATs			= 1;
	bpb->rootEntryCount			= ( FATType == FAT32 ? 0 : 512 );
	bpb->totalSectors			= ( numberOfSectors < 0x10000 ? ( UINT16 ) numberOfSectors : 0 );

	bpb->media					= 0xF8;
	fill_fat_size( bpb, FATType ); // fat 크기 구해서 bpb에 넣어주는 함수
	bpb->sectorsPerTrack		= 0;
	bpb->numberOfHeads			= 0;
	bpb->totalSectors32			= ( numberOfSectors >= 0x10000 ? numberOfSectors : 0 );

	// FAT32에만 들어가는것들 처리
	if( FATType == FAT32 )
	{
		bpb->BPB32.extFlags		= 0x0081;	/* active FAT : 1, only one FAT is active */
		bpb->BPB32.FSVersion	= 0;
//		bpb->BPB32.rootCluster	= 2;
		// FSInfo : FSInfo가 위치하는 sector offset. 일반적으로 pbr 바로 뒤에 위치하므로 1의 값을 가짐
		bpb->BPB32.FSInfo		= 1;
		// backupBootSector : BPB의 Backup 영역이 존재하는 sector offset. 일반적으로 0의 값을 가짐
		bpb->BPB32.backupBootSectors	= 6;
		bpb->BPB32.backupBootSectors	= 0;
		// reserved : 예약된 영역. 무조건 0으로 설정
		ZeroMemory( bpb->BPB32.reserved, 12 );
	}


	if( FATType == FAT32 )
		bs = &bpb->BPB32.bs;
	else
		bs = &bpb->bs;

	if( FATType == FAT12 )
		bs->driveNumber	= 0x00;
	else
		bs->driveNumber	= 0x80;

	// Reserved : 예약된 영역으로 항상 0으로 채워진다.
	// Reserved1 : Windows NT 계열에서 사용하려고 만든 예약된 영역이며 0으로 채워져 있다.
	// Boot Signature : 확장 부트 서명으로 0x29라는 값이 들어간다(이후에 3가지 항목이 더 존재함을 의미)
	// Volume ID : 볼륨의 시리얼 번호
	// Volume Label : 볼륨 레이블을 적어준다
	// File System Type : 항상 FAT32로 적혀있다.
	bs->reserved1		= 0;
	bs->bootSignature	= 0x29;
	bs->volumeID		= 0;
	memcpy( bs->volumeLabel, VOLUME_LABEL, 11 );
	memcpy( bs->filesystemType, filesystemType[FATType], 8 );

	return FAT_SUCCESS;
}

int get_fat_type( FAT_BPB* bpb )
{
	UINT32	totalSectors, dataSector, rootSector, countOfClusters, FATSize;

	rootSector = ( ( bpb->rootEntryCount * 32 ) + ( bpb->bytesPerSector - 1 ) ) / bpb->bytesPerSector;

	if( bpb->FATSize16 != 0 )
		FATSize = bpb->FATSize16;
	else
		FATSize = bpb->BPB32.FATSize32;

	if( bpb->totalSectors != 0 )
		totalSectors = bpb->totalSectors;
	else
		totalSectors = bpb->totalSectors32;

	dataSector = totalSectors - ( bpb->reservedSectorCount + ( bpb->numberOfFATs * FATSize ) + rootSector );
	countOfClusters = dataSector / bpb->sectorsPerCluster;

	if( countOfClusters < 4085 )
		return FAT12;
	else if( countOfClusters < 65525 )
		return FAT16;
	else
		return FAT32;

	return FAT_ERROR;
}

FAT_ENTRY_LOCATION get_entry_location( const FAT_DIR_ENTRY* entry )
{
	FAT_ENTRY_LOCATION	location;

	location.cluster	= GET_FIRST_CLUSTER( *entry );
	location.sector		= 0;
	location.number		= 0;

	return location;
}

/* fills the reserved fields of FAT */
int fill_reserved_fat( FAT_BPB* bpb, BYTE* sector )
{
	BYTE	FATType;
	DWORD*	shutErrBit12;
	WORD*	shutBit16;
	WORD*	errBit16;
	DWORD*	shutBit32;
	DWORD*	errBit32;

	FATType = get_fat_type( bpb );
	if( FATType == FAT12 )
	{
		shutErrBit12 = ( DWORD* )sector;

		*shutErrBit12 = 0xFF0 << 20;
		*shutErrBit12 |= ( ( DWORD )bpb->media & 0x0F ) << 20;
		*shutErrBit12 |= MS_EOC12 << 8;
	}
	else if( FATType == FAT16 )
	{
		shutBit16 = ( WORD* )sector;
		errBit16 = ( WORD* )sector + sizeof( WORD );

		*shutBit16 = 0xFFF0 | bpb->media;
		*errBit16 = MS_EOC16;
	}
	else
	{
		shutBit32 = ( DWORD* )sector;
		errBit32 = ( DWORD* )sector + sizeof( DWORD );

		*shutBit32 = 0x0FFFFFF0 | bpb->media;
		*errBit32 = MS_EOC32;
	}

	return FAT_SUCCESS;
}

// FAT 테이블 영역을 초기화하는 함수
int clear_fat( DISK_OPERATIONS* disk, FAT_BPB* bpb )
{
	UINT32	i, end;
	UINT32	FATSize;
	SECTOR	fatSector;
	
	// sector array의 역할은 sector단위 read, write를 위한 응용프로그램에서의(실제로는 커널) sector크기의 버퍼 
	BYTE	sector[MAX_SECTOR_SIZE];

	// sector부터 sector 크기만큼을 0으로 채움
	ZeroMemory( sector, sizeof( sector ) );
	
	// bpb->reservedSectorCount는 주어진 디스크에서 앞에서부터 0번으로 시작하여
	// 몇번 섹터까지 메타데이터를 위한 섹터로 사용이 되었고,
	// 그래서 지금 몇번째 섹터에 메타데이터가 들어갈 수 있는지를 의미함
	// -> FAT영역이 이곳에 저장됨
	fatSector = bpb->reservedSectorCount;

	// FATSize16 : FAT영역의 섹터 수를 저장한는 부분으로, FAT32에서는 0으로 채워진다.
	// FATSize32 : FAT영역의 섹터 수를 의미하는데 이 수는 FAT#1과 FAT#2의 합산이 아닌 한 개의 FAT영역의 섹터 수를 의미함
	if( bpb->FATSize16 != 0 )
		FATSize = bpb->FATSize16;
	else
		FATSize = bpb->BPB32.FATSize32;

	// numberofFATs : 해당 볼륨에 존재하는 FAT영역의 개수 --> end는 결국 그 FAT파일시스템의 끝 섹터번호 가리킴
	end = fatSector + ( FATSize * bpb->numberOfFATs );

	// 이 부분부터 이 함수의 끝까지 FAT영역을 초기화하는 코드
	/* (FAT 테이블을 위해 사용될 공간은 모두 0으로 초기화되어 사용가능 상태를 나타내야 한다.)
		단, 클러스터 0,1에 대응하는 FAT링크의 경우 FAT버전에 따라 특별한 값을 가지게 되고
		그 부분을 제외한 나머지만 0으로 초기화해야하기 때문에 0으로 초기화시킨 섹터버퍼를 
		fill_reserved_fat으로 넘겨서 처리, 그리고 나머지 FAT영역의 섹터들은 모두 0으로 아래 for문에서 처리한다.*/
	fill_reserved_fat( bpb, sector );
	
	// fatSector번 섹터에 sector배열 내용 씀
	disk->write_sector( disk, fatSector, sector );

	// 섹터를 섹터크기만큼 0으로 채움
	ZeroMemory( sector, sizeof( sector ) );

	// 이 for문 안에서 나머지 FAT영역의 섹터들을 0으로 채움
	for( i = fatSector + 1; i < end; i++ )
		disk->write_sector( disk, i, sector );

	return FAT_SUCCESS;
}

// 루트 디렉터리 생성하는 함수
int create_root( DISK_OPERATIONS* disk, FAT_BPB* bpb )
{
	BYTE	sector[MAX_SECTOR_SIZE]; // sector버퍼
	SECTOR	rootSector = 0;
	FAT_DIR_ENTRY*	entry;

	// sector버퍼 0으로 초기화
	ZeroMemory( sector, MAX_SECTOR_SIZE );
	entry = ( FAT_DIR_ENTRY* )sector;

	// FAT_DIR_ENTRY의 name과 attribute를 채우는 코드
	// name : 파일, 디렉터리의 이름
	// attribute : 해당 디렉터리 엔트리의 용도를 기록하는 항목

	// VOLUME_LABEL 11바이트만큼을 entry의 name에 복사
	memcpy( entry->name, VOLUME_LABEL, 11 );
	// attribute = 0x08
	entry->attribute = ATTR_VOLUME_ID;

	/* Mark as no more directory is in here */
	entry++;
	// 디렉터리의 끝을 나타냄
	entry->name[0] = DIR_ENTRY_NO_MORE;

	// 루트 디렉터리 엔트리를 몇번 섹터에 위치시킬지 정하고, 그 위치의 섹터에 write하는 과정
	// (위치하게 되는곳이 fat의 버전마다 다름)
	if( get_fat_type( bpb ) == FAT32 )
	{
		/* Not implemented yet */
	}
	else
		// bpb->FATSize16은 FAT16버전에서 FAT하나를 위해서 필요한 섹터가 몇개인지를 나타냄
		// 결국 아래코드는 Reserved영역과 FAT영역까지의 섹터수를 합해서 다음 사용가능한 섹터수를 구하는 코드
		rootSector = bpb->reservedSectorCount + ( bpb->numberOfFATs * bpb->FATSize16 );

	// 그 섹터에 sector버퍼의 내용 씀
	disk->write_sector( disk, rootSector, sector );

	return FAT_SUCCESS;
}

// FAT영역 내에서의 cluster번호 offset정보(볼륨에서의 sector, sector내에서의 offset)
int get_fat_sector( FAT_FILESYSTEM* fs, SECTOR cluster, SECTOR* fatSector, DWORD* fatEntryOffset )
{
	DWORD	fatOffset;

	switch( fs->FATType )
	{
	case FAT32:
		fatOffset = cluster * 4;
		break;
	case FAT16:
		fatOffset = cluster * 2;
		break;
	case FAT12:
		fatOffset = cluster + ( cluster / 2 );
		break;
	default:
		WARNING( "Illegal file system type\n" );
		fatOffset = 0;
		break;
	}

	// 몇 번째 sector인지
	*fatSector		= fs->bpb.reservedSectorCount + ( fatOffset / fs->bpb.bytesPerSector );
	
	// sector 내에서의 오프셋?
	*fatEntryOffset	= fatOffset % fs->bpb.bytesPerSector;

	return FAT_SUCCESS;
}

// cluster가 존재하는 fat영역 내의 sector를 읽음
// offset을 수정해서 sector를 읽었을 경우 이를 알리기 위해 1을 리턴???????????
int prepare_fat_sector( FAT_FILESYSTEM* fs, SECTOR cluster, SECTOR* fatSector, DWORD* fatEntryOffset, BYTE* sector )
{
	get_fat_sector( fs, cluster, fatSector, fatEntryOffset );
	fs->disk->read_sector( fs->disk, *fatSector, sector );

	if( fs->FATType == FAT12 && *fatEntryOffset == fs->bpb.bytesPerSector - 1 )
	{
		fs->disk->read_sector( fs->disk, *fatSector + 1, &sector[fs->bpb.bytesPerSector] );
		return 1;
	}

	return 0;
}

/* Read a FAT entry from FAT Table */
// FAT 영역에서 cluster 번호에 해당하는 정보를 읽어옴(엔트리)
DWORD get_fat( FAT_FILESYSTEM* fs, SECTOR cluster )
{
	BYTE	sector[MAX_SECTOR_SIZE * 2];
	SECTOR	fatSector;
	DWORD	fatEntryOffset;

	// cluster가 존재하는 fat영역 내의 sector를 읽음
	// sector에 해당 섹터 데이터가 들어감
	prepare_fat_sector( fs, cluster, &fatSector, &fatEntryOffset, sector );

	// 해당 sector에서 cluster의 정보(entry)를 읽음
	// FAT버전에 따라서 FAT table entry의 크기가 다르기 때문에 하나의 entry를 추출해서 return하는 방식은 모두 다름
	switch( fs->FATType )
	{
	case FAT32:
		return ( *( ( DWORD* )&sector[fatEntryOffset] ) ) & 0xFFFFFFF;
	case FAT16:
		return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) );
	case FAT12:
		if( cluster & 1 )	/* Cluster number is ODD	*/
			return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) >> 4 );
		else				/* Cluster number is EVEN	*/
			return ( DWORD )( *( ( WORD *)&sector[fatEntryOffset] ) & 0xFFF );
	}

	return FAT_ERROR;
}

/* Write a FAT entry to FAT Table */
// FAT 영역에 cluster 정보 추가(첫 cluster에 파일 끝을 나타내는 value setting)
int set_fat( FAT_FILESYSTEM* fs, SECTOR cluster, DWORD value )
{
	BYTE	sector[MAX_SECTOR_SIZE * 2];
	SECTOR	fatSector;
	DWORD	fatEntryOffset;
	int		result;

	// cluster가 존재하는 fat영역 내의 sector를 읽음
	result = prepare_fat_sector( fs, cluster, &fatSector, &fatEntryOffset, sector );

	switch( fs->FATType )
	{
	case FAT32:
		value &= 0x0FFFFFFF;
		*( ( DWORD* )&sector[fatEntryOffset] ) &= 0xF0000000;
		*( ( DWORD* )&sector[fatEntryOffset] ) |= value;
		break;
	case FAT16:
		*( ( WORD* )&sector[fatEntryOffset] ) = ( WORD )value;
		break;
	case FAT12:
		if( cluster & 1 )
		{
			value <<= 4;
			*( ( WORD* )&sector[fatEntryOffset] ) &= 0x000F;
		}
		else
		{
			value &= 0x0FFF;
			*( ( WORD* )&sector[fatEntryOffset] ) &= 0xF000;
		}
		*( ( WORD* )&sector[fatEntryOffset] ) |= ( WORD )value;
		break;
	}

	// fatSector번 섹터에 sector버퍼의 내용 씀
	fs->disk->write_sector( fs->disk, fatSector, sector ); 

	if( result )
		fs->disk->write_sector( fs->disk, fatSector + 1, &sector[fs->bpb.bytesPerSector] );

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Format disk as a specified file system                                     */
/******************************************************************************/

// BPB, FAT, Root directory영역을 모두 초기화한다. 디스크가 정상적으로 사용될 수 있도록 필요한 정보 등록하고 초기화한다.
int fat_format( DISK_OPERATIONS* disk, BYTE FATType )
{
	FAT_BPB bpb; // 부트 파라미터 블록(BIOS parameter block)

	// bpb를 채워주는 함수, 성공하면 FAT_SUCCESS리턴해줌
	if( fill_bpb( &bpb, FATType, disk->numberOfSectors, disk->bytesPerSector ) != FAT_SUCCESS ) // bpb 초기화
		return FAT_ERROR;

	// disk의 0번섹터에 BPB내용 써줌
	disk->write_sector( disk, 0, &bpb );

	// 출력
	PRINTF( "bytes per sector       : %u\n", bpb.bytesPerSector );
	PRINTF( "sectors per cluster    : %u\n", bpb.sectorsPerCluster );
	PRINTF( "number of FATs         : %u\n", bpb.numberOfFATs );
	PRINTF( "root entry count       : %u\n", bpb.rootEntryCount );
	PRINTF( "total sectors          : %u\n", ( bpb.totalSectors ? bpb.totalSectors : bpb.totalSectors32 ) );
	PRINTF( "\n" );

	// FAT 테이블 초기화
	clear_fat( disk, &bpb ); 

	// root 디렉터리 생성 + 초기화
	create_root( disk, &bpb ); 

	return FAT_SUCCESS;
}

int validate_bpb( FAT_BPB* bpb )
{
	int FATType;

	if( !( bpb->jmpBoot[0] == 0xEB && bpb->jmpBoot[2] == 0x90 ) &&
		!( bpb->jmpBoot[0] == 0xE9 ) )
		return FAT_ERROR;

	FATType = get_fat_type( bpb );

	if( FATType < 0 )
		return FAT_ERROR;

	return FAT_SUCCESS;
}

/* when FAT type is FAT12 or FAT16 */
int read_root_sector( FAT_FILESYSTEM* fs, SECTOR sectorNumber, BYTE* sector )
{
	SECTOR	rootSector;

	rootSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->bpb.FATSize16 );

	return fs->disk->read_sector( fs->disk, rootSector + sectorNumber, sector );
}

int write_root_sector( FAT_FILESYSTEM* fs, SECTOR sectorNumber, const BYTE* sector )
{
	SECTOR	rootSector;

	rootSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->bpb.FATSize16 );

	return fs->disk->write_sector( fs->disk, rootSector + sectorNumber, sector );
}

/* Translate logical cluster and sector numbers to a physical sector number */
SECTOR	calc_physical_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber )
{
	SECTOR	firstDataSector;
	SECTOR	firstSectorOfCluster;
	SECTOR	rootDirSectors;

	rootDirSectors = ( ( fs->bpb.rootEntryCount * 32 ) + ( fs->bpb.bytesPerSector - 1 ) ) / fs->bpb.bytesPerSector ;
	firstDataSector = fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * fs->FATSize ) + rootDirSectors;
	firstSectorOfCluster = ( ( clusterNumber - 2 ) * fs->bpb.sectorsPerCluster ) + firstDataSector;

	return firstSectorOfCluster + sectorNumber;
}

int read_data_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber, BYTE* sector )
{
	return fs->disk->read_sector( fs->disk, calc_physical_sector( fs, clusterNumber, sectorNumber ), sector );
}

int write_data_sector( FAT_FILESYSTEM* fs, SECTOR clusterNumber, SECTOR sectorNumber, const BYTE* sector )
{
	return fs->disk->write_sector( fs->disk, calc_physical_sector( fs, clusterNumber, sectorNumber ), sector );
}

/* search free clusters from FAT and add to free cluster list */
int search_free_clusters( FAT_FILESYSTEM* fs )
{
	UINT32	totalSectors, dataSector, rootSector, countOfClusters, FATSize;
	UINT32	i, cluster;

	rootSector = ( ( fs->bpb.rootEntryCount * 32 ) + ( fs->bpb.bytesPerSector - 1 ) ) / fs->bpb.bytesPerSector;

	if( fs->bpb.FATSize16 != 0 )
		FATSize = fs->bpb.FATSize16;
	else
		FATSize = fs->bpb.BPB32.FATSize32;

	if( fs->bpb.totalSectors != 0 )
		totalSectors = fs->bpb.totalSectors;
	else
		totalSectors = fs->bpb.totalSectors32;

	dataSector = totalSectors - ( fs->bpb.reservedSectorCount + ( fs->bpb.numberOfFATs * FATSize ) + rootSector );
	countOfClusters = dataSector / fs->bpb.sectorsPerCluster;

	for( i = 2; i < countOfClusters; i++ )
	{
		cluster = get_fat( fs, i );
		if( cluster == FREE_CLUSTER )
			add_free_cluster( fs, i );
	}

	return FAT_SUCCESS;
}

// root 전달인자에 루트 디렉터리 정보가 저장되는 함수
int fat_read_superblock( FAT_FILESYSTEM* fs, FAT_NODE* root )
{
	INT		result;
	BYTE	sector[MAX_SECTOR_SIZE]; // 섹터버퍼

	// 전달인자 검사
	if( fs == NULL || fs->disk == NULL )
	{
		WARNING( "DISK_OPERATIONS : %p\nFAT_FILESYSTEM : %p\n", fs, fs->disk );
		return FAT_ERROR;
	}

	// disk의 첫번째 sector(BPB가 저장되어있는 sector)를 읽어서 fs->bpb에 저장
	if( fs->disk->read_sector( fs->disk, 0, &fs->bpb ) )
		return FAT_ERROR;
		
	// super block 유효검사 (bpb)
	result = validate_bpb( &fs->bpb );

	if( result )
	{
		WARNING( "BPB validation is failed\n" );
		return FAT_ERROR;
	}

	fs->FATType = get_fat_type( &fs->bpb );

	// FAT타입 유효검사 : FAT12, 16, 32
	if( fs->FATType > FAT32 )
		return FAT_ERROR;

	// root directory sector 읽어서 섹터버퍼에 저장
	if( read_root_sector( fs, 0, sector ) ) 
		return FAT_ERROR;

	// 전달받은 root디렉터리 노드정보 setting
	ZeroMemory( root, sizeof( FAT_NODE ) );
	memcpy( &root->entry, sector, sizeof( FAT_DIR_ENTRY ) );
	root->fs = fs;

	// FAT 파일시스템의 경우 FAT 테이블에서 EOC(end of cluster)를 나타내는 비트열이 모두 다른데
	// 이것이 버전에 맞게 설정되었는지 확인하는 코드
	// 0,1번째 cluster는 나머지 cluster와는 다르게 파일할당에 사용되지 않고 특별한 목적으로 이용됨
	// 따라서 FAT Table에서 해당하는 부분은 EOC로 체크되어있음. 따라서 이 부분을 이용함
	
	// fs영역에서 해당 cluster에 해당하는 정보 읽어옴
	fs->EOCMark = get_fat( fs, 1 ); 
	
	// 버전에 따라 확인
	if( fs->FATType == 2 )
	{
		if( fs->EOCMark & SHUT_BIT_MASK32 ) 
			WARNING( "disk drive did not dismount correctly\n" );
		if( fs->EOCMark & ERR_BIT_MASK32 )
			WARNING( "disk drive has error\n" );
	}
	else
	{
		if( fs->FATType == 1)
		{
			if( fs->EOCMark & SHUT_BIT_MASK16 )
				PRINTF( "disk drive did not dismounted\n" );
			if( fs->EOCMark & ERR_BIT_MASK16 )
				PRINTF( "disk drive has error\n" );
		}
	}

	/* FAT버전에 따라서 FATsize를 저장하기 위한 멤버가 다름
	   FAT32인 경우 bpb.FATSize16을 0으로 하고 FATSize32에 값을 기록함
	   FAT16, 12의 경우 bpb.FATSize16만 사용한다
	   bpb에 있는 데이터를 fs구조체 멤버(fs->FATSize)에 복사하고 나면 
	   FAT버전에 관계없이 fs->FATSize로 사용할 수 있음*/
	if( fs->bpb.FATSize16 != 0 )
		fs->FATSize = fs->bpb.FATSize16;
	else
		fs->FATSize = fs->bpb.BPB32.FATSize32;

	// fs구조체가 가리키는 freeClusterList를 0으로 초기화
	init_cluster_list( &fs->freeClusterList );

	// free cluster를 찾고 freeClusterList에 추가
	search_free_clusters( fs );

	// 전달받은 root의 entry의 name에 0x20(공백) 11바이트 채움
	memset( root->entry.name, 0x20, 11 );
	return FAT_SUCCESS;
}

/******************************************************************************/
/* On unmount file system                                                     */
/******************************************************************************/
void fat_umount( FAT_FILESYSTEM* fs )
{
	// free cluster_list 해제
	release_cluster_list( &fs->freeClusterList );
}

// sector단위에 저장되어있는 dir_entry들을 읽음
int read_dir_from_sector( FAT_FILESYSTEM* fs, FAT_ENTRY_LOCATION* location, BYTE* sector, FAT_NODE_ADD adder, void* list )
{
	UINT		i, entriesPerSector;
	FAT_DIR_ENTRY*	dir;
	FAT_NODE	node;

	// sector 당 entry개수
	entriesPerSector = fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );

	// sector를 dir_entry 배열로 관리
	dir = ( FAT_DIR_ENTRY* )sector;

	// sector의 모든 directory entry 순회
	for( i = 0; i < entriesPerSector; i++ )
	{
		if( dir->name[0] == DIR_ENTRY_FREE )
			;

		// 더이상 엔트리 없으면 break
		else if( dir->name[0] == DIR_ENTRY_NO_MORE )
			break;

		else if( !( dir->attribute & ATTR_VOLUME_ID ) )
		{
			node.fs = fs;
			node.location = *location; // cluster, sector정보
			node.location.number = i; // number에는 지금sector에서 현재 엔트리 offset
			node.entry = *dir; // 해당 순서의 디렉터리 엔트리
			adder( list, &node ); // fat_node를 shell_entry로해서 list에 추가
		}

		dir++; // 디렉터리 엔트리 증가
	}

	// 다 돌았으면 0리턴
	// 다 못돌았으면 (DIR_ENTRY_NO_MORE인 경우) -1리턴
	return ( i == entriesPerSector ? 0 : -1 );
}

DWORD get_MS_EOC( BYTE FATType )
{
	switch( FATType )
	{
	case FAT12:
		return MS_EOC12;
	case FAT16:
		return MS_EOC16;
	case FAT32:
		return MS_EOC32;
	}

	WARNING( "Incorrect FATType(%u)\n", FATType );
	return -1;
}

int is_EOC( BYTE FATType, SECTOR clusterNumber )
{
	switch( FATType )
	{
	case FAT12:
		if( EOC12 <= ( clusterNumber & 0xFFF ) )
			return -1;

		break;
	case FAT16:
		if( EOC16 <= ( clusterNumber & 0xFFFF ) )
			return -1;

		break;
	case FAT32:
		if( EOC32 <= ( clusterNumber & 0x0FFFFFFF ) )
			return -1;
		break;
	default:
		WARNING( "Incorrect FATType(%u)\n", FATType );
	}

	return 0;
}

/******************************************************************************/
/* Read all entries in the current directory                                  */
/******************************************************************************/
// 디렉터리 안에 있는 모든 entry 읽음
int fat_read_dir( FAT_NODE* dir, FAT_NODE_ADD adder, void* list )
{
	BYTE	sector[MAX_SECTOR_SIZE]; // 섹터버퍼
	SECTOR	i, j, rootEntryCount;
	FAT_ENTRY_LOCATION location;

	// 전달받은 fat_node의 entry가 루트 디렉터리일때
	if( IS_POINT_ROOT_ENTRY( dir->entry ) && ( dir->fs->FATType == FAT12 || dir->fs->FATType == FAT16 ) )
	{
		// FAT32가 아닌경우 rootEntryCount를 bpb에 있는것 그대로(루트 디렉터리가 수용하는 엔트리 개수)
		if( dir->fs->FATType != FAT32 )
			rootEntryCount = dir->fs->bpb.rootEntryCount;

		// 루트 디렉터리의 모든 엔트리 순회
		for( i = 0; i < rootEntryCount; i++ )
		{
			// i번째 섹터를 sector버퍼에 읽어옴
			read_root_sector( dir->fs, i, sector );
			location.cluster = 0; // 클러스터 위치는 0으로 고정
			location.sector = i; // 섹터만 변경
			location.number = 0;
			
			// 한 섹터에서 dir_entry를 읽어서 리스트에 넣었줌
			// 다 돌았으면(sector에 들어갈 수 있는 dir_entry 개수만큼) 0리턴 -> 다음 sector도 봐야함
			// 다 안돌았으면(dir_entry가 더이상 없으면) -1리턴 -> break;, for문 탈출
			if( read_dir_from_sector( dir->fs, &location, sector, adder, list ) )
				break;
		}
	}
	// 루트 디렉터리가 아닐 때(일반)
	else
	{
		// dir_entry가 시작되는 cluster 위치
		i = GET_FIRST_CLUSTER( dir->entry );
		do
		{
			// cluster 하나의 섹터를 모두 순회
			for( j = 0; j < dir->fs->bpb.sectorsPerCluster; j++ )
			{
				// j번째 섹터를 sector버퍼에 읽어옴
				read_data_sector( dir->fs, i, j, sector );
				location.cluster = i;
				location.sector = j;
				location.number = 0;

				// 한 섹터에서 dir_entry를 읽어서 리스트에 넣었줌
				// 다 돌았으면(sector에 들어갈 수 있는 dir_entry 개수만큼) 0리턴 -> 다음 sector도 봐야함
				// 다 안돌았으면(dir_entry가 더이상 없으면) -1리턴 -> break;, for문 탈출
				if( read_dir_from_sector( dir->fs, &location, sector, adder, list ) )
					break;
			}
			// 다음 cluster로 이동
			i = get_fat( dir->fs, i ); 
		} while( !is_EOC( dir->fs->FATType, i ) && i != 0 );
	}

	return FAT_SUCCESS;
}

int add_free_cluster( FAT_FILESYSTEM* fs, SECTOR cluster )
{
	return push_cluster( &fs->freeClusterList, cluster );
}

SECTOR alloc_free_cluster( FAT_FILESYSTEM* fs )
{
	SECTOR	cluster;

	if( pop_cluster( &fs->freeClusterList, &cluster ) == FAT_ERROR )
		return 0;

	return cluster;
}

SECTOR span_cluster_chain( FAT_FILESYSTEM* fs, SECTOR clusterNumber )
{
	UINT32	nextCluster;

	nextCluster = alloc_free_cluster( fs );

	if( nextCluster )
	{
		set_fat( fs, clusterNumber, nextCluster );
		set_fat( fs, nextCluster, get_MS_EOC( fs->FATType ) );
	}

	return nextCluster;
}

// begin에서 last까지 formattedName을 가진 entry를 sector에서 검색해서 그 인덱스를 number에 저장하는 함수
int find_entry_at_sector( const BYTE* sector, const BYTE* formattedName, UINT32 begin, UINT32 last, UINT32* number )
{
	UINT32	i;
	const FAT_DIR_ENTRY*	entry = ( FAT_DIR_ENTRY* )sector;

	for( i = begin; i <= last; i++ )
	{
		if( formattedName == NULL )
		{
			if( entry[i].name[0] != DIR_ENTRY_FREE && entry[i].name[0] != DIR_ENTRY_NO_MORE )
			{
				// formattedName이 null인 경우에는 현재 사용중인 첫번째 entry를 읽어오면 됨
				*number = i;
				return FAT_SUCCESS;
			}
		}
		else // formattedName이 어떤 값을 가지고 있는 경우
		{
			if( ( formattedName[0] == DIR_ENTRY_FREE || formattedName[0] == DIR_ENTRY_NO_MORE ) &&
				( formattedName[0] == entry[i].name[0] ) )
			{
				*number = i;
				return FAT_SUCCESS;
			}

			// 두 문자열이 같으면
			if( memcmp( entry[i].name, formattedName, MAX_ENTRY_NAME_LENGTH ) == 0 )
			{
				*number = i;
				return FAT_SUCCESS;
			}
		}

		// 더이상 찾을 디렉터리가 없음
		if( entry[i].name[0] == DIR_ENTRY_NO_MORE )
		{
			*number = i;
			return -2;
		}
	}

	// i = last+1
	*number = i;
	return -1;
}

// 호출문장 : find_entry_on_root(fs, first, entryName, ret);
int find_entry_on_root( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* formattedName, FAT_NODE* ret )
{
	BYTE	sector[MAX_SECTOR_SIZE]; // sector버퍼
	UINT32	i, number;
	UINT32	lastSector;
	UINT32	entriesPerSector, lastEntry;
	INT32	begin = first->number;
	INT32	result;
	FAT_DIR_ENTRY*	entry;

	entriesPerSector	= fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );
	lastEntry			= entriesPerSector - 1;
	lastSector			= fs->bpb.rootEntryCount / entriesPerSector;

	// root sector 영역에서 sector 단위로 변위를 주어 모든 sector 검색이 가능하게 한다
	for( i = first->sector; i <= lastSector; i++ )
	{
		// root sector중에서 i번째 sector를 sector버퍼에 write
		read_root_sector( fs, i, sector );

		// 읽어온 sector의 첫번째 FAT_DIR_ENTRY를 entry에 연결
		entry = ( FAT_DIR_ENTRY* )sector;

		/* 아래 함수는 하나의 sector에서 찾고자 하는 formattedName을 가진 entry를 검사해서
		   있으면 하나의 sector를 FAT_DIR_ENTRY의 배열로 보았을 때 찾은 entry의
		   sector에서의 인덱스를 number에 넣어준다*/
		result = find_entry_at_sector( sector, formattedName, begin, lastEntry, &number );
		begin = 0;

		// 못찾은 경우
		if( result == -1 )
			continue;
		else
		{
			// 찾을 directory entry가 더이상 없는경우(DIR_ENTRY_NO_MORE)
			if( result == -2 )
				return FAT_ERROR;
			else
			{
				// FAT_NODE* ret에서 가리키는 FAT_NODE를 찾은 entry정보로 초기화하는 코드

				// formattedName으로 검색하여 찾은 FAT_DIR_ENTRY를 ret->entry에 write
				memcpy( &ret->entry, &entry[number], sizeof( FAT_DIR_ENTRY ) );
				// cluster위치는 고정
				ret->location.cluster	= 0;
				// sector의 실제 위치
				ret->location.sector	= i;
				// sector 내부에서의 실제 인덱스
				ret->location.number	= number;

				// 파일시스템 연결
				ret->fs = fs;
			}

			return FAT_SUCCESS; //찾았으면(있음)
		}
	}

	return FAT_ERROR; // 못찾았으면
}


int find_entry_on_data( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* formattedName, FAT_NODE* ret )
{
	BYTE	sector[MAX_SECTOR_SIZE]; // 섹터버퍼
	UINT32	i, number;
	UINT32	entriesPerSector, lastEntry;
	UINT32	currentCluster;
	INT32	begin = first->number;
	INT32	result;
	FAT_DIR_ENTRY*	entry;

	currentCluster		= first->cluster;
	entriesPerSector	= fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY );
	lastEntry			= entriesPerSector - 1;

	while( -1 )
	{
		UINT32	nextCluster;
		// first->sector는 찾고자 하는 directory entry의 parent directory entry가 존재하는 sector임
		// currentCluster 에 존재하는 모든 sector를 검사
		for( i = first->sector; i < fs->bpb.sectorsPerCluster; i++ )
		{
			// currentCluster로 cluster에 접근하고 i로 sector에 접근해서 sector버퍼에 저장
			read_data_sector( fs, currentCluster, i, sector );
			entry = ( FAT_DIR_ENTRY* )sector;

			// 섹터 내부검사
			result = find_entry_at_sector( sector, formattedName, begin, lastEntry, &number );
			begin = 0;

			// 못찾은 경우
			if( result == -1 )
				continue;
			else 
			{
				if( result == -2 ) // 찾을 directory entry가 더이상 없는경우(DIR_ENTRY_NO_MORE)
					return FAT_ERROR;
				else
				{
					// FAT_NODE* ret에서 가리키는 FAT_NODE를 찾은 entry정보로 초기화하는 코드

					memcpy( &ret->entry, &entry[number], sizeof( FAT_DIR_ENTRY ) );

					ret->location.cluster	= currentCluster;
					ret->location.sector	= i;
					ret->location.number	= number;

					ret->fs = fs;
				}

				return FAT_SUCCESS;
			}
		}

		// currentCluster에 대응하는 FAT테이블 엔트리를 얻어오고
		nextCluster = get_fat( fs, currentCluster );

		/* 이 if~else if에서 걸리지 않았으면 nextCluster는 정상적으로 cluster chain에서 다음 부분을 가리키고 있는 것이고,
		   cluster chain을 이루고 있다는 것은 현재 파일이 여러개의 cluster를 사용하고 있다는 것이기 때문에 
		   다음 클러스터로 옮겨서 검색작업 반복*/
		if( is_EOC( fs->FATType, nextCluster ) )
			break;
		else if( nextCluster == 0)
			break;

		currentCluster = nextCluster;
	}

	return FAT_ERROR;
}

/* entryName = NULL -> Find any valid entry */
// 호출 문장 : lookup_entry( parent->fs, &first, name, retEntry )
/* 찾고자 하는 entryName이 존재하는 경우 FAT_SUCCESS를 반환하고
   찾은 ENTRY로 FAT_NODE* ret이 가리키는 부분을 초기화시켜줌. 없으면 FAT_ERROR반환*/
int lookup_entry( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* first, const BYTE* entryName, FAT_NODE* ret )
{
	/* root sector 생성 시 cluster에 대한 정보는 모두 0으로 초기화되었다
		(sector 버퍼를 최초 0으로 다 초기화하고 특정한 멤버 몇개만 값을 넣고 이 섹터버퍼로 초기화했었음)
		따라서 아래 if문은 현재 위치가 root인지를 묻는것*/
	if( first->cluster == 0 && ( fs->FATType == FAT12 || fs->FATType == FAT16 ) )
		return find_entry_on_root( fs, first, entryName, ret ); // 루트에서 엔트리 찾기
	else
		return find_entry_on_data( fs, first, entryName, ret ); 
}

// FAT_ENTRY_LOCATION* location의 cluster, sector, number로 주어진 부분에 value로 값을 write하는 함수
int set_entry( FAT_FILESYSTEM* fs, const FAT_ENTRY_LOCATION* location, const FAT_DIR_ENTRY* value )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	FAT_DIR_ENTRY*	entry;

	// location이 root디렉터리 영역인 경우
	if( location->cluster == 0 && ( fs->FATType == FAT12 || fs->FATType == FAT16 ) )
	{
		// 해당 섹터에 대한 내용을 sector버퍼에 써줌
		read_root_sector( fs, location->sector, sector );

		// 그 섹터의 해당 위치(number번째) 엔트리에 value 연결
		entry = ( FAT_DIR_ENTRY* )sector;
		entry[location->number] = *value;

		// sector버퍼의 내용을 디스크의 해당 섹터에 써줌
		write_root_sector( fs, location->sector, sector );
	}
	// location이 root디렉터리가 아닌경우
	else
	{
		// 위의 read_root_sector와 다른점 : 0이 아닌cluster로 접근함
		read_data_sector( fs, location->cluster, location->sector, sector );

		entry = ( FAT_DIR_ENTRY* )sector;
		entry[location->number] = *value;

		write_data_sector( fs, location->cluster, location->sector, sector );
	}

	return FAT_ERROR;
}

// 부모 디렉터리에 새로운 dir_entry 추가
int insert_entry( const FAT_NODE* parent, FAT_NODE* newEntry, BYTE overwrite )
{
	FAT_ENTRY_LOCATION	begin;
	FAT_NODE			entryNoMore;
	BYTE				entryName[2] = { 0, };

	// parent directory의 시작 cluster
	begin.cluster = GET_FIRST_CLUSTER( parent->entry );
	begin.sector = 0;
	begin.number = 0;

	// root디렉터리가 아니고 overwrite을 요구한 경우
	if( !( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) ) && overwrite )
	{
		// 첫 dir_entry에 덮어쓰기, 그 다음 엔트리에서 End of entries 설정
		begin.number = 0;

		set_entry( parent->fs, &begin, &newEntry->entry );
		newEntry->location = begin;

		/* End of entries */
		// 다음 dir_entry위치에 dir_entry_no_more로 setting : dir_entry array의 끝 지정
		begin.number = 1;
		ZeroMemory( &entryNoMore, sizeof( FAT_NODE ) );
		entryNoMore.entry.name[0] = DIR_ENTRY_NO_MORE;
		set_entry( parent->fs, &begin, &entryNoMore.entry );

		return FAT_SUCCESS;
	}

	/* find empty(unused) entry */
	// dir_entry_free로 검색 : dir_entry가 추가될 위치를 검색
	entryName[0] = DIR_ENTRY_FREE; 

	// free_dir_entry를 찾은 경우
	if( lookup_entry( parent->fs, &begin, entryName, &entryNoMore ) == FAT_SUCCESS )
	{	
		// entryNoMore.location의 cluster, sector, number로 주어진 부분에 newEntry->entry로 값을 write하는 함수
		set_entry( parent->fs, &entryNoMore.location, &newEntry->entry );
		newEntry->location = entryNoMore.location;
	}
	else // free_dir_entry를 찾지 못한 경우
	{
		// root디렉터리인 경우
		if( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) )
		{
			// dir_entry개수를 구한 후 root디렉터리의 최대치보다 크다면 에러
			UINT32	rootEntryCount = newEntry->location.sector * ( parent->fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY ) ) + newEntry->location.number;
			if( rootEntryCount >= parent->fs->bpb.rootEntryCount )
			{
				WARNING( "Cannot insert entry into the root entry\n" );
				return FAT_ERROR;
			}
		}

		/* add new entry to end */
		// dir_entry_no_more로 다시 검색 : dir_entry_array의 끝을 나타내는 위치
		entryName[0] = DIR_ENTRY_NO_MORE;
		if( lookup_entry( parent->fs, &begin, entryName, &entryNoMore ) == FAT_ERROR )
			return FAT_ERROR; // 다시 실패하면 오류

		// dir_entry_no_more 위치에 새로운 entry를 추가
		set_entry( parent->fs, &entryNoMore.location, &newEntry->entry );
		newEntry->location = entryNoMore.location;
		
		// 새로운 dir_entry_no_more을 setting하기 위한 위치를 구함
		entryNoMore.location.number++;

		// dir_entry 위치가 섹터를 초과하면
		if( entryNoMore.location.number == ( parent->fs->bpb.bytesPerSector / sizeof( FAT_DIR_ENTRY ) ) )
		{
			// 다음 섹터로 세팅
			entryNoMore.location.sector++;
			entryNoMore.location.number = 0;

			// dir_entry 위치가 cluster를 초과하면
			if( entryNoMore.location.sector == parent->fs->bpb.sectorsPerCluster )
			{	
				// 루트 디렉터리가 아닌 경우
				if( !( IS_POINT_ROOT_ENTRY( parent->entry ) && ( parent->fs->FATType == FAT12 || parent->fs->FATType == FAT16 ) ) )
				{
					// 새로운 cluster를 할당 받음
					entryNoMore.location.cluster = span_cluster_chain( parent->fs, entryNoMore.location.cluster );

					if( entryNoMore.location.cluster == 0 )
					{
						NO_MORE_CLUSER();
						return FAT_ERROR;
					}
					entryNoMore.location.sector = 0;
				}
			}
		}

		/* End of entries */
		// 새로운 dir_entry_no_more 지정
		set_entry( parent->fs, &entryNoMore.location, &entryNoMore.entry );
	}

	return FAT_SUCCESS;
}

void upper_string( char* str, int length )
{
	while( *str && length-- > 0 )
	{
		*str = toupper( *str );
		str++;
	}
}

// name을 이 파일 시스템 형식에 맞게 고치는 함수
int format_name( FAT_FILESYSTEM* fs, char* name )
{
	UINT32	i, length;
	UINT32	extender = 0, nameLength = 0;
	UINT32	extenderCurrent = 8;
	BYTE	regularName[MAX_ENTRY_NAME_LENGTH];

	// regularName 주소부터 sizeof(regularName)바이트를 0x20으로 채움
	memset( regularName, 0x20, sizeof( regularName ) );
	length = strlen( name );

	// hidden directory일 경우
	// name과 ..를 2바이트만큼 비교해서 같으면 0
	if( strncmp( name, "..", 2 ) == 0 )
	{
		// 같으면 name에 "..~~"부터 11바이트만큼을 복사해넣음(이름을 항상 11자 유지)
		memcpy( name, "..         ", 11 );
		return FAT_SUCCESS;
	}
	// ..이 아닐경우 .과 비교
	else if( strncmp( name, ".", 1 ) == 0 )
	{
		// .과 같으면 name에 ".~~"부터 11바이트만큼 복사해넣음
		memcpy( name, ".          ", 11 );
		return FAT_SUCCESS;
	}

	// hidden directory는 아닌경우
	if( fs->FATType == FAT32 )
	{
	}
	// FATType이 FAT32가 아니면
	else
	{
		// name의 모든 문자를 대문자로 변경
		upper_string( name, MAX_ENTRY_NAME_LENGTH );

		// name문자열의 길이만큼 반복
		for( i = 0; i < length; i++ )
		{
			// name 문자열 중에 '.', 숫자, 알파벳을 제외한 문자가 있으면 에러
			if( name[i] != '.' && !isdigit( name[i] ) && !isalpha( name[i] ) )
				return FAT_ERROR;

			// extender은 위에서 0으로 초기화했었음
			// '.'은 파일명 문자열에서 두개 이상일 수 없음(하나까지만 가능)
			if( name[i] == '.' )
			{
				if( extender )
					return FAT_ERROR;		/* dot character is allowed only once */
				extender = 1;
			}

			// 파일명과 확장자를 구분하는 코드
			else if( isdigit( name[i] ) || isalpha( name[i] ) )
			{
				// .이 하나 나와서 extender가 1이 된 경우
				/* ex) "abc.txt"에서 txt부분. 위에서 UINT32 extenderCurrent = 8;이므로
				   확장자는 언제나 8번 위치부터 시작. 이름의 끝부터 8번 위치 전까지는 
				   초기 memset(regularname, 0x20, sizeof(regularname))으로 모두 공백으로 차있음*/
				if( extender )
					regularName[extenderCurrent++] = name[i];
				
				// 파일명 부분, .을 만나기 전
				else
					regularName[nameLength++] = name[i];
			}
			else
				return FAT_ERROR;			/* non-ascii name is not allowed */
		}

		if( nameLength > 8 || nameLength == 0 || extenderCurrent > 11 )
			return FAT_ERROR;
	}

	// regularName을 name으로 복사
	memcpy( name, regularName, sizeof( regularName ) );
	return FAT_SUCCESS;
}

/******************************************************************************/
/* Create new directory                                                       */
/******************************************************************************/
int fat_mkdir( const FAT_NODE* parent, const char* entryName, FAT_NODE* ret )
{
	FAT_NODE		dotNode, dotdotNode;
	DWORD			firstCluster;
	BYTE			name[MAX_NAME_LENGTH];
	int				result;

	// entryName을 name에 copy
	strncpy( name, entryName, MAX_NAME_LENGTH );

	// name 형식 맞춰줌
	if( format_name( parent->fs, name ) )
		return FAT_ERROR;

	/* newEntry */
	// 새로 생성할 디렉터리 0으로 초기화
	ZeroMemory( ret, sizeof( FAT_NODE ) );

	// 그 디렉터리의 entry.name에 name 넣음
	memcpy( ret->entry.name, name, MAX_ENTRY_NAME_LENGTH );
	
	// 속성은 directory로
	ret->entry.attribute = ATTR_DIRECTORY;
	
	// 첫 cluster 할당해줌
	firstCluster = alloc_free_cluster( parent->fs );

	// 에러 처리
	if( firstCluster == 0 )
	{
		NO_MORE_CLUSER();
		return FAT_ERROR;
	}

	// FAT 영역에 cluster 정보 추가
	set_fat( parent->fs, firstCluster, get_MS_EOC( parent->fs->FATType ) );

	// dir_entry에 할당받은 첫 cluster setting
	SET_FIRST_CLUSTER( ret->entry, firstCluster );

	// 부모 디렉터리에 새로운 dir_entry 추가
	result = insert_entry( parent, ret, 0 );
	if( result )
		return FAT_ERROR;

	ret->fs = parent->fs;

	/* dotEntry "." */
	ZeroMemory( &dotNode, sizeof( FAT_NODE ) );
	memset( dotNode.entry.name, 0x20, 11 );
	dotNode.entry.name[0] = '.';
	dotNode.entry.attribute = ATTR_DIRECTORY;
	
	// 현재 디렉터리가 시작되는 cluster로 setting
	SET_FIRST_CLUSTER( dotNode.entry, firstCluster ); 
	insert_entry( ret, &dotNode, DIR_ENTRY_OVERWRITE ); // overwrite

	/* dotdotEntry ".." */
	ZeroMemory( &dotdotNode, sizeof( FAT_NODE ) );
	memset( dotdotNode.entry.name, 0x20, 11 );
	dotdotNode.entry.name[0] = '.';
	dotdotNode.entry.name[1] = '.';
	dotdotNode.entry.attribute = ATTR_DIRECTORY;

	// 부모 디렉터리가 시작되는 cluster로 setting
	SET_FIRST_CLUSTER( dotdotNode.entry, GET_FIRST_CLUSTER( parent->entry ) ); 
	insert_entry( ret, &dotdotNode, 0 ); // overwrite X

	return FAT_SUCCESS;
}

int free_cluster_chain( FAT_FILESYSTEM* fs, DWORD firstCluster )
{
	DWORD	currentCluster = firstCluster;
	DWORD	nextCluster;

	while( !is_EOC( fs->FATType, currentCluster ) && currentCluster != FREE_CLUSTER )
	{
		nextCluster = get_fat( fs, currentCluster );
		set_fat( fs, currentCluster, FREE_CLUSTER );
		add_free_cluster( fs, currentCluster );
		currentCluster = nextCluster;
	}

	return FAT_SUCCESS;
}

int has_sub_entries( FAT_FILESYSTEM* fs, const FAT_DIR_ENTRY* entry )
{
	FAT_ENTRY_LOCATION	begin;
	FAT_NODE			subEntry;

	begin = get_entry_location( entry );
	begin.number = 2;		/* Ignore the '.' and '..' entries */

	if( !lookup_entry( fs, &begin, NULL, &subEntry ) )
		return FAT_ERROR;

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Remove directory                                                           */
/******************************************************************************/
int fat_rmdir( FAT_NODE* dir )
{
	if( has_sub_entries( dir->fs, &dir->entry ) )
		return FAT_ERROR;

	if( !( dir->entry.attribute & ATTR_DIRECTORY ) )		/* Is directory? */
		return FAT_ERROR;

	dir->entry.name[0] = DIR_ENTRY_FREE;
	set_entry( dir->fs, &dir->location, &dir->entry );
	free_cluster_chain( dir->fs, GET_FIRST_CLUSTER( dir->entry ) );

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Lookup entry(file or directory)                                            */
/******************************************************************************/
int fat_lookup( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry )
{
	FAT_ENTRY_LOCATION	begin;
	BYTE	formattedName[MAX_NAME_LENGTH] = { 0, };

	// cluster의 위치를 parent의 첫 cluster로 set
	begin.cluster = GET_FIRST_CLUSTER( parent->entry );
	begin.sector = 0;
	begin.number = 0;

	// 전달받은 entryName을 formattedName에 복사
	strncpy( formattedName, entryName, MAX_NAME_LENGTH );

	// 이름 형식 
	if( format_name( parent->fs, formattedName ) )
		return FAT_ERROR;

	// parent entry가 부모디렉터리면
	if( IS_POINT_ROOT_ENTRY( parent->entry ) )
		begin.cluster = 0; // cluster는 0

	/* lookup_entry : 찾고자 하는 entryName이 존재하는 경우 FAT_SUCCESS를 반환하고
   찾은 ENTRY로 FAT_NODE* ret이 가리키는 부분을 초기화시켜줌. 없으면 FAT_ERROR반환*/
	return lookup_entry( parent->fs, &begin, formattedName, retEntry );
}

/******************************************************************************/
/* Create new file                                                            */
/******************************************************************************/
int fat_create( FAT_NODE* parent, const char* entryName, FAT_NODE* retEntry )
{
	FAT_ENTRY_LOCATION	first;
	BYTE				name[MAX_NAME_LENGTH] = { 0, };
	int					result;

	// entryName에 있는 문자열을 name에 MAX_NAME_LENGTH만큼 복사함
	strncpy( name, entryName, MAX_NAME_LENGTH );


	// name 지정되어 옴
	if( format_name( parent->fs, name ) )
		return FAT_ERROR;

	/* newEntry */
	// newEntry를 0으로 초기화 후 name 등록
	ZeroMemory( retEntry, sizeof( FAT_NODE ) );
	memcpy( retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH );

	// 검색할 디렉터리의 location정보 setting
	first.cluster = parent->entry.firstClusterLO;
	first.sector = 0;
	first.number = 0;

	// entryName을 가지는 file이 parent 디렉터리에 있는지 확인, 있으면 에러
	if( lookup_entry( parent->fs, &first, name, retEntry ) == FAT_SUCCESS )
		return FAT_ERROR;

	retEntry->fs = parent->fs;
	result = insert_entry( parent, retEntry, 0 );
	if( result )
		return FAT_ERROR;

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Read file                                                                  */
/******************************************************************************/
int fat_read( FAT_NODE* file, unsigned long offset, unsigned long length, char* buffer )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	DWORD	currentOffset, currentCluster, clusterSeq = 0;
	DWORD	clusterNumber, sectorNumber, sectorOffset;
	DWORD	readEnd;
	DWORD	clusterSize, clusterOffset = 0;

	currentCluster = GET_FIRST_CLUSTER( file->entry );
	readEnd = MIN( offset + length, file->entry.fileSize );

	currentOffset = offset;

	clusterSize = ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );
	clusterOffset = clusterSize;
	while( offset > clusterOffset )
	{
		currentCluster = get_fat( file->fs, currentCluster );
		clusterOffset += clusterSize;
		clusterSeq++;
	}

	while( currentOffset < readEnd )
	{
		DWORD	copyLength;

		clusterNumber	= currentOffset / ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );
		if( clusterSeq != clusterNumber )
		{
			clusterSeq++;
			currentCluster = get_fat( file->fs, currentCluster );
		}
		sectorNumber	= ( currentOffset / ( file->fs->bpb.bytesPerSector ) ) % file->fs->bpb.sectorsPerCluster;
		sectorOffset	= currentOffset % file->fs->bpb.bytesPerSector;

		if( read_data_sector( file->fs, currentCluster, sectorNumber, sector ) )
			break;

		copyLength = MIN( file->fs->bpb.bytesPerSector - sectorOffset, readEnd - currentOffset );

		memcpy( buffer,
				&sector[sectorOffset],
				copyLength );

		buffer += copyLength;
		currentOffset += copyLength;
	}

	return currentOffset - offset;
}

/******************************************************************************/
/* Write file                                                                 */
/******************************************************************************/
int fat_write( FAT_NODE* file, unsigned long offset, unsigned long length, const char* buffer )
{
	BYTE	sector[MAX_SECTOR_SIZE];
	DWORD	currentOffset, currentCluster, clusterSeq = 0;
	DWORD	clusterNumber, sectorNumber, sectorOffset;
	DWORD	readEnd;
	DWORD	clusterSize;

	currentCluster = GET_FIRST_CLUSTER( file->entry );
	readEnd = offset + length;

	currentOffset = offset;

	clusterSize = ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );
	while( offset > clusterSize )
	{
		currentCluster = get_fat( file->fs, currentCluster );
		clusterSize += clusterSize;
		clusterSeq++;
	}

	while( currentOffset < readEnd )
	{
		DWORD	copyLength;

		clusterNumber	= currentOffset / ( file->fs->bpb.bytesPerSector * file->fs->bpb.sectorsPerCluster );

		if( currentCluster == 0 )
		{
			currentCluster = alloc_free_cluster( file->fs );
			if( currentCluster == 0 )
			{
				NO_MORE_CLUSER();
				return FAT_ERROR;
			}

			SET_FIRST_CLUSTER( file->entry, currentCluster );
			set_fat( file->fs, currentCluster, get_MS_EOC( file->fs->FATType ) );
		}

		if( clusterSeq != clusterNumber )
		{
			DWORD nextCluster;
			clusterSeq++;

			nextCluster = get_fat( file->fs, currentCluster );
			if( is_EOC( file->fs->FATType, nextCluster ) )
			{
				nextCluster = span_cluster_chain( file->fs, currentCluster );

				if( nextCluster == 0 )
				{
					NO_MORE_CLUSER();
					break;
				}
			}
			currentCluster = nextCluster;
		}
		sectorNumber	= ( currentOffset / ( file->fs->bpb.bytesPerSector ) ) % file->fs->bpb.sectorsPerCluster;
		sectorOffset	= currentOffset % file->fs->bpb.bytesPerSector;

		copyLength = MIN( file->fs->bpb.bytesPerSector - sectorOffset, readEnd - currentOffset );

		if( copyLength != file->fs->bpb.bytesPerSector )
		{
			if( read_data_sector( file->fs, currentCluster, sectorNumber, sector ) )
				break;
		}

		memcpy( &sector[sectorOffset],
				buffer,
				copyLength );

		if( write_data_sector( file->fs, currentCluster, sectorNumber, sector ) )
			break;

		buffer += copyLength;
		currentOffset += copyLength;
	}

	file->entry.fileSize = MAX( currentOffset, file->entry.fileSize );
	set_entry( file->fs, &file->location, &file->entry );

	return currentOffset - offset;
}

/******************************************************************************/
/* Remove file                                                                */
/******************************************************************************/
int fat_remove( FAT_NODE* file )
{
	if( file->entry.attribute & ATTR_DIRECTORY )		/* Is directory? */
		return FAT_ERROR;

	file->entry.name[0] = DIR_ENTRY_FREE;
	set_entry( file->fs, &file->location, &file->entry );
	free_cluster_chain( file->fs, GET_FIRST_CLUSTER( file->entry ) );

	return FAT_SUCCESS;
}

/******************************************************************************/
/* Disk free spaces                                                           */
/******************************************************************************/
int fat_df( FAT_FILESYSTEM* fs, UINT32* totalSectors, UINT32* usedSectors )
{
	if( fs->bpb.totalSectors != 0 )
		*totalSectors = fs->bpb.totalSectors;
	else
		*totalSectors = fs->bpb.totalSectors32;

	*usedSectors = *totalSectors - ( fs->freeClusterList.count * fs->bpb.sectorsPerCluster );

	return FAT_SUCCESS;
}

