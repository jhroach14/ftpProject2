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
#include <vector>

using namespace std;

void fatal_error(string);
void error(string);
void log(string);
int handleCommand(string, int, bool);
void handleTermination(int);

FILE* sendFile;
FILE* receiveFile;

int socketFd1 = -1;
int socketFd2 = -1;
//int newsocketFd1;
const char * portNum;
char buffer[256];
struct addrinfo prepInfo;
struct addrinfo *serverInfo1, *serverInfo2;
struct sockaddr_storage clientAddress;
socklen_t socketLength;
struct CommandInfo{
	int commandId;
	int processId;
	string commandFile;
	string commandName;
	CommandInfo(int id, string file, string name){
		commandId=id;
		processId=-1;
		commandFile=file;
		commandName=name;
		//strcpy(commandFile, file);
		//strcpy(commandName, name);
	}
};
std::vector<CommandInfo*> commands;

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
	cout<<message<<'\n';
}

int main(int argc, char *argv[]){
	if(argc < 4){
		printf("Usage %s hostname nport tport\n", argv[0]);
	}

	//Socket prep work 
	memset(&prepInfo, 0, sizeof prepInfo);
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;


	int code1, code2;
	//Getting addrinfo for nport
	if((code1 = getaddrinfo(argv[1],argv[2],&prepInfo,&serverInfo1))!=0){
		cout<<gai_strerror(code1)<<'\n';
		fatal_error("Error on addr info for nport");
	}

	//Getting addrinfo for tport
	if((code2 = getaddrinfo(argv[1],argv[3],&prepInfo,&serverInfo2))!=0){
		cout<<gai_strerror(code2)<<'\n';
		fatal_error("Error on addr info for tport");
	}

	//Connect nport socket 
	for(struct addrinfo *addrOption = serverInfo1; addrOption!=NULL; addrOption = addrOption->ai_next){

		if((socketFd1 = socket(addrOption->ai_family, addrOption->ai_socktype, addrOption->ai_protocol)) == -1){
			//error("socket creation failed");
			continue;
		}
		//log("socket created");

		if (connect(socketFd1, addrOption->ai_addr, addrOption->ai_addrlen) == -1) {
			close(socketFd1);
			//error("error on connect");
			continue;
		}
		//log("connected");
		freeaddrinfo(serverInfo1);

		break;
	}

	//Connect tport socket
	for(struct addrinfo *addrOption = serverInfo2; addrOption!=NULL; addrOption = addrOption->ai_next){

		if((socketFd2 = socket(addrOption->ai_family, addrOption->ai_socktype, addrOption->ai_protocol)) == -1){
			//error("socket creation failed");
			continue;
		}
		//log("socket created");

		if (connect(socketFd2, addrOption->ai_addr, addrOption->ai_addrlen) == -1) {
			close(socketFd2);
			//error("error on connect");
			continue;
		}
		//log("connected");
		freeaddrinfo(serverInfo2);

		break;
	}


	if(socketFd1 == -1){
		fatal_error("socket1 creation failed");
	}


	if(socketFd2 == -1){
		fatal_error("socket2 creation failed");
	}


	string input;

	while(true){//prompt loop
		int needResponse = 0;
		cout << "myftp>";
		getline(cin,input);


		bool isFileOperation = false;
		if(!input.compare(0,2,"ls") || !input.compare(0,3,"pwd")){
			needResponse = 1;
		}
		if(!input.compare(0,3,"get") || !input.compare(0,3,"put")){
			needResponse = 1;
			isFileOperation = true;
		}
		
		if(!input.compare(input.length()-1,1,"&")){//execute command in child process
			input=input.substr(0, input.length()-1);
			log("forking process to execute " + input);
			int pid=fork();
			if(pid == -1){
				fatal_error("error forking child process");
			}
			if(pid==0){//child
				int result = handleCommand(input, needResponse, isFileOperation);	
				if(result >= 0){//response was a command id
					int index = input.find(" ");
					CommandInfo* cmd = new CommandInfo(result, input.substr(index).c_str(), input.substr(0,index).c_str());
					cmd -> processId = pid;
					commands.push_back(cmd);
				}	
				log("finished execution, killing CHILD");
				if(raise(SIGKILL) != 0){
					error("error killing child process");
				}
			}
			else{
				if(waitpid((pid_t)pid, NULL, 0) == -1){
					error("error waiting on child process");
				}
				continue;
			}	
		}
		else{//execute command normally (single process)
			log("executing command in single process");
			int result = handleCommand(input, needResponse, isFileOperation);
			/*NOTE: handleCommand() will execute the given command properly. If it requires a server response, 1 of 2 cases will happen:
				* The server response will be to an "ls" or "pwd" command, and handleCommand() will print the result and return -1
				* The server response will be command ID for a "get" or "put" command, in which case handleCommand() will return the ID
			*/
			if(result >= 0){//response was a command id
				int index = input.find(" ");
				CommandInfo* cmd = new CommandInfo(result, input.substr(index), input.substr(0, index));
				commands.push_back(cmd);
			}
		}

		//Shutdown connection to be repopened on next iteration of loop
		//shutdown(mySocket->mysocketFd1, 2);

		/*size_t found = input.find(" ");
		if(found != string::npos){
			//cout << "User input contains a space..." << endl;
			char* inputArr = new char[input.length() + 1];
			string firstWord(strtok(inputArr, " "));

			//Getting a file from remote host
			if(firstWord.compare("get") == 0){
				int index = input.find(" ");
				string fileName = input.substr(index);
				receiveFile = fopen(fileName.c_str(), "w");
				int len;
				while((len = recv(socketFd1, buffer, 256, 0) > 0)){
					fwrite(buffer, sizeof(char), len, receiveFile);
				}
				fclose(receiveFile);

			}
				//Sending a file to remote host
			else if(firstWord.compare("put") == 0){
				int index = input.find(" ");
				string fileName = input.substr(index);
				sendFile = fopen(fileName.c_str(), "w");
				fseek(sendFile, 0, SEEK_END);
				long size = ftell(sendFile);
				rewind(sendFile);
				char sendBuffer[size];
				fgets(sendBuffer, size, sendFile);
				int n;
				while((n = fread(sendBuffer, sizeof(char), size, sendFile)) > 0){
					if(send(socketFd1, sendBuffer, n, 0) < 0){
						//cout << "Error sending file";
					}
					bzero(sendBuffer, size);
				}
			}
		}*/
	}//end of prompt loop

	return 0;

}//end of main

