#include "mpi.h"
#include "mpiMsgList.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>

#define RECV_OK    		0
#define RECV_EOF  		-1
#define DEFAULT_BACKLOG		5

//MPI TAGS
#define MPI_COMM_DUP_TAG   -10
#define MPI_CONNECT_REQ    -11
#define MPI_CONNECTED      -12
#define MPI_FINAL_GOODBY   -13
#define MPI_ABORT		   -911
#define MPI_GATHER_TAG     -14
#define MPI_ATTR_PUT       -15
#define MPI_ATTR_GET       -16
#define MPI_BCAST          -17
#define MPI_RSEND          -18
#define MPI_RSEND_RSP	   -19
#define MPI_WIN_PUT        -20
#define MPI_WIN_GET        -21
#define MPI_WIN_CREATE     -22
#define MPI_WIN_REQUEST    -23 
#define MPI_WIN_GET_RSP    -24
#define MPI_WIN_PUT_RSP    -25
#define MPI_BARRIER_TAG    -26


char * value;
char * PP_MPI_RANK = "PP_MPI_RANK";
char * PP_MPI_SIZE = "PP_MPI_SIZE";
char * PP_MPI_HOST_PORT = "PP_MPI_HOST_PORT";
char * PP_MPI_HOSTNAME = "PP_MPI_HOSTNAME"; 
char * RANK_HOSTNAME;

int MPI_RANK; //current assigned rank
int MPI_SIZE; //size of the world
int MPI_HOST_PORT; //mpiexec default port
int MPI_HOST_FDCONN;//mpiexec conn
int MPI_RANK_PORT; //local rank default port
int MPI_RANK_SOCKET;//local rank socket
char * MPI_HOST_HOSTNAME; //mpiexe host name
int OkToAbort;
int AbortHasBeenCalled;
//Global Variables

double wtime;   

typedef struct {
   int      rank;
   char     hostname[4];   
   int      port;
   int      fdConn;   
} MPI_CommGroup;

typedef struct{
	MPI_Comm context;
	int size;
	MPI_CommGroup * group;
}MPI_Communicators;

MPI_Communicators * WorldComm;
int WorldCommSize;

MPI_MsgList_t * mpiProgressList;

//special case recieve objects, extra global info needed when calling arecive.
typedef struct
{
int CurrentlyRecieving;
int tag;
int rank;
int commIndex;
}RecieveInfo;

RecieveInfo RecieveFlags;

typedef struct
{
	MPI_Win win;
	int * winpt;
	int commIndex;		
}window;

window * windows;
int windows_size;

int connect_to_server(char *, int);	
int accept_connection(int);
int send_msg(int , char *, int );
int recv_msg(int *, char **);
void error_check(int , char *);
void MPI_Progress_Engine();
void MPI_GetConnectedCollective();
void MPI_GetCollectiveOpCompelete(MPI_Comm, int);
void MPI_RunProgressLookForTag(MPI_Comm , int , int );
void CheckIfSomeoneWantsToConnect();
void DemandConnection(int);
void MPI_GetCollectiveOpCompeleteNoDelete(MPI_Comm, int);
void MPI_RunProgressLookForTagNoDelete(MPI_Comm , int , int );
int demandMatch(int , int , int , int , int , int );
void MindLocks(int ,MPI_Win , int , int, int );
void TellMPIEXECToDupAttributes(int , int );

int MPI_Init(int * argc , char *** argv)
{
	int i;
	int rc;
	int new_socket;	
	int bytes_read;
	char * buf;	
	
	OkToAbort = 0;
	AbortHasBeenCalled = 0;
	
	RANK_HOSTNAME = (char *)malloc(4 * sizeof(char));
	
	//Get Globals
	//printf("Init\n");
	//fflush(0);
	value = getenv(PP_MPI_RANK);
	MPI_RANK = atoi(value);	
	//printf("PP_MPI_RANK: %d\n",MPI_RANK);
	
	value = getenv(PP_MPI_SIZE);
	MPI_SIZE = atoi(value);
	//printf("PP_MPI_SIZE: %d\n",MPI_SIZE);
	
	value = getenv(PP_MPI_HOST_PORT);
	MPI_HOST_PORT = atoi(value);
	//printf("PP_MPI_HOST_PORT: %d\n",MPI_HOST_PORT);	
		
	value = getenv(PP_MPI_HOSTNAME);
	MPI_HOST_HOSTNAME = (char*)malloc( (strlen(value)) * sizeof(char));
	strcpy(MPI_HOST_HOSTNAME,value);	
	//printf("MPI_HOST_HOSTNAME: %s\n",MPI_HOST_HOSTNAME);	
	
	//Get HostName of this Rank	
    RANK_HOSTNAME[4] = '\0';
	gethostname(RANK_HOSTNAME,4);
	
	
	//setup local socket
	MPI_RANK_SOCKET = socket(AF_INET, SOCK_STREAM, 0);
	error_check(MPI_RANK_SOCKET,"setup_to_accept socket");	
	
	struct sockaddr_in sin, from;
    memset(&sin,0,sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    
    rc = bind(MPI_RANK_SOCKET, (struct sockaddr *)&sin ,sizeof(sin));
    error_check(rc,"setup_to_accept bind");

	socklen_t channellen;
	channellen = sizeof(sin);
    getsockname(MPI_RANK_SOCKET, (struct sockaddr*) &sin, &channellen);    
    MPI_RANK_PORT = ntohs(sin.sin_port);
    //printf("PortNumber: %d\n",MPI_RANK_PORT);
   
    rc = listen(MPI_RANK_SOCKET, DEFAULT_BACKLOG);
    error_check(rc,"setup_to_accept listen");
	
		
	//connect to MPIEXEC
	MPI_HOST_FDCONN = connect_to_server(MPI_HOST_HOSTNAME, MPI_HOST_PORT);
	
	
	
	int size = 8+8;//rankHostname + portno
	buf = (char*)malloc(size * sizeof(char));
	sprintf(buf,"%08d%08d",MPI_RANK,MPI_RANK_PORT);
	
	//Send Phone Book to mpiexec
	send_msg(MPI_HOST_FDCONN,buf,size);	
	
	//Create WorldComm with inital MPI_COMM_WORLD
	WorldComm = (MPI_Communicators *) malloc(1 * sizeof(MPI_Communicators));
	WorldCommSize = 1;
	
	WorldComm[0].group = (MPI_CommGroup *)malloc(sizeof(MPI_CommGroup)*MPI_SIZE);
	WorldComm[0].size = MPI_SIZE;	
	WorldComm[0].context = MPI_COMM_WORLD;		
	
	//Get Phone Book.  Actually MPI_COMM_WORLD Create;	
	for(i=0;i<MPI_SIZE;i++)
	{
		bytes_read = recv_msg(&MPI_HOST_FDCONN, &buf);
		if(bytes_read != 0)
		{
			sscanf(buf,"%08d%3s%08d",&WorldComm[0].group[i].rank,&WorldComm[0].group[i].hostname,&WorldComm[0].group[i].port);
			WorldComm[0].group[i].fdConn = -1; //mark all connections as not established.
		}else
		{
			printf("Got issues getting the phone book\n");
			fflush(0);	
		}
	}
	
	//setup progress list;
	mpiProgressList = (MPI_MsgList_t *)malloc(sizeof(MPI_MsgList_t));
	CreateMPIMsgList(mpiProgressList);		
	
	//Everyone needs to at least connect to rank 0;	
	if(MPI_RANK==0)
	{		
		MPI_GetConnectedCollective();// accept all connections	
	
	}else
	{	    
		DemandConnection(0); //request to connect to Rank 0;
	}
	
	//set wtime
	struct timeval tv1;
    
    gettimeofday(&tv1, NULL);

	wtime = tv1.tv_sec + tv1.tv_usec / 1000000.0;
	
	RecieveFlags.CurrentlyRecieving = 0;
	
	windows_size = 0;
	
	//printf("End Init\n");
	//fflush(0);
	
	return MPI_SUCCESS;
}

int MPI_PRINT_COMM(MPI_Comm comm)
{
	int i;
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	//print phone book.	
	for(i=0;i<MPI_SIZE;i++)
	{
		printf("PhoneBook Entry Rank:%d Hostname:%s Port:%d\n",WorldComm[CommIndex].group[i].rank,WorldComm[CommIndex].group[i].hostname,WorldComm[CommIndex].group[i].port);
	}
	//*/
	
return MPI_SUCCESS;
}

int GetComm(MPI_Comm comm)
{
	if(comm==NULL)
		return 0;
	
	int i;
	for(i=0; i<WorldCommSize;i++)
	{
		if(WorldComm[i].context == comm)
			return i;	
	}
	
	return MPI_ERR_COMM;
}

int MPI_Comm_dup(MPI_Comm comm, MPI_Comm * newcomm)
{
	int i,x,CommIndex;
	//printf("MPI_Comm_dup\n");
	//fflush(0);
	//get the index to the communicator we want to dup.
	CommIndex = GetComm(comm);
	
	//printf("Index:%d\n",CommIndex);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	MPI_Communicators * NewWorldComm;
	
	//Expand a NewWorldComm with an entry for a new new dup comm;
	NewWorldComm = (MPI_Communicators *) realloc(WorldComm,(++WorldCommSize) * sizeof(MPI_Communicators));
	WorldComm = NewWorldComm;
	
	//printf("WorldCommSize:%d\n",WorldCommSize);
	
	//comm dup	
	WorldComm[WorldCommSize-1].size = WorldComm[CommIndex].size;	
	WorldComm[WorldCommSize-1].context = WorldComm[WorldCommSize-2].context+1; //Last Context Plus 1	
	WorldComm[WorldCommSize-1].group = (MPI_CommGroup *)malloc(sizeof(MPI_CommGroup) * NewWorldComm[WorldCommSize-1].size);
	
	for(x=0;x<WorldComm[CommIndex].size;x++)
	{
		WorldComm[WorldCommSize-1].group[x].rank = WorldComm[CommIndex].group[x].rank;
		sprintf(WorldComm[WorldCommSize-1].group[x].hostname,"%3s",WorldComm[CommIndex].group[x].hostname);
		WorldComm[WorldCommSize-1].group[x].port = WorldComm[CommIndex].group[x].port;
		WorldComm[WorldCommSize-1].group[x].fdConn = WorldComm[CommIndex].group[x].fdConn;	
	}
	
	//set the new com context for the return.
	MPI_Comm context = WorldComm[WorldCommSize-1].context;
	*newcomm = context;
	
	int commIndex = GetComm(context);
	//Com_Dup Complete, Do Collective Part
	//printf("CommDup Complete, now collective part!\n");
	//fflush(0);
	if(MPI_RANK==0)
	{
		//printf("Rank 0 in Comm Dup\n");
		//fflush(0);
		MPI_GetConnectedCollective();// accept all connections	
		//printf("everyone connected, going to get all collective ops\n");
		//fflush(0);
		MPI_GetCollectiveOpCompelete(context,MPI_COMM_DUP_TAG); //read in all comm dup msg, all accounted for.
		
		//MPIEXEC needs to dup any attributes;
		TellMPIEXECToDupAttributes(CommIndex,commIndex);
		
		//Send Rank to everyone, collective operation over.
		char * buf;
		int size = 8+8+8+8;//tag,commIndex,rank,payload
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d",MPI_COMM_DUP_TAG,commIndex,MPI_RANK,1); //send 1 for done
		for(i=0;i<WorldComm[0].size; i++)
		{
			if(WorldComm[0].group[i].rank != MPI_RANK)//no need to send msg to yourself.
			{
				send_msg(WorldComm[0].group[i].fdConn,buf,size);
			}
		}
		
	}else
	{
		char * buf;
		int size, bytes_read;
		int bytes_sent;
	    
	    //Collective part with Rank 0;
	    
		DemandConnection(0); //request to connect to Rank 0;
				
		
		//send Tag,CommIndex,Rank, No Payload	
		size = 24;//Tag+Context+Rank
		buf = (char*)malloc(size * sizeof(char));
		
		sprintf(buf,"%08d%08d%08d",MPI_COMM_DUP_TAG,commIndex,MPI_RANK); //check if MPI_Comm is %10d
		
		send_msg(WorldComm[0].group[0].fdConn,buf,size);
		//printf("msg sent to rank 0\n");
		//fflush(0);
		
		//TODO Make Progress Engine.  could have a msg from this RANK before pending.
		//ack saying its ready to go. it will just be the rank.
		//bytes_read = recv_msg(WorldComm[WorldCommSize-1].group[0].fdConn, &buf);
		
		//Get Comm Dup finished from Rank 0
		MPI_RunProgressLookForTag(context, MPI_COMM_DUP_TAG, 0);
		
		//printf("Collective Operation Complete, Good to Go from Rank:%d\n",MPI_RANK);
		//fflush(0);
			
	}
	

	
	return 	MPI_SUCCESS;
}

//will not leave untill connected to ReqRank
void DemandConnection(int ReqRank)
{
	int NotConnected = 1;
	
	while(NotConnected)
	{	
		//TODO Check for First Time Connect	
		if(WorldComm[0].group[ReqRank].fdConn==-1)
		{	
			//ask MPIEXEC for permission;
			char * buf;
			int size = 8+8+8+8;//tag,commIndex,rank,payload=ReqRank
			buf = (char *)malloc(size * sizeof(char));
			sprintf(buf,"%08d%08d%08d%08d",MPI_CONNECT_REQ,0,MPI_RANK,ReqRank); //send 1 for done
			int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
			
			//hang untill MPIEXEC reply's
			int response = 0;
			int bytes_read = 0;
			char * revBuf;
			bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
			if(bytes_read != 0)
			{
				sscanf(revBuf,"%08d",&response);
				
				//printf("Got Response:%d\n",response);
				//fflush(0);
				
				if(response == 1)
				{	
					//printf("attepting to connect to Rank 0 for first time\n");
					//fflush(0);
					WorldComm[0].group[ReqRank].fdConn = connect_to_server(WorldComm[0].group[ReqRank].hostname, WorldComm[0].group[ReqRank].port);
					//printf("connected to rank 0\n");
					//fflush(0);
					
					//tell MPIEXEC we connected;
					int size = 8+8+8+8;//tag,commIndex,rank,payload=ReqRank
					buf = (char *)realloc(buf,size * sizeof(char));
					sprintf(buf,"%08d%08d%08d%08d",MPI_CONNECTED,0,MPI_RANK,ReqRank); //send 1 for done
					int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
					
					//fist time connect
					size = 8+3;//Tag+Context+Rank
					buf = (char*)malloc(size * sizeof(char));
					sprintf(buf,"%3s%08d",RANK_HOSTNAME,MPI_RANK);
					//printf("%s\n",buf);
					
					bytes_sent = send_msg(WorldComm[0].group[ReqRank].fdConn,buf,size);	
							
					//printf("first time connect msg sent: MPI_Rank:%d Hostname:%s bytes_sent:%d\n",MPI_RANK,RANK_HOSTNAME,bytes_sent);
					//fflush(0);
					
					//Connected Now
					NotConnected = 0;		
				}else if(response==0)
				{
					//MPIEXEC Denied my req to connect to ReqRank,
					//so ReqRank is already trying to connect to this Rank.
					CheckIfSomeoneWantsToConnect();	
				}else if(response==MPI_ABORT)
				{
					//DO Abort
					OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
					MPI_Abort(MPI_COMM_WORLD,-911);
				}
			}else
			{
				printf("No Longer Connected to MPIEXEC\n");
				fflush(0);	
			}
		}
		else
		{
			//Already Connected;
			NotConnected = 0;	
		}
	}
	
}

void MPI_RunProgressLookForTag(MPI_Comm comm, int Tag, int fromRank)
{
	int commIndex = GetComm(comm);
	int MsgFound =0;
	while(MsgFound == 0)
	{
		//spin progress engin once.
		MPI_Progress_Engine();	
		//traverse list		
		link_t * link;
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	if(link->mpiMsg->tag==Tag && link->mpiMsg->commIndex == commIndex && link->mpiMsg->rank == fromRank)
        	{
        		MsgFound = 1;
        		DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
        		//printf("pe size:%d\n",mpiProgressList->size);
				//fflush(0);
        		break; //break for
        	}
    	}
	}
}


