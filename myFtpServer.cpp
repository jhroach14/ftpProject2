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
#include <vector>
#include <algorithm>
#include <sstream>
#include <time.h>

using namespace std;

//helper methods for user communication
void fatal_error(string output); //print error to stdout stderr and exit
void error(string output);       //print error to stdout stderr
void log(string message);       //log message to stdout

struct BackgroundSocketInfo{

	int fd;
	const char* portnum;

	BackgroundSocketInfo(int fd, const char* portChar);

};

BackgroundSocketInfo::BackgroundSocketInfo(int fd, const char* portChar) {

	this->fd = fd;
	this->portnum = portChar;

}

struct Command {//data structure for command scheduling
	string action;
	string filename;
	pid_t pid;
	string status;//null, pending, active, terminate
	string createTime;

	Command(string input);
	string getCommandId();
	void terminateCommand();
};

Command::Command(string input){

	time_t timer;

	if(input.find("&")!=string::npos){

		for(int i =0; i<input.size();i++){
			if(input.at(i)=='&') {
				input = input.substr(0, i);
				break;
			}
		}
	}

	//node creation
	if (!input.compare(0,3,"get")){
		action = "get";
	}else
	if (!input.compare(0,3,"put")){
		action = "put";
	}else{
		error("invalid command action");
	}

	int index = input.find(" ");
	string fn = input.substr(index+1);

	time(&timer);
	stringstream ss;
	ss<<timer;

	this->action = action;
	this->filename = fn;
	this->pid = getpid();
	this->status = "null";
	this->createTime = ss.str();
};

string Command::getCommandId() {
	return (action+":"+filename+":"+createTime);
}

void Command::terminateCommand() {
	this->status = "terminate";
}

struct CommandQueue{

	vector<Command*> activeCommands;//filenames unique
	vector<Command*> pendingCommands;//not necessarily unique filenames

	void insertCommand(Command*);
	string checkCommandStatus(Command*);
	void removeCommand(vector<Command*>,Command*);
	string terminateCommand(string input);

	private://internal helper methods
	int isFileNameActive(string fileName);
	void insertActive(Command(*));
	void insertPending(Command(*));

};

string CommandQueue::terminateCommand(string input) {

	int index = input.find(" ");
	string commandId = input.substr(index+1);

	for(int i =0; i < activeCommands.size(); i++){

		if(!activeCommands.at(i)->getCommandId().compare(commandId)){
			activeCommands.at(i)->terminateCommand();
			return "success";
		}

	}

	return "not found";

}

void CommandQueue::insertCommand(Command * command) {

	int isFileActive = isFileNameActive(command->filename);

	if(isFileActive == 0){
		insertActive(command);
	} else
	if(isFileActive == 1){
		insertPending(command);
	}

}

void CommandQueue::insertActive(Command* command){

	activeCommands.push_back(command);
	command->status = "active";
}

void CommandQueue::insertPending(Command* command){

	pendingCommands.push_back(command);
	command->status = "pending";
}

string CommandQueue::checkCommandStatus(Command * command) {

	if(!command->status.compare("active")){
		return command->status;
	} else
	if(!command->status.compare("pending")){
		if(isFileNameActive(command->filename) == 0){
			removeCommand(pendingCommands, command);
			insertActive(command);
			return command->status;
		} else{
			return command->status;
		}
	}else{
		return command->status;
	}

}

void CommandQueue::removeCommand(vector<Command *> commandVector, Command * command) {

	vector<Command *>::iterator position = find(commandVector.begin(),commandVector.end(),command);

	if(position!= commandVector.end()){
		commandVector.erase(position);
	}

}

int CommandQueue::isFileNameActive(string fileName) {
	int isActive =0;

	for(int i=0; i<activeCommands.size();i++){
		if(!activeCommands.at(i)->filename.compare(fileName)){
			isActive = 1;
		}
	}

	return isActive;
}

int serverSocketSetup(const char *portNum);  //sets up a server socket
void clientGetFile(Command*, int newSocketFd);
void handleCommand(string input, int newSocketFd);//directs command to proper method for execution
void clientQuit( int newSocketFd);
void clientCd(string input);
void clientDelete(string input);
void clientPutFile(Command*, int newSocketFd);
void handleSpecialCommand(Command* command,CommandQueue*, int newTermSocketFd);

