 /******************************************************************************/
/*                                                                            */
/* Project : FAT12/16 File System                                             */
/* File    : clusterlist.h                                                    */
/* Author  : Kyoungmoon Sun(msg2me@msn.com)                                   */
/* Company : Dankook Univ. Embedded System Lab.                               */
/* Notes   : Cluster list header                                              */
/* Date    : 2008/7/2                                                         */
/*                                                                            */
/******************************************************************************/

#ifndef _CLUSTERLIST_H_
#define _CLUSTERLIST_H_

#include "common.h"

#define CLUSTERS_PER_ELEMENT	1023

typedef struct CLUSTER_LIST_ELEMENT
{
	SECTOR				clusters[CLUSTERS_PER_ELEMENT]; //int��(sector) Ŭ������ �Ҵ� 

	struct CLUSTER_LIST_ELEMENT*	next;
} CLUSTER_LIST_ELEMENT;

typedef struct
{
	UINT32				count;
	UINT32				pushOffset;
	UINT32				popOffset;

	CLUSTER_LIST_ELEMENT*	first;
	CLUSTER_LIST_ELEMENT*	last;
} CLUSTER_LIST;

int	init_cluster_list( CLUSTER_LIST* ); //Ŭ������ ����Ʈ �ʱ�ȭ
int	push_cluster( CLUSTER_LIST*, SECTOR ); // Ŭ������ ����Ʈ�� ����
int pop_cluster( CLUSTER_LIST*, SECTOR* );
void	release_cluster_list( CLUSTER_LIST* );

#endif