void MPI_GetCollectiveOpCompelete(MPI_Comm comm, int Tag)
{
	//printf("MPI_GetCollectiveOpCompelete\n");
	//fflush(0);
	int i;
	int commIndex = GetComm(comm);
	int CollectiveOpComplete = 0;
	
	//looking a message from all ranks in comm saying collective op is complete.
	
	int count = 0;
	int * reportedInRanks;
		
	reportedInRanks = (int*)malloc((WorldComm[commIndex].size) * sizeof(int));
	reportedInRanks[MPI_RANK] = 1; //Rank 0 is always running this code it must already be reported in.
	count++;
	
	while(CollectiveOpComplete==0)
	{
		//printf("before pe\n");
		//fflush(0);
		
		//run progress engine.
		MPI_Progress_Engine();
		//printf("after pe\n");
		//fflush(0);
		
		//traverse list	
		link_t * link;
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	if(link->mpiMsg->tag==Tag && link->mpiMsg->commIndex == commIndex)
        	{
        		count++;
        		reportedInRanks[link->mpiMsg->rank] = 1;
        		DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
        	}
    	}
    	
    	if(count == WorldComm[commIndex].size)
    	{
    		//TODO ensure all have a 1;
    		//printf("Collective Op Complete, everyone as reported in\n");
    		//fflush(0);
    		int AllReported = 1;
    		for(i=0;i<WorldComm[commIndex].size;i++)
    		{
    			if(reportedInRanks[i] != 1)
    			{
    				printf("Some How We had a Dup Collective Ops Msg From Someone, or Someone Lied about Rank, Bad Bad!\n");
    				fflush(0);
    				AllReported=0;
    			}
			}
			
			if(AllReported==1)
			{
				//printf("pe size:%d\n",mpiProgressList->size);
				//fflush(0);
    			CollectiveOpComplete = 1;
			}	
    	}
    	//count = 1; //deleting from PE list don't reset;
	
	}
	//everyone as reported in.
	
}

//ensures everyone is connected to specified comm.
void MPI_GetConnectedCollective()
{
	int i;
	int everyoneConnected = 0;
	int notConnectedStill = 0;
	int CommIndex = 0;
	
	while(everyoneConnected == 0)
	{
		//check to see if everyone is connected
		for(i=0;i<WorldComm[CommIndex].size;i++)
		{
			
			if(WorldComm[CommIndex].group[i].fdConn < 0)
			{
				//printf("rank:%d fdconn:%d CurrRank:%d\n",WorldComm[CommIndex].group[i].rank,WorldComm[CommIndex].group[i].fdConn,MPI_RANK);
				//fflush(0);
				if(WorldComm[CommIndex].group[i].rank != MPI_RANK) //skip current rank, it will never connect to itself.
				{
					//printf("still not connected\n");
					//fflush(0);
					notConnectedStill = 1;
					break;
				}
			}	
		}
		
		//if everyone is not connected, wait for a new connection.
		if(notConnectedStill==1)
		{
			CheckIfSomeoneWantsToConnect();
			
			notConnectedStill=0;//check again
		}
		else
		{
			//printf("Everyone connected\n");
			//fflush(0);
			everyoneConnected=1; //exit loop	
		}			
	}
	//everyone is connected
		
}

