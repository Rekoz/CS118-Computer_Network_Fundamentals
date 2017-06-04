#include "httpmessage.h"
#include <iostream>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <vector>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <limits.h>
#include <time.h>

using namespace std;

const unsigned int MAX_BUFFER_SIZE = 4096;

bool TIMEDOUT;
string portnum[20], hostname[20], path[20], filename[20];
int sockfd;

bool connectToServer(struct addrinfo* server, string addr, const char* message, int nUrl, const int &argc) {
  
  if (nUrl == 0 || hostname[nUrl] != hostname[nUrl-1] || portnum[nUrl] != portnum[nUrl-1]) {
    close(sockfd);
    sockfd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sockfd == -1) {
      perror("Socket");
      return false;
    }
    if (connect(sockfd, server->ai_addr, server->ai_addrlen) == -1) {
      close(sockfd);
      perror("Connect");
      return false;
    }
  }
  if (send(sockfd, message, strlen(message), 0) < 0) {
    perror("Send");
    return false;
  }
  cout << message;

  vector<char> buffer(MAX_BUFFER_SIZE);
  ofstream ofile;
  string s;
  bool writeToFile = false;
  HttpResponse rsp;
  int currentLength = 0, contentLength = INT_MAX;
  fd_set fds;
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(sockfd, &fds);
  while (currentLength < contentLength) {
    int rv = select(sockfd+1, &fds, nullptr, nullptr, &timeout);
    if (rv == 0) {
      TIMEDOUT = true;
      return false;
    }
    int result = recv(sockfd, buffer.data(), buffer.capacity(), 0);
    if (result < 0) {
      perror("recv");
      return false;
    } else if (result == 0)
      break;
    if (!writeToFile)
      for (int i = 0; i < result; i++) {
        s += buffer[i];
        size_t l = s.length();
        if (s[l-1] == '\n' && s[l-2] == '\r' && s[l-3] == '\n' && s[l-4] == '\r') {
          writeToFile = true;
          ofile.open(filename[nUrl], ofstream::out);
          ofile.write(buffer.data()+i+1, result-i-1);
          istringstream iss(s);
          string line;
          getline(iss, line);
          rsp.decodeFirstLine(line);
          if (rsp.getStatus() != "200") {
            cout << rsp.getStatus() + " " + rsp.getDescription() << endl;
            return false;
          }
          while (getline(iss, line))
            rsp.decodeHeaderLine(line);
          currentLength = result-i-1;
          string contentLengthString = rsp.getHeader("Content-Length");
          if (contentLengthString != "") contentLength = stoi(contentLengthString, nullptr);
          break;
        }
      }
    else {
      ofile.write(buffer.data(), result);
      currentLength += result;
    }
  }

  if (nUrl == argc) close(sockfd);

  return true;
}

int main(int argc , char *argv[]) {

  if (argc < 2) {
    cerr << "usage: web-client [URL] [URL]..." << endl;
    return 1;
  }

  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int status = 0;

  for (int i = 0; i < argc-1; i++) {
    string addr(argv[i+1]);
    int j, k, l = addr.length();
    for (j = 0; j < l; j++)
      if (addr[j] == '/') break;
    if (j > 6) j = 0; else j += 2;
    for (k = j; k < l; k++)
      if (addr[k] == ':') break;
    if (k == l) {
      for (k = j; k < l; k++)
        if (addr[k] == '/') break;
      portnum[i] = "80";
      hostname[i] = addr.substr(j, k-j);
    }
    else {
      hostname[i] = addr.substr(j, k-j);
      for (++k; k < addr.length(); k++) {
        if (addr[k] < '0' || addr[k] > '9') break;
        portnum[i] += addr[k];
      }
    }
    path[i] = addr.substr(k);
    if (path[i] == "") path[i] = "/";
    char message[200];
    strcpy(message, "GET ");
    strcat(message, path[i].c_str());
    strcat(message, " HTTP/1.1\r\nHost: ");
    strcat(message, (hostname[i]+":"+portnum[i]+"\r\n").c_str());
    strcat(message, "Connection: keep-alive\r\n\r\n");

    for (j = path[i].length(); j > 0; j--)
    if (path[i][j-1] == '/') break;
    filename[i] = path[i].substr(j);
    if (filename[i] == "") filename[i] = "index.html";
    if ((status = getaddrinfo(hostname[i].c_str(), portnum[i].c_str(), &hints, &res)) != 0) {
      cerr << "getaddrinfo: " << gai_strerror(status) << endl;
      return 2;
    }
    bool success = false;
    TIMEDOUT = false;
    for(struct addrinfo* p = res; p != 0; p = p->ai_next)
      if (connectToServer(p, string(argv[i+1]), message, i, argc)) {
        success = true;
        break;
      }

    if (!success) {
      string errorMsg = "Cannot connect to server ";
      errorMsg.append(argv[i+1]);
      cerr << errorMsg << endl;
      if (TIMEDOUT) cerr << "Timeout" << endl;
    }
  }
}
