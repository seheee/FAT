/******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : entrylist.c                                                      */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Shell directory entry list                                       */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#include "common.h"
#include "shell.h"

#ifndef NULL
#define NULL	( ( void* )0 )
#endif

// SHELL_ENTRY_LIST 초기화 함수
int init_entry_list( SHELL_ENTRY_LIST* list )
{
	// list를 0으로 초기화
	memset( list, 0, sizeof( SHELL_ENTRY_LIST ) );
	return 0;
}

int add_entry_list( SHELL_ENTRY_LIST* list, SHELL_ENTRY* entry )
{
	SHELL_ENTRY_LIST_ITEM*	newItem;

	newItem = ( SHELL_ENTRY_LIST_ITEM* )malloc( sizeof( SHELL_ENTRY_LIST_ITEM ) );
	newItem->entry	= *entry;
	newItem->next	= NULL;

	if( list->count == 0 )
		list->first = list->last = newItem;
	else
	{
		list->last->next = newItem;
		list->last = newItem;
	}

	list->count++;

	return 0;
}

// list free
void release_entry_list( SHELL_ENTRY_LIST* list )
{
	SHELL_ENTRY_LIST_ITEM*	currentItem;
	SHELL_ENTRY_LIST_ITEM*	nextItem;

	if( list->count == 0 )
		return;

	// list의 first가 nextItem
	nextItem = list->first;

	do
	{	
		// currentItem에 nextItem넣고, 그 nextItem에는 자기가 가리키던 next를 넣음
		currentItem = nextItem;
		nextItem = currentItem->next;

		// 그리고 free해줌
		free( currentItem );
	} while( nextItem ); // nextItem 없을때까지 반복

	// list 구조체 setting
	list->count	= 0;
	list->first	= NULL;
	list->last	= NULL;
}

