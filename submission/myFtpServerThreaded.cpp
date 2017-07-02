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
#include <thread>

using namespace std;

//helper methods for user communication
void error(string output); //print error to stdout stderr and exit
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
	log("Command "+this->getCommandId()+" status set to terminate");
	this->status = "terminate";
}

struct CommandQueue{

	vector<Command*> activeCommands;//filenames unique
	vector<Command*> pendingCommands;//not necessarily unique filenames

	void insertCommand(Command*);
	string checkCommandStatus(Command*);
	void removeCommand(vector<Command*>&,Command*);
	string terminateCommand(string input);
	void printOut();

private://internal helper methods
	int isFileNameActive(string fileName);
	void insertActive(Command(*));
	void insertPending(Command(*));

};

void CommandQueue::printOut() {

	cout<<"ACTIVE QUEUE\n";
	for(int i=0;i<activeCommands.size();i++){
		cout<<"  "<<activeCommands.at(i)->getCommandId()<<'\n';
	}

	cout<<"PENDING QUEUE\n";
	for(int i=0;i<pendingCommands.size();i++){
		cout<<"  "<<pendingCommands.at(i)->getCommandId()<<'\n';
	}

}

string CommandQueue::terminateCommand(string input) {

	int index = input.find(" ");
	string commandId = input.substr(index+1);

	for(int i =0; i < activeCommands.size(); i++){

		if(!activeCommands.at(i)->getCommandId().compare(commandId)){
			activeCommands.at(i)->terminateCommand();
			log("command successfully set to terminate in activeCommands");
			return "success";
		}
	}

	for(int i =0; i < pendingCommands.size(); i++){

		if(!pendingCommands.at(i)->getCommandId().compare(commandId)){
			pendingCommands.at(i)->terminateCommand();
			log("command successfully set to terminate in pendingCommands");
			return "success";
		}
	}

	log("command for termination not found in command queues");
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

	log("Command with id "+command->getCommandId()+" inserted into active queue");
	activeCommands.push_back(command);
	command->status = "active";
	printOut();
}

void CommandQueue::insertPending(Command* command){

	log("Command with id "+command->getCommandId()+" inserted into pending queue");
	pendingCommands.push_back(command);
	command->status = "pending";
	printOut();
}

string CommandQueue::checkCommandStatus(Command * command) {

	if(!command->status.compare("active")){
		log("command status = active");
		return command->status;
	} else
	if(!command->status.compare("pending")){
		log("command status = pending");
		if(isFileNameActive(command->filename) == 0){
			log("moving command with id "+command->getCommandId()+" from pending queue to active queue");
			removeCommand(pendingCommands, command);
			insertActive(command);
			return command->status;
		} else{
			log("command status staying pending for now");
			return command->status;
		}
	}else{
		return command->status;
	}

}

void CommandQueue::removeCommand(vector<Command *> &commandVector, Command * command) {

	string commandID = command->getCommandId();

	for(int i=0;i<commandVector.size();i++){

		if(!(commandVector.at(i)->getCommandId()).compare(0,commandID.size(),commandID)){
			commandVector.erase(commandVector.begin()+i);
			log("command with id "+command->getCommandId()+" removed from command queue");
			break;
		}
		if(i==(commandVector.size()-1)){
			log("remove failed. command with id "+ command->getCommandId()+" ");
		}

	}

	printOut();

}

int CommandQueue::isFileNameActive(string fileName) {

	log("checking "+fileName+" for active");
	int isActive =0;

	for(int i=0; i<activeCommands.size();i++){
		string potential = activeCommands.at(i)->filename;
		if(!potential.compare(fileName)){
			isActive = 1;
			log(fileName+" is active");
		}
	}

	return isActive;
}

//utility methods
int serverSocketSetup(const char *portNum);  //sets up a server socket
void clientGetFile(Command*, int newSocketFd);
int handleCommand(string input, int newSocketFd);//directs command to proper method for execution
void clientQuit( int newSocketFd);
void clientCd(string input);
void clientDelete(string input);
void clientPutFile(Command*, int newSocketFd);
void handleSpecialCommand(Command* command,CommandQueue*, int newTermSocketFd);
BackgroundSocketInfo* serverBackgroundSocketSetup(const char *portNum);

