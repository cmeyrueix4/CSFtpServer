#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include "dir.h"
#include "usage.h"
#include <ifaddrs.h>
#include <sys/stat.h>

struct sockaddr_in pasvConnection;
int pasvConnectionMade = 0;
int fd = -1; 
char* serverDir = 0;
bool pasvCalled = false; 

//Find address to bind pasv connection to by using getifaddr
void getAddress(){
  struct ifaddrs * ifAddrStruct=NULL;
  struct ifaddrs * ifa=NULL;
  void * tmpAddrPtr=NULL;

  getifaddrs(&ifAddrStruct);

  for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {

      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family == AF_INET6 || strcasecmp(ifa->ifa_name, "lo") == 0) {
          continue;
      }
      if (ifa->ifa_addr->sa_family == AF_INET) { 
          // check it is IP4
          // is a valid IP4 Address
          tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
          char addressBuffer[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
          pasvConnection.sin_addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
          break;
        }
      }
}

//Establish pasv Connection
int pasvCommand(){
  if(pasvConnectionMade == 0){
    pasvConnectionMade = socket(PF_INET, SOCK_STREAM, 0);

    if (pasvConnectionMade < 0)
    {
      perror("Failed to create the PASV socket.");

      return -1;
    }
    // Reuse the address
    int value = 1;
    
    if (setsockopt(pasvConnectionMade, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int)) != 0)
    {
      perror("Failed to set the PASV socket option");
      return -1;
    }
    
    bzero(&pasvConnection, sizeof(struct sockaddr_in));
    pasvConnection.sin_family = AF_INET;
    pasvConnection.sin_port = 0;
    pasvConnection.sin_addr.s_addr = htonl(INADDR_ANY);


    //Get address to bind to
    getAddress();


    if (bind(pasvConnectionMade, (const struct sockaddr*) &pasvConnection, sizeof(struct sockaddr_in)) != 0)
    {
      perror("Failed to bind the PASV socket");
      return -1;
    }

    //Get port number
    socklen_t socketPortLength = sizeof(struct sockaddr);
    getsockname(pasvConnectionMade, (struct sockaddr *) &pasvConnection, &socketPortLength);

    //Listen for connections and allow for one connection
    if (listen(pasvConnectionMade, 1) != 0)
    {
      perror("Failed to listen for PASV connections");
      return -1;
    }
  }
  return 0;
  
}

//Calculate the first part of the port
int pasvPortOne(){
  int port = ntohs(pasvConnection.sin_port);
  int port1 = port/256;
  return port1;
}

//Calculate the second part of the port
int pasvPortTwo(){
  int port = ntohs(pasvConnection.sin_port);
  int port2 = port%256;
  return port2;
}

//Read a file for retrieve, takes in the filename and both the client socket and the pasv socket
int readFile(char *fileName, int retrSock, int clientd){
  FILE *file = NULL;
  file = fopen(fileName, "r");

  //If the file successfully opened
  if(file){
    char information[4096];
    int numberRead;

    //Read the file and send it to the client 4KB at a time
    do{
      bzero(information, 4096);
      numberRead = fread(information, 1, sizeof(information), file);
      write(retrSock, information, numberRead);
    } while(numberRead > 0);

    //close file
    bzero(information, 4096);
    fclose(file);

    //return a success
    return 0;
  }

  return -1;
} 


//Trim whitespace for commands
void trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';
}


