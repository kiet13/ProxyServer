﻿// ProxyServer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "ProxyServer.h"
#include "afxsock.h"
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#define BUFSIZE 4096
#define MAXSIZE 131072
#define PORT 8888

using namespace std;
class Client
{
public:
	CSocket mainClient;
	SOCKET connected;

	Client() {  };
	Client(const Client&) {};
	~Client() {};
};

#ifdef _DEBUG
#define new DEBUG_NEW
#endif
// The one and only application object

CWinApp theApp;

using namespace std;

//Ref: http://stackoverflow.com/questions/19715144/how-to-convert-char-to-lpcwstr
wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
	wchar_t* wString = new wchar_t[4096];
	MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
	return wString;
}

/*
Get ip from domain name
*/
char *get_ip(char *host)
{
	struct hostent *hent;
	int iplen = 15; //XXX.XXX.XXX.XXX
	char *ip = (char *)malloc(iplen + 1);
	memset(ip, 0, iplen + 1);
	if ((hent = gethostbyname(host)) == NULL)
	{
		perror("Can't get IP");
		exit(1);
	}
	if (inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip, iplen) == NULL)
	{
		perror("Can't resolve host");
		exit(1);
	}
	return ip;
}

// Kiem tra trang web dang truy cap co nam trong blacklist hay khong
bool isInBlackList(char* hostname, vector<string> blacklist)
{
	for (std::vector<string>::iterator it = blacklist.begin(); it != blacklist.end(); ++it)
	{
		if (strcmp(hostname, (*it).c_str()) == 0)
			return true;
	}
	return false;
}

//Đổi 1.1 thành 1.0 và connect
void http_change(char request[]) {
	//Sửa 1.1 thành 1.0
	string temp(request);
	int pos_http = 0;
	pos_http = temp.find("HTTP/1.") + 7;
	temp[pos_http] = '0';

	pos_http = temp.find("Proxy-Connection:") + 18;
	// xoa dong keep-alive
	temp.erase(temp.begin() + pos_http, temp.begin() + pos_http + 10);
	// thay vao do la close
	temp.insert(pos_http, "close");

	strcpy(request, temp.c_str());
	return;
}

