#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define PORT "8080"
#define BUFLEN 512
#define IP "26.173.198.217"

#include <iostream>
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <string>
#include <stdio.h>
#include <conio.h>
#pragma comment(lib, "Ws2_32.lib")

using namespace std;

HANDLE hMutex;
WSADATA wsaData;
SOCKET ConnectSocket = INVALID_SOCKET;
SECURITY_ATTRIBUTES sa;
string inputBuffer = "";
struct addrinfo* result = NULL, * ptr = NULL, hints;

DWORD WINAPI ReceiveMessages(LPVOID lpParam) {
    SOCKET clientSocket = *(SOCKET*)lpParam;
    char recvbuf[BUFLEN];
    int iResult;

    extern string inputBuffer;

    while (true) {
        iResult = recv(clientSocket, recvbuf, BUFLEN - 1, 0);
        if (iResult > 0) {
            recvbuf[iResult] = '\0';

            WaitForSingleObject(hMutex, INFINITE);
            printf("\r\33[K");

            cout << recvbuf << endl;
            cout << "> " << inputBuffer;
            fflush(stdout);
            ReleaseMutex(hMutex);
        }
        else if (iResult == 0) {
            cout << "Соединение с сервером закрыто." << endl;
            break;
        }
        else {
            cout << "Ошибка получения данных: " << WSAGetLastError() << endl;
            break;
        }
    }
    return 0;
}




int main() {
    setlocale(0, "rus");

    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("Ошибка загрузки библиотеки\n");
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(IP, PORT, &hints, &result);
    if (iResult != 0) {
        printf("Ошибка getaddrinfo\n");
        WSACleanup();
        return 2;
    }

    ptr = result;
    ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (ConnectSocket == INVALID_SOCKET) {
        printf("Ошибка создания сокета: %d\n", WSAGetLastError());
        WSACleanup();
        return 3;
    }

    iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("Ошибка подключения к серверу: %d\n", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 4;
    }

    freeaddrinfo(result);

    char recvbuf[BUFLEN];
    iResult = recv(ConnectSocket, recvbuf, BUFLEN - 1, 0);
    if (iResult > 0) {
        recvbuf[iResult] = '\0';
        printf("%s", recvbuf);

        string name;
        getline(cin, name);

        iResult = send(ConnectSocket, name.c_str(), (int)name.length(), 0);
        if (iResult == SOCKET_ERROR) {
            printf("Ошибка отправки имени: %d\n", WSAGetLastError());
            closesocket(ConnectSocket);
            WSACleanup();
            return 5;
        }
    }

    hMutex = CreateMutex(NULL, FALSE, NULL);
    DWORD threadId;
    CreateThread(NULL, 0, ReceiveMessages, &ConnectSocket, 0, &threadId);

    while (true) {
        cout << "> ";
        inputBuffer = "";
        char ch;

        while (true) {
            ch = _getch();

            if (ch == '\r') {
                cout << endl;
                break;
            }
            else if (ch == 8) {
                if (!inputBuffer.empty()) {
                    inputBuffer.pop_back();
                    cout << "\b \b";
                    fflush(stdout);
                }
            }
            else if (isprint(ch)) {
                inputBuffer.push_back(ch);
                cout << ch;
                fflush(stdout);
            }
        }

        if (inputBuffer == "/exit") {
            printf("Выход из чата...\n");
            break;
        }

        string toSend = inputBuffer + "\n";
        iResult = send(ConnectSocket, toSend.c_str(), (int)toSend.length(), 0);
        if (iResult == SOCKET_ERROR) {
            printf("Ошибка отправки сообщения: %d\n", WSAGetLastError());
            break;
        }

        inputBuffer.clear();
    }

    shutdown(ConnectSocket, SD_BOTH);
    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}
