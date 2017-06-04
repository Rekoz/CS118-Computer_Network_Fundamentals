#ifndef HTTPMESSAGE_H
#define HTTPMESSAGE_H

#include <string>
#include <map>

using namespace std;

typedef string HttpVersion;
typedef string HttpMethod;
typedef string HttpStatus;
typedef string ByteBlob;

class HttpMessage {
 public:
  virtual void decodeFirstLine(ByteBlob line) = 0;
  HttpVersion getVersion();
  void setVersion(HttpVersion version);
  void setHeader(string key, string value);
  string getHeader(string key);
  void decodeHeaderLine(ByteBlob line);
 private:
  HttpVersion m_version;
  map<string, string> m_headers;
};

class HttpRequest: public HttpMessage {
 public:
  virtual void decodeFirstLine(ByteBlob line);
  HttpMethod getMethod();
  void setMethod(HttpMethod method);
  string getUrl();
  void setUrl(string url);
 private:
  HttpMethod m_method;
  string m_url;
};

class HttpResponse: public HttpMessage {
 public:
  virtual void decodeFirstLine(ByteBlob line);
  HttpStatus getStatus();
  void setStatus(HttpStatus status);
  string getDescription();
  void setDescription(string description);
 private:
  HttpStatus m_status;
  string m_description;
};

#endif
