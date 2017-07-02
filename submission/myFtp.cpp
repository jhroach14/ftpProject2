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
void handleCommand(string input, int socketFd,char* argv[]);
int clientSocketSetup(char *argv[],int);

void getResponse(int socketFd, string);

void error(string output);

void getFileFromServer(string input,int socketFd);

void putFileOnServer(string input, int socketFd);

void getGPResponse(int socketFd);

void terminateCommand(char*argv[],string input);

int debug = 0;

int main(int argc, char *argv[]){

	log("starting client");

	FILE* sendFile;
	FILE* receiveFile;
	char buffer[256];
	int socketFd;


	if(argc < 4){
		printf("Usage %s hostname nport tport\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	socketFd = clientSocketSetup(argv,2);

	string input;

	while(true){//prompt loop
		cout << "myftp>";
		getline(cin,input);

		handleCommand(input, socketFd,argv);

	}//end of prompt loop

}//end of main

void handleCommand(string input, int socketFd,char* argv[]){

	int needResponse = responseNeeded(input); //1 indicates the client expects a message back from server

	if(!input.compare("quit")){
		log("executing quit");
		if(send(socketFd,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
		char ackBuf[3];
		recv(socketFd,ackBuf,3,0);//commandID ack
		log(ackBuf);
		log("received quit ack");
		close(socketFd);
		sleep(1);
		exit(EXIT_SUCCESS);

	}
	if(!input.compare(0,9,"terminate")){
		terminateCommand(argv,input);
	}else
	if(input.compare(input.length()-1,1,"&")){//foreground

		log("executing command in single process");


		if(send(socketFd,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
		char ackBuf[3];
		recv(socketFd,ackBuf,3,0);//commandID ack
		log(ackBuf);
		log("received foreground command ack");

		if(!input.compare(0,3,"get")){
			getGPResponse(socketFd);
			getFileFromServer(input,socketFd);

		}else
		if(!input.compare(0,3,"put")){
			getGPResponse(socketFd);
			putFileOnServer(input,socketFd);

		}else
		if(needResponse) {

			log("executing command requiring response");
			string commandType;
			if(!input.compare(0,2,"ls")){
				commandType = "ls";
			}else{
				commandType = "pwd";
			}
			getResponse(socketFd,commandType);

		}

	}else
	if((input.compare(0,3,"get")!=0)&&(input.compare(0,3,"put")!=0)){//background not get or put

		log("executing command in child process");

		input = input.substr(0,input.size()-1);//cut off ampersand then do normal

		if(send(socketFd,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
		char ackBuf[3];
		recv(socketFd,ackBuf,3,0);//commandID ack
		log(ackBuf);
		log("received background command ack");
		if(needResponse) {
			string commandType;
			if(!input.compare(0,2,"ls")){
				commandType = "ls";
			}else{
				commandType = "pwd";
			}
			log("executing command requiring response");
			getResponse(socketFd,commandType);

		}

	}else{//get or put background
		log("executing get/put command in background");
		if(send(socketFd,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
		log("command sent to server");

		char ackBuf[3];
		recv(socketFd,ackBuf,3,0);//commandID ack
		log(ackBuf);
		log("recieved background get/put command ack");

		getGPResponse(socketFd);

		log("waiting for port num");
		char portNum[5];
		if(recv(socketFd,portNum,4,0)==-1){
			error("recv portnum error");
		}
		portNum[4] = '\0';

		log("recieved portnum sending ack");
		if(send( socketFd, "ACK", 3, 0) < 0){//ack portnum
			error("error ack portnum");
		}

		int pid=fork();
		if(pid==-1){
			fatal_error("error forking");
		}
		if(pid == 0){

			close(socketFd);

			char * fakeArgv[3];
			fakeArgv[1] = argv[1];
			fakeArgv[2] = portNum;

			int backgroundSocketFd = clientSocketSetup(fakeArgv,2);

			input = input.substr(0,input.length()-1);

			if(!input.compare(0,3,"get")){

				getFileFromServer(input,backgroundSocketFd);

			}else
			if(!input.compare(0,3,"put")){

				putFileOnServer(input,backgroundSocketFd);

			}

			close(backgroundSocketFd);

			log("child finish executing, killing child");
			exit(EXIT_SUCCESS);
		}//end child

	}

}

void terminateCommand(char*argv[],string input){

	log("terminating command...");

	int termSocketFd = clientSocketSetup(argv,3);//connect to terminate port

	if(send(termSocketFd,input.c_str(),input.size(),0)==-1){
		log("error on terminate send");
	}

	char buffer[256];
	int readLength;
	if((readLength = recv(termSocketFd,buffer,256,0))==-1){//receive command from socket into buffer
		fatal_error("terminate recv error");
	}
	buffer[readLength]='\0';
	log(buffer);

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
		string ack(ackBuf);

		totalBytes +=numRead;

		if(!ack.compare(0,3,"TRM")){
			log("terminate recieved stopping put of "+ fileName);
			break;
		}

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
	recv(socketFd,sizeBuffer,64,0);//get file size
	if(send( socketFd, "ACK", 3, 0) < 0){//sync2
		error("error sending file");
	}

	string sizeStr(sizeBuffer);
	log("server file size "+sizeStr);
	int size = stoi(sizeStr);

	int len;
	int totalBytes=0;
	char receiveBuffer[1024];
	int hasTerminated = 0;

	while(size>0){

		len = recv(socketFd, receiveBuffer, 1024, 0);//sync3
		string termString = "TERMINATED";
		for(int i = 0;i<10;i++){
			if(receiveBuffer[i]!=termString.at(i)){
				break;
			}
			if(i==9){
				log("command terminated. write interupted");
				hasTerminated=1;
				size = -1;
			}
		}
		if(hasTerminated!=1){
			if(send( socketFd, "ACK", 3, 0) < 0){//sync4
				error("error sending file");
			}

			totalBytes+=len;
			fwrite(receiveBuffer, sizeof(char), len, receiveFile);
			log(to_string(len)+" bytes received and written");

			size -= len;
		}
	}

	log(to_string(totalBytes)+" bytes received from server");
	fclose(receiveFile);

	if(hasTerminated==1){
		if(remove(fileName.c_str())!=0){
			error("error on get term delete");
		}
	}

}

void getGPResponse(int socketFd){

	char responseBuffer[256];
	int bytesRead;

	log("waiting for command id");
	if ((bytesRead = recv(socketFd, responseBuffer, 255, 0)) == -1) {//receive commandID
		fatal_error("recv error");
	}
	log("received command id sending ack");
	if(send( socketFd, "ACK", 3, 0) < 0){//ack command id
		error("error sending file");
	}

	responseBuffer[bytesRead] = '\0';

	cout<<"COMMAND ID: ";
	for (int i = 0; i < bytesRead; i++) {
		printf("%c", responseBuffer[i]);
	}
	cout<<'\n';



}

void getResponse(int socketFd,string commandType){

	char responseBuffer[256];
	int bytesRead;

	log("waiting for server response");
	if ((bytesRead = recv(socketFd, responseBuffer, 255, 0)) == -1) {
		fatal_error("recv error");
	}


	responseBuffer[bytesRead] = '\0';
	if(strcmp(responseBuffer, "ACK")!=0){//not ack means we need to wait

		int hasAck =0;
		for (int i = 0; i < bytesRead; i++) {
			if(responseBuffer[i]=='A'&&responseBuffer[i+1]=='C'&&responseBuffer[i+2]=='K'){
				hasAck = 1;
				break;
			}
			printf("%c", responseBuffer[i]);
		}

		if(hasAck==0&&(!commandType.compare(0,2,"ls"))){

			log("waiting for ack");
			char ackBuf[3];
			recv(socketFd,ackBuf,3,0);

		}

		log("recieved server response sending ack");
		if(send(socketFd,"ACK",3,0)==-1){
			error("response ack failed");
		}
	}else{
		log("sending ack");
		if(send(socketFd,"ACK",3,0)==-1){
			error("response ack failed");
		}
	}


}

int responseNeeded(string input){

	int needResponse = 0;

	if(!input.compare(0,2,"ls")){
		needResponse = 1;
	}else
	if(!input.compare(0,3,"pwd")){
		needResponse=1;
	}
	if(!input.compare(0,3,"get")){
		needResponse=1;
	}else
	if(!input.compare(0,3,"put")){
		needResponse=1;
	} else
	if(!input.compare(0,9,"terminate")){
		needResponse = 1;
	}

	return needResponse;

}

int clientSocketSetup(char *argv[],int portLoc){

	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;

	memset(&prepInfo, 0, sizeof prepInfo);
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;

	int code;
	if((code = getaddrinfo(argv[1],argv[portLoc],&prepInfo,&serverInfo))!=0){
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
		log("connected to server");
		freeaddrinfo(serverInfo);

		break;
	}

	if(socketFd == -1){
		fatal_error("socket creation failed");
	}

	log("Socket creation completed with fd "+to_string(socketFd));

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