//thread execution logic paths
void executeTerminateLoop(int termSocketFd, CommandQueue *commandQueue);
void executeTerminateChild(int newTermSocketFd, CommandQueue* commandQueue);
void executeConnectionLoop(int newSocketFd, CommandQueue* commandQueue, char * argv[], sockaddr_storage clientAddress, socklen_t socketLength);
void executeBackgroundTransfer(Command * command, CommandQueue* commandQueue, int newBackgroundSocketFd);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
int main(int argc, char *argv[]) {

	log("starting server");

	int socketFd;   //command channel
	int newSocketFd;

	int termSocketFd; //terminate channel


	//ensure we get a port number
	if (argc == 3) {

		socketFd = serverSocketSetup(argv[1]);//handles all code needed to create a server socket
		termSocketFd = serverSocketSetup(argv[2]);

	} else {
		cout << "Invalid server startup args. usage: portNumber terminatePortNumber\n";
		return -1;
	}

	CommandQueue *commandQueue = new CommandQueue();//main data structure to handle concurrency

	log("starting terminate thread");
	thread terminateThread(executeTerminateLoop, termSocketFd, commandQueue);
	terminateThread.detach();//dont want to have to wait for it to finish

	while(true){//main server loop

		struct sockaddr_storage clientAddress;
		socklen_t socketLength = sizeof(clientAddress);
		log("main server thread waiting for connection");

		if((newSocketFd = accept(socketFd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
			error("accept error");
		}

		log("connection accepted. creating thread to handle client requests");
		thread connectionThread(executeConnectionLoop, newSocketFd, commandQueue, argv, clientAddress, socketLength);
		connectionThread.detach();//dont want to deal with it after this

	}//main loop

}

//utility methods to abstract logic from the main method

void executeConnectionLoop(int newSocketFd, CommandQueue* commandQueue, char * argv[], sockaddr_storage clientAddress, socklen_t socketLength){

	int saftey=0;
	while(saftey<65){//main child loop

		saftey++;
		char buffer[256];
		int readLength;

		log("client request thread waiting for request");
		if((readLength = recv(newSocketFd,buffer,256,0))==-1){//receive command from socket into buffer
			error("CHILD read error");
		}

		log("received command sending ack to client");
		if(send( newSocketFd, "ACK", 3, 0) < 0){//ack portnum
			error("error ack portnum");
		}

		buffer[readLength]='\0';
		string input(buffer);//use buffer to create a string holding input

		log("client request = "+input);

		if(!input.compare(0,3,"get") || !input.compare(0,3,"put")){//file transfers

			Command * command = new Command(input);
			commandQueue->insertCommand(command);
			log("sending commandID to client");
			if(send(newSocketFd,command->getCommandId().c_str(),command->getCommandId().size(),0)==-1){
				error("error on commandID send");
			}

			log("waiting for commandID ack");
			char ackBuf[3];
			recv(newSocketFd,ackBuf,3,0);//commandID ack
			log(ackBuf);
			log("recieved commandID ack");

			if(input.find('&',0)!=string::npos){//if background transfer

				log("background transfer inititated");

				BackgroundSocketInfo* backgroundSocketInfo = serverBackgroundSocketSetup(argv[2]);
				log("sending port number");
				if(send(newSocketFd,backgroundSocketInfo->portnum,4,0)==-1){
					error("error on portnum send");
				}

				log("waiting for portnum ack");
				recv(newSocketFd,ackBuf,3,0);//sync2
				log(ackBuf);
				log("received portnum ack");

				log("waiting for background transfer connection ");
				int newBackgroundSocketFd;
				if((newBackgroundSocketFd = accept(backgroundSocketInfo->fd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
					error("accept error");
				}

				log("background transfer connection accepted. creating thread to handle background transfer");
				thread backgroundTransferThread(executeBackgroundTransfer, command, commandQueue, newBackgroundSocketFd);
				backgroundTransferThread.detach();


			}else{
				handleSpecialCommand(command,commandQueue,newSocketFd);
			}

		} else
		if(!input.compare(0,6,"delete")){

			int index = input.find(" ");
			string filepath = input.substr(index+1);

			log("delete requested on file "+filepath);

			int whereIsIt=0;
			Command* command;
			for(int i=0;i<commandQueue->activeCommands.size();i++){
				if(commandQueue->activeCommands.at(i)->filename.compare(filepath)){
					whereIsIt=1;
					command = commandQueue->activeCommands.at(i);
					log("file to be deleted is in active queue");
				}
			}
			if(whereIsIt!=1){

				for(int i=0;i<commandQueue->pendingCommands.size();i++){
					if(commandQueue->pendingCommands.at(i)->filename.compare(filepath)){
						whereIsIt=1;
						command = commandQueue->pendingCommands.at(i);
						log("file to be deleted is in pending queue");
					}
				}
			}

			if(whereIsIt == 0){
				log("client to be deleted is in no queue's");
				clientDelete(input);
			} else
			if(whereIsIt == 1){
				command->terminateCommand();
			}

		}
		else{

			saftey += handleCommand(input,newSocketFd);

		}

	}

}// end child

void executeBackgroundTransfer(Command * command, CommandQueue* commandQueue, int newBackgroundSocketFd){


	handleSpecialCommand(command, commandQueue, newBackgroundSocketFd);
	close(newBackgroundSocketFd);

}


void executeTerminateLoop(int termSocketFd, CommandQueue *commandQueue){


	int newTermSocketFd;

	while(true){

		struct sockaddr_storage clientAddress;
		socklen_t socketLength = sizeof(clientAddress);

		log("terminate thread waiting for connection");

		if((newTermSocketFd = accept(termSocketFd,(struct sockaddr*)&clientAddress, &socketLength))==-1){
			error("accept error");
		}

		log("term connection accepted. Starting thread to handle terminate command");
		thread childTerminateThread(executeTerminateChild, newTermSocketFd, commandQueue);
		childTerminateThread.detach();//dont wait for return

	}//end termloop

}

void executeTerminateChild(int newTermSocketFd, CommandQueue* commandQueue){

	log("terminate child says helloWorld");

	char buffer[256];
	int readLength;
	if((readLength = recv(newTermSocketFd,buffer,256,0))==-1){//receive command from socket into buffer
		error("TERMCHILD read error");
	}
	buffer[readLength]='\0';

	string input(buffer);//use buffer to create a string holding input
	log("terminate received: "+input);

	if(!input.compare(0,9,"terminate")){

		string response = commandQueue->terminateCommand(input);

		if(send(newTermSocketFd,response.c_str(),response.size(),0) ==-1){
			error("error on term response");
		}

	}else{
		error("invalid term command");
	}

}

void handleSpecialCommand(Command* command, CommandQueue* commandQueue, int newTermSocketFd){

	log("handling get put for command "+command->getCommandId());

	int done = 0;
	int saftey =0;
	//Client requests file
	if(!command->action.compare("get")){

		while(done != 1&&saftey<65) {

			saftey++;

			if(!commandQueue->checkCommandStatus(command).compare("active")) {

				clientGetFile(command,newTermSocketFd);
				commandQueue->removeCommand(commandQueue->activeCommands,command);
				done = 1;

			} else
			if(!commandQueue->checkCommandStatus(command).compare("terminate")){
				done = 1;
				commandQueue->removeCommand(commandQueue->pendingCommands,command);
			}else{
				usleep(1500000);
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
				usleep(150000);
			}
		}

	}


}

int handleCommand(string input, int newSocketFd){


	int savedStdout;
	int savedStderr;
	int hasQuit =0;

	/*savedStderr = dup(STDERR_FILENO);
	dup2(newSocketFd, STDERR_FILENO);*/

	if(!input.compare("quit")){
		clientQuit(newSocketFd);
		hasQuit = 65;
	}

	//Changes directory
	if(!input.compare(0,2,"cd")){
		clientCd(input);
	}

	// Redirects Standard output into socket then runs ls on server side
	if(!input.compare("ls")){

		log("running ls");

		savedStdout = dup(1);
		close(1);
		dup2(newSocketFd, 1);

		if(system("ls")==-1){
			error("ls failed");
		}
		dup2(savedStdout,1);

		if(send(newSocketFd,"ACK",3,0)==-1){
			error("response ack failed");
		}

		log("ls output sent waiting for ack");
		char ackBuf[3];
		recv(newSocketFd,ackBuf,3,0);
		log("ls ack recieved");

	}

	// Redirects Standard output into socket then runs pwd on server side
	if (!input.compare("pwd")){

		log("running pwd");

		savedStdout = dup(1);
		close(1);
		dup2(newSocketFd, 1);

		if(system("pwd")==-1){
			error("pwd failed");
		}

		dup2(savedStdout,1);

		log("pwd output sent waiting for ack");
		char ackBuf[3];
		recv(newSocketFd,ackBuf,3,0);
		log("pwd ack recieved");
	}
	// makes new directory on FTP server
	if(!input.compare(0,5,"mkdir")){

		log("running mkdir");
		if(system(input.c_str())==-1){
			error("mkdir failed");
		}

	}

	/*dup2(savedStderr,STDERR_FILENO);*/
	return hasQuit;

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
		error("Error on addr info port");
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
		error("socket creation failed");
	}

	if(listen(socketFd, 5) == -1){
		error("error on listen");
	}
	log("socket listening");

	return socketFd;
}

BackgroundSocketInfo* serverBackgroundSocketSetup(const char *portNum) {

	int socketFd = -1;
	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;

	memset(&prepInfo, 0, sizeof(prepInfo));
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;
	prepInfo.ai_flags = AI_PASSIVE; // sets local host

	int socketBound = 0;
	while (socketBound == 0) {

		int code;
		if ((code = getaddrinfo(NULL, "0", &prepInfo, &serverInfo)) != 0) {//uses prep info to fill server info
			cout << gai_strerror(code) << '\n';
			error("Error on addr info port");
		}

		log("addr info success");

		for (struct addrinfo *addrOption = serverInfo; addrOption != NULL; addrOption = addrOption->ai_next) {

			if ((socketFd = socket(addrOption->ai_family, addrOption->ai_socktype, addrOption->ai_protocol)) == -1) {
				error("socket creation failed");
				continue;
			}
			log("socket created");
			if (bind(socketFd, addrOption->ai_addr, addrOption->ai_addrlen) == -1) {
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

	if (socketFd == -1) {
		error("socket creation failed");
	}

	if (listen(socketFd, 5) == -1) {
		error("error on listen");
	}
	log("socket listening");

	string port = "null";

	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	if (getsockname(socketFd, (struct sockaddr *) &sin, &len) == -1){
		error("getsockname");
	}else{
		port = to_string(ntohs(sin.sin_port));
	}

	return new BackgroundSocketInfo(socketFd,port.c_str());
}

const char * incrementPort(const char* portChar){

	stringstream strValue;
	strValue << portChar;

	int intport;
	strValue >> intport;
	intport++;

	char chrarray[5];
	to_string(intport).copy(chrarray,4);
	chrarray[4] = '\0';
	const char * constarray = chrarray;

	cout<<"port incremented to ";
	for(int i=0;i<5;i++){
		cout<<chrarray[i];
	}
	cout<<'\n';
	return constarray;
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

		if(!command->status.compare("terminate")){
			hasTerminated = 1;
			if(send( newSocketFd, "TRM", 3, 0) < 0){//sync4
				error("error sending file");
			}

		}else
		if(send( newSocketFd, "ACK", 3, 0) < 0){//sync4
			error("error sending file");
		}
		totalBytes+=len;

		if(hasTerminated==1){
			break;
		}

		fwrite(receiveBuffer, sizeof(char), len, receiveFile);
		log(to_string(len)+" bytes received and written");

		size -= len;

		bzero(receiveBuffer,1024);

		log("sleeping 1 sec inorder to more easily demo concurrency without large file sizes");
		usleep(1000000);

	}

	log("CHILD "+to_string(totalBytes)+" bytes received from client");
	fclose(receiveFile);

	if(hasTerminated==1){
		if(remove(fileName.c_str())!=0){
			error("error on get term delete");
		}
	}

}

void clientGetFile(Command *command, int newSocketFd) {

	string fileName = command->filename;

	log("CHILD getting file " +fileName);

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

			if(send( newSocketFd, "TERMINATED", 10, 0) < 0){
				error("error sending term notice");
			}
			break;
		}
		log("sleeping 1 sec inorder to more easily demo concurrency without large file sizes");
		usleep(1000000);
	}

	log("CHILD "+to_string(totalBytes)+" bytes sent to client");
	fclose(sendFile);

}

// user communication methods
void log(string message){
	cout << "LOG: " << message << '\n';
}

void error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';

}