void* interact(void* args)
{
  int clientd = *(int*) args;

  // Interact with the client
  char buffer[1024];
  char *initMessage = "220 Welcome\n"; 
  bool client_login = false;

  // Set server directory
  int setServer = chdir(serverDir);

  // Send the init message to client
  send(clientd , initMessage , strlen(initMessage) , 0 );

  while (true)
  {
    bzero(buffer, 1024);

    // Receive the client message
    ssize_t length = recv(clientd, buffer, 1024, 0);


    if (length < 0)
    {
      perror("Failed to read from the socket");

      break;
    }

    if (length == 0)
    {
      printf("EOF\n");

      break;
    }

    trimwhitespace(buffer);

    //Set up client response array
    char *clientResponseArray[1024];
    char* goodbyeMsg = "221 Goodbye.\n";

    //Initialize numargs to keep track of the amount of arguments inputted
    int numArgs = 0;
    int i = 0;

    //Tokenize the buffer by splitting on space and insert arguments into client response array
    char *token = strtok (buffer, " ");
    while (token != NULL)
    {
      clientResponseArray[i++] = token;
      numArgs++;
      token = strtok (NULL, " ");
    }


    if (strcasecmp(clientResponseArray[0], "quit") == 0)
    {
      if(numArgs != 1)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
      } 
      else
      {
        send(clientd , goodbyeMsg, strlen(goodbyeMsg) , 0 );
        break;
      }

    }
    else if (strcasecmp(clientResponseArray[0], "user") == 0 )
    {
     if(numArgs != 2)
     {
      length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
    } 
    else
    {
      if (strcasecmp(clientResponseArray[1], "cs317") == 0)
      {
        client_login = true;
        length = snprintf(buffer, 1024, "%s\r\n", "230 Login successful.");
      }
      else
      {
        length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in.");

      } 
    }

  }
  else if (strcasecmp(clientResponseArray[0], "cwd") == 0)
  {
    if(numArgs != 2)
    {
      length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
    } 
    else
    {
      if (!client_login)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
      }
      else
      {
       if(strstr(clientResponseArray[1], "./") != 0 || strstr(clientResponseArray[1], "../") != 0) 
       {
        length = snprintf(buffer, 1024, "%s\r\n", "550 Trying to access root directory.");

      }
      else 
      {

        char *path = clientResponseArray[1];
        int correctPath = chdir(path);

        if (correctPath != 0)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "431 No such directory."); 
        }
        else 
        {
          length = snprintf(buffer, 1024, "%s\r\n", "200 Directory changed");
        }
      }
    }
  }
}
else if (strcasecmp(clientResponseArray[0], "cdup") == 0)
{
 if(numArgs != 1)
  {
    length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
  } 
  else
  {
    if (!client_login)
    {
      length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
    }
    else
    {
      char s[1024]; 
      // set current working directory 
      char* currDir = getcwd(s,1024);


      if (strcasecmp(currDir, serverDir) == 0){
        length = snprintf(buffer, 1024, "%s\r\n", "550 Trying to access server root directory.");
      }
      else
      {
        char s[1024]; 
        // set new working directory to one above
        char* newDir = getcwd(s,1024);
        char *endingDir = strrchr(newDir,'/');   //Get the ending dir name
        *endingDir = '\0';             //Replace the ending dir name with null char
        // Now the newDir is set to parent dir

        if (chdir(newDir) != 0)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "431 No such directory.");
        }
          else 
        {
          length = snprintf(buffer, 1024, "%s\r\n", "200 Directory changed");
        }
      }

    }
  }
}
else if (strcasecmp(clientResponseArray[0], "type") == 0)
{
        //If there aren't the correct amount of arguments send error
        if(numArgs != 2)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
        } 
        else
        {
          if (!client_login)
          {
            length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
          }
          else
          {
            if (strcasecmp(clientResponseArray[1], "a") == 0)
            {
              length = snprintf(buffer, 1024, "%s\r\n", "200 Switching to ASCII mode.");
            }
            else if (strcasecmp(clientResponseArray[1], "i") == 0)
            {
              length = snprintf(buffer, 1024, "%s\r\n", "200 Switching to Binary mode.");
            } else 
            {
              //Send error message for unsupported types
              length = snprintf(buffer, 1024, "%s\r\n", "500 Unrecognised TYPE command.");

            }
          } 
        }

      }
      else if (strcasecmp(clientResponseArray[0], "mode") == 0)
      {
       if(numArgs != 2)
       {
        length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
      } 
      else
      {
        if (!client_login)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
        }
        else
        {
          if (strcasecmp(clientResponseArray[1], "s") == 0)
          {
            length = snprintf(buffer, 1024, "%s\r\n", "200 Mode set to S.");
          }
          else
          {
            length = snprintf(buffer, 1024, "%s\r\n", "504 Bad MODE command. We only support stream mode.");
            
          }
        } 
      }

    }
    else if (strcasecmp(clientResponseArray[0], "stru") == 0)
    {
      if(numArgs != 2)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
      } 
      else
      {
        if (!client_login)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
        }
        else
        {
          if (strcasecmp(clientResponseArray[1], "f") == 0)
          {
            length = snprintf(buffer, 1024, "%s\r\n", "200 Structure set to F.");
          }
          else
          {
            length = snprintf(buffer, 1024, "%s\r\n", "504 Bad STRU command. We only support file structure.");
            
          }
        } 
      }

    }
    else if (strcasecmp(clientResponseArray[0], "retr") == 0)
    {
      if(numArgs != 2)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
        pasvConnectionMade = 0;
      } 
      else 
      {
        if (!client_login)
        {
          length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
        }
        else
        {
          if(!pasvCalled){
        //If pasv connection not yet made, make one
          char* unsuccessful = "425 Use pasv first.\r\n";
          send(clientd , unsuccessful, strlen(unsuccessful) , 0 );
          continue;

      }

          //PASV already established so we accept the socket connection
          struct sockaddr_in retrAddress;
          socklen_t retrAddressLength = sizeof(struct sockaddr_in);
          int retrSock = accept(pasvConnectionMade, (struct sockaddr*) &retrAddress, &retrAddressLength);

          if (retrSock < 0)
          {
            length = snprintf(buffer, 1024, "%s\r\n", "425 Can't open data connection.");
            continue;
          }

          //Get the file name
          char fileName[1024];
          strcpy(fileName, clientResponseArray[1]);

          //Set up to grab file size
          struct stat buf;
          int exists = stat(fileName, &buf);
          bzero(buffer, 1024);  
          int totalSize = buf.st_size;  

          //If it doesn't exist send error message and continue
          if(exists != 0){
            char* fileNotFound = "550 Requested action not taken. File unavailable.\r\n";
            send(clientd , fileNotFound, strlen(fileNotFound) , 0 );
            continue;
          }

          //Convert size to string
          char num[sizeof(totalSize)];
          sprintf(num, "%d", totalSize);

          //Create response string and send
          char str[1024];
          strcpy(str, "150 Opening BINARY mode data connection for ");
          strcat(str, fileName);
          strcat(str, " (");
          strcat(str, num);
          strcat(str, " bytes).\r\n");

          char* message = str;
          send(clientd , message, strlen(message) , 0 );

          //Read the file, read == 0 if successful, -1 otherwise
          int read = readFile(fileName, retrSock, clientd);

          //Close the two connections and set pasvconnectionmade to 0 to keep track of pasv connections
          close(retrSock);
          pasvCalled = false;


          //if the read was successful send 226, otherwise skip this
          if(read == 0){
           char* fileSent = "226 Transfer complete.\r\n";
           send(clientd , fileSent, strlen(fileSent) , 0 );
         }

       }
     }

   }
   else if (strcasecmp(clientResponseArray[0], "pasv") == 0)
   {
    if (!client_login)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
      } else{
    //Go to pasv helper
    int connectionSuccessful = pasvCommand();
    if(connectionSuccessful == -1){
      char* unsuccessful = "425 Can't open data connection.\r\n";
      send(clientd , unsuccessful, strlen(unsuccessful) , 0 );
      continue;
    }
    pasvCalled = true;


    //Convert IP to string
    char* ip = inet_ntoa(pasvConnection.sin_addr);

    char *ipArray[1024]; 
    int j = 0;
    char *tokenIP = strtok (ip, ".");
    while (tokenIP != NULL)
    {
      ipArray[j++] = tokenIP;
      tokenIP = strtok (NULL, ".");
    }

    //Calculate port
    int port1 = pasvPortOne();
    int port2 = pasvPortTwo();

    length = snprintf(buffer, 1024, "%s%s,%s,%s,%s,%d,%d%s\r\n", "227 Entering Extended Passive Mode (", ipArray[0], ipArray[1], ipArray[2], ipArray[3], port1, port2, ").");
  }
  }
  else if (strcasecmp(clientResponseArray[0], "nlst") == 0)
  {
    if(numArgs != 1)
    {
      length = snprintf(buffer, 1024, "%s\r\n", "501 invalid number of arguments.");
      pasvConnectionMade = 0;
    } 
    else
    {
      if (!client_login)
      {
        length = snprintf(buffer, 1024, "%s\r\n", "530 Not logged in. ");
      } 
      else
      {
        if(!pasvCalled){
          //If pasv connection not yet made, make one
          char* unsuccessful = "425 Use pasv first.\r\n";
          send(clientd , unsuccessful, strlen(unsuccessful) , 0 );
          continue;

      }
        //Set up NLST socket
        if(fd != -1){
          close(fd);
        }

        struct sockaddr_in retrAddress;
        socklen_t retrAddressLength = sizeof(struct sockaddr_in);
        fd = accept(pasvConnectionMade, (struct sockaddr*) &retrAddress, &retrAddressLength);

        if (fd < 0)
        {
          char* unsuccessful = "425 Can't open data connection.\r\n";
          send(clientd , unsuccessful, strlen(unsuccessful) , 0 );
          continue;
        }

        //Send dir 150 message
        char* dirList = "150 Here comes the directory listing.\r\n";
        send(clientd , dirList, strlen(dirList) , 0 );

        //Call to list files, and return of proper error messages upon receipt of -1 or -2
        int numberOfItems = listFiles(fd, ".");
        if(numberOfItems == -1){
          char* dirErr = "451 Requested action aborted: local error in processing.\r\n";
          send(clientd , dirErr, strlen(dirErr) , 0 );
        } else if(numberOfItems == -2){
          char* dirErr2 = "452 Requested action not taken. Insufficient storage space in system.\r\n";
          send(clientd , dirErr2, strlen(dirErr2) , 0 );
        } else{
          // If there was no error then send success message
          char* dirDone = "226 Directory send OK.\r\n";
          send(clientd , dirDone, strlen(dirDone) , 0 );
        }

        //Close everything out
        bzero(buffer, 1024);
        close(fd);
        pasvCalled = false;
        fd = -1;
      }
    }

  }
  else
  {
    length = snprintf(buffer, 1024, "%s\r\n", "500 Command not supported.");
  }


  // Send the server response
  if (send(clientd, buffer, length, 0) != length)
  {
    perror("Failed to send to the socket");

    break;
  }


}

