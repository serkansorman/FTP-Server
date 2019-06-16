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

#define END_OF_DIR -99


typedef struct{
    char name[512];
    mode_t mode; 		
    char content[4096];
    long size;	
    time_t modifiedTime;		
}file;

volatile int done = 0;

void sendFiles(char *sourcePath,char *pathToServer,int sockfd){
	
	DIR *dir;
    struct stat statbuf; 
	struct dirent *ent;
	file f;
	int infd;
	char response[10];
	char *currentSourcePath;
	char *currentPathToServer;
	
	memset(&f,0,sizeof(f));

	if ((dir = opendir (sourcePath)) != NULL) {
	  	while ((ent = readdir (dir)) != NULL) {
	  	 	
     	 	if(strcmp(ent->d_name,".") != 0 && strcmp(ent->d_name,"..") != 0){

     	 		currentSourcePath = malloc(sizeof(sourcePath)+sizeof(ent->d_name)+sizeof(char));/* path/fileName */
				sprintf(currentSourcePath,"%s/%s", sourcePath, ent->d_name); /* Concatanate current file name and path */

				currentPathToServer = malloc(512);/* path/fileName */
				sprintf(currentPathToServer,"%s/%s", pathToServer, ent->d_name); /* Concatanate current file name and path */

     	 		lstat(currentSourcePath, &statbuf);
 

				if(S_ISDIR(statbuf.st_mode)){/* Checks current file is directory */
 	 				
					strcpy(f.name,currentPathToServer);	// Set directory name
					f.mode = statbuf.st_mode;			// Set directory permissions
					f.modifiedTime = statbuf.st_mtime; // Set last modificatio time

					f.size = 0;
					strcpy(f.content," ");

					write(sockfd,&f,sizeof(file));			 // Send directory info via socket
					read(sockfd,response,sizeof(response));	 // Read response from server to determine directory info is sent successfully
   
 	 				sendFiles(currentSourcePath,currentPathToServer,sockfd);
 					
				}	
				else if(S_ISREG(statbuf.st_mode) || S_ISFIFO(statbuf.st_mode)){	//Fifo, Regular file

					strcpy(f.name,currentPathToServer);	// Set file name
					f.mode = statbuf.st_mode;			// Set file permissions
					f.modifiedTime = statbuf.st_mtime; // Set last modification time
					
					if(S_ISREG(statbuf.st_mode))
						infd = open(currentSourcePath,O_RDONLY); // Open file to read
					else
						infd = open(currentSourcePath,O_RDONLY | O_NONBLOCK); // Open file to read
				
					if(infd == -1){
						continue;
					}

					//File is readen from client directory then send file to server as 4 KB (max limit) parts
					while((f.size = read(infd,f.content,sizeof(f.content))) > 0){
						write(sockfd,&f,sizeof(file));               // Send file via socket
						read(sockfd,response,sizeof(response));		// Read response from server to determine file is sent successfully
     	 				
						memset(f.content, 0, sizeof(f.content));  // Clear the file struct content to next 4 KB part
					}
				}	

				if(infd != -1)
					close(infd);
    			free(currentSourcePath);
    			free(currentPathToServer);
    			
			}		
     	}		
	}
	else {
	  
	}   
    closedir(dir);
} 

int sendFilesWrapper(char *sourcePath,int sockfd){

	struct stat statbuf;
	char response[10];
	char pathToServer[512];
	file f;
	memset(&f,0,sizeof(f));

	strcpy(pathToServer,sourcePath);
	lstat(sourcePath, &statbuf);
	

	// Set client root path name and mode
	strcpy(f.name,basename(pathToServer));
	f.mode = statbuf.st_mode;

	f.size = 0;
	f.modifiedTime = 0;
	strcpy(f.content," ");

	//Send rooth path of client to server and get status response
	write(sockfd,&f,sizeof(file));
	read(sockfd,response,sizeof(response));
	

	if(strcmp(response,"fail") == 0)
		return 1;
	
	sendFiles(sourcePath,basename(pathToServer),sockfd);
	return 0;		
}


void handle_signal(int signo){
	if(signo == SIGINT)
		printf("SIGINT is handled\n");
	else if(signo == SIGTERM)
		printf("SIGTERM is handled\n");
	done = 1;
}

int main(int argc, char *argv[]){
	DIR *dir;
	int sockfd; 
	struct sockaddr_in servaddr;
	int portNumber;
	char response[10];


	if (argc != 4 ||  ((portNumber = atoi (argv[3]))) <= 0) {
		fprintf(stderr, "Usage: %s [dirName] [ip address] [portnumber]\n", argv[0]);
		return 1;
	}

	if((dir = opendir(argv[1])) == NULL){
		fprintf(stderr, "%s: invalid path\n",argv[1]);
		return 1;
	}

	closedir(dir);

	signal(SIGINT,handle_sigint);
	signal(SIGTERM,handle_sigint);

	// socket create and varification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("Socket creation failed...\n"); 
		exit(1); 
	} 
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign PORT and IP 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr(argv[2]); 
	servaddr.sin_port = htons(portNumber); 

	// connect the client socket to server socket 
	if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) { 
		printf("Connection with the server failed...\n"); 
		exit(1); 
	} 
	else
		printf("Connected to the server..\n"); 

	file f;
	memset(&f,0,sizeof(f));

	strcpy(f.name," ");
	f.mode = 0;
	f.size = END_OF_DIR;
	f.modifiedTime = 0;
	strcpy(f.content," ");

	while(!done){
		if(sendFilesWrapper(argv[1],sockfd)){
			printf("Connection refused because there is a online client with same directory\n");
			break;
		}
		write(sockfd,&f,sizeof(file));
		read(sockfd,response,sizeof(response));
		if(strcmp(response,"shutdown") == 0){
			printf("Server is shutted down.\n");
			break;
		}
	}

	close(sockfd); 

	return 0;
} 
