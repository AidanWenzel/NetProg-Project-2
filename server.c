// compile:   gcc server.c -ldns_sd -Wall
// find:     avahi-browse -r _rps._tcp

#include <stdio.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <dns_sd.h>

const int DATA_SIZE = 512;
//const int BUF_LEN = /*(DATA_SIZE + 4)*/ 516;
#define BUF_LEN 516
// All the headers are 4 bytes with the exception of RRQ/WRQ, which are already handled by the code given in class
const int RETRY_TIME = 1;
const int EXIT_TIME = 10;


//[Connected - "true" or "false", player name, player move][PlayerID]
char* playerInfo[3][2];
int playerTurns[2];

/*

A game goes as follows:

Client connection
    |
    V
Client Name Entry   <--------
    |                       |
    V                       |
Client Name Verification ---|
    |
    |
    V
Client move types  <-------   <--------
    |                     |           |
    V                     |           |
Move type verification ---|           |
    |                                 |
    |                                 |
    V                                 |
Send win state or retry move if tie --|

All packets are sent with plain text

*/

// handle ret from dns register
static void register_cb(DNSServiceRef service,
                        DNSServiceFlags flags,
                        DNSServiceErrorType errorCode,
                        const char * name,
                        const char * type,
                        const char * domain,
                        void * context) {

    if (errorCode != kDNSServiceErr_NoError)
        fprintf(stderr, "register_cb returned %d\n", errorCode);
    else
        printf("%-15s %s.%s%s\n","REGISTER", name, type, domain);
}



struct DataPack
{
    unsigned int playerID;
    unsigned int size;	//Size of data
    char data[BUF_LEN];

    //Info for connections
    //I'm going off the assumption that we only need to allocate 1 packet per connection
    //Therefore it can hold connection info alongside data
    struct sockaddr_in *addr_server_info;
    socklen_t server_len;  //Might as well store this here as well
		struct sockaddr_in *addr_client_info;
		socklen_t child_len;	//Might as well store this here as well
};


void empty(struct DataPack* dp)
{
    //No need to clear the buffer, we can reset the size and be fine
    dp->size = 0;
}

void appendByte(struct DataPack* dp, char byte)
{
    memcpy(dp->data + dp->size, &byte, 1);
    dp->size ++;
}
void appendShort(struct DataPack* dp, unsigned short bytes)
{
    bytes = htons(bytes);
    memcpy(dp->data + dp->size, &bytes, 2);
    dp->size += 2;
}
void appendBytes(struct DataPack* dp, char* buff, short buflen)
{
    for(int i = 0; i < buflen; i++)
    {
        appendByte(dp, buff[i]);
    }
}

//sendto a datapack with error handling
void handle_send(int sockfd, struct DataPack* dp)
{
    ssize_t n;
    n = sendto(sockfd, dp->data, dp->size, 0, (struct sockaddr *)dp->addr_server_info, dp->server_len);
/*
    printf("# HANDLE SEND: Size %d Addr %s Port %d aka %d socket %d sent %d\n",
           dp->size, inet_ntoa(dp->addr_server_info->sin_addr), dp->addr_server_info->sin_port, ntohs(dp->addr_server_info->sin_port), sockfd, (int) n);
           */
    if(n < 0) {
        perror("handle_send");
        exit(-1);
    }
}

//recvfrom a datapack with error handling
//Strips any newlines
int handle_recv(int sockfd, struct DataPack* dp)
{
    empty(dp);
    dp->size = recvfrom(sockfd, dp->data, BUF_LEN, 0, (struct sockaddr*) dp->addr_client_info, &(dp->child_len));

    //Replace newlines with null terminators
    for (int i = 0; i < dp->size; i++)
   {
       if ( dp->data[i] == '\n' || dp->data[i] == '\r' )
       {
      //   printf("Replaced newline at line %d\n", i);
           dp->data[i] = '\0';
      }
   }
/*
    printf("# HANDLE RECV: Size %d Addr %s Port %d aka %d socket %d\n",
           dp->size, inet_ntoa(dp->addr_server_info->sin_addr), dp->addr_server_info->sin_port, ntohs(dp->addr_server_info->sin_port), sockfd);
           */
    if(dp->size < 0) {
        perror("handle_recv");
        exit(-1);
    }
    return dp->size;
}