close(clientd);

return NULL;
}

int main(int argc, char *argv[]) {

  // This is some sample code feel free to delete it
  // This is the main program for the thread version of nc

  int i;

  // Check the command line arguments
  if (argc != 2) {
    usage(argv[0]);
    return -1;
  }

  int port = (int) strtol(argv[1], NULL, 10);

  // Create socket descriptor
  // Set the socket to TCP
  int socketd = socket(PF_INET, SOCK_STREAM, 0);

  if (socketd < 0)
  {
    perror("Failed to create the socket.");

    exit(-1);
  }

  // Reuse the address
  int value = 1;

  if (setsockopt(socketd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int)) != 0)
  {
    perror("Failed to set the socket option");

    exit(-1);
  }

  // Put the socket in passive mode to listen for client connections
  struct sockaddr_in address;

  bzero(&address, sizeof(struct sockaddr_in));

  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(socketd, (const struct sockaddr*) &address, sizeof(struct sockaddr_in)) != 0)
  {
    perror("Failed to bind the socket");

    exit(-1);
  }

  // Set the socket to listen for connections
  if (listen(socketd, 4) != 0)
  {
    perror("Failed to listen for connections");

    exit(-1);
  }

  while (true)
  {
    // Accept the connection
    struct sockaddr_in clientAddress;

    socklen_t clientAddressLength = sizeof(struct sockaddr_in);

    printf("Waiting for incoming connections...\n");

    int clientd = accept(socketd, (struct sockaddr*) &clientAddress, &clientAddressLength);

    if (clientd < 0)
    {
      perror("Failed to accept the client connection");

      continue;
    }

    printf("Accepted the client connection from %s:%d.\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));



    pthread_t thread;

    // Set server directory
    if (serverDir == 0) 
    {
      char s[1024]; 
      serverDir = getcwd(s,1024);
    }

    if (pthread_create(&thread, NULL, interact, &clientd) != 0)
    {
      perror("Failed to create the thread");

      continue;
    }

    // The main thread just waits until the interaction is done
    pthread_join(thread, NULL);

    printf("Interaction thread has finished.\n");
  }

  return 0;

}