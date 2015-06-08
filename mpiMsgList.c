#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpiMsgList.h"


void CreateMPIMsgList(MPI_MsgList_t * list)
{
	//zero is used to check like true false;
	list->first = list->last = 0;	
}

//adds to end
link_t * AddToMPIMsgList (MPI_MsgList_t * list, char * buf, int size)
{
    link_t * link;
    
    MPI_Msg * NewMsg;
	NewMsg = (MPI_Msg *)malloc(sizeof(MPI_Msg));
	NewMsg->payLoadSize = size - 24;//std msg size info;
	NewMsg->payLoad = (char *)malloc(NewMsg->payLoadSize * sizeof(char));	
	sscanf(buf,"%08d%08d%08d",&NewMsg->tag,&NewMsg->commIndex,&NewMsg->rank);
	
	//printf("payLoadSize:%d\n",NewMsg->payLoadSize);
	//fflush(0);
		
	if(NewMsg->payLoadSize > 0)
	{
		strncpy(NewMsg->payLoad, buf+24, NewMsg->payLoadSize);
		//printf("payLoad:%s\n",NewMsg->payLoad);
    }	
    
    //calloc sets the field to zero. 
    link = calloc (1, sizeof (link_t));
    link->mpiMsg = NewMsg;
    
    if (list->last) {
        //add to last
        list->last->next = link;
        link->prev = list->last;
        list->last = link;
    }
    else {
    	//add to first
    	//first time only, when list is empty
        list->first = link;
        list->last = link;
    }
    list->size++;
    return link;
}

MPI_Msg * GetElementFromMPIMsgList (MPI_MsgList_t * list, int element)
{
    link_t * link;
    int x = 0;
    
    if(element > list->size)
    	return NULL;
	
    for (link = list->first; link; link = link->next) {
    	if(x == element)
    	{
        	return link->mpiMsg;
		}
		x++;
    }
}

void DeleteFromMPIMsgListSpecNode(MPI_MsgList_t * list, link_t * link)
{
    link_t * prev;
    link_t * next;

    prev = link->prev;
    next = link->next;
    if (prev) {
        if (next) {
            /* Both the previous and next links are valid, so just
               bypass "link" without altering "list" at all. */
            prev->next = next;
            next->prev = prev;
        }
        else {
            /* Only the previous link is valid, so "prev" is now the
               last link in "list". */
            prev->next = 0;
            list->last = prev;
        }
    }
    else {
        if (next) {
            /* Only the next link is valid, not the previous one, so
               "next" is now the first link in "list". */
            next->prev = 0;
            list->first = next;
        }
        else {
            /* Neither previous nor next links are valid, so the list
               is now empty. */
            list->first = 0;
            list->last = 0;
        }
    }
    list->size--;
    free (link);
}


void DeleteFromMPIMsgList(MPI_MsgList_t * list, int element)
{
	link_t * link;
    int x = 0;
    
    for (link = list->first; link; link = link->next) 
    {
    	if(x == element)
    	{
        	//delete here
        	link_t * prev;
    		link_t * next;

			prev = link->prev;
			next = link->next;
			if (prev) {
				if (next) {
					/* Both the previous and next links are valid, so just
					   bypass "link" without altering "list" at all. */
					prev->next = next;
					next->prev = prev;
				}
				else {
					/* Only the previous link is valid, so "prev" is now the
					   last link in "list". */
					prev->next = 0;
					list->last = prev;
				}
			}
			else {
				if (next) {
					/* Only the next link is valid, not the previous one, so
					   "next" is now the first link in "list". */
					next->prev = 0;
					list->first = next;
				}
				else {
					/* Neither previous nor next links are valid, so the list
					   is now empty. */
					list->first = 0;
					list->last = 0;
				}
			}
			free (link);
			list->size--;
			break;
		}
		x++;
    }
}

/*
int main(int argc, char * argv[])
{
	mpiProgressList = (MPI_MsgList_t *)malloc(sizeof(MPI_MsgList_t));
	
	CreateMPIMsgList(mpiProgressList);	
	
	
	char * buf;	
	buf = (char *)malloc((24+5)* sizeof(char));
	sprintf(buf,"%08d%08d%08d%s",-10,1,0,"hello");
	
	//printf("%s\n",buf);
	AddToMPIMsgList(mpiProgressList,buf,24+5);
	
	MPI_Msg * mpiMsg; 
	mpiMsg = (MPI_Msg *)GetElementFromMPIMsgList (mpiProgressList, 0);
	
	printf("tag:%d,CommIndex:%d,rank:%d\n",mpiMsg->tag,mpiMsg->commIndex,mpiMsg->rank);
	printf("payload:%s\n",mpiMsg->payLoad);
	printf("size:%d\n",mpiProgressList->size);
	
	DeleteFromMPIMsgList(mpiProgressList,0);
	
	printf("size:%d\n",mpiProgressList->size);
	
	return 0;	
}*/
