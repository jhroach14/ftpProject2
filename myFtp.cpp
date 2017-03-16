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

void error(string output){

	perror(strerror(errno));
	cout<<"\n"<<output<<'\n';

}

void log(string message){
	cout<<message<<'\n';
}

int main(int argc, char *argv[]){
	FILE* sendFile;
	FILE* receiveFile;

	int socketFd = -1;
	int newSocketFd;
	const char * portNum;
	char buffer[256];
	struct addrinfo prepInfo;
	struct addrinfo *serverInfo;
	struct sockaddr_storage clientAddress;
	socklen_t socketLength;

	if(argc < 2){
		printf("Usage %s hostname port\n", argv[0]);
	}


	memset(&prepInfo, 0, sizeof prepInfo);
	prepInfo.ai_family = AF_UNSPEC;
	prepInfo.ai_socktype = SOCK_STREAM;

	int code;
	if((code = getaddrinfo(argv[1],argv[2],&prepInfo,&serverInfo))!=0){
		cout<<gai_strerror(code)<<'\n';
		fatal_error("Error on addr info");
	}

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


	string input;

	while(true){//prompt loop
		int needResponse = 0;
		cout << "myftp>";
		getline(cin,input);

		if(!input.compare(0,2,"ls")){
			needResponse = 1;
		}
		if(input.length()>2&&!input.compare(0,3,"pwd")){
			needResponse=1;
		}

		if(!input.compare(input.length()-1,1,"&")){//execute command in child process
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
		else{//execute command normally (single process)
			log("executing command in single process");
			if(send(socketFd,input.c_str(),input.length(),0)==-1){
					fatal_error("send failed");
			}

			//cout << "User input "<<input<<" sent to server..." << endl;

			if(needResponse) {
				log("PARENT executing command requiring response");
				char responseBuffer[256];
				int bytesRead;
				if ((bytesRead = recv(socketFd, responseBuffer, sizeof(responseBuffer), 0)) == 1) {
					fatal_error("recv error");
				}

				responseBuffer[bytesRead] = '\0';

				for (int i = 0; i < bytesRead; i++) {
					printf("%c", responseBuffer[i]);
				}
				log("PARENT finished printing server response");
			}

			if(input.length()>2&&!input.compare(0,3,"get")){
				log("PARENT executing get");
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
					if(send(socketFd, sendBuffer, n, 0) < 0){
						//cout << "Error sending file";
					}
					bzero(sendBuffer, size);
				}
			}

			if(!input.compare("quit")){
				log("PARENT executing quit");
				close(socketFd);
				sleep(1);
				return 0;

			}

		}

	}//end of prompt loop



}//end of main