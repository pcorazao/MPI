#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "mpi.h"

#define BUFFERSIZE 10
#define RECV_OK    		0
#define RECV_EOF  		-1
#define DEFAULT_BACKLOG		5

//Developer: Peter Corazao, mpiexec
//Class: 6130
//Assignment: pp6130_p1
//Program Description: Own implementation of MPI Exec.
//Date: Sept 5th, 2011

//struct type to mange a rank process.
typedef struct {
   char     hostname[3];
   int      rank;
   int      fd;
   int      port;
   FILE  *  fp;
   char *   sshExecAndArgs;
   int      fdConn;
} workerProcess;

typedef struct{
	int commIndex;
	int key;
	int value;
}Attributes;

typedef struct
{
	int * WinLock; //-1 for unlocked or rankvalue if locked.
	int * LockType;	//0 exclusive else shared
}lock;

lock * windowLocks;
int windowLocksSize;

//Global Variables
workerProcess *workers;
Attributes * attributes;
int attrSize;
char *ExecAndArgs;
int RankSize;
int * ReqRankLookup;
int Abort;


int readline(int, char *, int);

void depleatIO(int  fd, int rank)
{
	struct timeval tv;
	fd_set Rankfds;
	char buf[400];
	int done = 0;
	int rc;
	
	while(done==0)
	{
	
		FD_ZERO( &Rankfds );
	
		FD_SET( fd, &Rankfds );
		
		tv.tv_sec = 0;
        tv.tv_usec = 0;
        
        rc = select( FD_SETSIZE, &Rankfds, NULL, NULL, &tv );

		if (rc == 0)
		{
			
		}else if (rc == -1  &&  errno == EINTR)
		{
		   
		}else if ( rc < 0 )
		{
			
		}else
		{
			if (FD_ISSET(fd,&Rankfds))  
			{
				int n = readline(fd,buf,400);
				if (n == -1)
				{
					printf("read failed\n");
					done = 1;
				}
				else if (n == 0)
				{
					//nothing found, mark a done count
					//printf("closing:%d\n",lastRankRead);
					done = 1;					
					//round robin to next file descriptor					
					
				}
				else
				{					  
					   char * Output;
					   Output = (char *)malloc((n+10) * sizeof(char));
					   sprintf(Output,"[%d]%s",rank,buf);
					   printf("%s",Output);						  			   
				}	
			}
		}
	}
}

