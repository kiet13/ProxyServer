// ProxyServer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "ProxyServer.h"
#include "afxsock.h"
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#define BUFSIZE 4096
#define PORT 8888

using namespace std;

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


DWORD WINAPI threadProc(SOCKET param)
{
	// Tao mot socket de proxy de nhan request tu client
	CSocket proxyRecv;
	proxyRecv.Create(PORT, SOCK_STREAM, NULL);
	proxyRecv.Attach(param);

	// Proxy nhan request tu client va luu vao clientRequest
	char clientRequest[BUFSIZE + 1];
	memset((char*)clientRequest, 0, BUFSIZE + 1);
	proxyRecv.Receive((char*)clientRequest, BUFSIZE, 0);

	if (clientRequest == NULL || strcmp(clientRequest, "") == 0)
	{
		proxyRecv.Close();
		return 0;
	}

	// Lay dong dau tien cua request ra de tach URL
	char temp[BUFSIZE + 1];
	strcpy(temp, clientRequest);
	char* firstLine = strtok(temp, "\n");

	// Tach method va URL tu dong dau tien
	char* method = strtok(firstLine, " ");

	// Kiem tra method la CONNECT hay GET hoac POST
	if (strcmp(method, "CONNECT") == 0)
		return 0;

	// Tach URL
	char* URL = strtok(NULL, " ");

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
		proxyRecv.Close();
		return 0;
	}
		

	// Lay dia chi IP tu host name
	char* ip = get_ip(hostname);

	// Xuat ra cac request ma proxy server nhan duoc
	cout << clientRequest << endl;

	// Tao mot client ao tai proxy de gui request da nhan tu client that toi server
	CSocket proxyClient;
	proxyClient.Create();
	if (proxyClient.Connect(convertCharArrayToLPCWSTR(ip), 80) < 0)
	{
		perror("Could not connect");
		exit(1);
	}
	proxyClient.Send((char*)clientRequest, BUFSIZE, 0);

	while (1)
	{
		// Nhan du lieu tu web server
		char data[BUFSIZE + 1];
		memset((char*)data, 0, BUFSIZE + 1);
		int lenData;
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
					CSocket mainClient;
					proxyServer.Accept(mainClient);
					SOCKET connected = mainClient.Detach();
					CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&threadProc, (LPVOID)connected, 0, 0);
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