const char * incrementPort(const char* portChar);

BackgroundSocketInfo* serverBackgroundSocketSetup(const char *portNum);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
int main(int argc, char *argv[]) {

	log("starting server");

	int socketFd;   //command channel
	int newSocketFd;

	int termSocketFd; //terminate channel
	int newTermSocketFd;

	//ensure we get a port number
	if(argc == 3 ){

		socketFd = serverSocketSetup(argv[1]);//handles all code needed to create a server socket
		termSocketFd = serverSocketSetup(argv[2]);

	}else{
		cout << "Invalid server startup args. usage: portNumber terminatePortNumber\n";
		return -1;
	}


	CommandQueue* commandQueue = new CommandQueue();

	log("starting terminate thread");
	int termPid = fork();

	if(termPid == 0){//terminate child
		close(socketFd);
		while(true){

			struct sockaddr_storage clientAddress;
			socklen_t socketLength = sizeof(clientAddress);

			if((newTermSocketFd = accept(termSocketFd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
				fatal_error("accept error");
			}

			log("term connection accepted. forking child to handle terminate");
			int pid = fork();

			if(pid == 0){//child

				log("TERMCHILD says helloWorld");

				close(termSocketFd);//child has no use for parent's socket

				char buffer[256];
				int readLength;
				if((readLength = recv(newTermSocketFd,buffer,256,0))==-1){//receive command from socket into buffer
					fatal_error("TERMCHILD read error");
				}
				buffer[readLength]='\0';

				string input(buffer);//use buffer to create a string holding input

				if(!input.compare(0,9,"terminate")){

					string response = commandQueue->terminateCommand(input);

					if(send(newTermSocketFd,response.c_str(),response.size(),0) ==-1){
						error("error on term response");
					}

				}else{
					error("invalid term command");
				}

				exit(EXIT_SUCCESS);
			}// end termchild

			close(newTermSocketFd);

		}//end termloop

	}else{//parent
		close(termSocketFd);
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

					if(!input.compare(0,3,"get") || !input.compare(0,3,"put")){//handle delete

						Command * command = new Command(input);
						commandQueue->insertCommand(command);
						if(send(newSocketFd,command->getCommandId().c_str(),command->getCommandId().size(),0)==-1){
							error("error on commandID send");
						}

						char ackBuf[3];
						recv(newSocketFd,ackBuf,3,0);//sync2
						log(ackBuf);

						if(input.find('&',0)!=string::npos){//if background transfer

							BackgroundSocketInfo* backgroundSocketInfo = serverBackgroundSocketSetup(argv[1]);
							if(send(newSocketFd,backgroundSocketInfo->portnum,4,0)==-1){
								error("error on portnum send");
							}
							recv(newSocketFd,ackBuf,3,0);//sync2
							log(ackBuf);

							int newBackgroundSocketFd;
							if((newBackgroundSocketFd = accept(backgroundSocketInfo->fd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
								fatal_error("accept error");
							}

							int backgroundpid = fork();

							if(backgroundpid==0) {
								close(newSocketFd);
								close(backgroundSocketInfo->fd);
								handleSpecialCommand(command, commandQueue, newBackgroundSocketFd);
								close(newBackgroundSocketFd);
								exit(EXIT_SUCCESS);
							}


						}else{
							handleSpecialCommand(command,commandQueue,newSocketFd);
						}

					} else
					if(!input.compare(0,6,"delete")){

						int index = input.find(" ");
						string filepath = input.substr(index+1);

						int whereIsIt=0;
						Command* command;
						for(int i=0;i<commandQueue->activeCommands.size();i++){
							if(commandQueue->activeCommands.at(i)->filename.compare(filepath)){
								whereIsIt=1;
								command = commandQueue->activeCommands.at(i);
							}
						}
						if(whereIsIt!=1){

							for(int i=0;i<commandQueue->pendingCommands.size();i++){
								if(commandQueue->pendingCommands.at(i)->filename.compare(filepath)){
									whereIsIt=1;
									command = commandQueue->pendingCommands.at(i);
								}
							}
						}

						if(whereIsIt == 0){
							clientDelete(input);
						} else
						if(whereIsIt == 1){
							command->terminateCommand();
						}

					}
					else{

						handleCommand(input,newSocketFd);

					}

				}

			}// end child

			close(newSocketFd);//parent does not need

		}//main loop

	}

}
#pragma clang diagnostic pop

//utility methods to abstract logic from the main method

void handleSpecialCommand(Command* command, CommandQueue* commandQueue, int newTermSocketFd){

	int done = 0;
	//Client requests file
	if(!command->action.compare("get")){

		while(done != 1) {

			if(!commandQueue->checkCommandStatus(command).compare("active")) {

				clientGetFile(command,newTermSocketFd);
				commandQueue->removeCommand(commandQueue->activeCommands,command);
				done = 1;

			} else
			if(!commandQueue->checkCommandStatus(command).compare("terminate")){
				done = 1;
				commandQueue->removeCommand(commandQueue->pendingCommands,command);
			}else{
				usleep(1000);
			}
		}

	}else//Client putting file on server
	if(!command->action.compare("put")){

		while(done != 1) {

			if(!commandQueue->checkCommandStatus(command).compare("active")) {

				clientPutFile(command, newTermSocketFd);
				commandQueue->removeCommand(commandQueue->activeCommands, command);
				done = 1;
			}else
			if(!commandQueue->checkCommandStatus(command).compare("terminate")){
				done = 1;
				commandQueue->removeCommand(commandQueue->pendingCommands,command);
			}else{
				usleep(1000);
			}
		}

	}


}

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

		if(send( newSocketFd, "ACK", 3, 0) < 0){//sync2
			error("error sending file");
		}

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

		if(send( newSocketFd, "ACK", 3, 0) < 0){//sync2
			error("error sending file");
		}
	}
	// makes new directory on FTP server
	if(!input.compare(0,5,"mkdir")){

		log("CHILD running mkdir");
		if(system(input.c_str())==-1){
			error("mkdir failed");
		}

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

BackgroundSocketInfo* serverBackgroundSocketSetup(const char *portNum){

	int socketFd = -1;
	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;

	memset(&prepInfo, 0, sizeof(prepInfo));
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;
	prepInfo.ai_flags = AI_PASSIVE; // sets local host

	int socketBound = 0;
	while(socketBound==0){

		portNum = incrementPort(portNum);

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
			socketBound = 1;
			break;
		}
	}

	if(socketFd == -1){
		fatal_error("socket creation failed");
	}

	if(listen(socketFd, 5) == -1){
		fatal_error("error on listen");
	}
	log("socket listening");

	return new BackgroundSocketInfo(socketFd,portNum);
}

