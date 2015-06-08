all: mpiexec mpiTest test1 test2	
	
mpiexec: mpiexec.c
	gcc -o mpiexec mpiexec.c mpi.h mpi.c mpiMsgList.h mpiMsgList.c -lrt
	
mpiTest: mpiTest.c
	gcc -o mpiTest mpiTest.c mpi.h mpi.c mpiMsgList.h mpiMsgList.c -lrt

test1: test1.c
	gcc -o test1 test1.c mpi.h mpi.c mpiMsgList.h mpiMsgList.c -lrt

test2: test2.c
	gcc -o test2 test2.c mpi.h mpi.c mpiMsgList.h mpiMsgList.c -lrt
	
clean:
	rm mpiexec mpiTest test1 test2