//one spin
void MPI_Progress_Engine()
{	
		int i,x,rc;
		struct timeval tv;
		fd_set readfds;
		int commIndex = 0;
		int done = 0;
		int somethingToSelect = 0;
		int prevsize = mpiProgressList->size;
		
		while(done==0)
		{	
			somethingToSelect = 0;
			
			//printf("before\n");
			//fflush(0);
			CheckIfSomeoneWantsToConnect();
				
			//Select off all connections.
		
			FD_ZERO( &readfds );
			
			for(i=0;i<WorldComm[commIndex].size;i++)
			{
					if(WorldComm[commIndex].group[i].fdConn != -1)
					{
						somethingToSelect = 1;
						int fdconn = WorldComm[commIndex].group[i].fdConn;
						//printf("fdConn:%d rank:%d\n",WorldComm[x].group[i].fdConn,WorldComm[x].group[i].rank);
						//fflush(0);
						FD_SET( fdconn, &readfds );
					}
				
			}
			
			if(somethingToSelect == 1)
			{
			
			
			//set time outs for select
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			
			//select statment
			rc = select( FD_SETSIZE, &readfds, NULL, NULL, &tv );

			if (rc == 0)
			{
				
			}else if (rc == -1  &&  errno == EINTR)
			{
			   
			}else if ( rc < 0 )
			{
				//done = 1;
				//printf("select failed\n");
			}else{
				//something on the select
				//printf("Something on Select\n");
				//fflush(0);
				//see if there is something on the lastRankRead file desc.
				for(i=0;i<WorldComm[commIndex].size;i++)
				{
						
						if(WorldComm[commIndex].group[i].fdConn != -1)
						{
							//printf("fdConn:%d rank:%d\n",WorldComm[x].group[i].fdConn,WorldComm[x].group[i].rank);
							//fflush(0);
							if (FD_ISSET(WorldComm[commIndex].group[i].fdConn,&readfds))  
							{	
								//printf("Hupla!\n");
								//fflush(0);
								
								char * buf;
								int bytes_read = recv_msg(&WorldComm[commIndex].group[i].fdConn, &buf);
								if(bytes_read != 0)
								{
									//printf("Got Msg:%s\n",buf);
									//fflush(0);
									//add to progress list, must be in Tag,MPI_COMM,Rank
									link_t * link;	
									link = (link_t *)AddToMPIMsgList(mpiProgressList,buf,bytes_read);
									//printf("Tag:%d\n",link->mpiMsg->tag);	
									
									//TODO Make this into a function maybe
									switch(link->mpiMsg->tag)
									{
										case MPI_ABORT:
											if(AbortHasBeenCalled == 0)
											{
												OkToAbort = 1; //if i got this from my fdConns then Rank Zero has been notified of the abort.
												MPI_Abort(MPI_COMM_WORLD,-911);
											}
										break;
										case MPI_RSEND:
										;
												//special case rsend
												int rSendResponse = 0;
											    if(RecieveFlags.CurrentlyRecieving == 1)
											    {
											    	//get special sause.
											    	int embeddedTag;
											    	sscanf(link->mpiMsg->payLoad,"%08d",&embeddedTag);
											    	int isAMatch = demandMatch(embeddedTag, RecieveFlags.tag, link->mpiMsg->commIndex, RecieveFlags.commIndex, link->mpiMsg->rank, RecieveFlags.rank);
											    	
											    	if( isAMatch == 1)
											    	{
											    		rSendResponse = 1; 
											    		
											    		
											    	}else
											    	{
											    	   rSendResponse = 0;	
											    	}
											    	
											    }else
											    {
											    	   rSendResponse = 0;
											    }
											    
											    
											    char * sendBuf;
											    int size = 8+8+8+8;//tag,commIndex,rank,payload(0 or 1)
											    sendBuf = (char *)malloc(size * sizeof(char));
											    sprintf(sendBuf,"%08d%08d%08d%08d",MPI_RSEND_RSP,link->mpiMsg->commIndex,MPI_RANK,rSendResponse);
											    send_msg(WorldComm[commIndex].group[i].fdConn,sendBuf,size);
											    //regardless delete the link here
											    DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
											    free(sendBuf);
											    
											    if(rSendResponse==1)
											    {
											    	//I just told the guy i would participate in handshake, i must now receive his msg.
											    	char * recvbuf;
													int bytes_read = recv_msg(&WorldComm[commIndex].group[i].fdConn, &recvbuf);
													if(bytes_read != 0)
													{
											    		link_t * newlink;	
														newlink = (link_t *)AddToMPIMsgList(mpiProgressList,recvbuf,bytes_read);
													}else
													{
														//should never happen
														printf("things have gone horribly wrong on rsend request, trying to recieve\n");
													}
													free(recvbuf);	
											    }
											    
										break;
										case MPI_WIN_PUT: //TODO Lock spin
											;
											//put into windows
											int win,disp,count,value,z;											
											sscanf(link->mpiMsg->payLoad,"%08d%08d%08d",&win,&disp,&count);
											//printf("win:%d disp:%d count:%d\n",win,disp,count);fflush(0);
											for(z=0;z<count;z++)
											{
												sscanf(((link->mpiMsg->payLoad+24)+(z*8)),"%08d",&value);
												//printf("Put:%d\n",value);
												windows[win].winpt[disp+z] = value;												
											}
											
											//send done back
											
											
											char * sBuff;	
											size = 8+8+8;										
											sBuff = (char *)malloc(size * sizeof(char));
											sprintf(sBuff,"%08d%08d%08d",MPI_WIN_PUT_RSP,windows[win].commIndex,MPI_RANK);
											
											send_msg(WorldComm[commIndex].group[i].fdConn,sBuff,size);
											//printf("Put Done %s\n",sBuff);fflush(0);
											free(sBuff);
											
											    	
										break;
										case MPI_WIN_GET: //TODO Lock Spin
										;
											sscanf(link->mpiMsg->payLoad,"%08d%08d%08d",&win,&disp,&count);
											//printf("win:%d disp:%d count:%d\n",win,disp,count);fflush(0);
											
											char * getBuff;	
											size = 8+8+8+(8*count);										
											getBuff = (char *)malloc(size * sizeof(char));
											sprintf(getBuff,"%08d%08d%08d",MPI_WIN_GET_RSP,windows[win].commIndex,MPI_RANK);
											for(z=0;z<count;z++)
											{
												int tempValue = 0;
												tempValue = windows[win].winpt[disp+z];
												sprintf(getBuff+24+(8*z),"%08d",tempValue);
												//printf("getting:%d\n",tempValue);
											}
											//printf("sbuf:%s\n",getBuff);fflush(0);
											//sleep(1);
											//send
											send_msg(WorldComm[commIndex].group[i].fdConn,getBuff,size);
											//printf("Get Done:%s\n",getBuff);
											free(getBuff);
										
										break;
										
										default:
											//Do Nothing For Now.
										break;	
									}
								}else
								{
									//the other guy hung up on me...
									//I'll have to request again if i need to talk to him.	
								}
																
							}
						}				
				}			
			}
			
			
		}//check for something to select
			
			if(prevsize == mpiProgressList->size)
			{
				//printf("Nothing Found\n");
				//fflush(0);
				//nothing found, leave
				done = 1;	
			}else{
				prevsize = mpiProgressList->size;
			}
			
		}	//end while
}


void CheckIfSomeoneWantsToConnect()
{
	int i,rc;
	struct timeval tv;
	fd_set readfds;
	int commIndex = 0;
			
	int done = 0;
	
	int prev = 0;
	int newConn = 0;
	int someoneTried = 0;
			
	while(done==0)
	{
		
			//Select off all connections.
	
			FD_ZERO( &readfds );
			FD_SET( MPI_RANK_SOCKET, &readfds );
			
			//set time outs for select
        	tv.tv_sec = 0;
        	tv.tv_usec = 0;
			
			//select statment
        	rc = select( FD_SETSIZE, &readfds, NULL, NULL, &tv );

			if (rc == 0)
			{
				
			}else if (rc == -1  &&  errno == EINTR)
			{
			   
			}else if ( rc < 0 )
			{
				//done = 1;
				//printf("select failed\n");
			}else
			{
				if (FD_ISSET(MPI_RANK_SOCKET,&readfds))  
				{			
						int new_socket = accept_connection(MPI_RANK_SOCKET);
						if(new_socket > 0)
						{
							char * buf;
							int bytes_read = recv_msg(&new_socket, &buf);
							if(bytes_read != 0)
							{
								newConn++;
								//printf("size:%d buf:%s\n",bytes_read,buf);
								//fflush(0);
								int remoteRank;
								char remoteHost[3];
								
								
								
								//sscanf(buf,"%08d%3s%08d",&WorldComm[0].group[i].rank,&WorldComm[0].group[i].hostname,&WorldComm[0].group[i].port);
								sscanf(buf,"%3s%08d",&remoteHost,&remoteRank);
								
								//printf("connected host:%s rank:%d\n",remoteHost,remoteRank);
								//fflush(0);
								
								//populate the comm with the fd
								for(i=0;i<WorldComm[commIndex].size;i++)
								{
									//printf("host:%s remotehost:%s rank:%d remoteRank:%d\n",WorldComm[CommIndex].group[i].hostname,remoteHost,WorldComm[CommIndex].group[i].rank,remoteRank);
									//fflush(0);
									int cmp = strcmp(WorldComm[commIndex].group[i].hostname,remoteHost);
									//printf("cmp:%d\n",cmp);
									//fflush(0);
									if(WorldComm[commIndex].group[i].rank==remoteRank)
									{	
										//TODO Maybe: Check if fdConn == -1 before assign.
										//if -1 assign if not, low rank wins connection.
														
										WorldComm[commIndex].group[i].fdConn = new_socket;
										
										//if(MPI_RANK == 0)
										//{
										//	printf("Index:%d rank:%d fdconn:%d\n",commIndex,i,WorldComm[commIndex].group[i].fdConn);
										//	fflush(0);
										//}
									}
										
								}
							}else
							{
								printf("this should never happen\n");
								fflush(0);	
							}
						}
						else
						{
							//someone tried to get connected an failed.
							someoneTried = 1;
						}
					}	
				
				}
				
				if(prev == newConn)
				{
					if(someoneTried==0)
					{
						done = 1; //on one's calling us,or tried to call
					}else
					{
						someoneTried = 0; //reset and look again.
						prev = newConn;
					}				
					
				}else
				{
					prev = newConn;	
				}
		}
}

int MPI_Finalize()
{
	//printf("MPI_Finalize\n");
	//fflush(0);
	//TODO loop to close all conns int comms.
	MPI_Barrier(MPI_COMM_WORLD); //ensure everyone made it to finalize
	
	
	close(MPI_RANK_SOCKET);
	
	//Tell MPIEXEC Goodbye
	char * buf;
	int size = 8+8+8;//tag,commIndex,rank,
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d%08d%08d",MPI_FINAL_GOODBY,0,MPI_RANK); //send 1 for done
	send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
	int bytes_read = 0;
	char * revBuf;
	bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
	
	//dosn't matter what i get back i am quiting.	
		
	close(MPI_HOST_FDCONN);
	return 	MPI_SUCCESS;	
}

