#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <ctime>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;

int TIMEOUT = 5; // for persistent
int MAXCON = 10; // maximam # of persistent connections

// from online source binarytides.com
int hostname_to_ip(char * hostname , char* ip)
{
  struct hostent *he;
  struct in_addr **addr_list;
  int i;

  if ( (he = gethostbyname( hostname ) ) == NULL)
    {
      // get the host info
      herror("gethostbyname");
      return 1;
    }

  addr_list = (struct in_addr **) he->h_addr_list;

  for(i = 0; addr_list[i] != NULL; i++)
    {
      //Return the first one;
      strcpy(ip , inet_ntoa(*addr_list[i]) );
      return 0;
    }

  return 1;
}

bool update_data_buffer(string& dataBuffer, char* recvBuffer, int read)
{
  for(int i=0; i<read; i++){
    dataBuffer += recvBuffer[i];
  }
  size_t endIndex = dataBuffer.find("\r\n\r\n");
  if(endIndex == string::npos){// not terminated yet
    return false;
  }else{
    return true;
  }
}

// from online source: http://ysonggit.github.io
vector<string> split(const string &s, char delim) {
  stringstream ss(s);
  string item;
  vector<string> tokens;
  while (getline(ss, item, delim)) {
    tokens.push_back(item);
  }
  return tokens;
}

