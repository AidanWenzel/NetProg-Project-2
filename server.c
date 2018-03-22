#include <stdio.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
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

const int DATA_SIZE = 512;
//const int BUF_LEN = /*(DATA_SIZE + 4)*/ 516;
#define BUF_LEN 516
// All the headers are 4 bytes with the exception of RRQ/WRQ, which are already handled by the code given in class
const int RETRY_TIME = 1;
const int EXIT_TIME = 10;

//Packet types
const int RRQ = 1;
const int WRQ = 2;
const int DATA = 3;
const int ACK = 4;
const int ERROR = 5;

//Error types
const int NOT_DEFINED = 0;
const int	FILE_NOT_FOUND = 1;
const int	ACCESS_VIOLATION = 2;
const int	DISK_FULL = 3;
const int	ILLEGAL_OP = 4;
const int	UNKNOWN_PORT = 5;
const int	FILE_ALREADY_EXISTS = 6;
const int	NO_SUCH_USER = 7;

int * player1Turn;// = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
int * player2Turn;// = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
char ** player1Name;// = mmap(NULL, sizeof(char*), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
char ** player2Name;// = mmap(NULL, sizeof(char*), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
int * player1Move;// = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
int * player2Move;// = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

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
    n = send(sockfd, dp->data, dp->size, 0);
/*
    printf("# HANDLE SEND: Size %d Addr %s Port %d aka %d socket %d sent %d\n",
           dp->size, inet_ntoa(dp->addr_server_info->sin_addr), dp->addr_server_info->sin_port, ntohs(dp->addr_server_info->sin_port), sockfd, (int) n);
           */
    if(n < 0) {
        perror("handle_send");
        exit(-1);
    }
    sleep(1);
}

//recvfrom a datapack with error handling
//Strips any newlines
int handle_recv(int sockfd, struct DataPack* dp)
{
    empty(dp);
    dp->size = recv(sockfd, dp->data, BUF_LEN, 0);

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
    sleep(1);
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
    printf("Player %d has joined the game\n", data_pack_current->playerID + 1);

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

      if(data_pack_current->playerID == 0)
      {
        *player1Name = playerName;
      }
      else
      {
        *player2Name = playerName;
      }


      while(1){

        char* moveType = "";
        while(0 != isValidMove(moveType))
        {
          empty(data_pack_current);
          appendBytes(data_pack_current, "Rock, paper, or scissors?", 25);
          handle_send(sockfd, data_pack_current);

          empty(data_pack_current);
          handle_recv(sockfd, data_pack_current);
          moveType = data_pack_current->data;
        }

        if(data_pack_current->playerID == 0)
        {
          (*player1Turn)++;
          (*player1Move) = (strcmp(moveType, "rock") == 0 ? 0 : (strcmp(moveType, "paper") == 0 ? 1 : 2));
          printf("Player 1 moves %d on turn %d\n", *player1Move, *player1Turn);
        }
        else
        {
          (*player2Turn)++;
          (*player2Move) = (strcmp(moveType, "rock") == 0 ? 0 : (strcmp(moveType, "paper") == 0 ? 1 : 2));
          printf("Player 2 moves %d on turn %d\n", *player2Move, *player2Turn);
        }


        //Wait for opponent to be on the same turn
        while((*player1Turn) != (*player2Turn))
        {
          printf("Waiting for both players. My view of the game looks like this:\n");
          printf("%s %d , %s %d\n", *player1Name, *player1Turn, *player2Name, *player2Turn);
          sleep(2);
        }
        printf("Both players have moved!\n");

        //Check for victory condition

        //If tie, continue
        if((*player1Move) == (*player2Move))
        {
          printf("Tie, retrying...\n");
          empty(data_pack_current);
          appendBytes(data_pack_current, "Tie!", 4);
          handle_send(sockfd, data_pack_current);
        }
        else
        {
          //Rock
          if(0 == (*player1Move))
          {
            //Paper
            if(1 == (*player2Move))
            {
              printf("Player 2 beats player 1");
              empty(data_pack_current);
              appendBytes(data_pack_current, "PAPER covers ROCK! Player2 defeats Player1!", 43);
              handle_send(sockfd, data_pack_current);
            }
            else
            {
              printf("Player 1 beats player 2");
              empty(data_pack_current);
              appendBytes(data_pack_current, "ROCK smashes SCISSORS! Player1 defeats Player2!", 47);
              handle_send(sockfd, data_pack_current);
            }
          }
          //Paper
          else if(1 == (*player1Move))
          {
            //Scissors
            if(2 == (*player2Move))
            {
              printf("Player 2 beats player 1");
              empty(data_pack_current);
              appendBytes(data_pack_current, "SCISSORS cuts PAPER! Player2 defeats Player1!", 45);
              handle_send(sockfd, data_pack_current);
            }
            else
            {
              printf("Player 1 beats player 2");
              empty(data_pack_current);
              appendBytes(data_pack_current, "PAPER covers ROCK! Player1 defeats Player2!", 43);
              handle_send(sockfd, data_pack_current);
            }
          }
          //Scissors
          else if(2 == (*player1Move))
          {
            //Rock
            if(0 == (*player2Move))
            {
              printf("Player 2 beats player 1");
              empty(data_pack_current);
              appendBytes(data_pack_current, "ROCK smashes SCISSORS! Player2 defeats Player1!", 47);
              handle_send(sockfd, data_pack_current);
            }
            else
            {
              printf("Player 1 beats player 2");
              empty(data_pack_current);
              appendBytes(data_pack_current, "SCISSORS cuts PAPER! Player2 defeats Player1!", 45);
              handle_send(sockfd, data_pack_current);
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

  player1Turn = 0;
  player2Turn = 0;

  player1Turn = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  player2Turn = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  player1Name = mmap(NULL, sizeof(char*), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  player2Name = mmap(NULL, sizeof(char*), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  player1Move = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
   player2Move = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    char buffer[BUF_LEN];
    int sockfd, pid, n;
    unsigned short int opcode;
    unsigned short int* opcode_ptr;
    struct sockaddr_in addr_server_info;
    struct sockaddr_in addr_client_info;
    socklen_t sockaddr_len;
    socklen_t child_len;

    int playercount = 0;

    struct DataPack data_pack_current;
    empty(&data_pack_current);

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
                    if((accept(sockfd, (struct sockaddr*) &addr_client_info, child_len)) < 0) {
                        perror("accept in main 2");
                        exit(-1);
                    }
                    break;
                }
        }

    printf("Main 2: Port %d aka %d socket %d PID %d\n\n", data_pack_current.addr_server_info->sin_port, ntohs(data_pack_current.addr_server_info->sin_port), sockfd, getpid());

    handle_game(sockfd, buffer, n, &data_pack_current);
    close(sockfd);
    return 0;
}