connect_to_server(char *hostname, int port)	
{
	int rc, client_socket;
	int optval = 1,optlen;
	struct sockaddr_in listener;
	struct hostent *hp;
	
	hp = gethostbyname(hostname);
	if (hp == NULL)
	{
		printf("connect_to_server: gethostbyname %s: %s -- exiting\n",hostname, strerror(errno));
		exit(99);
	}

	bzero((void *)&listener, sizeof(listener));
	bcopy((void *)hp->h_addr, (void *)&listener.sin_addr, hp->h_length);
	listener.sin_family = hp->h_addrtype;
	listener.sin_port = htons(port);
	
	
	rc = -1;
	while(rc < 0)
	{
		client_socket = socket(AF_INET, SOCK_STREAM, 0);
			error_check(client_socket, "net_connect_to_server socket");

		rc = connect(client_socket,(struct sockaddr *) &listener, sizeof(listener));
			error_check(rc, "net_connect_to_server connect");
		
		setsockopt(client_socket,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
	}
    return(client_socket);
}

void error_check(int val, char *str)	
{
    if (val < 0)
    {
		printf("%s :%d: %s\n", str, val, strerror(errno));
		fflush(0);
		//exit(1); //try again.
    }
}

int send_msg(int fd, char *buf, int size)	
{
	//printf("size:%d\n",size);
	char * sendbuf;
	
	
	sendbuf = (char*)malloc((size+8)*sizeof(char));
		
	sprintf(sendbuf,"%08d%s",size,buf);
	//printf("sendbuf:%s\n",sendbuf);
	
	size += 8; //in crease size for length to send;
	
		
    int bytes_sent = 0;
    
    while(bytes_sent != (size))
    {
    	char * sendbuffer;
    	int newsize = (size - bytes_sent);
    	//printf("newsize:%d\n",newsize);
    	sendbuffer = (char*)malloc(newsize * sizeof(char));
    	//printf("newbufsize:%d\n",strlen(sendbuffer));
    	strncpy(sendbuffer,sendbuf+bytes_sent,newsize);
    	//printf("sendbuf:%s\n",sendbuffer);    	
    	bytes_sent += write(fd, sendbuffer, newsize);
    	//bytes_sent = size;    		
    }
    //printf("sent:%s bytes_sent:%d\n",sendbuf,bytes_sent);  
    //*/
    return bytes_sent; 
    //error_check(n, "send_msg write");
}

int recv_msg(int * fd, char ** retbuf)
{
    int bytes_read, msg_size;
    char * smsg_size;
    char * smsg;
    char * buf;    
    int i,rc;
	struct timeval tv;
	fd_set Rankfds;
	int done = 0;
	int firstTimeRead = 1;
	
	while(done==0)
	{
		FD_ZERO( &Rankfds );
		FD_SET( *fd, &Rankfds );
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
			smsg_size = (char*)malloc(8*sizeof(char));
			smsg = (char*)malloc(8*sizeof(char));
			bytes_read = 0;

			while(bytes_read!=8)
			{
				int lastread = bytes_read;    	
				bytes_read += read(*fd, smsg, 8-lastread);
				if(firstTimeRead==1 && bytes_read==0)
				{
					//guy on the other end is gone and is not comming back
					close(*fd);
					*fd = -1; //mark as closed 
					return 0;
					
				}else
				{
					firstTimeRead = 0;
					strncpy(smsg_size+lastread,smsg,bytes_read);  
				}  	    	
			}
			
			sscanf(smsg_size,"%08d",&msg_size);
			//printf("size:%d\n",msg_size);
			
			
			bytes_read = 0;
			buf = (char *)malloc(msg_size * sizeof(char));
			smsg = (char *)malloc(msg_size * sizeof(char));
			while(bytes_read!=msg_size)
			{
				int lastread = bytes_read;    	
				bytes_read += read(*fd, smsg, msg_size-lastread);
				//printf("bytesread:%d, lastread:%d\n",bytes_read,lastread);
				strncpy(buf+lastread,smsg,bytes_read);
				//printf("buf:%s\n",buf);
			}
			
			*retbuf = buf;
			done = 1;					
		}	
	}
	
    return bytes_read;
}

int accept_connection(int default_socket)	
{
    int fromlen, new_socket, gotit;
    int optval = 1,optlen;
    struct sockaddr_in from;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
		new_socket = accept(default_socket, (struct sockaddr *)&from, &fromlen);
		if (new_socket == -1)
		{
			/* Did we get interrupted? If so, try again */
			if (errno == EINTR)
			continue;
			else
			error_check(new_socket, "accept_connection accept");
		}
		else
		{
			//printf("socket:%d\n",new_socket);
			gotit = 1;
		}
    }
    setsockopt(new_socket,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
    if(new_socket < 0)
    {
    	printf("Got issues\n");
    	fflush(0);
    }
    
    return(new_socket);
}

int MPI_Comm_rank(MPI_Comm comm, int * rank)
{
	int i;
	int CommIndex =  GetComm(comm);
	
	//CheckIfSomeoneWantsToConnect();
	MPI_Progress_Engine();
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	
	for(i=0;i<WorldComm[CommIndex].size;i++)
	{
		if(WorldComm[CommIndex].group[i].port == MPI_RANK_PORT)	
		{
			*rank = WorldComm[CommIndex].group[i].rank;	
		}
	}
	
	return 	MPI_SUCCESS;
}

int MPI_Comm_size(MPI_Comm comm, int * size)
{
	int i;
	int CommIndex =  GetComm(comm);
	
	//CheckIfSomeoneWantsToConnect();
	MPI_Progress_Engine();
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	
	 *size = WorldComm[CommIndex].size;
	
	
	return 	MPI_SUCCESS;
		
}

int MPI_Abort(MPI_Comm comm, int code)
{
	AbortHasBeenCalled = 1;
	int i;
	//printf("MPI_Abort\n");
	//fflush(0);
	//TODO loop to close all conns int comms.
	
	//Tell MPIEXEC I am Aborting, no more connections allowed.
	char * buf;
	int size = 8+8+8;//tag,commIndex,rank,
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d%08d%08d",MPI_ABORT,0,MPI_RANK); //send 1 for done
	send_msg(MPI_HOST_FDCONN,buf,size);
	close(MPI_HOST_FDCONN);
	
	int commIndex = GetComm(MPI_COMM_WORLD);
	
	if(MPI_RANK == 0)
	{
		for(i = 0; i < WorldComm[0].size; i++)
		{
			//printf("Count: %d fdConn: %d\n",i,WorldComm[0].group[i].fdConn );
			//fflush(0);
			
			if(WorldComm[0].group[i].fdConn != -1)
			{
				if(WorldComm[0].group[i].rank != MPI_RANK)//no need to send msg to yourself.
				{					
					size = 24;//Tag+Context+Rank
					buf = (char*)realloc(buf,size * sizeof(char));
					sprintf(buf,"%08d%08d%08d",MPI_ABORT,commIndex,MPI_RANK);
					send_msg(WorldComm[0].group[i].fdConn,buf,size);
					//printf("Abort Sent to Rank %d\n",i);
					//fflush(0);	
				}			
			}			
		}			
	}else
	{		
		if(OkToAbort == 0)
		{
			//Just in case tell Rank 0 we want to Abort.
			size = 24;//Tag+Context+Rank
			buf = (char*)realloc(buf,size * sizeof(char));
			sprintf(buf,"%08d%08d%08d",MPI_ABORT,commIndex,MPI_RANK);
			send_msg(WorldComm[0].group[0].fdConn,buf,size);
						
			MPI_RunProgressLookForTag(MPI_COMM_WORLD, MPI_ABORT, 0);
			//CheckIfSomeoneWantsToConnect();
			OkToAbort = 1;
		}			
	}
	
	//Everyone needs to serve incase someone is hung to me in a connect
	CheckIfSomeoneWantsToConnect();
		
	//Everyone close socket
	close(MPI_RANK_SOCKET);
	
	//Everyone Exit
	exit(0);
}

int MPI_Barrier(MPI_Comm comm )
{
	int i,x,CommIndex;
	
	CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	if(MPI_RANK==0)
	{		
		MPI_GetConnectedCollective();// accept all connections	
		
		MPI_GetCollectiveOpCompelete(comm,MPI_BARRIER_TAG); //read in all comm dup msg, all accounted for.
		
		//Send Rank to everyone, collective operation over.
		char * buf;
		int size = 8+8+8+8;//tag,commIndex,rank,payload
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d",MPI_BARRIER_TAG,CommIndex,MPI_RANK,1); //send 1 for done
		for(i=0;i<WorldComm[0].size; i++)
		{
			if(WorldComm[0].group[i].rank != MPI_RANK)//no need to send msg to yourself.
			{
				send_msg(WorldComm[0].group[i].fdConn,buf,size);
			}
		}
		
	}else
	{
		char * buf;
		int size, bytes_read;
		int bytes_sent;
	    
	    //Collective part with Rank 0;
	    
		DemandConnection(0); //request to connect to Rank 0;
				
		
		//send Tag,CommIndex,Rank, No Payload	
		size = 24;//Tag+Context+Rank
		buf = (char*)malloc(size * sizeof(char));
		
		sprintf(buf,"%08d%08d%08d",MPI_BARRIER_TAG,CommIndex,MPI_RANK); //check if MPI_Comm is %10d
		
		send_msg(WorldComm[0].group[0].fdConn,buf,size);
				
		//Get Comm Dup finished from Rank 0
		MPI_RunProgressLookForTag(comm, MPI_BARRIER_TAG, 0);
	}
	
	return MPI_SUCCESS;
}

int MPI_Attr_put(MPI_Comm comm, int keyval, void * attr_value)
{
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	int * value = (int *)attr_value;	
	
	//printf("key:%d. value:%d\n",keyval,*value);
	//fflush(0);
	
	char * buf;
	int size = 8+8+8+8+8;//tag,commIndex,rank,keyval,attr_value
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d%08d%08d%08d%08d",MPI_ATTR_PUT,CommIndex,MPI_RANK,keyval,*value); //send 1 for done
	send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
	
	char * revBuf;
	int bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
	
	if(bytes_read != 0)
	{
		int response;
		sscanf(revBuf,"%08d",&response);
				
		//printf("Got Response:%d\n",response);
		//fflush(0);
				
		if(response == 1)
		{	free(revBuf);
			return MPI_SUCCESS;				
	    }else if(response==0)
		{
			//MPIEXEC did store it, what a jerk.. should happen ever.
			printf("MPIEXEC is a jerk\n");
			fflush(0);		
		}else if(response==MPI_ABORT)
		{
				//DO Abort
				free(revBuf);
				OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
				MPI_Abort(MPI_COMM_WORLD,-911);
		}
	}else
	{
		free(revBuf);
		//MPIEXEC hung up on me..
		return MPI_ERR_COMM;
	}	
	
}