DWORD WINAPI threadProc(void* param)
{
	// Tao mot socket de proxy de nhan request tu client
	Client* client = (Client*)param;
	CSocket proxyRecv;
	proxyRecv.Create(PORT, SOCK_STREAM, NULL);
	proxyRecv.Attach(client->connected);

	// Proxy nhan request tu client va luu vao clientRequest
	unsigned char clientRequest[BUFSIZE + 1];
	memset((char*)clientRequest, 0, BUFSIZE + 1);

	// proxy nhận phần header
	int readBytes = proxyRecv.Receive((unsigned char*)clientRequest, BUFSIZE, 0);
	//http_change(clientRequest);
	if (clientRequest == NULL || strcmp((char*)clientRequest, "") == 0)
	{
		proxyRecv.Close();
		return 0;
	}

	// Lay dong dau tien cua request ra de tach URL
	char temp[BUFSIZE + 1];
	strncpy(temp, (char*)clientRequest, BUFSIZE + 1);
	char* firstLine = strtok(temp, "\n");

	// Tach method va URL tu dong dau tien
	char* method = strtok(firstLine, " ");

	// Kiem tra method la CONNECT hay GET hoac POST
	if (strcmp(method, "CONNECT") == 0)
		return 0;

	// Tach URL

	char* URL = strtok(NULL, " ");
	vector<string> path;
	int count = 0;
	string strURL(URL);
	stringstream input_stringstream(strURL);
	path.resize(count + 1);
	while (getline(input_stringstream, path[count], '/'))
	{
		count++;
		path.resize(count + 1);
	}

	// Tach hostname
	char* hostname = strtok(URL, "/");
	hostname = strtok(NULL, "/");

	// Doc danh sach domain trong blacklist
	vector<string> blacklist;
	string domain;
	ifstream inFile("blacklist.config", ios::in);
	while (!inFile.eof())
	{
		getline(inFile, domain);
		if (domain[0] == '<')
			continue;
		blacklist.push_back(domain);
	}

	// Neu hostname nam trong blacklist thi gui ve client thong bao 403 Forbidden
	if (isInBlackList(hostname, blacklist) == true)
	{
		// Gui thong bao 403 Forbidden
		ifstream fileHTML("403ForbiddenError.html", ios::in);
		char htmlContent[BUFSIZE + 1];
		int i = 0;
		memset((char*)htmlContent, 0, BUFSIZE + 1);
		while (1)
		{
			char c = fileHTML.get();
			if (fileHTML.eof() == true)
				break;
			htmlContent[i] = c;
			i++;
		}

		proxyRecv.Send((char*)htmlContent, i, 0);
		cout << clientRequest << endl;
		proxyRecv.Close();
		return 0;
	}


	// Lay dia chi IP tu host name
	char* ip = get_ip(hostname);

	string tempURL(ip);
	path[0] += "//";
	tempURL = path[0] + tempURL;
	for (int i = 3; i < count; i++)
		tempURL = tempURL + "/" + path[i];

	// Kiem tra xem url co subpath hay khong
	string tempRequest((char*)clientRequest);
	int sizeHostName = strlen(hostname);
	if (!((path.size() > 3 && path[3] == "") || path.size() <= 3)) // Neu khong co subpath
	{
		// Xoa domain name trong clientRequest
		int firstIndex = 0;
		if (strcmp(method, "GET") == 0)
			firstIndex = 4;
		else if (strcmp(method, "POST") == 0)
			firstIndex = 5;

		tempRequest.erase(tempRequest.begin() + firstIndex, tempRequest.begin() + firstIndex + sizeHostName + 7);
	}
	cout << tempRequest << endl;

	// Tao mot client ao tai proxy de gui request da nhan tu client that toi server
	CSocket proxyClient;
	proxyClient.Create();
	if (proxyClient.Connect(convertCharArrayToLPCWSTR(ip), 80) < 0)
	{
		perror("Could not connect");
		exit(1);
	}

	if (strcmp(method, "POST") == 0)
	{
		// Doc content-length
		// Doc dong thu 4
		string lines((char*)clientRequest);
		stringstream s(lines);

		string contentLength = "";

		while (1)
		{
			getline(s, contentLength, '\n');
			if (contentLength.find("Content-Length:") != string::npos)
			{
				s.str(contentLength);
				getline(s, contentLength, ' ');
				getline(s, contentLength, '\r');
				break;
			}
		}
		// Doc content-type
		string contentType;
		s.str(lines);
		while (1)
		{
			getline(s, contentType, '\n');
			if (contentType.find("Content-Type:") != string::npos)
			{
				s.str(contentType);
				getline(s, contentType, ' ');
				getline(s, contentType, ';');
				break;
			}
		}


		if (contentType != "application/x-www-form-urlencoded")
		{
			int contentLen = stoi(contentLength);
			unsigned char firstRequest[BUFSIZE + 1];
			memset((char*)firstRequest, 0, BUFSIZE + 1);
			int startHostName = 3;
			

			while (1)
			{
				if (clientRequest[startHostName] == ' ')
					break;
				else
					startHostName++;
			}
			startHostName++;

			int endHostName = startHostName + 8;
			while (1)
			{
				if (clientRequest[endHostName] == '/')
					break;
				else
					endHostName++;
			}

			// Xoa phan hostname trong clientRequest
			int j = 0;
			for (int i = 0; i < startHostName; i++)
			{
				firstRequest[j] = clientRequest[i];
				j++;
			}

			for (int i = endHostName; i < readBytes; i++)
			{
				firstRequest[j] = clientRequest[i];
				j++;
			}
			readBytes = j;
			
			// Neu file > 127KB thi thoat 
			if (contentLen > MAXSIZE - 1024)
			{
				cout << "File size is too large!!!" << endl;
				return 0;
			}
			unsigned char* fullRequest = new unsigned char[MAXSIZE + 1];
			memset((char*)fullRequest, 0, MAXSIZE + 1);

			int firstBytes = readBytes;
			int sizeRequest = firstBytes;
			for (int i = 0; i < firstBytes; i++)
			{
				fullRequest[i] = firstRequest[i];
			}

			int sizeHeader = tempRequest.find("\r\n\r\n") + 4;

			while (sizeRequest < contentLen)
			{
				proxyRecv.Accept(client->mainClient);
				unsigned char bodyRequest[BUFSIZE + 1];
				memset((char*)bodyRequest, 0, BUFSIZE + 1);
				if (contentLen + sizeHeader - sizeRequest >= BUFSIZE)
					readBytes = proxyRecv.Receive((unsigned char*)bodyRequest, BUFSIZE, 0);
				else
					readBytes = proxyRecv.Receive((unsigned char*)bodyRequest, contentLen + sizeHeader - sizeRequest, 0);

				// Ghep cac phan cua body lai voi phan header
				for (int i = 0; i < readBytes; i++)
					fullRequest[sizeRequest + i] = bodyRequest[i];
				sizeRequest += readBytes;
			}

			cout << fullRequest << endl;
			proxyClient.Send((unsigned char*)fullRequest, sizeRequest, 0);
			delete[]fullRequest;
		}
		else
			proxyClient.Send((char*)tempRequest.c_str(), BUFSIZE, 0);
	}
	else
		proxyClient.Send((char*)tempRequest.c_str(), BUFSIZE, 0);
		

	while (1)
	{
		// Nhan du lieu tu web server
		char data[BUFSIZE + 1];
		memset((char*)data, 0, BUFSIZE + 1);
		int lenData = 0;
		if ((lenData = proxyClient.Receive(data, BUFSIZE)) > 0)
			proxyRecv.Send((char*)data, lenData, 0); // Tra du lieu ve client that
		else
			break;
	}

	proxyClient.Close();
	proxyRecv.Close();
	return 0;
}


int main()
{
	int nRetCode = 0;

	HMODULE hModule = ::GetModuleHandle(nullptr);

	if (hModule != nullptr)
	{
		// initialize MFC and print and error on failure
		if (!AfxWinInit(hModule, nullptr, ::GetCommandLine(), 0))
		{
			// TODO: code your application's behavior here.
			wprintf(L"Fatal Error: MFC initialization failed\n");
			nRetCode = 1;
		}
		else
		{
			// TODO: code your application's behavior here.
			AfxSocketInit(NULL);

			CSocket proxyServer;
			proxyServer.Create(PORT, SOCK_STREAM, NULL); // Tao proxy server su dung port 8888, giao thuc TCP
			if (proxyServer.Listen(1) == false) //lang nghe client
			{
				cout << "Khong the lang nghe tren port nay!" << endl;
				proxyServer.Close();
			}
			else {
				cout << "Lang nghe thanh cong!" << endl;
				while (1) //server khong dung luon nhan ket noi tu client
				{
					// Moi vong lap xu ly mot request tu client
					Client* user = new Client;
					proxyServer.Accept(user->mainClient);
					user->connected = user->mainClient.Detach();
					CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&threadProc, user, 0, 0);
				}
			}
		}
	}
	else
	{
		// TODO: change error code to suit your needs
		wprintf(L"Fatal Error: GetModuleHandle failed\n");
		nRetCode = 1;
	}

	return nRetCode;
}