//Returns 0 on empty names or names that do not start with alphanumeric characters
int isValidPlayerName(char* name, int nameLen)
{
  if(nameLen <= 0)
  {
    return 0;
  }
  return isalnum(name[0]);
}

int isValidMove(char* move)
{
  if(0 == strcmp(move, "rock"))
    return 0;
  if(0 == strcmp(move, "paper"))
    return 0;
  if(0 == strcmp(move, "scissors"))
    return 0;
  return 1;
}

void handle_game(int sockfd, char buffer[], int size, struct DataPack* data_pack_current) {

    //Initial packet
    printf("Player %d has joined the game\n", data_pack_current->playerID);

    //Set player as connected
    playerInfo[0][data_pack_current->playerID] = "true";

      int playernamesize = 0;
      char* playerName = "";

      while(!isValidPlayerName(playerName, playernamesize))
      {
        empty(data_pack_current);
        appendBytes(data_pack_current, "What is your name?", 18);
        handle_send(sockfd, data_pack_current);

        empty(data_pack_current);
        handle_recv(sockfd, data_pack_current);
        playerName = data_pack_current->data;
        playernamesize = data_pack_current->size;
      }
      printf("Player %d is named %s\n", data_pack_current->playerID, playerName);
      playerInfo[1][data_pack_current->playerID] = playerName;

      while(1){

        char* moveType = "";
        while(0 != isValidMove(moveType))
        {
          empty(data_pack_current);
          appendBytes(data_pack_current, "What is your move?", 18);
          handle_send(sockfd, data_pack_current);

          empty(data_pack_current);
          handle_recv(sockfd, data_pack_current);
          moveType = data_pack_current->data;
        }
        playerInfo[2][data_pack_current->playerID] = moveType;
        playerTurns[data_pack_current->playerID] ++;

        printf("Player %d moves %s on turn %d\n", data_pack_current->playerID, moveType, playerTurns[data_pack_current->playerID]);

        //Wait for opponent to be on the same turn
        while(playerTurns[0] != playerTurns[1])
        {
          sleep(1);
        }
        printf("Both players have moved!\n");

        //Check for victory condition

        //If tie, continue
        if(0 == strcmp(playerInfo[2][0], playerInfo[2][1]))
        {
          printf("Tie, retrying...\n");
        }
        else
        {
          if(0 == strcmp(playerInfo[2][0], "rock"))
          {
            if(0 == strcmp(playerInfo[2][1], "paper"))
            {
              printf("%s beats %s", playerInfo[1][1], playerInfo[1][0]);
            }
          }
          else  if(0 == strcmp(playerInfo[2][0], "paper"))
          {
            if(0 == strcmp(playerInfo[2][1], "scissors"))
            {
              printf("%s beats %s", playerInfo[1][1], playerInfo[1][0]);
            }
          }
          else if(0 == strcmp(playerInfo[2][0], "scissors"))
          {
            if(0 == strcmp(playerInfo[2][1], "rock"))
            {
              printf("%s beats %s", playerInfo[1][1], playerInfo[1][0]);
            }
          }

          return;
        }

      }

    return;
}

//Signal handlers
void sig_child(int signum) {
    //TODO
}
//Signal handlers
void sig_alarm(int signum)
{
    //TODO
}

