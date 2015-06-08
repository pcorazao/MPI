#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

int main(int argc, char * argv[])
{
	MPI_Comm newcomm;
	int rank,size;
	double start,end;
	
	MPI_Init(&argc, &argv);
	
	/*
	MPI_Comm_dup(MPI_COMM_WORLD,&newcomm);
	
	MPI_Comm_rank(newcomm, &rank);
	
	//if(rank == 0)
	//{
	//	printf("Rank Zero Calling Abort!\n");
	//	MPI_Abort(MPI_COMM_WORLD, 911);
	//}
	
	
	//printf("Before Barrier\n");
	//fflush(0);
	
	MPI_Barrier(MPI_COMM_WORLD);
	
	//printf("After Barrier\n");
	//fflush(0);
	
	
	//MPI_PRINT_COMM(newcomm);	
	
	MPI_Comm_size(newcomm, &size);
	
	printf("NewComm Rank is:%d NewComm Size:%d\n",rank, size);
	fflush(0);
	
	int key,value;
	
	
	
	if(rank == 0)
	{
		key = 10;
		value = 777;
		MPI_Attr_put(MPI_COMM_WORLD, key, &value);
	}else if(rank==1)
	{
		key = 20;
		value = 888;
		MPI_Attr_put(MPI_COMM_WORLD, key, &value);
	}else
	{
		key = 30;
		value = 999;
		MPI_Attr_put(MPI_COMM_WORLD, key, &value);
	}	
	
	
	MPI_Barrier(MPI_COMM_WORLD);
				
	int flag;
	MPI_Attr_get(MPI_COMM_WORLD, key, &value, &flag);
	
	if(flag == 1)
	{
		printf("value:%d\n",value);	
	}
	
	start = MPI_Wtime();
	sleep(1);
	end = MPI_Wtime();
	
	printf("sleep:%1.2f\n",end-start);
	
	double tick;
	
	tick = MPI_Wtick();
    printf("A single MPI tick is %0.9f seconds\n", tick);
    fflush(0);
	
	//*/
	
	/*
	int send[2] = {1,2};
	int recv[4] = {0,0,0,0};
	
	printf("Send: %d\n",send[0]);
	
	MPI_Gather(&send,2,MPI_INT,&recv,2,MPI_INT,0,MPI_COMM_WORLD);
	
	int i;
	for(i=0;i<4;i++)
	{
		printf("Recv: %d\n",recv[i]);
	}
	//*/
	
	/*
 	char send[] = "WhatsUP?";
 	char recv[7] = "\0"; 
	
	printf("%s\n",send);
	
	MPI_Gather(&send,6,MPI_CHAR,&recv,6,MPI_CHAR,0,MPI_COMM_WORLD);
	
	printf("%s\n",recv);
	//*/
	
	/*
	int send[2] = {1,2};
	int recv[4] = {0,0,0,0};
	
	MPI_Gather(&send,2,MPI_INT,&recv,2,MPI_INT,0,MPI_COMM_WORLD);
	
	int i;
	for(i=0;i<4;i++)
	{
		printf("Recv: %d\n",recv[i]);
	}
	//*/
	
	/*
	char send[] = "abcd";
	char recv[5] = "\0"; 
	
	printf("recv: %s\n",recv);
	fflush(0);
	
	MPI_Gather(&send,2,MPI_CHAR,&recv,2,MPI_CHAR,0,MPI_COMM_WORLD);
	
	printf("recv: %s\n",recv);
	fflush(0);
	//*/
	
	/*
	int send[1];
		
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	if(rank==0)
	{
		send[0] = 7;	
	}else
	{
		send[0] = 2;	
	}
	
	MPI_Bcast(send,1,MPI_INT,0,MPI_COMM_WORLD);
	
	printf("Lucky Number: %d\n",send[0]);
	//*/
	
	/*
	int send[10];
	int i;
		
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	if(rank==0)
	{
		send[0] = 0;	
		send[1] = 1;
		send[2] = 2;
		send[3] = 3;
		send[4] = 4;
		send[5] = 5;
		send[6] = 6;
		send[7] = 7;
		send[8] = 8;
		send[9] = 9;
	}else
	{
		send[0] = 0;	
		send[1] = 0;
		send[2] = 0;
		send[3] = 0;
		send[4] = 0;
		send[5] = 0;
		send[6] = 0;
		send[7] = 0;
		send[8] = 0;
		send[9] = 0;	
	}
	
	MPI_Bcast(send,10,MPI_INT,0,MPI_COMM_WORLD);
	
	for(i=0;i<10;i++)
	{
		printf("%d,",send[i]);
	}
	printf("\n");
	//*/
	
	/*
	char send[6];
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	if(rank==0)
	{
	   sprintf(send,"%s","hello");
		
	}else if(rank==2)
	{
	  sprintf(send,"%s","newks");
	}
	
	
	
	MPI_Bcast(send,5,MPI_CHAR,2,MPI_COMM_WORLD);
	
	printf("%s\n",send);
	fflush(0);
	
	MPI_Barrier(MPI_COMM_WORLD);
	*/
	
	
	/*
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if(rank==0)
	{
		int send[] = {0,1,2,3,4,5,6,7,8,9};
		MPI_Request request;
		MPI_Isend(send, 10, MPI_INT, 1, 10, MPI_COMM_WORLD, &request);
		printf("Request:%d\n",request);
	}else
	{
		MPI_Request request;
		int revc[] = {0,0,0,0,0,0,0,0,0,0};
		MPI_Irecv(revc,10,MPI_INT,0,10,MPI_COMM_WORLD,&request);
		int i;
		for(i=0;i<10;i++)
		{
			printf("%d,",revc[i]);
		}
		printf("\n");
	}
	*/
	
	/*
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if(rank==0)
	{
		int send[] = {0,1,2,3,4,5,6,7,8,9};
		MPI_Request request;
		MPI_Ssend(send, 10, MPI_INT, 1, 10, MPI_COMM_WORLD);
		//printf("HandShack Complete with Rank:%d\n",1);
	}
	
	if(rank==1)
	{
		
		MPI_Status status;
		int recv[10]; 
		MPI_Recv(recv, 10 , MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);	
		int i;
		for(i=0;i<10;i++)
		{
			printf("%d,",recv[i]);
		}	
		printf("\n");
		
	}
	
	
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if(rank==0)
	{
		char send[] = "Hupla!";
		MPI_Request request;
		MPI_Isend(send, 5, MPI_CHAR, 0, 10, MPI_COMM_WORLD, &request);
		printf("Request:%d\n",request);
	}
	
	if(rank==0)
	{
		MPI_Request request;
		char revc[6]; 
		MPI_Irecv(revc,5,MPI_CHAR,0,10,MPI_COMM_WORLD,&request);
		printf("%s\n",revc);
	}
	*/
	
	//RMA test

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int buf[rank];
	MPI_Win win;
	int putBuf[] = {1,2,3,4,5,6,7,8,9,10};
	
	MPI_Win_create(&buf, sizeof(int) * 512,sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);
	
	if(rank==0)
	{
		//put 10 ints in rank 1s mem
		MPI_Win_lock(0, 1, 0, win); //lock rank 1 mem
		MPI_Put(&putBuf, 10, MPI_INT, 1, 0, 10, MPI_INT, win);
		printf("this should always print first\n");
		fflush(0);	
		sleep(10);	
		MPI_Win_unlock(1,win);
		fflush(0);		
	}
	
	//MPI_Barrier(MPI_COMM_WORLD);
	
	if(rank==1)
	{
		sleep(1);
		
		int getBuf[] = {0,0,0,0,0,0,0,0,0,0};
		MPI_Get(getBuf,10,MPI_INT,1,0,10,MPI_INT,win); //read from my window	
		
		int i;
		for(i=0;i<10;i++)
		{
			printf("%d,",getBuf[i]);
		}	
		printf("\n");
		fflush(0);
	}
	
	printf("%d\n",win);
	
	MPI_Finalize();
	
	return 0;	
}
