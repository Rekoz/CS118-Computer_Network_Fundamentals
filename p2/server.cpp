#include <string>
#include <thread>
#include <iostream>
#include <fstream>
#include <queue>
// c headers

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <math.h>


using namespace std;

#define MAX_PACKET_LENGTH 1032 //
#define MAX_SEQ_NUMBER 30720
#define INIT_WINDOW_SIZE 1024
#define INIT_SLOW_THRESHOLD 30720
#define TIME_OUT 500
#define HEADER_SIZE 8

int flowWindow, sshthresh;
double congestionWindow;

bool timerOn = false;
int firstByteSeq;
int fileSeq;
int base;
fstream file;

struct header
{
  short seqNum;
  short ackNum;
  short windowSize;
  bool ACK;
  bool SYN;
  bool FIN;
};

void startTimer(struct itimerval* timerPtr, int mSec)
{
  timerPtr->it_value.tv_sec = 0;
  timerPtr->it_value.tv_usec = mSec * 1000;
  timerPtr->it_interval.tv_sec = 0;
  timerPtr->it_interval.tv_usec = 0;
  timerOn = true;
  setitimer (ITIMER_VIRTUAL, timerPtr, 0);
}

void parseHeader(struct header* headerPtr, char* buf)
{
  // also prints out message for receiving ACK
  short* fieldPtr = (short*) buf;
  headerPtr->seqNum = fieldPtr[0];
  headerPtr->ackNum = fieldPtr[1];
  headerPtr->windowSize = fieldPtr[2];

  headerPtr->ACK = fieldPtr[3] & (1<<2);
  headerPtr->SYN = fieldPtr[3] & (1<<1);
  headerPtr->FIN = fieldPtr[3] & (1<<0);
}

void buildHeader(struct header* headerPtr, char* buf){
    short ack = (short)headerPtr->ackNum;
    short seq = (short)headerPtr->seqNum;
    short window = (short)headerPtr->windowSize;
    short shift_A = ((short)headerPtr->ACK)<<2;
    short shift_S = ((short)headerPtr->SYN)<<1;
    short shift_F = ((short)headerPtr->FIN);
    short ASF = shift_A | shift_S | shift_F;
    memcpy(buf, &ack, 2);
    memcpy(buf+2, &seq, 2);
    memcpy(buf+4, &window, 2);
    memcpy(buf+6, &ASF, 2);
}

int divideLargeFile(fstream &file, char* buffer, int buffer_size){
    int read_size = buffer_size-8;
    streampos current_ptr = file.tellg();
    file.seekg(0, file.end);
    streampos file_size = file.tellg();
    file.seekg((streampos)current_ptr);
    if(file_size == current_ptr){
        return 0;
    }
    if(file.is_open()){
        file.read(buffer+8, read_size);
    }
    if((file_size-current_ptr)<read_size){
        return (int)(file_size-current_ptr);
    }
    return read_size;
}

bool checkAck(struct header* ackPtr,
              queue<int>& pool)
{
  if( ackPtr->ackNum < pool.front() )
    {
      return false;
    }
  else if( ackPtr->ackNum == pool.front() )
    {
      pool.pop();
      flowWindow = ackPtr->windowSize;
      base = ackPtr->ackNum;
      return true;
    }
  else
    {
      pool.pop();
      return checkAck(ackPtr, pool);
    }
  return true;// won't get here
}

void synAck( struct header* sendPtr, int seq, int ack){
  sendPtr->seqNum = seq;
  sendPtr->ackNum = ack;
  sendPtr->windowSize = 0;
  // server window size don't matter to client
  sendPtr->ACK = 1;
  sendPtr->SYN = 1;
  sendPtr->FIN = 0;
}

void slowStart(bool ack){
  if (ack)
    {
      congestionWindow += INIT_WINDOW_SIZE;
    }
  else
    {
      sshthresh = congestionWindow/2;
      congestionWindow = INIT_WINDOW_SIZE;
    }
}

void congestionAvoidance(bool ack){
  if (ack)
    {
      congestionWindow += INIT_WINDOW_SIZE / floor(congestionWindow);
    }
  else
    {
      congestionWindow = congestionWindow/2;
    }
}

void congestionControl(bool ack){
  if (congestionWindow < sshthresh)
  {
    slowStart(ack);
  }
  else{
    congestionAvoidance(ack);
  }
}

void timer_handler (int signum)
{ // timeout happened!
  if(!timerOn)
    {
      return; // do nothing :)
    }
  else
    {
      congestionControl(false);
      file.seekg(base - firstByteSeq, file.beg);
      fileSeq = base;
    }
}