int MPI_Attr_get(MPI_Comm comm, int key, void * attr_value, int * flag)
{
	//CheckIfSomeoneWantsToConnect();
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	char * buf;
	int size = 8+8+8+8;//tag,commIndex,rank,keyval
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d%08d%08d%08d",MPI_ATTR_GET,CommIndex,MPI_RANK,key); //send 1 for done
	send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
	
	char * revBuf;
	int bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
	
	if(bytes_read != 0)
	{
		int response;
		sscanf(revBuf,"%08d",&response);
				
		//printf("Got Response:%d\n",response);
		//fflush(0);
				
		if(response == 1)
		{	
			int value;
			sscanf(revBuf,"%08d%08d",&response,&value);
			//printf("response: %d value:%d\n",response,value);fflush(0);
			if(flag!=NULL)
				*flag = 1;
			int * retValue;
			retValue = ((int *)attr_value);
			*retValue = value;
			//printf("response: %d value:%d\n",response,retValue);fflush(0);
			free(revBuf);
			return MPI_SUCCESS;				
	    }else if(response==0)
		{	
			*flag = 0;		
			free(revBuf);
			//keyvalue not found;
			return MPI_ERR_OTHER;	
		}else if(response==MPI_ABORT)
		{
				//DO Abort
				free(revBuf);
				OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
				MPI_Abort(MPI_COMM_WORLD,-911);
		}
	}else
	{
		*flag = 0;
		free(revBuf);
		//MPIEXEC hung up on me..
		return MPI_ERR_COMM;
	}
}

double MPI_Wtime()
{	
    struct timeval tv1;
    double td1;

    gettimeofday(&tv1, NULL);    

    td1 = tv1.tv_sec + tv1.tv_usec / 1000000.0;
        
	return td1 - wtime;
}

double MPI_Wtick(void)
{
	struct timespec res;
    int rc;

    rc = clock_getres( CLOCK_REALTIME, &res );
    if (!rc) 
		return res.tv_sec + 1.0e-6 * res.tv_nsec;
}


int MPI_Gather(void* sendbuf, int sendcnt, MPI_Datatype sendtype, void* recvbuf, int recvcnt, MPI_Datatype recvtype, int root, MPI_Comm comm)
{
	
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	char * buf;
    int i, size, offset;
    link_t * link;
	if(root == MPI_RANK)
	{
		//printf("Begin Gather\n");
		//fflush(0);
		
		//Begin Gatherer
		MPI_GetConnectedCollective();// accept all connections	
	
		//printf("Everyone's Connected\n");
		//fflush(0);
	
		//Roll a new Gather
		MPI_GetCollectiveOpCompeleteNoDelete(comm, MPI_GATHER_TAG);
		//Everyone has reported in and results are on the mpiProgressList
		
		//printf("All Msg here\n");
		//fflush(0);
		
		//traverse list		
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	if(link->mpiMsg->tag==MPI_GATHER_TAG && link->mpiMsg->commIndex == CommIndex)
        	{   
        		//void* recvbuf, int recvcnt
        		switch(recvtype)
				{
					case MPI_CHAR:	
						//printf("payload: %s\n",link->mpiMsg->payLoad);		
						//fflush(0);	
						offset = (recvcnt * link->mpiMsg->rank);
						char * charBuf;
						charBuf = ((char *)recvbuf); //type cast
						for(i=0;i<recvcnt;i++)
						{	
							//printf("%c\n",link->mpiMsg->payLoad[i]);
							charBuf[(offset+i)] = link->mpiMsg->payLoad[i];	
						}
						
						//strncpy(charBuf+offset, link->mpiMsg->payLoad, recvcnt);
						//printf("after str Copy: %s\n",charBuf);
						//fflush(0);							
					break;
					case MPI_INT:
						offset = (recvcnt * link->mpiMsg->rank);
						//printf("payload:%s rank:%d offset:%d\n",link->mpiMsg->payLoad,link->mpiMsg->rank,offset);
						//fflush(0);
						int * intBuf;
						intBuf = ((int *)recvbuf);
						for(i=0;i<recvcnt;i++)
						{
							int value;
							sscanf(link->mpiMsg->payLoad+(8*i),"%08d",&value);
							//printf("payload:%s payload offset: %d value:%d\n",link->mpiMsg->payLoad+(8*i),(8*i),value);
							
							intBuf[(offset+i)] = value;	
						}					
					break;
					default:
						return MPI_ERR_TYPE;
					break;
				} 				     		
        	}
    	}
    	
    	//printf("All Msg added to receve except mine\n");
		//fflush(0);
    	
    	//add self to recvbuf    	
    	switch(recvtype)
		{
			case MPI_CHAR:			
				offset = (recvcnt * MPI_RANK);				
				char * charBuf;
				charBuf = ((char *)recvbuf); //type cast
				char * charArrayBuf =  ((char *)sendbuf);
				for(i=0;i<recvcnt;i++)
				{
					//printf("%c\n",charArrayBuf[i]);
					charBuf[(offset+i)] = charArrayBuf[i];	
				}									
			break;
			case MPI_INT:
				offset = (recvcnt * MPI_RANK);
				//printf("offset: %d recvcnt: %d MPI_RANK\n",offset,recvcnt,MPI_RANK);
				//fflush(0);
				
				int * intBuf;
				intBuf = ((int *)recvbuf);
				int * intArrayBuf = ((int *)sendbuf);
				for(i=0;i<recvcnt;i++)
				{
					intBuf[(offset+i)] = intArrayBuf[i];	
				}					
			break;
			default:
				return MPI_ERR_TYPE;
			break;
		}
		
		//printf("Added My Msg added to receve\n");
		//fflush(0);
		
		//Delete all gather msg from list
    	for (link = mpiProgressList->first; link; link = link->next) 
		{	
        	if(link->mpiMsg->tag==MPI_GATHER_TAG && link->mpiMsg->commIndex == CommIndex)
        	{ 
    			DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
			}
		}
		//EndGather
		
		//printf("Clean Up, Complete, Sennding Gather Over Msg to All participating Ranks\n");
		//fflush(0);
		
		//Send Rank to everyone, collective operation over.
		size = 8+8+8+8;//tag,commIndex,rank,payload
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d",MPI_GATHER_TAG,CommIndex,MPI_RANK,1); //send 1 for done
		for(i=0;i<WorldComm[0].size; i++)
		{
			if(WorldComm[0].group[i].rank != MPI_RANK)//no need to send msg to yourself.
			{
				send_msg(WorldComm[0].group[i].fdConn,buf,size);
			}
		}
		free(buf);
	}else
	{
		//Senders
		
		DemandConnection(root);
				
		switch(sendtype)
		{
			case MPI_CHAR:			
				size = 8+8+8+sendcnt;//tag,commIndex,rank,payload(char)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",MPI_GATHER_TAG,CommIndex,MPI_RANK);
				char * charArray;
				charArray = ((char *)sendbuf);
				strncpy(buf+24, charArray, sendcnt);
				send_msg(WorldComm[0].group[root].fdConn,buf,size);
				free(buf);
			break;
			case MPI_INT:
				size = 8+8+8+(8 * sendcnt);//tag,commIndex,rank,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",MPI_GATHER_TAG,CommIndex,MPI_RANK);
				int * intArray;				
				intArray = ((int *)sendbuf);
				for(i=0;i<sendcnt;i++)
				{
					sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
				}			
				send_msg(WorldComm[0].group[root].fdConn,buf,size);
				free(buf);
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
		}
		
		//look for msg from root
		MPI_RunProgressLookForTag(comm, MPI_GATHER_TAG, root);				
	}
	return MPI_SUCCESS;
}


void MPI_GetCollectiveOpCompeleteNoDelete(MPI_Comm comm, int Tag)
{
	int i,count;
	int commIndex = GetComm(comm);
	int CollectiveOpComplete = 0;
	
	//looking a message from all ranks in comm saying collective op is complete.
	
	int * reportedInRanks;
		
	reportedInRanks = (int*)malloc((WorldComm[commIndex].size) * sizeof(int));
	
	//mark all as not reported in
	for(i=0;i<WorldComm[commIndex].size;i++)
    {
    	reportedInRanks[i] = -1;
	}
	
	count = 1;
	reportedInRanks[MPI_RANK] = 1; // Calling Rank is already reported in.
	
	
	while(CollectiveOpComplete==0)
	{
		//printf("Before PE\n");
		//fflush(0);
		
		MPI_Progress_Engine();	
		
		//printf("After PE\n");
		//fflush(0);	
		
		//traverse list	
		link_t * link;
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	if(link->mpiMsg->tag==Tag && link->mpiMsg->commIndex == commIndex)
        	{      
        		count++;  		
        		reportedInRanks[link->mpiMsg->rank] = 1;        		
        	}
    	}   
    	
    	//printf("Count: %d Size: %d\n",count,WorldComm[commIndex].size); 	
    	
    	if(count == WorldComm[commIndex].size)
    	{
    		//TODO ensure all have a 1;
    		//printf("Collective Op Complete, everyone as reported in\n");
    		//fflush(0);
    		int AllReported = 1;
    		for(i=0;i<WorldComm[commIndex].size;i++)
    		{
    			if(reportedInRanks[i] != 1)
    			{
    				printf("Some How We had a Dup Collective Ops Msg From Someone, or Someone Lied about Rank, Bad Bad!\n");
    				fflush(0);
    				AllReported=0;
    			}
			}
			
			if(AllReported==1)
			{
				//printf("pe size:%d\n",mpiProgressList->size);
				//fflush(0);
    			CollectiveOpComplete = 1;
			}	
    	}
    	count = 1; //added because we are not deleting from list, 1 because calling rank is always present	
	}
	//everyone as reported in.
	
}