void ServiceRanks()
{
	        int i,rc,x;
			struct timeval tv;
			fd_set Rankfds;
			int commIndex = 0;
		    int size, OK; 
		    
		    int availableConnections = 0;
		    
			//Select off all connections.
	
			FD_ZERO( &Rankfds );
			
			for(i=0;i<RankSize;i++)
			{
				if(workers[i].fdConn > 0)
				{
					availableConnections = 1;
					FD_SET( workers[i].fdConn, &Rankfds );
				}
			}
			
			//check if its worth reading anything
			if(availableConnections==1)
			{
			
			//set time outs for select
        	tv.tv_sec = 0;
        	tv.tv_usec = 0;
			
			//select statment
        	rc = select( FD_SETSIZE, &Rankfds, NULL, NULL, &tv );

			if (rc == 0)
			{
				
			}else if (rc == -1  &&  errno == EINTR)
			{
			   
			}else if ( rc < 0 )
			{
				
			}else
			{
							
				for(i=0;i<RankSize;i++)
				{
					if(workers[i].fdConn > 0)
					{
						if (FD_ISSET(workers[i].fdConn,&Rankfds))  
						{
							char * buf;
							int tag,commIndex,rank, payLoadSize,bytes_read;
							char * payLoad;
							
							//char c;
							//int x = 0;
							//x = read(workers[i].fdConn, &c, 1);
							//printf("Rank:%d filedesc:%d\n",i,workers[i].fdConn);
													
							bytes_read = recv_msg(&workers[i].fdConn, &buf);
							
							if(bytes_read > 0)
							{
								//printf("came out of read\n");
													
								payLoadSize = bytes_read - 24;//std msg size info;
								payLoad = (char *)malloc(payLoadSize * sizeof(char));	
								sscanf(buf,"%08d%08d%08d",&tag,&commIndex,&rank);
								//printf("tag:%d,commIndex:%d,rank%d\n",tag,commIndex,rank);fflush(0);
								if(payLoadSize > 0)
								{
									strncpy(payLoad, buf+24, payLoadSize);
									//printf("payLoad:%s\n",NewMsg->payLoad);
								}	
								
								if(Abort==1)
								{
									//Tell this Rank, We Are Aborting.	
									
									int size = 8;//aborting response
									int MPI_ABORT = -911;
									buf = (char*)realloc(buf,size * sizeof(char));
									sprintf(buf,"%08d",MPI_ABORT);
					
									//Send to rank 
									send_msg(workers[i].fdConn,buf,size);
									
									if(tag == -911 || tag == -13)
									{
										close(workers[i].fdConn);
										workers[i].fdConn = -1;	
										//printf("rank %d aborting\n",i);
									}
										
								}else
								{
								
								int key,value;
								switch(tag)
								{
									case -10:
									;
									//commDup request
									
											int newCommIndex;
											sscanf(payLoad,"%08d",&newCommIndex);
											//printf("Dup commIndex:%d newCommIndex:%d\n",commIndex,newCommIndex);
											if(attrSize==0)
											{
												//done;
											}else
											{
												
												int attributesfound = 0;
												for(x=0;x<attrSize;x++)
												{
													if(attributes[x].commIndex==commIndex)
													{
														attributesfound++;														
													}	
												}
												
												if(attributesfound == 0)
												{
													//done
												}else
												{
													//make a copy of all attributes
													int originalSize = attrSize;
													int copyLocation = attrSize;
													attrSize+=attributesfound;
													//printf("New Size: %d\n",attrSize);
													attributes = (Attributes *)realloc(attributes,sizeof(Attributes) * attrSize);
													
													for(x=0;x<originalSize;x++)
													{
														if(attributes[x].commIndex==commIndex)
														{
															attributes[copyLocation].commIndex = newCommIndex; //use new comm
															attributes[copyLocation].key = attributes[x].key;
															attributes[copyLocation].value = attributes[x].value;
															copyLocation++;	
															//printf("Copied\n");
														}
													}													
													
													//for(x=0;x<attrSize;x++)
													//{
													//	printf("commIndex:%d key:%d value:%d\n",attributes[x].commIndex,attributes[x].key,attributes[x].value);
													//}
													//printf("done with dup\n");
												}
													
											}
									
											size = 8;//send 1 for ok.
											OK = 1;
											buf = (char*)realloc(buf,size * sizeof(char));
											sprintf(buf,"%08d", OK);
											//Send to rank 1 for OK proceed.
											send_msg(workers[i].fdConn,buf,size);		
									
									break;
									
									case -11:
									;
											//first time to connect request;
											int ReqRank;
											sscanf(payLoad,"%08d",&ReqRank);
											
											if(ReqRankLookup[ReqRank] != i) //avoid dead lock, allow connection as long as the reqRank is currently attempting to connect to rank;
											{
													ReqRankLookup[i] = ReqRank; //mark rank as requesting connection;
													
													int size = 8;//send 1 for ok.
													int OK = 1;
													buf = (char*)realloc(buf,size * sizeof(char));
													sprintf(buf,"%08d",OK);
					
													//Send to rank 1 for OK proceed.
													send_msg(workers[i].fdConn,buf,size);	
													//printf("Rank:%d Requested to Connect to ReqRank:%d\n",i,ReqRank);
											}else
											{
												//Not Allowed to Connect	
												int size = 8;//send 1 for ok.
												int DENIED = 0;
												buf = (char*)realloc(buf,size * sizeof(char));
												sprintf(buf,"%08d",DENIED);
					
												//Send to rank 1 for OK proceed.
												send_msg(workers[i].fdConn,buf,size);	
											}
									break;
									
									case -12:
											//first time connection was made
											ReqRankLookup[i] = -1; //free the rank from the request list							
									break;
									case -13:
											//Rank sent finalize, close it down.
											//printf("Rank:%d Finalized\n",i);
											
											size = 8;//send 1 for ok.
											OK = 1;
											buf = (char*)realloc(buf,size * sizeof(char));
											sprintf(buf,"%08d", OK);
											//Send to rank 1 for OK proceed.
											send_msg(workers[i].fdConn,buf,size);
											
											depleatIO(workers[i].fd, i);											
											close(workers[i].fdConn); 
											workers[i].fdConn = -1;
									break;
									case -911:
											//MPI_Abort
											close(workers[i].fdConn);
											workers[i].fdConn = -1;
											Abort = 1;
									break;
									case -15:
									;
											//store commIndex, key and value
										    //commIndex;
											sscanf(payLoad,"%08d%08d",&key,&value);
											//printf("Storing commIndex:%d key:%d value:%d\n",commIndex,key,value);
											if(attrSize==0)
											{
												attrSize++;
												attributes = (Attributes *)malloc(sizeof(Attributes)*1);
												attributes[0].commIndex = commIndex;
												attributes[0].key = key;
												attributes[0].value = value;	
											}else
											{
												
												int found = 0;
												for(x=0;x<attrSize;x++)
												{
													if(attributes[x].commIndex==commIndex && attributes[x].key==key)
													{
														found = 1;
														attributes[x].value = value;	
													}	
												}
												
												if(found == 0)
												{
													attrSize++;
													attributes = (Attributes *)realloc(attributes,sizeof(Attributes) * attrSize);
													attributes[attrSize-1].commIndex = commIndex;
													attributes[attrSize-1].key = key;
													attributes[attrSize-1].value = value;
												}
													
											}
									
											size = 8;//send 1 for ok.
											OK = 1;
											buf = (char*)realloc(buf,size * sizeof(char));
											sprintf(buf,"%08d", OK);
											//Send to rank 1 for OK proceed.
											send_msg(workers[i].fdConn,buf,size);											
									
									break;
									
									case -16:
									;
										//store commIndex, key and value
										
										//commIndex;
										sscanf(payLoad,"%08d",&key);
										
										int found = 0;
										for(x=0;x<attrSize;x++)
										{
											if(attributes[x].commIndex==commIndex && attributes[x].key==key)
											{
												
												found = 1;
												size = 8+8;//flag,value
												int flag = 1;
												buf = (char*)realloc(buf,size * sizeof(char));
												int value = attributes[x].value;
												
												sprintf(buf,"%08d%08d", flag,value);
												
												//printf("fdConn%d size:%d buf%s\n",workers[i].fdConn,size,buf);
												//Send responce with value
												send_msg(workers[i].fdConn,buf,size);	
												//printf("Here\n");
											}	
									    }
									    
									    if( found == 0)
									    {
									    	size = 8;//flag,value
											int flag = 0;
											buf = (char*)realloc(buf,size * sizeof(char));
											sprintf(buf,"%08d", flag);
											//Send to response
											send_msg(workers[i].fdConn,buf,size);
									    }	
									
									break;
									case -22:
									;//win create
										//printf("win Create\n");fflush(0);
										if(windowLocksSize == 0)
										{
											windowLocks = (lock *) malloc(1 * sizeof(lock));	
											windowLocksSize++;		
										}else
										{
												lock * newWindowLocks = (lock *)realloc(windowLocks,(++windowLocksSize)* sizeof(lock));
												windowLocks = newWindowLocks;	
										}
										
										windowLocks[windowLocksSize-1].WinLock = (int *)malloc(RankSize * sizeof(int));
										windowLocks[windowLocksSize-1].LockType = (int *)malloc(RankSize * sizeof(int));
										
										for(x=0;x<RankSize;x++)
										{
												windowLocks[windowLocksSize-1].WinLock[x] = -1;
												windowLocks[windowLocksSize-1].LockType[x]  = -1;	
										}
											
										
										//say ok, lock got created
										size = 8;//flag,value
										buf = (char*)realloc(buf,size * sizeof(char));
										sprintf(buf,"%08d", 1);
										//Send to response
										send_msg(workers[i].fdConn,buf,size);								   
									    //printf("win Create DONE\n");fflush(0);
									break;
									
									case -23:
									;
										//window request;
										int winIndex,requestType,lockType,WindowRankIndex;
										sscanf(payLoad,"%08d%08d%08d%08d",&winIndex,&requestType,&lockType,&WindowRankIndex);
										
										//printf("rank:%d winIndex:%d requestType:%d lockType:%d WinRankIndex:%d\n",rank,winIndex,requestType,lockType,WindowRankIndex);
										
										
										int result;
										
										switch(requestType)
										{
											case 1:// lock Request
											
												if(windowLocks[winIndex].WinLock[WindowRankIndex] == -1 )
												{
														//approved
														result = 1;
														windowLocks[winIndex].WinLock[WindowRankIndex] = rank;
														windowLocks[winIndex].LockType[WindowRankIndex] = lockType;
												}else
												{
														//denied	
														result = 0;
												}
											
											break;
											case 2: //Unlock Request
												 
												 if(windowLocks[winIndex].WinLock[WindowRankIndex] == rank)
												 {
												 	//approved
												 	result = 1;
												 	windowLocks[winIndex].WinLock[WindowRankIndex] = -1;
													windowLocks[winIndex].LockType[WindowRankIndex] = -1;
												 }else
												 {
												 	//denited
												 	result=0;	
												 }
												
											break;
											case 3: //Get Request
												 
												 if(windowLocks[winIndex].WinLock[WindowRankIndex] == -1)
												 {
												 	
												 	
												 	//approved
												 	result = 1;
												 	
												 }else
												 {
												 	//well its locked
												 	if(windowLocks[winIndex].WinLock[WindowRankIndex] == rank)
												 	{
												 		//this guy owns the lock, he can do what ever is needed during his window.
												 		//approved
												 		result = 1;												 	
												 	}else
												 	{
												 		//this rank does not own the lock.
												 		//any chance the lock is shared
												 		if(windowLocks[winIndex].LockType[WindowRankIndex] > 0)
												 		{
												 			//consider this a shared locked
												 			//approved
												 			result = 1;		
												 		}else
												 		{
												 			//denited
												 			result=0;
												 		}	
												 	}
												}
											break;
											case 4: //Set
												
												if(windowLocks[winIndex].WinLock[WindowRankIndex] == -1)
												 {
												 	//approved
												 	result = 1;
												 	
												 }else
												 {
												 	//well its locked
												 	if(windowLocks[winIndex].WinLock[WindowRankIndex] == rank)
												 	{
												 		//this guy owns the lock, he can do what ever is needed during his window.
												 		//approved
												 		result = 1;												 	
												 	}else
												 	{
												 		//Set is only allowed by the rank that owns the lock
												 		
												 		//denited
												 		result=0;
												 		
												 	}
												}
											break;
												
										}// end switch
										
										if(result==1)
										{
										        //approved
												size = 8;//flag,value
												buf = (char*)realloc(buf,size * sizeof(char));
												sprintf(buf,"%08d", 1);
												//Send to response
												//printf("sending approved\n");
												send_msg(workers[i].fdConn,buf,size);	
												//printf("done\n");
												
												//wait for complete
												char * winRecv;
												int bytes_n = recv_msg(&workers[i].fdConn, &winRecv);
										}else
										{
											    //Denied											
											  	size = 8;
												buf = (char*)realloc(buf,size * sizeof(char));
												sprintf(buf,"%08d", 0);
												//Send to response
												//printf("sending denied\n");
												send_msg(workers[i].fdConn,buf,size);	
												//printf("done\n");
										}
										//printf("Done\n");fflush(0);
										
									break;
																
									default:
											printf("Undefined Sys Tag\n");
									break;	
								}
								
								break;//break for loop
							}
						}
						free(buf);
					}
				}
				}
			}
		}
}