int main() {
    char buffer[BUF_LEN];
    int sockfd, pid, n;
    struct sockaddr_in addr_server_info;
    struct sockaddr_in addr_client_info;
    socklen_t sockaddr_len;
    socklen_t child_len;

    int playercount = 0;

    struct DataPack data_pack_current;
    empty(&data_pack_current);

    /* AIDAN'S CODE *********************************************/
    unsigned short port = 12345;

    fd_set readfds;
    int nfds, dns_sd_fd, child_fd, parent_fd;
    struct timeval delay;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    DNSServiceRef serviceRef;
    DNSServiceErrorType err;

    parent_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(parent_fd < 0) {
        perror("Failed to open socket");
        exit(1);
    }

    //don't have to wait to rebind ports while debugging
    int optval = 1;
    setsockopt(parent_fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval , sizeof(int));

    //addrs
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    //register dns service
    DNSServiceRegister(&serviceRef, 0, 0, "wenzea", "_rps._tcp",
                       "local", NULL, htons(port), 0, NULL, register_cb, NULL);
    dns_sd_fd = DNSServiceRefSockFD(serviceRef);
    if(dns_sd_fd > parent_fd) { nfds = dns_sd_fd + 1; }
    else {                      nfds = parent_fd + 1; }
    delay.tv_sec = 15;

    if(bind(parent_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to attach to port");
        exit(1);
    }
    if (listen(parent_fd, 5) < 0) {
        perror("ERROR on listen");
        exit(1);
    }

    printf("Starting on port %d\n", port);
    srand(time(NULL));

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(parent_fd, &readfds);
        FD_SET(dns_sd_fd, &readfds);
        FD_SET(0, &readfds);

        if(select(nfds, &readfds, NULL, NULL, &delay) < 0) {
            perror("ERROR in select");
            exit(1);
        }

        err = kDNSServiceErr_NoError;
        if(FD_ISSET(dns_sd_fd, &readfds)) {
            err = DNSServiceProcessResult(serviceRef);
            //printf("ZeroConf Activity\n");
        }
        if(err) {
            fprintf(stderr, "DNSServiceProcessResult returned %d\n", err);
        }

        if (FD_ISSET(parent_fd, &readfds)) {
            child_fd = accept(parent_fd, (struct sockaddr*)&client_addr, &client_len);

            if (child_fd < 0) {
                perror("ERROR on accept");
                exit(1);
            }
            //handle_game(child_fd);
        }
    }

    DNSServiceRefDeallocate(serviceRef);

    return EXIT_SUCCESS;

    /************************************************************/
    /*
    pid = getpid();

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        perror("socket in main");
        return EXIT_FAILURE;
    }

    int options = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&options , sizeof(int));

    sockaddr_len = sizeof(addr_server_info);
    memset(&addr_server_info, 0, sockaddr_len);
    addr_server_info.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_server_info.sin_port = htons(0);
    addr_server_info.sin_family = AF_INET;

    child_len = sizeof(addr_client_info);
    memset(&addr_client_info, 0, sockaddr_len);
    addr_client_info.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_client_info.sin_port = htons(0);
    addr_client_info.sin_family = AF_INET;

    if((bind(sockfd, (struct sockaddr*) &addr_server_info, sockaddr_len)) < 0) {
        perror("bind in main");
        return EXIT_FAILURE;
    }

    getsockname(sockfd, (struct sockaddr*) &addr_server_info, &sockaddr_len);

    data_pack_current.addr_server_info = &addr_server_info;
    data_pack_current.addr_client_info = &addr_client_info;
    data_pack_current.server_len = sizeof(addr_server_info);
    data_pack_current.child_len = sizeof(addr_client_info);

    printf("laurej2._rps._tcp.local. can be reached at port %d\n\n", ntohs(data_pack_current.addr_server_info->sin_port));
    playerTurns[0] = 0;
    playerTurns[1] = 0;


    while(1) {
        intr_recv:
            n = recvfrom(sockfd, buffer, BUF_LEN, 0, (struct sockaddr*) &addr_server_info, &sockaddr_len);
            if(n < 0) {
                if(errno == EINTR) goto intr_recv;
                perror("recvfrom in main");
                return EXIT_FAILURE;
            }
            else
            {
              data_pack_current.playerID = playercount;
              playercount++;
            }

            pid = fork();
            if(pid < 0)
            {
                perror("what the fork?");
                return EXIT_FAILURE;
            }
            if(pid == 0)
            {
                close(sockfd);
                sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                if(sockfd < 0) {
                    perror("socket in main 2");
                    exit(-1);
                }

                if((bind(sockfd, (struct sockaddr*) &addr_client_info, child_len)) < 0) {
                    perror("bind in main 2");
                    exit(-1);
                }
                break;
            }
        }

    printf("Main 2: Port %d aka %d socket %d PID %d\n\n", data_pack_current.addr_server_info->sin_port, ntohs(data_pack_current.addr_server_info->sin_port), sockfd, getpid());

    handle_game(sockfd, buffer, n, &data_pack_current);
    close(sockfd);
    return 0;*/
}