int handleCommand(string input, int needResponse, bool isFileOp){
	int retVal = -1;
	if(!input.compare("terminate")){
		if(send(socketFd2,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
	}
	else{
		if(send(socketFd1,input.c_str(),input.length(),0)==-1){
			fatal_error("send failed");
		}
	}
	if(needResponse){
		log("command requires a response");
		char responseBuffer[256];
		int bytesRead;
		if ((bytesRead = recv(socketFd1, responseBuffer, sizeof(responseBuffer), 0)) == 1) {
			fatal_error("recv error");
		}
		responseBuffer[bytesRead] = '\0';
		if(isFileOp){
			retVal = atoi(responseBuffer);
		}
		else{
			for (int i = 0; i < bytesRead; i++) {
				printf("%c", responseBuffer[i]);
			}
		}
	}
	if(input.length()>2&&!input.compare(0,3,"get")){
		int index = input.find(" ");
		string fileName = input.substr(index+1);
		receiveFile = fopen(fileName.c_str(), "w");
		int len;
		while((len = recv(socketFd1, buffer, 256, 0) > 0)){
			fwrite(buffer, sizeof(char), len, receiveFile);
		}
		fclose(receiveFile);
	}
	if(input.length()>2&&!input.compare(0,3,"put")){
		log("PARENT executing put");
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
			if(send(socketFd1, sendBuffer, n, 0) < 0){
				//cout << "Error sending file";
			}
			bzero(sendBuffer, size);
		}
	}
	if(!input.compare("quit")){
		if(close(socketFd1) != 0){
			fatal_error("error closing main socket");
		}
		if(close(socketFd2) != 0){
			fatal_error("error closing termination socket");
		}
		
		sleep(1);
	}
	if(!input.compare(0,9,"terminate")){
			int index=input.find(" ");
			int cmdId = atoi(input.substr(index).c_str());
			for(int i=0; i<commands.size(); i++){
				if((commands[i] -> commandId)==cmdId){
					handleTermination(i);
					break;
				}
			}
	}
	return retVal;
}

void handleTermination(int index){
	if((commands[index] -> processId) > 0){
		if(kill(commands[index] -> processId, SIGKILL) != 0){
			error("error killing process in handleTermination()");
		}
	}
	if(!commands[index]->commandName.compare("get")){
		if(remove(commands[index]->commandFile.c_str()) != 0){
			error("error removing file in handleTermination()");
		}
	}
	
}