//Reads in from HostNames
//containing line delimitied host machines
//to be used by MPI
void getHosts()
{
	int i,x,numberOfHosts;
	FILE * hosts;
	char *buff;
	
	
	buff = (char *)malloc(BUFFERSIZE);
	
	//open the file for read
	hosts = fopen("hostnames","r");	
	
	//read each line and store in struct till EOF.
	i=0;
	while((fgets(buff,BUFFERSIZE,hosts)) != NULL)
	{
		sscanf(buff,"%s",workers[i].hostname);
		workers[i].rank = i;
		i++;
		if(i==RankSize)
		break;
	}
	
	//if the -n RankSize excedes the hostnames
	//then we need to roundrobin the host
	//untill all Ranks are covered.
	
	x=0;
	numberOfHosts = i;
	if(i < RankSize)
	{
		for(i;i<RankSize;i++)
		{
			if(x==numberOfHosts)
				x=0;
			
			//printf("Host:%s\n",workers[x].hostname);
			strcpy(workers[i].hostname,workers[x].hostname);
			//printf("Host:%s\n",workers[i].hostname);
			workers[i].rank = i;		
			
			x++;
		}
	}
	
	//close the hostnames file	
	fclose(hosts);	
}


// prints out the hostnames and ranks of
// the MPI worker processes
void printWorkers(int Size)
{
	int x = 0;
	
	for(x =0;x<Size;x++)
	{
		printf("Hostname:%s, Rank:%d, Port:%d\n",workers[x].hostname,workers[x].rank,workers[x].port);	
	}
}