char* m_asctime(const struct tm *timeptr)
{
  static const char wday_name[][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };
  static const char mon_name[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  static char result[26];
  sprintf(result, "%.3s, %d %.3s %d %.2d:%.2d:%.2dGMT",
          wday_name[timeptr->tm_wday],
          timeptr->tm_mday, 
          mon_name[timeptr->tm_mon],
          1900 + timeptr->tm_year,
          timeptr->tm_hour,
          timeptr->tm_min,
          timeptr->tm_sec);
  return result;
}

string notFoundResponse(char* keepAlive)
{
  time_t now = time(0);
  tm* gmtm = gmtime(&now);
  string date(m_asctime(gmtm));
  string res;
  res += "HTTP/1.0 404 Not found\r\n";
  if(keepAlive != nullptr){
    res += keepAlive;
  }
  res += "Date:";
  res += date + "\r\n\r\n";
  return res;
}

string badRequestResponse()
{
  time_t now = time(0);
  tm* gmtm = gmtime(&now);
  string date(m_asctime(gmtm));
  string res = "HTTP/1.0 400 Bad request\r\n";
  res += "Date:";
  res += date + "\r\n\r\n";
  return res;
}

string versionNotSupportedResponse()
{
  time_t now = time(0);
  tm* gmtm = gmtime(&now);
  string date(m_asctime(gmtm));
  string res = "HTTP/1.0 505 HTTP Version Not Supported\r\n";
  res += "Date:";
  res += date + "\r\n\r\n";
  return res;
}

string okayResponse()
{
  time_t now = time(0);  
  tm* gmtm = gmtime(&now);
  string date(m_asctime(gmtm));
  string res = "HTTP/1.0 200 OK\r\n";
  res += "Date:";
  res += date + "\r\n";
  return res;
}

string parseRequest(string& dataBuffer, string& fileName, bool& isPersistent)
{
  // return a response header
  int breakPos = dataBuffer.find("\r\n\r\n");
  string header = dataBuffer.substr(0, breakPos+2);
  dataBuffer = dataBuffer.substr(breakPos+4, dataBuffer.size());
  // keep the next header
  int start = 0;
  int end = header.find("\r\n");

  // parse the request line:
  vector<string> req = split(header.substr(start, end-start), ' ');
  if(req.size() != 3 || req[0] != "GET"){
    return badRequestResponse();
    // leave the fileName unchanged
  }else if(req[2] != "HTTP/1.0" && req[2] != "HTTP/1.1"){
    return versionNotSupportedResponse();
  }else{
    if(req[2] == "HTTP/1.1" &&
       header.find("Connection: keep-alive")){
      isPersistent = true;
    }
    // TODO: assuming just the file path
    fileName = req[1];
    return okayResponse();
  }
}

long long addFileHeader(string& response, ifstream& fs, string& fileName)
{
  ostringstream ss;
  long long fileSize = fs.tellg();
    
  ss << fileSize;
  string contentLen = ss.str();
  // add file-related headers
  struct stat stat_buf;
  stat(fileName.c_str(), &stat_buf);
  const struct tm* mtime = gmtime(&(stat_buf.st_mtime));
  string mdate(m_asctime(mtime));
  response += "Last-Modified: " + mdate + "\r\n" +
    "Content-Length: " + contentLen + "\r\n\r\n";
  return fileSize;
}

int resPersistent(string fileName, int clientSockfd, string& response)
{
  char tmp[100] = {0};
  sprintf(tmp, "Keep-Alive: timeout=%d, max=%d\r\n", TIMEOUT, MAXCON);
  response += tmp;
  ifstream fs (fileName.c_str(), ifstream::ate|ifstream::binary);
  if(fs.is_open()){
    long long fileSize = addFileHeader(response, fs, fileName);
    // add real file
    int l = response.size();
    fs.seekg(0, ios::beg);    
    char* responseWithFile = new char[fileSize + l];
    memcpy(responseWithFile, response.c_str(), l);
    fs.read(responseWithFile + l, fileSize);
    if (send(clientSockfd, responseWithFile,
             fileSize + l, 0) == -1) {
      perror("send");
      return 6;
    }else{
      return 0;
    }
  }else{
    response = notFoundResponse(tmp);
    int l = response.size();
    if (send(clientSockfd, response.c_str(), l, 0) == -1) {
      perror("send");
      return 6;
    }else{
      return 0;
    }
  }
}

int resNonPersistent(string fileName, int clientSockfd, string& response)
{
  ifstream fs (fileName.c_str(), ifstream::ate|ifstream::binary);
  if(fs.is_open()){
    long long fileSize = addFileHeader(response, fs, fileName);
    // add real file
    int l = response.size();
    fs.seekg(0, ios::beg);    
    char* responseWithFile = new char[fileSize + l];
    memcpy(responseWithFile, response.c_str(), l);
    fs.read(responseWithFile + l, fileSize);
    if (send(clientSockfd, responseWithFile,
             fileSize + l, 0) == -1) {
      perror("send");
      return 6;
    }else{
      return 0;
    }
  }else{
    response = notFoundResponse(nullptr);
    int l = response.size();
    if (send(clientSockfd, response.c_str(), l, 0) == -1) {
      perror("send");
      return 6;
    }else{
      return 0;
    }
  }
}

int main(int argc, char* argv[])
{
  char* hostname;
  int portNum;
  string fileDir;
  string usage = "3 arguments: [hostname] [port] [file-dir]";
  string dataBuffer; // store header until end
  string response;
  string fileName;
  bool isPersistent = false;
  
  // HttpRequest req;
  
  if(argc != 4){
    cout << usage << endl;
    return 1;
  }else{
    hostname = argv[1];
    portNum = atoi(argv[2]);
    fileDir = argv[3];
  }

  // create a socket using TCP IP
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);

  // allow others to reuse the address
  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET,
                 SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("setsockopt");
    return 1;
  }

  // bind address to socket
  char ip[100];
  hostname_to_ip(hostname, ip);
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(portNum);
  addr.sin_addr.s_addr = inet_addr(ip);
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("bind");
    return 2;
  }

  // set socket to listen status
  if (listen(sockfd, 1) == -1) {
    perror("listen");
    return 3;
  }

  // accept a new connection
  struct sockaddr_in clientAddr;
  socklen_t clientAddrSize = sizeof(clientAddr);
  int clientSockfd;
  int pid;
  while(true){
    clientSockfd =
      accept(sockfd, (struct sockaddr*)&clientAddr, &clientAddrSize);

    if (clientSockfd == -1) {
      perror("accept");
      return 4;
    }

    pid = fork();
    if(pid<0){
      perror("fork");
      return 4;
    }else if(pid != 0){
      close(clientSockfd);
    }else{
      close(sockfd);

      // read/write data from/into the connection
      char buf[20] = {0};

      fd_set fds;
      struct timeval timeout;
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;
      FD_ZERO(&fds);
      FD_SET(clientSockfd, &fds);
  
      while (true) {
        memset(buf, '\0', sizeof(buf));

        int nRead = recv(clientSockfd, buf, 20, 0);
        // cout << buf;
        if (nRead == -1) {
          perror("recv");
          return 5;
        }
    

        bool getFullHeader = update_data_buffer(dataBuffer, buf, nRead);
        if(getFullHeader){
          response = parseRequest(dataBuffer, fileName, isPersistent);
          int l = response.size();
          if(fileName == "" && !isPersistent){
            if (send(clientSockfd, response.c_str(), l, 0) == -1) {
              perror("send");
              return 6;
            }else{
              return 0;
            }
          }else if(!isPersistent){
            fileName = fileDir + fileName;
            if(resNonPersistent(fileName, clientSockfd, response) == 0){
              return 0;
            }else{
              return 6;
            }
          }else{ // handle persistent request
            fileName = fileDir + fileName;
            resPersistent(fileName, clientSockfd, response);
            int rv = select(clientSockfd+1, &fds,
                            nullptr, nullptr, &timeout);
            if(rv <= 0){ // timeout
              return 7;
            }
          }
        }

      }

      close(clientSockfd);
      return 0;
    }
  }
  close(sockfd);
  return 0;
  
}