const char * incrementPort(const char* portChar){

	stringstream strValue;
	strValue << portChar;

	int intport;
	strValue >> intport;
	intport++;

	return to_string(intport).c_str();
}

void clientPutFile(Command* command, int newSocketFd){

	string fileName = command->filename;

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
	int hasTerminated =0;
	while(size>0){

		len = recv(newSocketFd, receiveBuffer, 1024, 0);//sync3
		if(send( newSocketFd, "ACK", 3, 0) < 0){//sync4
			error("error sending file");
		}
		totalBytes+=len;
		fwrite(receiveBuffer, sizeof(char), len, receiveFile);
		log(to_string(len)+" bytes received and written");

		size -= len;

		bzero(receiveBuffer,1024);

		if(!command->status.compare("terminate")){
			hasTerminated = 1;
			break;
		}

	}

	log("CHILD "+to_string(totalBytes)+" bytes received from client");
	fclose(receiveFile);

	if(hasTerminated==1){
		if(remove(fileName)!=0){
			error("error on get term delete");
		}
	}

}

void clientGetFile(Command *command, int newSocketFd) {

	string fileName = command->filename;

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
	int hasTerminated = 0;
	while((numRead = fread( sendBuffer, sizeof(char), 1024, sendFile)) > 0){

		log("sending bytes");
		if(send( newSocketFd, sendBuffer, numRead, 0) < 0){
			error("error sending file");
		}
		recv(newSocketFd,ackBuf,3,0);//sync4
		log(ackBuf);

		totalBytes +=numRead;

		bzero(sendBuffer,1024);

		if(!command->status.compare("terminate")){
			hasTerminated = 1;
			break;
		}
	}

	log("CHILD "+to_string(totalBytes)+" bytes sent to client");
	fclose(sendFile);

	if(hasTerminated==1){
		if(remove(fileName)!=0){
			error("error on get term delete");
		}
	}

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