int main(int argc, char* argv[])
{
  // set up udp socket to send a packet
  // looking at
  // https://www.cs.cmu.edu/afs/cs/academic/class/\
  //       15213-f99/www/class26/udpserver.c

  int portNum;
  string fileName;
  if(argc != 3)
    {
      cout << "usage: " << argv[0] << " PORT-NUMBER FILE-NAME" << endl;
      return 1;
    }
  else
    {
      portNum = atoi(argv[1]);
      fileName = argv[2];
    }

  // set up socket
  int sockFd;
  struct sockaddr_in myAddr;  
  if( (sockFd = socket(AF_INET, SOCK_DGRAM, 0) ) < 0)
    {
      cout << "cannot create socket" << endl;
      return 1; // error code
    }

  bzero( (char*) &myAddr, sizeof(myAddr));
  myAddr.sin_family = AF_INET;
  myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myAddr.sin_port = htons((unsigned short) portNum);

  if(bind(sockFd, (struct sockaddr*) &myAddr, sizeof(myAddr)) < 0)
    {
      cout << "ERROR on binding" << endl;
    }

  file.open(fileName, fstream::in);
  queue<int> bufPool;// buf ack#
  fileSeq = 0; // initialize during handshake
  base = 0;
  bool connection_established = false;
  flowWindow = INIT_WINDOW_SIZE;
  srand (time(NULL));
  
  /* Install timer_handler as the signal handler for SIGVTALRM. */
  struct itimerval timer;
  struct sigaction sa;
  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = &timer_handler;
  sigaction (SIGVTALRM, &sa, 0);
  
  // listen for datagram: Main Loop
  struct hostent *hostp;
  struct sockaddr_in clientAddr;
  unsigned int clientLen = sizeof(clientAddr);
  char buf[MAX_PACKET_LENGTH];
  int recLen;
  while(true){

    recLen = recvfrom(sockFd, buf, MAX_PACKET_LENGTH, 0,
                      (struct sockaddr*) &clientAddr, &clientLen);
    
    if(recLen < 0)
      {
        cout << "ERROR in recvfrom" << endl;
        continue;
      }

    hostp = gethostbyaddr((const char*) &clientAddr.sin_addr.s_addr,
                         sizeof(clientAddr.sin_addr.s_addr), AF_INET);
    if(hostp == nullptr)
      {
        cout << "ERROR on gethostbyaddr" << endl;
        continue;
      }

    // now start handle communication with client

    struct header hRec;
    struct header hSend;
    parseHeader(&hRec, buf);

    // TODO: save client addr for setting up connection
    // TODO: check client addr each time receiving message
    // TODO: suitable use of fork() to handle parallel connections
    if(hRec.SYN)
    {
      cout << "Receiving SYN packet " << hRec.seqNum << endl;
      
      fileSeq = rand() % MAX_SEQ_NUMBER;
      firstByteSeq = fileSeq+1;
      bufPool.push(fileSeq+1); // a pseudo head
      
      synAck(&hSend, fileSeq, hRec.seqNum+1);
      fileSeq++; // fileSeq is the seq# of first byte
      char ack_buffer[HEADER_SIZE];
      buildHeader(&hSend, ack_buffer);
      sendto(sockFd, ack_buffer, HEADER_SIZE, 0,
             (struct  sockaddr*) &clientAddr, clientLen);
      cout << "Sending ACK packet " << hSend.ackNum << endl;
    }

    if(hRec.ACK)
    {
      cout << "Receiving ACK packet " << hRec.ackNum << endl;
      if(checkAck(&hRec, bufPool))
        {
          if(bufPool.empty())
            {
              bufPool.push(fileSeq);
            }
          else
            {
              startTimer(&timer, 500);
            }
          
          congestionControl(true);
          if(!connection_established)
            {
              connection_established = true;
            }
        }
    }
    
    if(!connection_established){
      continue;
    }

    int nBytes;
    int minWindow = min(flowWindow, (int)congestionWindow );
    memset(buf, 0, MAX_PACKET_LENGTH);

    while(fileSeq + MAX_PACKET_LENGTH < base + minWindow &&
          (nBytes = divideLargeFile(file, buf, MAX_PACKET_LENGTH)) > 0){
      if(fileSeq == base)
        {
          startTimer(&timer, 500);
        }

      // build header
      hSend.seqNum = fileSeq;
      fileSeq += nBytes; // next seq# == current waiting ack#
      hSend.ackNum = hRec.seqNum+1; // client packet has no data
      hSend.windowSize = 0;
      hSend.ACK = true;
      hSend.SYN = false;
      hSend.FIN = false;

      // char* savedBuf = (char*) malloc(nBytes + HEADER_SIZE);
      // buildHeader(hSend, savedBuf);
      buildHeader(&hSend, buf);
      // memcpy(savedBuf+HEADER_SIZE, buf+HEADER_SIZE, nBytes);

      bool retransmission = true;
      if( fileSeq > bufPool.back() )
        { // a new buffer previously unsent
          //  bufPool.push(pair<char*, int>(savedBuf, fileSeq));
          bufPool.push(fileSeq);
          retransmission = false;
        }
      // send packet
      cout << "Sending data packet " << hSend.seqNum
           << " " << minWindow << " " << sshthresh;
      if(retransmission)
        {
          cout << " Retransmission";
        }
      cout << endl;
      sendto(sockFd, buf, HEADER_SIZE + nBytes, 0,
             (struct sockaddr*) &clientAddr, clientLen);

      // clear buf
      memset(buf, 0, MAX_PACKET_LENGTH);
    } // send file loop end
    
    
  } // main loop end

  

}