//gets the specified number of ranks
//and parses the program for which to exec
//and the args.
void GetArgs(int argc,char * argv[])
{
	int i,x;
	int RankSpecified = 0;
	int size = 0;	
		
	//check to see if rank size is specified			
	if(strcmp("-n",argv[1]) == 0)
	{
		RankSize = atoi(argv[2]);
		RankSpecified = 1;
	}else
	{
		RankSize = 1;	
	}
	
	//if the rank was specified 
	//the rest is the program to execute and its Args
	//other wise we use the first copy containing 
	// the program to execute and its Args	
	if(RankSpecified)
	{
		i=3;
	}else{
		i=1;
	}	
	
	//get the size for all the specifed args
	for(x = i;x<argc;x++)
	{
		size += strlen(argv[x]);		
	}
	
	//Grab all the args and place them into a global variable.
	
	//printf("size:%d spaces:%d\n",size,argc-i-1);
	ExecAndArgs = (char*) malloc( (size+(argc-i-1)) * sizeof(char));
	strcpy(ExecAndArgs,argv[i]);
	for(x = i+1;x<argc;x++)
	{
		char * strtemp;
		strtemp = (char *)malloc((strlen(argv[x])+1)* sizeof(char));
		strcat(strtemp," ");
		strcat(strtemp,argv[x]);
		strcat(ExecAndArgs,strtemp);		
	}
	//strcat(ExecAndArgs,"\0");		
	//printf("\nlen:%d",strlen(ExecAndArgs));
	//write(1,ExecAndArgs,strlen(ExecAndArgs));
}

