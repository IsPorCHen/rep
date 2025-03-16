// server.cpp
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define PORT "8080"
#define BUFLEN 512
#define SPAM_TIMEOUT 1

#include <iostream>
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <map>
#include <vector>
#include <string>
#include <ctime>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

WSADATA wsaData;
SOCKET ListenSocket = INVALID_SOCKET;
struct addrinfo* result = NULL, * ptr = NULL, hints;

std::map<int, std::string> clients;
std::map<int, time_t> lastMessageTime;
HANDLE hMutex;

void logToFile(const std::string& message) {
    FILE* logFile = fopen("chat_log.txt", "a");
    if (logFile) {
        fprintf(logFile, "%s\n", message.c_str());
        fclose(logFile);
    }
    else {
        printf("[Сервер]: Ошибка открытия лог-файла!\n");
    }
}

void logAll(const std::string& message) {
    printf("%s\n", message.c_str());
    logToFile(message);
}

bool isNameTaken(const std::string& name) {
    for (const auto& client : clients) {
        if (client.second == name) return true;
    }
    return false;
}

void sendToAllClients(const std::string& message, int senderSocket = -1) {
    WaitForSingleObject(hMutex, INFINITE);
    for (const auto& client : clients) {
        if (client.first != senderSocket) {
            send(client.first, message.c_str(), message.size(), 0);
        }
    }
    ReleaseMutex(hMutex);
}

bool isSpam(int clientSocket) {
    time_t currentTime = time(0);
    if (lastMessageTime.find(clientSocket) != lastMessageTime.end()) {
        double diff = difftime(currentTime, lastMessageTime[clientSocket]);
        if (diff < SPAM_TIMEOUT) return true;
    }
    lastMessageTime[clientSocket] = currentTime;
    return false;
}

DWORD WINAPI ClientThread(LPVOID lpParam) {
    SOCKET clientSocket = (SOCKET)lpParam;
    char recvbuf[BUFLEN];
    int iResult;
    std::string clientName;

    // Имя
    while (true) {
        const char* prompt = "Введите ваше имя: ";
        send(clientSocket, prompt, (int)strlen(prompt), 0);
        logAll("[Сервер]-[Пользователь]: Введите имя пользователя");

        iResult = recv(clientSocket, recvbuf, BUFLEN, 0);
        if (iResult <= 0) {
            closesocket(clientSocket);
            return 0;
        }

        clientName = std::string(recvbuf, iResult);
        clientName.erase(clientName.find_last_not_of(" \n\r") + 1);

        logAll("[Пользователь]: " + clientName);

        WaitForSingleObject(hMutex, INFINITE);
        if (!isNameTaken(clientName)) {
            clients[clientSocket] = clientName;
            ReleaseMutex(hMutex);
            break;
        }
        ReleaseMutex(hMutex);

        const char* takenMsg = "Этот логин уже занят. Попробуйте другой.\n";
        send(clientSocket, takenMsg, (int)strlen(takenMsg), 0);
        logAll("[Сервер]-[Пользователь]: Этот логин уже занят. Введите другой");
    }

    std::string joinMsg = "[Сервер]: " + clientName + " присоединился к чату.";
    logAll(joinMsg);
    sendToAllClients(joinMsg, clientSocket);

    // Чат
    while (true) {
        iResult = recv(clientSocket, recvbuf, BUFLEN, 0);
        if (iResult <= 0) break;

        std::string message(recvbuf, iResult);
        message.erase(message.find_last_not_of(" \n\r") + 1);

        if (isSpam(clientSocket)) {
            std::string spamMsg = "[Сервер]-[" + clientName + "]: Вы отправляете сообщения слишком быстро.";
            logAll(spamMsg);
            send(clientSocket, "Вы отправляете сообщения слишком быстро. Попробуйте позже.\n", 59, 0);
            continue;
        }

        if (!message.empty() && message[0] == '/') {
            std::string commandLog = "[" + clientName + "]-[Сервер]: " + message;
            logAll(commandLog);

            if (message == "/users") {
                std::string userList = "Активные пользователи: ";
                WaitForSingleObject(hMutex, INFINITE);
                for (const auto& c : clients) userList += c.second + ", ";
                ReleaseMutex(hMutex);
                if (!clients.empty()) userList.erase(userList.end() - 2);

                std::string response = "[Сервер]-[" + clientName + "]: " + userList;
                logAll(response);
                send(clientSocket, userList.c_str(), userList.length(), 0);
            }
            else {
                std::string unknownCmd = "[Сервер]-[" + clientName + "]: Неизвестная команда";
                logAll(unknownCmd);
                send(clientSocket, "Неизвестная команда.\n", 22, 0);
            }
        }
        else {
            std::string msg = "[" + clientName + "]: " + message;
            logAll(msg);
            sendToAllClients(msg, clientSocket);
        }
    }

    // Отключение
    std::string leaveMsg = "[Сервер]: " + clientName + " покинул чат.";
    logAll(leaveMsg);
    closesocket(clientSocket);
    WaitForSingleObject(hMutex, INFINITE);
    clients.erase(clientSocket);
    ReleaseMutex(hMutex);
    return 0;
}

int main() {
    setlocale(0, "rus");

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Ошибка инициализации Winsock\n");
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &result) != 0) {
        printf("Ошибка получения адреса\n");
        WSACleanup();
        return 2;
    }

    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("Ошибка создания сокета\n");
        WSACleanup();
        return 3;
    }

    if (bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("Ошибка привязки\n");
        closesocket(ListenSocket);
        WSACleanup();
        return 4;
    }

    freeaddrinfo(result);

    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Ошибка прослушивания\n");
        closesocket(ListenSocket);
        WSACleanup();
        return 5;
    }

    hMutex = CreateMutex(NULL, FALSE, NULL);

    printf("Сервер работает на порту %s...\n", PORT);

    while (true) {
        SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            printf("Ошибка accept: %d\n", WSAGetLastError());
            continue;
        }

        HANDLE hThread = CreateThread(NULL, 0, ClientThread, (LPVOID)ClientSocket, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }

    closesocket(ListenSocket);
    CloseHandle(hMutex);
    WSACleanup();
    return 0;
}