int MPI_Bcast(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm )
{
	int i,size;
	char * buf;
	link_t * link;
	
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	if(root == MPI_RANK)
	{
		//Send To All	
		MPI_GetConnectedCollective();// accept all connections
		//printf("everyone is here\n");
		//fflush(0);
		
		switch(datatype)
		{
			case MPI_CHAR:			
				size = 8+8+8+count;//tag,commIndex,rank,payload(char)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",MPI_BCAST,CommIndex,MPI_RANK);
				char * charArray;
				charArray = ((char *)buffer);
				strncpy(buf+24, charArray, count);
				
				
			break;
			case MPI_INT:
				size = 8+8+8+(8 * count);//tag,commIndex,rank,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",MPI_BCAST,CommIndex,MPI_RANK);
				int * intArray;				
				intArray = ((int *)buffer);
				for(i=0;i<count;i++)
				{
					sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
				}			
				
				
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
		}
		
		//printf("Sending:%s\n",buf);
		
		for(i=0;i<WorldComm[CommIndex].size;i++)
   		{
    		//Send to everyone but self.
    		if(i != MPI_RANK)
    		{
    			send_msg(WorldComm[0].group[i].fdConn,buf,size);
			}
		}
		//printf("Done with BCast\n");
		//free(buf);
	}else
	{
		DemandConnection(root);
		//printf("Got Connected!\n");
		//fflush(0);
		MPI_RunProgressLookForTagNoDelete(comm,MPI_BCAST,root);
		//printf("Got BCast\n");
		//fflush(0);
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	if(link->mpiMsg->tag==MPI_BCAST && link->mpiMsg->commIndex == CommIndex)
        	{   
        		//void* recvbuf, int recvcnt
        		switch(datatype)
				{
					case MPI_CHAR:	
					;
						//printf("payload: %s\n",link->mpiMsg->payLoad);		
						//fflush(0);	
						char * charBuf;
						charBuf = ((char *)buffer); //type cast
						for(i=0;i<count;i++)
						{	
							//printf("%c\n",link->mpiMsg->payLoad[i]);
							charBuf[(i)] = link->mpiMsg->payLoad[i];	
						}
						
						//strncpy(charBuf+offset, link->mpiMsg->payLoad, recvcnt);
						//printf("after str Copy: %s\n",charBuf);
						//fflush(0);							
					break;
					case MPI_INT:
					;
						//printf("payload:%s rank:%d offset:%d\n",link->mpiMsg->payLoad,link->mpiMsg->rank,offset);
						//fflush(0);
						int * intBuf;
						intBuf = ((int *)buffer);
						for(i=0;i<count;i++)
						{
							int value;
							sscanf(link->mpiMsg->payLoad+(8*i),"%08d",&value);
							//printf("payload:%s payload offset: %d value:%d\n",link->mpiMsg->payLoad+(8*i),(8*i),value);
							
							intBuf[i] = value;	
						}					
					break;
					default:
						return MPI_ERR_TYPE;
					break;
				}
				
				DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
				break; //break for loop       		
        	}
    	}
		
	}
		
	return MPI_SUCCESS;	
	
}


void MPI_RunProgressLookForTagNoDelete(MPI_Comm comm, int Tag, int fromRank)
{
	int commIndex = GetComm(comm);
	int MsgFound =0;
	while(MsgFound == 0)
	{
		//printf("her....\n");fflush(0);
		//spin progress engin once.
		MPI_Progress_Engine();	
		//traverse list		
		link_t * link;
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
			//fflush(0);
					
        	int match = demandMatch(link->mpiMsg->tag, Tag, link->mpiMsg->commIndex, commIndex, link->mpiMsg->rank, fromRank);	
		
			if(match == 1)
			{
        		MsgFound = 1;        		
        		//printf("pe size:%d\n",mpiProgressList->size);
				//fflush(0);
        		break; //break for
        	}
    	}
	}
}


