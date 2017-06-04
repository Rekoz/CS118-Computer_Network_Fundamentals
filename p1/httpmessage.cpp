#include "httpmessage.h"
#include <iostream>
#include <string>
#include <sstream>

HttpVersion HttpMessage::getVersion() {
  return m_version;
}

void HttpMessage::setVersion(HttpVersion version) {
  m_version = version;
}

void HttpMessage::setHeader(string key, string value) {
  m_headers[key] = value;
}

string HttpMessage::getHeader(string key) {
  return m_headers[key];
}

void HttpMessage::decodeHeaderLine(ByteBlob line) { 
  size_t pos = line.find(':');
  setHeader(line.substr(0, pos), line.substr(pos+2, line.length()));
}

void HttpRequest::decodeFirstLine(ByteBlob line) {
  istringstream iss(line);
  string tmp;
  iss >> tmp; setMethod(tmp);
  tmp = ""; iss >> tmp; setUrl(tmp);
  tmp = ""; iss >> tmp; setVersion(tmp);
}

HttpMethod HttpRequest::getMethod() {
  return m_method;
}

void HttpRequest::setMethod(HttpMethod method) {
  m_method = method;
}

string HttpRequest::getUrl() {
  return m_url;
}

void HttpRequest::setUrl(string url) {
  m_url = url;
}

void HttpResponse::decodeFirstLine(ByteBlob line) {
  istringstream iss(line);
  string tmp;
  iss >> tmp; setVersion(tmp);
  tmp = ""; iss >> tmp; setStatus(tmp);
  tmp = "";
  int i;
  for (i = line.length()-1; i >= 0; i--) 
    if (line[i] >= '0' && line[i] <= '9') break;
  setDescription(line.substr(i+2));
}

HttpStatus HttpResponse::getStatus() {
  return m_status;
}

void HttpResponse::setStatus(HttpStatus status) {
  m_status = status;
}

string HttpResponse::getDescription() {
  return m_description;
}

void HttpResponse::setDescription(string description) {
  m_description = description;
}