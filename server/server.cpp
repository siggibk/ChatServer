#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <iostream>
#include <arpa/inet.h>
#include <map>

// include the api we created
#include "api/api.h"

using namespace std;

void error(const char *msg);

int analyzeMessage(int sockfd, Api *api);

int createSocket(uint16_t port);

int main(int argc, char *argv[])
{
    // vector of ints to know the order of ports tried
    // for the port knocker
    vector<int> portWatcher = {-1, -1};

    // this is the correct port sequence to connect to the server
    vector<int> portSequence = {23001, 23002, 23003};

    string userName;

    int sockfd, sockfd2, sockfd3;
    int portNumber, portNumber2, portNumber3;
    int n, i;

    socklen_t clientLength;
    char buffer[256];
    struct sockaddr_in clientName;
    fd_set active_fd_set, read_fd_set;

    // map client usernames to their sockets
    map<string, int> usersToSockets;
    // map sockets to usernames
    map<int, string> socketsToUsers;

    Api *api = new Api();
    api->giveServerNewId();

    // our socket
    sockfd = createSocket(portSequence.at(2));
    api->addUserToList("Server", sockfd);

    sockfd2 = createSocket(portSequence.at(0));
    sockfd3 = createSocket(portSequence.at(1));

    // Listen on sockets for traffic, sockfd is our server and
    // the other two are the ports a client needs to go to first
    // to be able to connect to us
    if (listen(sockfd, 5) < 0 || listen(sockfd2, 5) < 0 || listen(sockfd3, 5) < 0)
    {
        perror("select");
        exit(EXIT_FAILURE);
    }

    // zero out the active fd set, then set the bits for our sockets
    FD_ZERO(&active_fd_set);
    FD_SET(sockfd, &active_fd_set);
    FD_SET(sockfd2, &active_fd_set);
    FD_SET(sockfd3, &active_fd_set);

    while (1)
    {
        // block until input arrives on one or more active sockets
        read_fd_set = active_fd_set;
        if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < FD_SETSIZE; ++i)
        {
            if (FD_ISSET(i, &read_fd_set))
            {
                // first port in the sequence a client needs to visit
                if (i == sockfd2)
                {
                    FD_CLR(i, &active_fd_set);
                    portWatcher.push_back(23001);
                }
                // second port in the sequence a client needs to visit
                else if (i == sockfd3)
                {
                    FD_CLR(i, &active_fd_set);
                    portWatcher.push_back(23002);
                }
                // our server socket, check if the client went through the
                // right sequence
                else if (i == sockfd)
                {
                    if (api->validPorts(portWatcher))
                    {
                        cout << "Valid ports go ahead" << endl;

                        // connection request on original socket
                        int newSockFd;
                        clientLength = sizeof(clientName);
                        newSockFd = accept(sockfd, (struct sockaddr *)&clientName, &clientLength);

                        if (newSockFd < 0)
                        {
                            perror("accept");
                            exit(EXIT_FAILURE);
                        }
                        fprintf(stderr,
                                "Server: connect from host %s, port %hd.\n",
                                inet_ntoa(clientName.sin_addr),
                                ntohs(clientName.sin_port));

                        // sets the new socket as active in the active_fd_set
                        FD_SET(newSockFd, &active_fd_set);

                        char welcomeMsg[] = "please choose a username:";
                        api->sendMessage(sockfd, newSockFd, welcomeMsg);
                        cout << "listening for username" << endl;
                        // listen for username
                        userName = api->receiveMessage(newSockFd);
                        userName = api->cleanString(userName);
                        cout << "USER: " << userName << endl;
                        // map username to its socket. <USERNAME, SOCKET>
                        api->addUserToList(userName, newSockFd);
                        api->printCommands(newSockFd);
                        // print out commands that are available here
                    }
                }
                else
                {
                    //
                    // message arriving on an already connected socket
                    if (analyzeMessage(i, api) < 0)
                    {
                        close(i);
                        FD_CLR(i, &active_fd_set);
                    }
                }
            }
        }
    }
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int analyzeMessage(int sockfd, Api *api)
{
    int n, nBytes, MAXMSG = 512;
    char buffer[MAXMSG];
    bzero(buffer, MAXMSG);
    vector<string> messageSeq;
    string delimiter, token;

    nBytes = read(sockfd, buffer, MAXMSG);

    if (nBytes < 0)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }
    else if (nBytes == 0)
    {
        // end of file
        return -1;
    }
    else
    {
        // message read
        fprintf(stderr, "Server: got message: %s", buffer);
        string checkCommand = api->arrayToString(buffer);

        // add all words to vector to check for command
        delimiter = " ";
        size_t pos = 0;
        while ((pos = checkCommand.find(delimiter)) != std::string::npos)
        {
            token = checkCommand.substr(0, pos);
            messageSeq.push_back(token);
            checkCommand.erase(0, pos + delimiter.length());
        }
        messageSeq.push_back(checkCommand);
        // end weird code

        if (messageSeq.at(0) == "ID")
        {
            cout << "Server id: " << api->getServerId() << endl;

            string servId = "Server id: ";
            servId += api->getServerId();
            int n = servId.length();
            char test[n + 1];
            strcpy(test, servId.c_str());
            api->sendMessage(-1, sockfd, test);
            // provide a unique ID for the server
        }
        else if (messageSeq.at(0) == "CONNECT")
        {
            // CONNECT <USER>
            // start a chat with a user
        }
        else if (messageSeq.at(0) == "LEAVE")
        {
            api->leaveServer(sockfd);

            char msg[] = "A user has left the server";
            api->sendMessageToAll(api->getSocket("Server"), msg);

            cout << sockfd << " left the server" << endl;
        }
        else if (messageSeq.at(0) == "WHO")
        {
            // list all usernames
            cout << "Listing all usernames for socket: " << sockfd << endl;
            api->listAllUsernames(sockfd);
        }
        else if (messageSeq.at(0) == "MSG" && messageSeq.at(1) == "ALL")
        {
            // rest of msg is all the characters after the command
            char restOfMsg[strlen(buffer) - 7];

            for (int i = 0; i < strlen(buffer) - 8; i++)
            {
                restOfMsg[i] = buffer[i + 7];
            }

            // send to the user: Message to everybody: <MSG> and read into buffer
            // and send
            api->sendMessageToAll(sockfd, restOfMsg);
        }
        // EXAMPLE: MSG jackie hello
        else if (messageSeq.at(0) == "MSG")
        {
            string username = messageSeq.at(1);
            int len = 4 + username.length();
            char restOfMsg[strlen(buffer) - len];

            for (int i = 0; i < strlen(buffer) - (len - 1); i++)
            {
                restOfMsg[i] = buffer[i + len];
            }

            int userSockFd = api->getSocket(username);

            api->sendMessage(sockfd, userSockFd, restOfMsg);
        }
        else if (messageSeq.at(0) == "CHANGE" && messageSeq.at(1) == "ID")
        {
            api->giveServerNewId();
            string servId = "ID changed to: ";
            servId += api->getServerId();
            int n = servId.length();
            char test[n + 1];
            strcpy(test, servId.c_str());
            api->sendMessage(-1, sockfd, test);
        }
        else
        {
            cout << "No command selected" << endl;
        }

        return 0;
    }
}

// creates a socket
int createSocket(uint16_t port)
{
    int sock;
    struct sockaddr_in name;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
        error("ERROR opening socket");

    bzero((char *)&name, sizeof(name));

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&name,
             sizeof(name)) < 0)
        error("ERROR on binding");

    return sock;
}