int MPI_Isend(void * sbuffer, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request * request)
{
	//No Spin Progress Engine to Save Time.
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	//single threaded, best to connect and send up front.
	if(dest < 0 || dest >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	if(tag < 0)
	{
		return MPI_ERR_TAG;		
	}
	

	
	char * buf;
	int i,size;
	
	switch(datatype)
	{
			case MPI_CHAR:			
				size = 8+8+8+count;//tag,commIndex,rank,payload(char)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				char * charArray;
				charArray = ((char *)sbuffer);
				strncpy(buf+24, charArray, count);
				
				
			break;
			case MPI_INT:
				size = 8+8+8+(8 * count);//tag,commIndex,rank,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				int * intArray;				
				intArray = ((int *)sbuffer);
				for(i=0;i<count;i++)
				{
					sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
				}				
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
	}
	
	//send
	
		if(dest==MPI_RANK)
		{
			//add to prosseslist
			link_t * link;	
			link = (link_t *)AddToMPIMsgList(mpiProgressList,buf,size);	
		}else
		{
			//send it!
			DemandConnection(dest);
			send_msg(WorldComm[0].group[dest].fdConn,buf,size);	
		}
	
	*request = 1;
	
	return MPI_SUCCESS;
	
}


int MPI_Irecv(void * rbuffer, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request * request)
{
	
	//No Spin Progress Engine to Save Time.
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	if(source < 0 || source >= WorldComm[0].size)
	{
		if(source != MPI_ANY_SOURCE)
			return MPI_ERR_RANK;			
	}
	
	if(tag < 0)
	{
		if(tag != MPI_ANY_TAG)
		{
			return MPI_ERR_TAG;
		}
	}	
	
	 RecieveFlags.CurrentlyRecieving = 1;
	 RecieveFlags.tag = tag;
	 RecieveFlags.commIndex = CommIndex;
	 RecieveFlags.rank = source;
											    
	
	//spin to look for tag, if i sent it to myself, i'll find it here.
	MPI_RunProgressLookForTagNoDelete(comm,tag, source);
	
	char * buf;
	int i,size;
	link_t * link;
	
	for (link = mpiProgressList->first; link; link = link->next) 
	{	
		
		int match = demandMatch(link->mpiMsg->tag, tag, link->mpiMsg->commIndex, CommIndex, link->mpiMsg->rank, source);	
		
		if(match == 1)
		{	       
				
				switch(datatype)
				{
					case MPI_CHAR:	
					;
						//printf("payload: %s\n",link->mpiMsg->payLoad);		
						//fflush(0);	
						char * charBuf;
						charBuf = ((char *)rbuffer); //type cast
						for(i=0;i<count;i++)
						{	
							//printf("%c\n",link->mpiMsg->payLoad[i]);
							charBuf[(i)] = link->mpiMsg->payLoad[i];	
						}
						
						//strncpy(charBuf+offset, link->mpiMsg->payLoad, recvcnt);
						//printf("after str Copy: %s\n",charBuf);
						//fflush(0);							
					break;
					case MPI_INT:
					;
						//printf("payload:%s rank:%d offset:%d\n",link->mpiMsg->payLoad,link->mpiMsg->rank,offset);
						//fflush(0);
						int * intBuf;
						intBuf = ((int *)rbuffer);
						for(i=0;i<count;i++)
						{
							int value;
							sscanf(link->mpiMsg->payLoad+(8*i),"%08d",&value);
							//printf("payload:%s payload offset: %d value:%d\n",link->mpiMsg->payLoad+(8*i),(8*i),value);
							
							intBuf[i] = value;	
						}					
					break;
					default:
						return MPI_ERR_TYPE;
					break;
				}
				
				DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
			    break; //break for loop
				       		
		}
	}
	RecieveFlags.CurrentlyRecieving = 0; //turn off recieve;	
	*request = 1;
	return MPI_SUCCESS;
}

int demandMatch(int tag, int compTag, int commIndex, int compCommIndex, int rank, int compRank)
{
		
	if(tag==compTag && commIndex == compCommIndex && rank==compRank)
    {
    	//case 1
    	return 1;
	}
		
	if(compTag == MPI_ANY_TAG) // if search tag is any
	{
		if(tag >= 0) //current Tag cannot be a system tag
		{
			//valid tag
			if(compRank==MPI_ANY_SOURCE)
			{
				if(rank >=0) //demand valid rank
				{
					if(commIndex==compCommIndex)  //demand matching commIndex
					{
						//case 3
						return 1;
					}
				}
				
			}else
			{				
				if(commIndex == compCommIndex && rank==compRank)
				{
					//case 2
					return 1;	
				}	
			}
		}
			
	}else
	{
		//tag is not any
		if(compRank==MPI_ANY_SOURCE)
		{
			if(rank >=0) //demand valid rank
			{
				if(commIndex==compCommIndex)  //demand matching commIndex
				{
					//case 4
					return 1;
				}
			}				
		}	
	}	
	
	return -1;	//not a match
}

int MPI_Probe(int source, int tag, MPI_Comm comm, MPI_Status * status)
{
	
	status->count = 0;
	status->cancelled = 0;
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
	{
		status->MPI_ERROR = MPI_ERR_COMM;	
		return MPI_ERR_COMM;
	}
	
	if(source < 0 || source >= WorldComm[0].size)
	{
		if(source != MPI_ANY_SOURCE)
		{
			status->MPI_ERROR = MPI_ERR_RANK;
			return MPI_ERR_RANK;	
		}		
	}
	
	if(tag < 0)
	{
		if(tag != MPI_ANY_TAG)
		{
			status->MPI_ERROR = MPI_ERR_TAG;
			return MPI_ERR_TAG;
		}
	}
	
	//spin to look for tag, will not return untill its found
	MPI_RunProgressLookForTagNoDelete(comm,tag, source);
	
	link_t * link;
	
	for (link = mpiProgressList->first; link; link = link->next) 
	{	
		//printf("Tag:%d Msg_tag:%d comm:%d Msg_comm:%d\n",Tag,link->mpiMsg->tag,commIndex,link->mpiMsg->commIndex);
		//fflush(0);
					
       	int match = demandMatch(link->mpiMsg->tag, tag, link->mpiMsg->commIndex, CommIndex, link->mpiMsg->rank, source);	
		
		if(match == 1)
		{
			    status->MPI_SOURCE = link->mpiMsg->rank;			        
				status->MPI_TAG = link->mpiMsg->tag;
       			status->count += 1;
       			break;//break for loop
		}
		
	}	
	
	return MPI_SUCCESS;
}

int MPI_Recv(void * rbuffer, int count , MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status * status)
{

	if(status != NULL)
	{
		status->count = 0;
		status->cancelled = 0;
	}
	
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
	{
		if(status != NULL)
		status->MPI_ERROR = MPI_ERR_COMM;
		return MPI_ERR_COMM;
	}
		
	if(source < 0 || source >= WorldComm[0].size)
	{
		if(source != MPI_ANY_SOURCE)
		{
			if(status != NULL)
			status->MPI_ERROR = MPI_ERR_RANK; 
			
			return MPI_ERR_RANK;
		}			
	}
	
	if(tag < 0)
	{
		if(tag != MPI_ANY_TAG)
		{
			if(status != NULL)
			status->MPI_ERROR = MPI_ERR_TAG; 
			
			return MPI_ERR_TAG;
		}
	}	
	
	 RecieveFlags.CurrentlyRecieving = 1;
	 RecieveFlags.tag = tag;
	 RecieveFlags.commIndex = CommIndex;
	 RecieveFlags.rank = source;
											    
	
	//spin to look for tag, if i sent it to myself, i'll find it here.
	MPI_RunProgressLookForTagNoDelete(comm,tag, source);
	
	char * buf;
	int i,size;
	link_t * link;
	
	for (link = mpiProgressList->first; link; link = link->next) 
	{	
		
		int match = demandMatch(link->mpiMsg->tag, tag, link->mpiMsg->commIndex, CommIndex, link->mpiMsg->rank, source);	
		
		if(match == 1)
		{	       
			    if(status != NULL)
			    {	
					status->MPI_SOURCE = link->mpiMsg->rank;
					status->MPI_TAG = link->mpiMsg->tag;
				}
				
				switch(datatype)
				{
					case MPI_CHAR:	
					;
						//printf("payload: %s\n",link->mpiMsg->payLoad);		
						//fflush(0);	
						char * charBuf;
						charBuf = ((char *)rbuffer); //type cast
						for(i=0;i<count;i++)
						{	
							//printf("%c\n",link->mpiMsg->payLoad[i]);
							charBuf[(i)] = link->mpiMsg->payLoad[i];	
						}
						
						//strncpy(charBuf+offset, link->mpiMsg->payLoad, recvcnt);
						//printf("after str Copy: %s\n",charBuf);
						//fflush(0);							
					break;
					case MPI_INT:
					;
						//printf("payload:%s rank:%d offset:%d\n",link->mpiMsg->payLoad,link->mpiMsg->rank,offset);
						//fflush(0);
						int * intBuf;
						intBuf = ((int *)rbuffer);
						for(i=0;i<count;i++)
						{
							int value;
							sscanf(link->mpiMsg->payLoad+(8*i),"%08d",&value);
							//printf("payload:%s payload offset: %d value:%d\n",link->mpiMsg->payLoad+(8*i),(8*i),value);
							
							intBuf[i] = value;	
						}					
					break;
					default:
						return MPI_ERR_TYPE;
					break;
				}
				
				DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
			    break; //break for loop
				       		
		}
	}
	RecieveFlags.CurrentlyRecieving = 0; //turn off recieve;	
	if(status != NULL)
	{
		status->count = 1;
	}
	return MPI_SUCCESS;
	
}


int MPI_Rsend(void* sbuffer, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{
	int i,size;
	char * buf;
	link_t * link;
	
	MPI_Progress_Engine();
	
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
	
	if(dest < 0 || dest >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	if(tag < 0)
	{
		return MPI_ERR_TAG;		
	}
	
	if(MPI_RANK == dest)
	{ 
		return MPI_ERR_RANK; //this will never happend
	}
	
	DemandConnection(dest);
	
	int done = 0;
	
	while(done == 0)
	{	
		//send handshake request
		
		size = 8+8+8+8;//tag,commIndex,rank,payload(embeddedTag)
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d",MPI_RSEND,CommIndex,MPI_RANK,tag);
		send_msg(WorldComm[0].group[dest].fdConn,buf,size);	
		free(buf);
		
		MPI_RunProgressLookForTagNoDelete(comm, MPI_RSEND_RSP, dest);
		
		link_t * link;	
		for (link = mpiProgressList->first; link; link = link->next) 
		{			
			if(link->mpiMsg->tag == MPI_RSEND_RSP &&  link->mpiMsg->commIndex==CommIndex &&link->mpiMsg->rank==dest )
			{
				int rSendResponse = 0;
				sscanf(link->mpiMsg->payLoad,"%08d",&rSendResponse);
				DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
				if(rSendResponse==1)
				{
					//Handshake is happening.	
					switch(datatype)
					{
						case MPI_CHAR:			
							size = 8+8+8+count;//tag,commIndex,rank,payload(char)
							buf = (char *)malloc(size * sizeof(char));
							sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
							char * charArray;
							charArray = ((char *)sbuffer);
							strncpy(buf+24, charArray, count);
							
							
						break;
						case MPI_INT:
							size = 8+8+8+(8 * count);//tag,commIndex,rank,payload(int)
							buf = (char *)malloc(size * sizeof(char));
							sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
							int * intArray;				
							intArray = ((int *)sbuffer);
							for(i=0;i<count;i++)
							{
								sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
							}				
						
						break;
						default:
							return MPI_ERR_TYPE;
						break;
				  }
				  send_msg(WorldComm[0].group[dest].fdConn,buf,size);	
				  free(buf);
				  done = 1;				
				}else
				{
					//not happening this time, try again?				
				}
				break; // for loop
			}
		}
	}
	
}


int MPI_Send(void* sbuffer, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{
	
	MPI_Progress_Engine();
		
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	//single threaded, best to connect and send up front.
	if(dest < 0 || dest >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	if(tag < 0)
	{
		return MPI_ERR_TAG;		
	}
	
	
	
	char * buf;
	int i,size;
	
	switch(datatype)
	{
			case MPI_CHAR:			
				size = 8+8+8+count;//tag,commIndex,rank,payload(char)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				char * charArray;
				charArray = ((char *)sbuffer);
				strncpy(buf+24, charArray, count);
				
				
			break;
			case MPI_INT:
				size = 8+8+8+(8 * count);//tag,commIndex,rank,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				int * intArray;				
				intArray = ((int *)sbuffer);
				for(i=0;i<count;i++)
				{
					sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
				}				
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
	}
	
	//send	
	if(dest==MPI_RANK)
	{
		//add to prosseslist
		link_t * link;	
		link = (link_t *)AddToMPIMsgList(mpiProgressList,buf,size);	
	}else
	{
		//send it!
		DemandConnection(dest);
		send_msg(WorldComm[0].group[dest].fdConn,buf,size);	
	}	
	
	return MPI_SUCCESS;
		
}


int MPI_Ssend(void* sbuffer, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{
	MPI_Progress_Engine();
		
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	//single threaded, best to connect and send up front.
	if(dest < 0 || dest >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	if(tag < 0)
	{
		return MPI_ERR_TAG;		
	}
	
	
	
	char * buf;
	int i,size;
	
	switch(datatype)
	{
			case MPI_CHAR:			
				size = 8+8+8+count;//tag,commIndex,rank,payload(char)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				char * charArray;
				charArray = ((char *)sbuffer);
				strncpy(buf+24, charArray, count);
				
				
			break;
			case MPI_INT:
				size = 8+8+8+(8 * count);//tag,commIndex,rank,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d",tag,CommIndex,MPI_RANK);
				int * intArray;				
				intArray = ((int *)sbuffer);
				for(i=0;i<count;i++)
				{
					sprintf((buf+24+(8*i)),"%08d", intArray[i]);	
				}				
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
	}
	
	//send	
	if(dest==MPI_RANK)
	{
		//add to prosseslist
		link_t * link;	
		link = (link_t *)AddToMPIMsgList(mpiProgressList,buf,size);	
	}else
	{
		//send it!
		DemandConnection(dest);
		send_msg(WorldComm[0].group[dest].fdConn,buf,size);	
	}	
	
	return MPI_SUCCESS;
}


int MPI_Test(MPI_Request * request, int * flag, MPI_Status * status)
{
	MPI_Progress_Engine();
	
	if(*request == 1)
	{
		*flag = 1;
		return MPI_SUCCESS;
	}else
	{
		return MPI_ERR_REQUEST;	
	}
}

int MPI_Wait(MPI_Request * request, MPI_Status * status)
{
	MPI_Progress_Engine();
	
	//everything was sent in I methods due to single thread, no work to be done here.
	
	if(*request == 1)
		return MPI_SUCCESS;
	else
	{
		//just hang ;) and help everyone else out by running my PE
		while(*request != 1)
		{
			MPI_Progress_Engine();
		}				
	}	
}

int MPI_Win_create(void * base, MPI_Aint size, int disp_unit, MPI_Info info, MPI_Comm comm, MPI_Win * win)
{
	MPI_Progress_Engine();
		
	int CommIndex = GetComm(comm);
	
	if(CommIndex == MPI_ERR_COMM)
		return MPI_ERR_COMM;
		
	
	//increase windows object for a new window	
	if(windows_size == 0)
	{
		windows = (window *) malloc(1 * sizeof(window));	
		windows_size++;		
	}else
	{
			window * newWindows = (window *)realloc(windows,(++windows_size)* sizeof(window));
			windows = newWindows;	
	}
	
	windows[windows_size-1].win = windows_size-1;
	//windows[windows_size-1].winpt = (int *)calloc(size , disp_unit);;
	windows[windows_size-1].commIndex = CommIndex;	
	
	//int z;
	//for(z=0;z<size;z++)
	//{
	//	windows[windows_size-1].winpt[z] = 0;
	//}
		
     windows[windows_size-1].winpt = ((int *)base);
	//*((int *)base) = windows[windows_size-1].winpt[0];
    
	
	*win = windows_size-1; //give him the window index
	
	if(MPI_RANK==0)
	{
		//tell MPI_EXEC we just created a window	
		//he will create a mutex for the window
		char * buf;
		int size = 8+8+8+8;//tag,commIndex,rank,ok
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d",MPI_WIN_CREATE,CommIndex,MPI_RANK,1); //send 1 for done
		int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
		free(buf);	
		
		//hang untill MPIEXEC reply's
		int response = 0;
		int bytes_read = 0;
		char * revBuf;
		bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
		if(bytes_read != 0)
		{
			sscanf(revBuf,"%08d",&response);
				
			//printf("Got Response:%d\n",response);
			//fflush(0);
			
			if(response == 1)
			{	
				//mutex created ok	
			}else if(response==0)
			{
				//MPIEXEC didn't create my window.
				//should never happen
				printf("MPIEXEC is a funny guy\n");
				fflush(0);
			}else if(response==MPI_ABORT)
			{
				//DO Abort
				OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
				MPI_Abort(MPI_COMM_WORLD,-911);
			}
		}else
		{
			printf("No Longer Connected to MPIEXEC\n");
			fflush(0);	
		}
	}
	
	
	MPI_Barrier(comm); //ensure everyone is here
	
	return MPI_SUCCESS;
}

int MPI_Put(void * origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win)
{
	MPI_Progress_Engine();
		
	int i;
	
	if(target_rank != MPI_RANK)
		DemandConnection(target_rank);
	
	//single threaded, best to connect and send up front.
	if(target_rank < 0 || target_rank >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	MindLocks(windows[win].commIndex, win, 4, 0, target_rank); // only returns if operation is allowed
	
	//send	
	if(target_rank==MPI_RANK)
	{
		switch(origin_datatype)
		{
			case MPI_INT:
			;	
				int * intArray;				
				intArray = ((int *)origin_addr);
				for(i=0;i<target_count;i++)
				{
						windows[win].winpt[target_disp+i] = intArray[i];		
				}	
										
			break;
		}
	}else
	{
		
		//send it!
		int size;
		char * buf;
		
		switch(origin_datatype)
		{
			case MPI_INT:
				size = 8+8+8+8+8+8+(8 * target_count);//tag,commIndex,rank,win,disp,count,payload(int)
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d%08d%08d%08d",MPI_WIN_PUT,windows[win].commIndex,MPI_RANK,win,target_disp,target_count);
				int * intArray;				
				intArray = ((int *)origin_addr);
				for(i=0;i<target_count;i++)
				{
					//printf("sending:%d\n",intArray[i]);
					sprintf((buf+48+(8*i)),"%08d", intArray[i]);	
				}				
			
			break;
			default:
				return MPI_ERR_TYPE;
			break;
		}
		
		//printf("%s\n",buf);
		//fflush(0);
		send_msg(WorldComm[0].group[target_rank].fdConn,buf,size);	
		free(buf);
		MPI_Comm comm = WorldComm[windows[win].commIndex].context;
		MPI_RunProgressLookForTag(comm, MPI_WIN_PUT_RSP, target_rank);
	}
	
	
	//tell MPI_EXEC i am done	
	//he is expecting it.
	char * buf;
	int size = 8;//any msg.
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d",1); //send 1 for done
	int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
	return MPI_SUCCESS;
	
}


int MPI_Get(void * origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win)
{
	
	MPI_Progress_Engine();
	
	int i;
	
	if(target_rank != MPI_RANK)
		DemandConnection(target_rank);
	
	
	//single threaded, best to connect and send up front.
	if(target_rank < 0 || target_rank >= WorldComm[0].size)
	{
		return MPI_ERR_RANK;			
	}
	
	//blocks untill we get approved
	MindLocks(windows[win].commIndex, win, 3, 0, target_rank);
	
	
	
	//send	
	if(target_rank==MPI_RANK)
	{
		//TODO check for lock first
		switch(origin_datatype)
		{
			case MPI_INT:
			;
				int * intArray;				
				intArray = ((int *)origin_addr);
				for(i=0;i<origin_count;i++)
				{
					//printf("value:%d\n,",windows[win].winpt[target_disp+i]);fflush(0);
					intArray[i] = windows[win].winpt[target_disp+i];		
				}						
			break;
		}
	}else
	{		
		//request it
		int size;
		char * buf;
		
		size = 8+8+8+8+8+8;//tag,commIndex,rank,win,disp,count
		buf = (char *)malloc(size * sizeof(char));
		sprintf(buf,"%08d%08d%08d%08d%08d%08d",MPI_WIN_GET,windows[win].commIndex,MPI_RANK,win,target_disp,target_count);
		
		
		//printf("here1\n");fflush(0);
		send_msg(WorldComm[0].group[target_rank].fdConn,buf,size);	
		free(buf);
		
		MPI_Comm comm = WorldComm[windows[win].commIndex].context;
		//printf("here2\n");fflush(0);
		MPI_RunProgressLookForTagNoDelete(comm,MPI_WIN_GET_RSP, target_rank);
	    //printf("here3\n");fflush(0);
		
		
		link_t * link;	
		for (link = mpiProgressList->first; link; link = link->next) 
		{	
			int compTag = link->mpiMsg->tag;
			int compCommIndex = link->mpiMsg->commIndex;
			int compRank = link->mpiMsg->rank;
		    
			if(compTag==MPI_WIN_GET_RSP &&  compCommIndex == windows[win].commIndex && compRank == target_rank)	
			{
			   // printf("tag:%d commIndex:%d rank:%d\n",compTag,compCommIndex,link->mpiMsg->rank);
				switch(origin_datatype)
				{
					  case MPI_INT:	
					  ;
							int * intBuf;
							intBuf = ((int *)origin_addr);
							for(i=0;i<origin_count;i++)
							{
								int value;
								sscanf(link->mpiMsg->payLoad+(8*i),"%08d",&value);
								//printf("payload:%s payload offset: %d value:%d\n",link->mpiMsg->payLoad+(8*i),(8*i),value);
								intBuf[i] = value;	
							}					
						break;
						default:
							return MPI_ERR_TYPE;
						break;
				}
					
				DeleteFromMPIMsgListSpecNode(mpiProgressList, link);
				break; //break for loop
			}
		
		}//end for	
	}	
	
	//tell MPI_EXEC i am done	
	//he is expecting it.
	char * buf;
	int size = 8;//any msg.
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d",1); //send 1 for done
	int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
	return MPI_SUCCESS;
	
}

//will hang untill lock has been minded.
void MindLocks(int commIndex,MPI_Win win, int RequestType, int LockType, int RankWin)
{
	//MPI_WIN_REQUEST
	int done =0;
	int reqMade = 0;
	while(done==0)
	{
			MPI_Progress_Engine();
		
			if(reqMade==0)
			{
				//ask MPIEXEC for permission to procced with a window request;
				char * buf;
				int size = 8+8+8+8+8+8+8;//tag,commIndex,rank,payload=win,requestinglock,locktype,requestingUnlock
				buf = (char *)malloc(size * sizeof(char));
				sprintf(buf,"%08d%08d%08d%08d%08d%08d%08d",MPI_WIN_REQUEST,commIndex,MPI_RANK,win,RequestType,LockType,RankWin); //send 1 for done
				int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
				free(buf);
				reqMade = 1;
			}
			//hang untill MPIEXEC reply's
			int response = 0;
			int bytes_read = 0;
			
			struct timeval tv;
			fd_set readfds;			
		
			FD_ZERO( &readfds );
			FD_SET( MPI_HOST_FDCONN, &readfds );			
			
			//set time outs for select
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			
			int rc;
			//select statment
			rc = select( FD_SETSIZE, &readfds, NULL, NULL, &tv );

			if (rc == 0)
			{
				
			}else if (rc == -1  &&  errno == EINTR)
			{
			   
			}else if ( rc < 0 )
			{
				//done = 1;
				//printf("select failed\n");
			}else
			{
				//something on the select, read				
				char * revBuf;
				bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
				if(bytes_read != 0)
				{
					sscanf(revBuf,"%08d",&response);
					
					//printf("Got Response:%d\n",response);
					//fflush(0);
					
					if(response == 1)
					{	
						done=1;		
					}else if(response==0)
					{
						//MPIEXEC Denied my req to do this window operation
						//my options are to spin again.
						//printf("Not This Time\n");
						reqMade = 0; //ask again
					}else if(response==MPI_ABORT)
					{
						//DO Abort
						OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
						MPI_Abort(MPI_COMM_WORLD,-911);
					}
				}else
				{
					printf("No Longer Connected to MPIEXEC\n");
					fflush(0);	
				}
			}
	}
}


int MPI_Win_lock(int lock_type, int rank, int assert, MPI_Win win)
{
	MPI_Progress_Engine();
	
	//hangs untill lock is accquired
	MindLocks(windows[win].commIndex, win, 1,lock_type, rank);
	
	//tell MPI_EXEC i am done	
	//he is expecting it.
	char * buf;
	int size = 8;//any msg.
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d",1); //send 1 for done
	int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
}

int MPI_Win_unlock(int rank, MPI_Win win)
{
	MPI_Progress_Engine();
	
	//hangs untill lock is accquired
	MindLocks(windows[win].commIndex, win, 2,0,rank);
	
	//tell MPI_EXEC i am done	
	//he is expecting it.
	char * buf;
	int size = 8;//any msg.
	buf = (char *)malloc(size * sizeof(char));
	sprintf(buf,"%08d",1); //send 1 for done
	int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
	free(buf);
	
}

void TellMPIEXECToDupAttributes(int commIndex, int commIndex2)
{
	        //ask MPIEXEC to dup Attributes from comm1 to comm2
			char * buf;
			int size = 8+8+8+8;//tag,commIndex,rank,payload=CommIndex2
			buf = (char *)malloc(size * sizeof(char));
			sprintf(buf,"%08d%08d%08d%08d",MPI_COMM_DUP_TAG,commIndex,MPI_RANK,commIndex2);
			int bytes_sent = send_msg(MPI_HOST_FDCONN,buf,size);
			free(buf);
			
			//hang untill MPIEXEC reply's
			int response = 0;
			int bytes_read = 0;
			char * revBuf;
			bytes_read = recv_msg(&MPI_HOST_FDCONN,&revBuf);
			if(bytes_read != 0)
			{
				sscanf(revBuf,"%08d",&response);
				
				//printf("Got Response:%d\n",response);
				//fflush(0);
				
				if(response == 1)
				{	
						
				}else if(response==0)
				{
					//MPIEXEC screwed me...
				}else if(response==MPI_ABORT)
				{
					//DO Abort
					OkToAbort = 1;//Rank 0 has already been told to Abort by Someone if i got this from MPIEXEC
					MPI_Abort(MPI_COMM_WORLD,-911);
				}
			}else
			{
				printf("No Longer Connected to MPIEXEC\n");
				fflush(0);	
			}	
}

