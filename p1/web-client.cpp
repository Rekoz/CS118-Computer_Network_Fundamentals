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

bool connectToServer(struct addrinfo* server, string addr, const char* message, const string& filename) {
  
  int sockfd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
  if (sockfd == -1) {
    perror("Socket");
    return false;
  }

  if (connect(sockfd, server->ai_addr, server->ai_addrlen) == -1) {
    close(sockfd);
    perror("Connect");
    return false;
  }

  if (send(sockfd, message, strlen(message), 0) < 0) {
    perror("Send");
    return false;
  }

  vector<char> buffer(MAX_BUFFER_SIZE);
  ofstream ofile;
  string s;
  bool writeToFile = false;
  HttpResponse rsp;
  long currentLength = 0, contentLength = LONG_MAX;
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
          ofile.open(filename, ofstream::out);
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

  close(sockfd);
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

  for (int i = 1; i < argc; i++) {
    string addr(argv[i]), portnum, hostname, path;
    int j, k;
    for (j = 0; j < addr.length(); j++)
      if (addr[j] == '/') break;
    if (j > 6) j = 0; else j += 2;
    for (k = j; k < addr.length(); k++)
      if (addr[k] == ':') break;
    if (k == addr.length()) {
      for (k = j; k < addr.length(); k++)
        if (addr[k] == '/') break;
      portnum = "80";
      hostname = addr.substr(j, k-j);
    }
    else {
      hostname = addr.substr(j, k-j);
      for (++k; k < addr.length(); k++) {
        if (addr[k] < '0' || addr[k] > '9') break;
        portnum += addr[k];
      }
    }
    path = addr.substr(k);
    if (path == "") path = "/";
    char message[200];
    strcpy(message, "GET ");
    strcat(message, path.c_str());
    strcat(message, " HTTP/1.0\r\nHost: ");
    strcat(message, (hostname+":"+portnum+"\r\n\r\n").c_str());

    for (j = path.length(); j > 0; j--)
      if (path[j-1] == '/') break;
    string filename = path.substr(j);
    if (filename == "") filename = "index.html";

    if ((status = getaddrinfo(hostname.c_str(), portnum.c_str(), &hints, &res)) != 0) {
      cerr << "getaddrinfo: " << gai_strerror(status) << endl;
      return 2;
    }
    bool success = false;
    TIMEDOUT = false;
    for(struct addrinfo* p = res; p != 0; p = p->ai_next)
      if (connectToServer(p, string(argv[i]), message, filename)) {
        success = true;
        break;
      }

    if (!success) {
      string errorMsg = "Cannot connect to server ";
      errorMsg.append(argv[i]);
      cerr << errorMsg << endl;
      if (TIMEDOUT) cerr << "Timeout" << endl;
    }
  }
}
