#include <sys/shm.h>		//Used for shared memory
#include <sys/sem.h>		//Used for semaphores

//********************************
//********************************
//********** INITIALISE **********
//********************************
//********************************
void initialise (void)
{

	//..... Do init stuff ....

	//-----------------------------------------------
	//----- CREATE SHARED MEMORY WITH SEMAPHORE -----
	//-----------------------------------------------
	printf("Creating shared memory with semaphore...\n");
	semaphore1_id = semget((key_t)SEMAPHORE_KEY, 1, 0666 | IPC_CREAT);		//Semaphore key, number of semaphores required, flags
	//	Semaphore key
	//		Unique non zero integer (usually 32 bit).  Needs to avoid clashing with another other processes semaphores (you just have to pick a random value and hope - ftok() can help with this but it still doesn't guarantee to avoid colision)

	//Initialize the semaphore using the SETVAL command in a semctl call (required before it can be used)
	union semun sem_union_init;
	sem_union_init.val = 1;
	if (semctl(semaphore1_id, 0, SETVAL, sem_union_init) == -1)
	{
		fprintf(stderr, "Creating semaphore failed to initialize\n");
		exit(EXIT_FAILURE);
	}

	//Create the shared memory
	shared_memory1_id = shmget((key_t)SHARED_MEMORY_KEY, sizeof(struct shared_memory1_struct), 0666 | IPC_CREAT);		//Shared memory key , Size in bytes, Permission flags
	//	Shared memory key
	//		Unique non zero integer (usually 32 bit).  Needs to avoid clashing with another other processes shared memory (you just have to pick a random value and hope - ftok() can help with this but it still doesn't guarantee to avoid colision)
	//	Permission flags
	//		Operation permissions 	Octal value
	//		Read by user 			00400
	//		Write by user 			00200
	//		Read by group 			00040
	//		Write by group 			00020
	//		Read by others 			00004
	//		Write by others			00002
	//		Examples:
	//			0666 Everyone can read and write

	if (shared_memory1_id == -1)
	{
		fprintf(stderr, "Shared memory shmget() failed\n");
		exit(EXIT_FAILURE);
	}

	//Make the shared memory accessible to the program
	shared_memory1_pointer = shmat(shared_memory1_id, (void *)0, 0);
	if (shared_memory1_pointer == (void *)-1)
	{
		fprintf(stderr, "Shared memory shmat() failed\n");
		exit(EXIT_FAILURE);
	}
	printf("Shared memory attached at %X\n", (int)shared_memory1_pointer);

	//Assign the shared_memory segment
	shared_memory1 = (struct shared_memory1_struct *)shared_memory1_pointer;


	//----- SEMAPHORE GET ACCESS -----
	if (!semaphore1_get_access())
		exit(EXIT_FAILURE);

	//----- WRITE SHARED MEMORY -----
	int Index;
	for (Index = 0; Index < sizeof(struct shared_memory1_struct); Index++)
		shared_memory1->data[Index] = 0x00;

	//Write initial values
	shared_memory1->some_data[0] = 'H';
	shared_memory1->some_data[1] = 'e';
	shared_memory1->some_data[2] = 'l';
	shared_memory1->some_data[3] = 'l';
	shared_memory1->some_data[4] = 'o';
	shared_memory1->some_data[5] = 0x00;
	shared_memory1->some_data[6] = 1;
	shared_memory1->some_data[7] = 255;
	shared_memory1->some_data[8] = 0;
	shared_memory1->some_data[9] = 0;

	printf("stored=%s\n", shared_memory1->some_data);

	//----- SEMAPHORE RELEASE ACCESS -----
	if (!semaphore1_release_access())
		exit(EXIT_FAILURE);
}


//***********************************
//***********************************
//********** MAIN FUNCTION **********
//***********************************
//***********************************
int main(int argc, char **argv)
{

	//**********************
	//**********************
	//***** INITIALISE *****
	//**********************
	//**********************

	//GENERAL INITIALISE
	initialise();


	//*********************
	//*********************
	//***** MAIN LOOP *****
	//*********************
	//*********************
    while (1)						// Do forever
    {
		delayMicroseconds(100);				//Sleep occasionally so scheduler doesn't penalise us


		//--------------------------------------------------------------------------------
		//----- CHECK FOR EXIT COMMAND RECEIVED FROM OTHER PROCESS VIA SHARED MEMORY -----
		//--------------------------------------------------------------------------------

		//----- SEMAPHORE GET ACCESS -----
		if (!semaphore1_get_access())
			exit(EXIT_FAILURE);
		
		//----- ACCESS THE SHARED MEMORY -----
		//Just an example of reading 2 bytes values passed from the php web page that will cause us to exit
		if ((shared_memory1->some_data[8] == 30) && (shared_memory1->some_data[9] = 255))
			break;
		
		//----- SEMAPHORE RELEASE ACCESS -----
		if (!semaphore1_release_access())
			exit(EXIT_FAILURE);


    }

    //------------------------------------------
    //------------------------------------------
    //----- EXIT MAIN LOOP - SHUTTING DOWN -----
    //------------------------------------------
    //------------------------------------------


	//----- DETACH SHARED MEMORY -----
	//Detach and delete
	if (shmdt(shared_memory1_pointer) == -1)
	{
		fprintf(stderr, "shmdt failed\n");
		//exit(EXIT_FAILURE);
	}
	if (shmctl(shared_memory1_id, IPC_RMID, 0) == -1)
	{
		fprintf(stderr, "shmctl(IPC_RMID) failed\n");
		//exit(EXIT_FAILURE);
	}
	//Delete the Semaphore
	//It's important not to unintentionally leave semaphores existing after program execution. It also may cause problems next time you run the program.
	union semun sem_union_delete;
	if (semctl(semaphore1_id, 0, IPC_RMID, sem_union_delete) == -1)
		fprintf(stderr, "Failed to delete semaphore\n");


#ifndef __DEBUG
	if (do_reset)
	{
		system("sudo shutdown \"now\" -r");
	}
#endif

    return 0;
}



//***********************************************************
//***********************************************************
//********** WAIT IF NECESSARY THEN LOCK SEMAPHORE **********
//***********************************************************
//***********************************************************
//Stall if another process has the semaphore, then assert it to stop another process taking it
static int semaphore1_get_access(void)
{
	struct sembuf sem_b;
	sem_b.sem_num = 0;
	sem_b.sem_op = -1; /* P() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(semaphore1_id, &sem_b, 1) == -1)		//Wait until free
	{
		fprintf(stderr, "semaphore1_get_access failed\n");
		return(0);
	}
	return(1);
}

//***************************************
//***************************************
//********** RELEASE SEMAPHORE **********
//***************************************
//***************************************
//Release the semaphore and allow another process to take it
static int semaphore1_release_access(void)
{
	struct sembuf sem_b;
	sem_b.sem_num = 0;
	sem_b.sem_op = 1; /* V() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(semaphore1_id, &sem_b, 1) == -1)
	{
		fprintf(stderr, "semaphore1_release_access failed\n");
		return(0);
	}
	return(1);
}