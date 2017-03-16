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
#include <sys/ioctl.h>
#include <math.h>

using namespace std;

//helper methods for user communication
void fatal_error(string output); //print error to stdout stderr and exit
void error(string output);       //print error to stdout stderr
void log(string message);       //log message to stdout


int serverSocketSetup(const char *portNum);  //sets up a server socket
void clientGetFile(string fileName, int newSocketFd);
void handleCommand(string input, int newSocketFd);//directs command to proper method for execution
void clientQuit( int newSocketFd);
void clientCd(string input);
void clientDelete(string input);
void clientPutFile(string input, int newSocketFd);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
int main(int argc, char *argv[]) {

	log("starting server");

	int socketFd;
	int newSocketFd;

	//stop children becoming zombies
	/*if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		fatal_error("zombie stopped");
	}*/

	//ensure we get a port number
	if(argc == 2 ){

		socketFd = serverSocketSetup(argv[1]);//handles all code needed to create a server socket

	}else{
		cout << "Invalid server startup args. usage: portNumber\n";
		return -1;
	}

	log("waiting for connection");

	while(true){//main server loop

		struct sockaddr_storage clientAddress;
		socklen_t socketLength = sizeof(clientAddress);

		if((newSocketFd = accept(socketFd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
			fatal_error("accept error");
		}

		log("connection accepted. forking child to handle requests");
		int pid = fork();

		if(pid == 0){//child

			log("CHILD says helloWorld");

			close(socketFd);//child has no use for parent's socket

			while(true){//main child loop

				char buffer[256];
				int readLength;
				if((readLength = recv(newSocketFd,buffer,256,0))==-1){//receive command from socket into buffer
					fatal_error("CHILD read error");
				}
				buffer[readLength]='\0';

				string input(buffer);//use buffer to create a string holding input

				handleCommand(input,newSocketFd);
			}

		}// end child

		close(newSocketFd);//parent does not need

	}//main loop

}
#pragma clang diagnostic pop

//utility methods to abstract logic from the main method
void handleCommand(string input, int newSocketFd){


	int savedStdout;
	int savedStderr;

	log("CHILD input: "+input);

	/*savedStderr = dup(STDERR_FILENO);
	dup2(newSocketFd, STDERR_FILENO);*/

	if(!input.compare("quit")){
		clientQuit(newSocketFd);
	}

	//Changes directory
	if(!input.compare(0,2,"cd")){
		clientCd(input);
	}

	// Redirects Standard output into socket then runs ls on server side
	if(!input.compare("ls")){

		log("CHILD running ls");

		savedStdout = dup(1);
		close(1);
		dup2(newSocketFd, 1);

		if(system("ls")==-1){
			error("ls failed");
		}

		dup2(savedStdout,1);

	}

	// Redirects Standard output into socket then runs pwd on server side
	if (!input.compare("pwd")){

		log("CHILD running pwd");

		savedStdout = dup(1);
		close(1);
		dup2(newSocketFd, 1);

		if(system("pwd")==-1){
			error("pwd failed");
		}

		dup2(savedStdout,1);
	}

	// Removes file
	if (!input.compare(0,6,"delete")){

		clientDelete(input);

	}
	// makes new directory on FTP server
	if(!input.compare(0,5,"mkdir")){

		log("CHILD running mkdir");
		if(system(input.c_str())==-1){
			error("mkdir failed");
		}

	}

	//Client requests file
	if(!input.compare(0,3,"get")){

		clientGetFile(input, newSocketFd);

	}
	//Client putting file on server
	if(!input.compare(0,3,"put")){

		clientPutFile(input,newSocketFd);

	}

	/*dup2(savedStderr,STDERR_FILENO);*/

}

void clientDelete(string input){

	int index = input.find(" ");
	string filepath = input.substr(index);

	string remove = "rm";
	remove.append(filepath);

	log("CHILD running delete on "+filepath);
	if(system(remove.c_str())==-1){
		error("delete failed");
	}

}

void clientCd(string input){

	int index = input.find(" ");
	string filepath = input.substr(index+1);

	log("CHILD requested cd dir is "+filepath);

	if(chdir(filepath.c_str())!=0){
		error("CHILD chirdir failed");
	}

	log("CHILD cd success");

}

void clientQuit( int newSocketFd){

	log("CHILD quit");
	close(newSocketFd);
	exit(EXIT_SUCCESS);

}

int serverSocketSetup(const char *portNum){

	int socketFd = -1;
	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;

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

	return socketFd;
}

void clientPutFile(string input, int newSocketFd){

	int index = input.find(" ");
	string fileName = input.substr(index +1);

	log("CHILD putting file "+fileName);

	FILE * receiveFile;
	if((receiveFile = fopen(fileName.c_str(), "w"))==NULL){
		error("file open failed for put file");
	}
	log("CHILD opened file for writing");

	char sizeBuffer[64];
	recv(newSocketFd,sizeBuffer,64,0);//sync1
	if(send( newSocketFd, "ACK", 3, 0) < 0){//sync2
		error("error sending file");
	}

	string sizeStr(sizeBuffer);
	int size = stoi(sizeStr);
	log("CHILD file size "+sizeStr);

	int len;
	int totalBytes=0;
	char receiveBuffer[1024];
	while(size>0){

		len = recv(newSocketFd, receiveBuffer, 1024, 0);//sync3
		if(send( newSocketFd, "ACK", 3, 0) < 0){//sync4
			error("error sending file");
		}
		totalBytes+=len;
		fwrite(receiveBuffer, sizeof(char), len, receiveFile);
		log(to_string(len)+" bytes received and written");

		size -= len;
	}

	log("CHILD "+to_string(totalBytes)+" bytes received from client");

	fclose(receiveFile);

}

void clientGetFile(string input, int newSocketFd){

	int index = input.find(" ");
	string fileName = input.substr(index +1);

	log("CHILD getting file" +fileName);

	FILE * sendFile;
	if((sendFile = fopen(fileName.c_str(), "r"))==NULL){
		error("file open failed for get file");
	}

	fseek(sendFile,0,SEEK_END);
	string size = to_string(ftell(sendFile));
	rewind(sendFile);

	log("CHILD file size "+size);
	if(send( newSocketFd, size.c_str(), sizeof(size.c_str()), 0) < 0){//syc1
		error("error sending file size");
	}

	char ackBuf[3];
	recv(newSocketFd,ackBuf,3,0);//sync2
	log(ackBuf);

	char sendBuffer[1024];// 1 char = 1 byte
	int numRead;
	int totalBytes=0;
	while((numRead = fread( sendBuffer, sizeof(char), 1024, sendFile)) > 0){

		log("sending bytes");
		if(send( newSocketFd, sendBuffer, numRead, 0) < 0){
			error("error sending file");
		}
		recv(newSocketFd,ackBuf,3,0);//sync4
		log(ackBuf);

		totalBytes +=numRead;

		//check terminate

		bzero(sendBuffer,1024);
	}

	log("CHILD "+to_string(totalBytes)+" bytes sent to client");

	fclose(sendFile);

}

// user communication methods
void log(string message){
	cout << "LOG: " << message << '\n';
}

void fatal_error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';
	exit(EXIT_FAILURE);

}

void error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';

}

