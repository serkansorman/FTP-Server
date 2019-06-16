#include <netdb.h> 
#include <string.h> 
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <libgen.h>
#include <sys/time.h>



#define WRITE_FLAGS (O_WRONLY | O_CREAT | O_TRUNC)
#define END_OF_DIR -99


typedef struct{
    char name[512];
    mode_t mode; // Permissions
    char content[4096];
    long size;
    time_t modifiedTime;
}file;

typedef struct{
    char name[512];
    mode_t mode;
    int isExist;

}serverFile;


typedef struct{
    char name[512];
    int isOnline;
}clientPath;


volatile int done = 0;
char serverPath[512];
pthread_t *threadPool;
clientPath *pathNames;
int pathIndex = 0;
int sockfd;
int threadPoolSize;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int getClientFiles(char *sourcePath,serverFile *fileNames,int fileIndex,char *logPath ){
	
	DIR *dir;
    struct stat statbuf; 
	struct dirent *ent;
	char *currentSourcePath;

	if ((dir = opendir (sourcePath)) != NULL) {
	  	while ((ent = readdir (dir)) != NULL) {
	  	 	
     	 	if(strcmp(ent->d_name,".") != 0 && strcmp(ent->d_name,"..") != 0){

     	 		currentSourcePath = malloc(sizeof(sourcePath)+sizeof(ent->d_name)+sizeof(char));/* path/fileName */
				sprintf(currentSourcePath,"%s/%s", sourcePath, ent->d_name); /* Concatanate current file name and path */

				if(strcmp(logPath,currentSourcePath) == 0){
					free(currentSourcePath);
					continue;
				}

     	 		lstat(currentSourcePath, &statbuf);

     	 		fileNames[fileIndex].mode = statbuf.st_mode;
     	 		fileNames[fileIndex].isExist = 0;
     			strcpy(fileNames[fileIndex++].name,currentSourcePath);
     			
				if(S_ISDIR(statbuf.st_mode))/* Checks current file is directory */
 	 				fileIndex = getClientFiles(currentSourcePath,fileNames,fileIndex,logPath);
 					
    			free(currentSourcePath);
			}		
     	}		
	}
	else {
	 	return -1;  
	}   
    closedir(dir);
    return fileIndex;
}

			
void writeLogTime(FILE *logfp){
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	fprintf(logfp,"%02d-%02d-%02d  %02d:%02d:%02d \t", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}


void writeLog(char *logMessage,char *logPath){

	FILE *logfp = fopen(logPath,"a");
	writeLogTime(logfp);
	fprintf(logfp, "%s\n",logMessage);
	fclose(logfp);

}

int setFileExist(serverFile *fileNames,char* fileName,int fileIndex){
	for(int i=0; i<fileIndex; ++i){
		if(strcmp(fileNames[i].name,fileName) == 0){
			fileNames[i].isExist = 1;
			return 1;
		}
	}
	return 0;
}


void removeFiles(serverFile *fileNames,int fileIndex,char* logPath){

	char tempStr[512];
		
	for(int i=0; i<fileIndex; ++i){
		if(fileNames[i].isExist == 0){
			if(remove(fileNames[i].name) == 0){
				if(S_ISDIR(fileNames[i].mode)){
					sprintf(tempStr,"Directory \t REMOVE \t %s",fileNames[i].name);
					writeLog(tempStr,logPath);
				}
					
				else{
					sprintf(tempStr,"File  \t\t REMOVE \t %s",fileNames[i].name);
					writeLog(tempStr,logPath);
				}
			}
		}
	}
}

void setClientOffline(char* clientPath){

	for(int i = 0; i< pathIndex; ++i){
		if(strcmp(clientPath,pathNames[i].name) == 0){
			pathNames[i].isOnline = 0;
			return;
		}
	}
}


int isSamePathOnline(char* clientPath){
	for(int i = 0; i< pathIndex; ++i){
		if(strcmp(clientPath,pathNames[i].name) == 0 && pathNames[i].isOnline == 1)
			return 1;
	}
		
	return 0;

}

 
void *serverProcess(void *arg) { 
	 
	char connAddress[50];
	int connfd,fd,sockfd;
	file f;
	socklen_t clientAddrLen;
	struct sockaddr_in cli;
	char newPath[512],rootPath[512],prevFileName[512] = "initVal",logPath[512],tempStr[512];
	
	struct stat statbuf;
	serverFile *fileNames;
	int fileIndex = 0;

	sockfd = *(int *)(arg);
	clientAddrLen = sizeof(struct sockaddr_in);
	memset(&f,0,sizeof(f));
	

	while (!done) { // Serves multiple client
		
		connfd = accept(sockfd, (struct sockaddr *)&cli, &clientAddrLen);
		
		if(connfd == -1){
			printf("Server acccept failed to client %s\n",connAddress);
			continue;
		}

		inet_ntop(AF_INET, &(cli), connAddress, sizeof(connAddress));
		
		read(connfd,&f,sizeof(file));
		
		sprintf(rootPath,"%s/%s",serverPath,f.name);
		sprintf(logPath,"%s/%s",rootPath,"Client.log");

		//############### Critical Section ###################
		pthread_mutex_lock(&lock); //lock
		// Send 'fail' response to client when two client connect same path simultaneously
		if(isSamePathOnline(rootPath)){
			write(connfd,"fail",sizeof("fail"));
			pthread_mutex_unlock(&lock); //unlock
			continue;
		}
		else{
			write(connfd,"ok",sizeof("ok"));
			pathNames[pathIndex].isOnline = 1;
			strcpy(pathNames[pathIndex++].name,rootPath);
		}
		pthread_mutex_unlock(&lock); //unlock
		//############### Critical Section ###################


		printf("Server acccept the client from %s\n",connAddress);
		
		fileNames = malloc(1024 * sizeof(serverFile));
		fileIndex = getClientFiles(rootPath,fileNames,0,logPath);  // Get current client file on server side

		if(S_ISDIR(f.mode)){
			mkdir(rootPath,f.mode); // Return -1 if Dir is already exist
		}

		writeLog("Client is connected",logPath);

		while(read(connfd,&f,sizeof(file)) > 0){ // Loop until end of clients directory

			// Check directory is finished then check files to be removed
			if(f.size == END_OF_DIR){
				removeFiles(fileNames,fileIndex,logPath);
				fileIndex = getClientFiles(rootPath,fileNames,0,logPath); // Get current client file on server side
				if(done == 1){
					write(connfd,"shutdown",sizeof("shutdown"));
					break;
				}
				else{
					write(connfd,"ok",sizeof("ok")); // Send respond to client to indicate file struct is taken successfully
					continue;
				}
				
			}
			
			write(connfd,"ok",sizeof("ok")); // Send respond to client to indicate file struct is taken successfully

			sprintf(newPath,"%s/%s",serverPath,f.name);
			
			if(S_ISDIR(f.mode)){
				if(mkdir(newPath,f.mode) != -1){ // Return -1 if Dir is already exist
					sprintf(tempStr,"Directory \t ADD \t\t %s",newPath);
					writeLog(tempStr,logPath);
				}
				
				setFileExist(fileNames,newPath,fileIndex);
				
			}
			else if(S_ISREG(f.mode) || S_ISFIFO(f.mode)){ //Regular and fifo

				if(strcmp(prevFileName,f.name) != 0){ // Checks file is changed
					if(fd != -1)
						close(fd); 					   // Close previous file
					strcpy(prevFileName,f.name);	   // Update previous file name

					if(lstat(newPath, &statbuf) != -1){         // Check file is already exist
						if(f.modifiedTime > statbuf.st_mtime){  // Check file in client side is modified then open file to modify
							fd = open(newPath,WRITE_FLAGS,f.mode);
							sprintf(tempStr,"File  \t\t UPDATE \t %s",newPath);
							writeLog(tempStr,logPath);
						}	    
							
						else{
							setFileExist(fileNames,newPath,fileIndex);
							// if same file is exist, do not make any operation
							continue;
						}										
															
					}
					else {// if file is not in server, create file
						fd = open(newPath,WRITE_FLAGS,f.mode);
						sprintf(tempStr,"File  \t\t ADD \t\t %s",newPath);
						writeLog(tempStr,logPath);	
					}

	
					setFileExist(fileNames,newPath,fileIndex);
			
				}

				if(fd != -1)
					write(fd,f.content,f.size);	// Write given content to given file
			}
		}

		//############### Critical Section ###################
		pthread_mutex_lock(&lock);
		setClientOffline(rootPath);
		pthread_mutex_unlock(&lock);
		//############### Critical Section ###################

		writeLog("Client is disconnected",logPath);
		printf("Client %s is disconnected\n",basename(rootPath));

		free(fileNames);

	}
	
	return NULL;
} 

long getCurrentTime() {
    struct timeval te; 
    gettimeofday(&te, NULL); 
    return te.tv_sec*1000 + te.tv_usec/1000;
}

void delay(unsigned long ms){
    unsigned long end = getCurrentTime() + ms;
    while(end > getCurrentTime());
}


void handle_signal(int signo){
	if(signo == SIGINT)
		printf("SIGINT is handled\n");
	else if(signo == SIGTERM)
		printf("SIGTERM is handled\n");

	done = 1;
	delay(1000);
	close(sockfd);
	for(int i = 0; i<threadPoolSize; ++i){
		pthread_cancel(threadPool[i]);
	}
}

int main(int argc, char const *argv[]){
	DIR *dir;
	
	struct sockaddr_in servaddr; 
	int portNumber;
	

	if (argc != 4 || ((threadPoolSize = atoi (argv[2]))) <= 0 || ((portNumber = atoi (argv[3]))) <= 0) {
		fprintf(stderr, "Usage: %s [directory] [threadPoolSize] [portnumber]\n", argv[0]);
		return 1;
	}


	if((dir = opendir(argv[1])) == NULL){
		fprintf(stderr, "%s: invalid path\n",argv[1]);
		return 1;
	}
	closedir(dir);

	signal(SIGINT,handle_signal);
	signal(SIGTERM,handle_signal);

	strcpy(serverPath,argv[1]);
	pathNames = malloc(1024 * sizeof(clientPath));


	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("Socket creation failed...\n"); 
		exit(1); 
	} 
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(portNumber); 


	if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0) { 
		printf("Socket bind failed...\n"); 
		exit(1); 
	} 
	else
		printf("Socket successfully binded..\n"); 

	if ((listen(sockfd, 5)) != 0) { 
		printf("Listen failed...\n"); 
		exit(1); 
	} 
	else
		printf("Server listening..\n"); 
	
	// Create a thread pool
	threadPool = malloc(threadPoolSize * sizeof(pthread_t));
	for(int i = 0; i<threadPoolSize; ++i){
		if(pthread_create(&threadPool[i], NULL, serverProcess, (void*)&sockfd)){
			fprintf(stderr, "Error on thread creation, program is terminated.\n");
			exit(1);
		}
	}

	// Wait all threads finished
	for(int i = 0; i<threadPoolSize; ++i){
		if(pthread_join(threadPool[i], NULL)){
			fprintf(stderr, "Error on join thread, program is terminated.\n");
			exit(1);
		}
	}
	

	free(threadPool);
	free(pathNames);
	close(sockfd);

	return 0;
} 