int main(int argc, char * argv[])
{
	int i, rc, n, done, lastRankRead,default_socket, new_socket, mpiexecPort;
    fd_set readfds;
    char popenbuf[400];
    struct timeval tv;
	attrSize = 0;
	
	windowLocksSize =0;
	
	//check for correct args.
	if(argc >= 2)
	{
		//printf("%d %s\n",argc,argv[0]);
		GetArgs(argc,argv);
		//printf("\nRankSize:%d ExecAndArgs:%s\n",RankSize,ExecAndArgs);	
			
	}else{
	  printf("Usage:\n");
	  printf("-n int value to specifty the number of ranks\n");
	  printf("Name of exec program to exec followed by args\n");
	  printf("example: mpiexec -n 5 echo test\n");
	  return -1;
	}
	
	Abort=0;
		
	//create worker struct 
	workers = (workerProcess *)malloc(sizeof(workerProcess)*RankSize);	
	getHosts();	
	//printWorkers(RankSize);
	
	ReqRankLookup = (int *)malloc(RankSize * sizeof(int));
	for(i=0;i<RankSize;i++)
	{
		ReqRankLookup[i] = -1;	
	}
	
	//setup default socket
	default_socket = socket(AF_INET, SOCK_STREAM, 0);
	error_check(default_socket,"setup_to_accept socket");
	
	//setup socket here
	struct sockaddr_in sin, from;
    memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    
    rc = bind(default_socket, (struct sockaddr *)&sin ,sizeof(sin));
    error_check(rc,"setup_to_accept bind");

	//get port number
	socklen_t channellen;
	channellen = sizeof(sin);
    getsockname(default_socket, (struct sockaddr*) &sin, &channellen);    
    mpiexecPort = ntohs(sin.sin_port);
    //printf("PortNumber: %d\n",mpiexecPort);

    rc = listen(default_socket, DEFAULT_BACKLOG);
    error_check(rc,"setup_to_accept listen");
		
	//MPI Exec
	for(i=0;i<RankSize;i++)
	{
		//example:ssh b01 'bash -c "(cd cwd ; export PP_MPIRANK=1; export PP_MPI_SIZE=10; export PP_MPI_HOST_PORT=1234; echo \$PP_MPI_SIZE)" 2>&1'
		
		//Create the exec cmd
		char * cwd;
		cwd = (char *)malloc(100 * sizeof(char));
		getcwd(cwd,100);
		//printf("%s\n",cwd);
		workers[i].sshExecAndArgs = (char*)malloc((strlen(ExecAndArgs)+250) * sizeof(char));
		char mpiexechostname[4];
		mpiexechostname[4] = '\0';
		gethostname(mpiexechostname,4);
		sprintf(workers[i].sshExecAndArgs,"ssh %s 'bash -c \"(cd %s ; export PP_MPI_RANK=%d; export PP_MPI_SIZE=%d; export PP_MPI_HOST_PORT=%d; export PP_MPI_HOSTNAME=%s; %s;  )\" 2>&1'\0",workers[i].hostname,cwd,workers[i].rank,RankSize,mpiexecPort,mpiexechostname,ExecAndArgs);
		
		//printf("%s\n",workers[i].sshExecAndArgs);
		
		//Exec Ranks
		workers[i].fp = popen(workers[i].sshExecAndArgs,"r");
		//get file descriptor for a later select.
		workers[i].fd = fileno(workers[i].fp);				
	}
	int bytes_read;
		
	//Handel MPI INIT Phone Book.	
	for(i=0;i<RankSize;i++)
	{
		int bytes_read;		
		char * buf;
		int remoteRank;
		int remoteRankPortNumber;
		
		//printf("waiting for connection next step \n");
		new_socket = accept_connection(default_socket);
			
		bytes_read = recv_msg(&new_socket, &buf);
		if(bytes_read > 0)
		{
			sscanf(buf,"%08d%08d",&remoteRank,&remoteRankPortNumber);
			
			//printf("bytes_read:%d rank:%d port:%d\n",bytes_read,remoteRank,remoteRankPortNumber);
			
			//add port to phone book.
			int x;
			for(x =0; x<RankSize; x++)
			{
				if(workers[x].rank == remoteRank)
				{
					workers[x].port = remoteRankPortNumber;
					workers[x].fdConn = new_socket;
				}
			}	
		}		
	}
	
	//Print Phone Book
	//printWorkers(RankSize);
	
	//Send Phone Book Out
	//connect to Ranks
	for(i=0;i<RankSize;i++)
	{
		int x;
		for(x = 0; x< RankSize; x++)
		{
			char * buf;
			int size = 8+3+8;//Rank + Hostname + Port
			buf = (char*)malloc(size * sizeof(char));
			sprintf(buf,"%08d%3s%08d",workers[x].rank,workers[x].hostname,workers[x].port);
			
			//Send Phone Book to rank
			send_msg(workers[i].fdConn,buf,size);	
		}		
	}
	
    //Read Output from Execed MPI Ranks, and Service Ranks;
    done = 0;
    lastRankRead=0;
    int doneCount = 0;
    int waitChildCount = 0;
    int strike = 0;
    int somethingStillOpen;
    while ( !done ) 
    {
    	ServiceRanks();
    	
    	somethingStillOpen = 0;
    	
        //printf("donecount:%d lastRankRead%d waitChildCount:%d\n",doneCount,lastRankRead,waitChildCount);
    	//Zero out our file descriptor set.
        FD_ZERO( &readfds );
        //Set our file descriptors to the out put for each Rank.
        for(i=0;i<RankSize;i++)
        {
        	if(workers[i].fd > 0)
        	{
        		somethingStillOpen=1;
				FD_SET( workers[i].fd, &readfds );  /* set file desc for each worker */
			}else
			{
				doneCount++;
			}
        }
        
        //if there is nothing open leave now we are done.
        if(somethingStillOpen==0)
        {
        	done=1;	
        	break;
        }
        
        //set time outs for select
        tv.tv_sec = 0;
        tv.tv_usec = 0;

		//select statment
        rc = select( FD_SETSIZE, &readfds, NULL, NULL, &tv );
		
		//printf("Here\n");
		
        if (rc == 0)
		{
            //printf("select timed out\n");
            continue;
        }
         
        if (rc == -1  &&  errno == EINTR)
		{
            printf("select interrupted; continuing\n");
            continue;
        }
        
        if ( rc < 0 )
		{
            done = 1;
            printf("select failed\n");
        }

		

		//see if there is something on the lastRankRead file desc.
		if(workers[lastRankRead].fd > 0)
		{
			if (FD_ISSET(workers[lastRankRead].fd,&readfds))  
			{
				//printf("Here1\n");
				n = readline(workers[lastRankRead].fd,popenbuf,400);
				//printf("Here2\n");
				
				if (n == -1)
				{
					printf("read failed\n");
					done = 1;
				}
				else if (n == 0)
				{
					//nothing found, mark a done count
					//printf("closing:%d\n",lastRankRead);
					close(workers[lastRankRead].fd);
					workers[lastRankRead].fd = -1;
					
					//round robin to next file descriptor
					
					if(lastRankRead >= RankSize-1)
							lastRankRead = 0;
					else
							lastRankRead++;	
				}
				else
				{
					   //print out what we read;
					   char * Output;
					   Output = (char *)malloc((n+10) * sizeof(char));
					   sprintf(Output,"[%d]%s",workers[lastRankRead].rank,popenbuf);
					   printf("%s",Output);			   
					   doneCount = 0;	   
					   strike = 0;				   
				}
			}else
			{
			    //Still alive just not talking,Round Robin to next file descripter
				if(lastRankRead >= RankSize-1)
					lastRankRead = 0;
				else
					lastRankRead++;	
			}
		}else
		{
			//round robin to next file descriptor			
			if(lastRankRead >= RankSize-1)
				lastRankRead = 0;
			else
				lastRankRead++;				
		}			
				
		//printf("donecount:%d lastRankRead%d waitChildCount:%d\n",doneCount,lastRankRead,waitChildCount);
		
		//if all processes report done
		if(doneCount >= RankSize)
		{   
			//printf("done\n");				
			done=1;		
		}
		doneCount = 0;
		  
    }//end while
    
    //clean up, close all fds and fps.
    /*
    for(i=0;i<RankSize;i++)
    {
    	pclose(workers[i].fp);
    	close(workers[i].fd);
    	close(workers[i].fdConn);	
    } 
    	*/
    
    //free struct
    free(workers);
    
    //done.
    //printf("done!\n");	
}


int readline(int fd, char *str, int maxlen) 
{
  int n;           /* no. of chars */  
  int readcount;   /* no. characters read */
  char c;

  for (n = 1; n < maxlen; n++) {
    /* read 1 character at a time */
    readcount = read(fd, &c, 1); /* store result in readcount */
    if (readcount == 1) /* 1 char read? */
    {
      *str = c;      /* copy character to buffer */
      str++;         /* increment buffer index */         
      if (c == '\n') /* is it a newline character? */
         break;      /* then exit for loop */
    } 
    else if (readcount == 0) /* no character read? */
    {
      if (n == 1)   /* no character read? */
        return (0); /* then return 0 */
      else
        break;      /* else simply exit loop */
    } 
    else 
      return (-1); /* error in read() */
  }
  *str=0;       /* null-terminate the buffer */
  return (n);   /* return number of characters read */
}


