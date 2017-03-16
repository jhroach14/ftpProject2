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

using namespace std;

void log(string message);
void fatal_error(string output);

int responseNeeded(string input);
void handleCommand(string input, int socketFd);
int clientSocketSetup(char *argv[]);

void getResponse(int socketFd);

void error(string output);

void getFileFromServer(string input,int socketFd);

void putFileOnServer(string input, int socketFd);

int debug = 1;

int main(int argc, char *argv[]){

	log("starting client");

	FILE* sendFile;
	FILE* receiveFile;
	char buffer[256];
	int socketFd;


	if(argc < 3){
		printf("Usage %s hostname port\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	socketFd = clientSocketSetup(argv);

	string input;

	while(true){//prompt loop
		cout << "myftp>";
		getline(cin,input);

		handleCommand(input, socketFd);

	}//end of prompt loop

}//end of main

void handleCommand(string input, int socketFd){

	if(send(socketFd,input.c_str(),input.length(),0)==-1){
		fatal_error("send failed");
	}

	int needResponse = responseNeeded(input); //1 indicates the client expects a message back from server

	if(!input.compare("quit")){
		log("PARENT executing quit");
		close(socketFd);
		sleep(1);
		exit(EXIT_SUCCESS);

	}

	if(input.compare(input.length()-1,1,"&")){//foreground

		log("executing command in single process");

		if(needResponse) {

			log("executing command requiring response");
			getResponse(socketFd);

		}

		if(!input.compare(0,3,"get")){

			getFileFromServer(input,socketFd);

		}

		if(!input.compare(0,3,"put")){

			putFileOnServer(input,socketFd);

		}

	}else{//background

	}

}

void putFileOnServer(string input, int socketFd){

	int index = input.find(" ");
	string fileName = input.substr(index+1);

	log("putting file " +fileName+" on server");

	FILE * sendFile;
	if((sendFile = fopen(fileName.c_str(), "r"))==NULL){
		error("file open failed for put file "+fileName);
	}

	fseek(sendFile,0,SEEK_END);
	string size = to_string(ftell(sendFile));
	rewind(sendFile);

	log("filesize "+size);
	if(send( socketFd, size.c_str(), sizeof(size.c_str()), 0) < 0){//syc1
		error("error sending file size");
	}
	char ackBuf[3];
	recv(socketFd,ackBuf,3,0);//sync2
	log(ackBuf);

	char sendBuffer[1024];// 1 char = 1 byte
	int numRead;
	int totalBytes=0;
	while((numRead = fread( sendBuffer, sizeof(char), 1024, sendFile)) > 0){

		log("sending bytes");
		if(send( socketFd, sendBuffer, numRead, 0) < 0){//sync3
			error("error sending file");
		}
		recv(socketFd,ackBuf,3,0);//sync4
		log(ackBuf);

		totalBytes +=numRead;

		//check terminate

		bzero(sendBuffer,1024);
	}

	log(to_string(totalBytes)+" bytes sent to server");

	fclose(sendFile);

}

void getFileFromServer(string input,int socketFd){

	int index = input.find(" ");
	string fileName = input.substr(index+1);

	log("getting file "+fileName+" from server and writing to working directory");

	FILE * receiveFile;
	if((receiveFile = fopen(fileName.c_str(), "w"))==NULL){
		error("file open failed for get file");
	}
	log("file opened for writing");

	char sizeBuffer[64];
	recv(socketFd,sizeBuffer,64,0);//sync1
	if(send( socketFd, "ACK", 3, 0) < 0){//sync2
		error("error sending file");
	}

	string sizeStr(sizeBuffer);
	log("server file size "+sizeStr);
	int size = stoi(sizeStr);

	int len;
	int totalBytes=0;
	char receiveBuffer[1024];

	while(size>0){

		len = recv(socketFd, receiveBuffer, 1024, 0);//sync3
		if(send( socketFd, "ACK", 3, 0) < 0){//sync4
			error("error sending file");
		}
		totalBytes+=len;
		fwrite(receiveBuffer, sizeof(char), len, receiveFile);
		log(to_string(len)+" bytes received and written");

		size -= len;
	}

	log(to_string(totalBytes)+" bytes received from server");

	fclose(receiveFile);

}

void getResponse(int socketFd){

	char responseBuffer[256];
	int bytesRead;


	if ((bytesRead = recv(socketFd, responseBuffer, 255, 0)) == -1) {
		fatal_error("recv error");
	}

	responseBuffer[bytesRead] = '\0';

	for (int i = 0; i < bytesRead; i++) {
		printf("%c", responseBuffer[i]);
	}

}

int responseNeeded(string input){

	int needResponse = 0;

	if(!input.compare(0,2,"ls")){
		needResponse = 1;
	}else
	if(!input.compare(0,3,"pwd")){
		needResponse=1;
	}/*else
	if(!input.compare(0,3,"get")){
		needResponse=1;
	}else
	if(!input.compare(0,3,"put")){
		needResponse=1;
	}*/

	return needResponse;

}

int clientSocketSetup(char *argv[]){

	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;

	memset(&prepInfo, 0, sizeof prepInfo);
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;

	int code;
	if((code = getaddrinfo(argv[1],argv[2],&prepInfo,&serverInfo))!=0){
		cout<<gai_strerror(code)<<'\n';
		fatal_error("Error on addr info");
	}
	int socketFd=-1;
	for(struct addrinfo *addrOption = serverInfo; addrOption!=NULL; addrOption = addrOption->ai_next){

		if((socketFd = socket(addrOption->ai_family, addrOption->ai_socktype, addrOption->ai_protocol)) == -1){
			//error("socket creation failed");
			continue;
		}
		//log("socket created");

		if (connect(socketFd, addrOption->ai_addr, addrOption->ai_addrlen) == -1) {
			close(socketFd);
			//error("error on connect");
			continue;
		}
		//log("connected");
		freeaddrinfo(serverInfo);

		break;
	}

	if(socketFd == -1){
		fatal_error("socket creation failed");
	}

	log("Socket creation successful with fd "+to_string(socketFd));

	return socketFd;

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

void log(string message){
	if(debug) {
		cout << "LOG: " << message << '\n';
	}
}

//cout << "User input "<<input<<" sent to server..." << endl;




/*if(!input.compare(input.length()-1,1,"&")){//execute command in child process

	input=input.substr(0, input.length()-1);
	log("forking process to execute " + input);
	int pid=fork();
	if(pid==0){//child



		if(send(socketFd,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}

		//cout << "User input "<<input<<" sent to server..." << endl;

		if(needResponse) {
			log("CHILD executing command requiring response");
			char responseBuffer[256];
			int bytesRead;
			if ((bytesRead = recv(socketFd, responseBuffer, sizeof(responseBuffer), 0)) == 1) {
				fatal_error("recv error");
			}

			responseBuffer[bytesRead] = '\0';

			for (int i = 0; i < bytesRead; i++) {
				printf("%c", responseBuffer[i]);
			}
			log("CHILD finished printing server response");
		}

		if(input.length()>2&&!input.compare(0,3,"get")){
			log("CHILD executing get");
			int index = input.find(" ");
			string fileName = input.substr(index+1);
			receiveFile = fopen(fileName.c_str(), "w");
			int len;
			while((len = recv(socketFd, buffer, 256, 0) > 0)){
				fwrite(buffer, sizeof(char), len, receiveFile);
			}
			fclose(receiveFile);

		}

		if(input.length()>2&&!input.compare(0,3,"put")){
			log("CHILD executing put");
			int index = input.find(" ");
			string fileName = input.substr(index);
			sendFile = fopen(fileName.c_str(), "w");
			fseek(sendFile, 0, SEEK_END);
			long size = ftell(sendFile);
			rewind(sendFile);
			//log("reached rewind");
			char sendBuffer[size];
			fgets(sendBuffer, size, sendFile);
			int n;
			//log("reached while");
			while((n = fread(sendBuffer, sizeof(char), size, sendFile)) > 0){
				if(send(socketFd, sendBuffer, n, 0) < 0){
					//cout << "Error sending file";
				}
				bzero(sendBuffer, size);
			}
		}

		if(!input.compare("quit")){
			log("CHILD executing quit");
			close(socketFd);
			sleep(1);
			return 0;

		}
		log("finished execution, killing CHILD");
		raise(SIGKILL);
	}
	else{
		waitpid((pid_t)pid, NULL, 0);
		continue;
	}
}
else{*///execute command normally (single process)


//}