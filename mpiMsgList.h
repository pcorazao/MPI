
typedef struct {
   int      tag;
   int      commIndex;   
   int      rank;
   char *   payLoad;
   int      payLoadSize;      
} MPI_Msg;

typedef struct link link_t;

struct link {
   MPI_Msg * mpiMsg; //data
   link_t * prev; 
   link_t * next;        
};

typedef struct MPI_MsgList
{
	int size;
	link_t * first;
	link_t * last;	
}MPI_MsgList_t;
 
void CreateMPIMsgList(MPI_MsgList_t * );
link_t *  AddToMPIMsgList (MPI_MsgList_t * , char * , int );
MPI_Msg * GetElementFromMPIMsgList (MPI_MsgList_t * , int );
void DeleteFromMPIMsgList(MPI_MsgList_t * , int );
void DeleteFromMPIMsgListSpecNode(MPI_MsgList_t * , link_t *);
