#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

void fatal_error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';
	exit(EXIT_FAILURE);

}

struct node{
	int process
	string filename
	pid_t prid
	node *next;
}

Node(Node *next, int process, string filename, pid_t prid){
	 this->next = NULL;
	 this->process = process;
	 this->filename = filename;
	 this->prid = prid;
 }
 

void error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';

}

void log(string message){
	cout<<message<<'\n';
}

int main(int argc, char *argv[]) {
	node *head = NULL;
	node *tail = NULL;
	node *conductor = NULL;
	int socketFd = -1;
	int newSocketFd;
	const char * portNum;
	char buffer[256];
	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;
	struct sockaddr_storage clientAddress;
	socklen_t socketLength = sizeof(clientAddress);

	//stop children becoming zombies
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		fatal_error("zombie stopped");
	}

	//ensure we get a port number
	if(argc !=2 ){
		cout << "Invalid server command. exiting\n";
		return -1;
	}

	portNum = argv[1];
	memset(&prepInfo, 0, sizeof(prepInfo));
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;
	prepInfo.ai_flags = AI_PASSIVE; // sets local host

	int code;
	if((code=getaddrinfo(NULL,portNum,&prepInfo,&serverInfo))!=0){//uses prep info to fill server info
		cout<<gai_strerror(code)<<'\n';
		fatal_error("Error on addr info port");
	}
	
	log("addr info success");

	for(struct addrinfo *addrOption = serverInfo; addrOption!=NULL; addrOption = addrOption->ai_next){

		if((socketFd = socket(addrOption->ai_family, addrOption->ai_socktype, addrOption->ai_protocol)) == -1){
			error("socket creation failed");
			continue;
		}
		log("socket created");
		if(bind(socketFd, addrOption->ai_addr, addrOption->ai_addrlen) == -1){
			close(socketFd);
			error("bind failed");
			continue;
		}
		log("socket bound");
		freeaddrinfo(serverInfo);

		break;
	}

	if(socketFd == -1){
		fatal_error("socket creation failed");
	}

	if(listen(socketFd, 5) == -1){
		fatal_error("error on listen");
	}
	log("socket listening");



	while(true){

		log("waiting for connection");

		if((newSocketFd = accept(socketFd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
			fatal_error("accept error");
		}
		log("connection accepted");
		
		
		// Processes input
		int count=0;
		while(count<10){
			count++;
			int readLength;
			if((readLength = recv(newSocketFd,buffer,255,0))==-1){
				fatal_error("CHILD read error");		
			}		
		}
		buffer[readLength]='\0';
		string input(buffer);
		int length = (int) input.length();
		log("CHILD input read:\n"+input);
		cout<<"CHILD length: "<<length<<"\n";
		
		// Creates Node
		if (!input.compare(0,3,"get") || !input.compare(0,3,"put"){
			node *temp = new node;
			// Asigns process, 0 = get, 1 = put
			if (!input.compare(0,3,"get"))
				temp->process = 0;
			else
				temp->process = 1;
			
			int index = input.find(" ");
			string fileName = input.substr(index);
			temp->filename = fileName;
		}
		
		//Checks if it needs to block for other processes
		node* conductor = head;
		while(conductor->next != NULL){
			if ((temp->process == 1) && (temp->filename == conductor->next->filename)){
				waitpid(conductor->next->pwid, &status, 0)
				sleep(10)
				
				// Removes node
				node *del = conductor->next;
				conductor->next = conductor->next->next;
				delete(del);
				conductor == NULL;
			}else ((temp->process == 0) && (conductor->next->process == 1) && (temp->filename == conductor->next->filename)){
				waitpid(conductor->next->prid, &status, 0)
				sleep(10)
			
				// Removes node
				node *del = conductor->next;
				conductor->next = conductor->next->next;
				delete(del);
				conductor == NULL;
			}
		}
		
		
		
		// Adds Node to linked list
		if (*head == NULL){
			head = temp;
			tail = temp;
		}else{
			tail->next = temp;
			tail = temp;
		}
		
		
		
		
		pid_t pid =fork();
		if(pid == 0){//child
			close(socketFd);
			int savedStdout;
			int savedStderr;


				if(!input.compare("quit")){
					log("CHILD quit");
					close(newSocketFd);
					exit(EXIT_SUCCESS);
				}

				//Changes directory
				if(!input.compare(0,2,"cd")){
					log("CHILD running cd");
					int index = input.find(" ");
					cout<<"CHILD input length = "<<input.length()<<" index = "<<index<<"\n";
					string filepath = input.substr(index+1);
					log("CHILD requested dir is "+filepath);
					if(chdir(filepath.c_str())!=0){
						fatal_error("CHILD chirdir failed");
					}
				}

				//command execution goes here

				// Redirects Standard output into socket then runs ls on server side
				if(!input.compare("ls")){


					log("CHILD running ls");
					savedStdout = dup(STDOUT_FILENO);
					savedStderr = dup(STDERR_FILENO);
					dup2(newSocketFd, STDOUT_FILENO);
					dup2(newSocketFd, STDERR_FILENO);
					system("ls");
					dup2(savedStdout,STDOUT_FILENO);
					dup2(savedStderr,STDERR_FILENO);
				}

				// Redirects Standard output into socket then runs pwd on server side
				if (!input.compare("pwd")){
					savedStdout = dup(STDOUT_FILENO);
					savedStderr = dup(STDERR_FILENO);
					log("CHILD running pwd");
					dup2(newSocketFd, STDOUT_FILENO);
					dup2(newSocketFd, STDERR_FILENO);
					system("pwd");
					dup2(savedStdout,STDOUT_FILENO);
					dup2(savedStderr,STDERR_FILENO);
				}

				// Removes file
				if (!input.compare(0,6,"delete")){
					log("CHILD running delete");
					int index = input.find(" ");
					string filepath = input.substr(index);
					string remove = "rm";
					remove.append(filepath);
					system(remove.c_str());
				}
				// makes new directory on FTP server
				if(!input.compare(0,5,"mkdir")){
					log("CHILD running mkdir");
					system(input.c_str());
				}
				//Client requests file
				if(!input.compare(0,3,"get")){
					log("CHILD running get");
					int index = input.find(" ");
					string fileName = input.substr(index);
					FILE * sendFile = fopen(fileName.c_str(), "r");
					fseek(sendFile, 0, SEEK_END);
					long size = ftell(sendFile);
					rewind(sendFile);
					char sendBuffer[size];
					fgets(sendBuffer, size, sendFile);
					int n;
					while((n = fread(sendBuffer, sizeof(char), size, sendFile)) > 0){
						if(send(newSocketFd, sendBuffer, (size_t) n, 0) < 0){
							cout << "Error sending file";
						}
						bzero(sendBuffer, size);
					}
				}
				//Client putting file on server
				if(!input.compare(0,3,"put")){
					log("CHILD running put");
					int index = input.find(" ");
					string fileName = input.substr(index);
					FILE * receiveFile = fopen(fileName.c_str(), "w");
					int len;
					while((len = recv(newSocketFd, buffer, 256, 0) > 0)){
						fwrite(buffer, sizeof(char), len, receiveFile);
					}
					fclose(receiveFile);
				}
			}
		}
		
		tail->prid = pid;
		
		
		
		close(newSocketFd);

	}


	return 0;
}