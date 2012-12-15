// IAG0010Client.cpp : définit le point d'entrée pour l'application console.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include "winsock2.h"
#include <iostream>
#include <fstream>
#include <Windows.h>
#include "process.h"  // necessary for threading
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#if defined(UNICODE) || defined(_UNICODE)
#define _tcout std::wcout
#define _tcin std::wcin
#else
#define _tcout std::cout
#define _tcin std::cin
#endif 
using namespace std;
typedef std::basic_string<TCHAR> _tstring;

// _tprintf() est un macro qui sera éxecuté comme printf() ou wprintf() selon les #define.
// _T() est un macro qui créera de l'ANSI ou de l'UNICODE  selon les #define.
// _tcout est un macro qui sera éxecuté comme cout ou wcout selon les #define.


// Global variables

HANDLE file; // File where data will be writen
WSADATA WSAData;          // For Winsock initialization 
SOCKET clientSocket = INVALID_SOCKET;
SOCKADDR_IN serverSocket;	// Structure of the server address
DWORD error;
boolean downloadingCompleted = false;

// Variables for exit command
HANDLE stopEvent;   // The user type "exit"

// Variables for the receiving thread
WSAOVERLAPPED recvOverlapped;
HANDLE WSArecvCompletedEvents[2]; // Include stopEvent
HANDLE packetReceivedEvents[2]; // Include stopEvent

// Variables for the sending thread
WSAOVERLAPPED sendOverlapped;
HANDLE WSASendCompletedEvents[2]; // Include stopEvent
HANDLE readySentEvents[2]; // Include stopEvent


// Three threads of the application
DWORD WINAPI readingKeyboardThread();
HANDLE readKeyboardThread;     // keyboard reading thread handle
DWORD readingKeyboardThreadId;
DWORD WINAPI receivingDataThread();
HANDLE receiveDataThread;       // thread for receiving data from TCP/IP socket
DWORD receivingDataThreadId;
DWORD WINAPI sendingDataThread();
HANDLE sendDataThread;       // thread for sending data to TCP/IP socket
DWORD sendingDataThreadId;

void closeClient(void);


void closeClient(void){

	// Closing and cleaning of socket and threads.
	if (receiveDataThread)	{
		WaitForSingleObject(receiveDataThread, INFINITE); // Wait until the end of receive thread
		CloseHandle(receiveDataThread);
	}
	if (sendDataThread)	{
		WaitForSingleObject(sendDataThread, INFINITE); // Wait until the end of send thread
		CloseHandle(sendDataThread);
	}
	if (clientSocket != INVALID_SOCKET)	{
		if (shutdown(clientSocket, SD_RECEIVE) == SOCKET_ERROR)		{
			if ((error = WSAGetLastError()) != WSAENOTCONN) // WSAENOTCONN means that the connection was not established
				_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
		}
		closesocket(clientSocket);
	}
	if (readKeyboardThread)	{
		_tprintf(_T("Type 'exit' to stop IAG0010Client.\n"));
		WaitForSingleObject(readKeyboardThread, INFINITE); // Wait until the end of keyboard thread
		CloseHandle(readKeyboardThread);
	}
	WSACleanup();
	// Close file Handle
	CloseHandle(file);
	// Close command Handle
	CloseHandle(stopEvent);
	// Close receive Handle
	CloseHandle(WSArecvCompletedEvents[1]);
	CloseHandle(packetReceivedEvents[1]);
	// Close send Handle
	CloseHandle(WSASendCompletedEvents[1]);
	CloseHandle(readySentEvents[1]);

	if(downloadingCompleted)
		system("IAG0010Client_pohikiri.doc");
}


int _tmain(int argc, _TCHAR* argv[]){

	// Initializations of keyboard events for multithreading
	stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!stopEvent) {
		_tprintf(_T("CreateEvent() failed for stopEvent, error %d\n"), GetLastError());
		closeClient();
		return 1;
	}

	_tcout << "stopEvent initialized." << endl;
	
	// Start keyboard thread
	if(!(readKeyboardThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)readingKeyboardThread, NULL, NULL, &readingKeyboardThreadId))){
		_tprintf(_T("Unable to create keyboard thread\n"));
		closeClient();
		return 1;
	}

	_tcout << "Keyboard thread started." << endl;

	//Initializations of network events for multithreading
	WSASendCompletedEvents[0] = stopEvent;
	WSArecvCompletedEvents[0] = stopEvent;
	readySentEvents[0] = stopEvent;
	packetReceivedEvents[0] = stopEvent;
	memset(&recvOverlapped, 0, sizeof recvOverlapped);
	memset(&sendOverlapped, 0, sizeof sendOverlapped);
	recvOverlapped.hEvent = WSArecvCompletedEvents[1] = WSACreateEvent(); // manual and nonsignaled
	sendOverlapped.hEvent = WSASendCompletedEvents[1] = WSACreateEvent(); // manual and nonsignaled
	readySentEvents[1] = CreateEvent(NULL, TRUE, FALSE, NULL); // manual and nonsignaled
	packetReceivedEvents[1] = CreateEvent(NULL, TRUE, FALSE, NULL); // manual and nonsignaled
	if (!WSArecvCompletedEvents[1] || !WSASendCompletedEvents[1] || !readySentEvents[1] || !packetReceivedEvents[1]) {
		_tprintf(_T("CreateEvent() failed for network events, error %d\n"), GetLastError());
		closeClient();
		return 1;
	}

	_tcout << "Network events for multithreading initialized." << endl;

	// Initialization of winsock
	if (error = WSAStartup(MAKEWORD(2, 0), &WSAData)) {
		_tprintf(_T("WSAStartup() failed, error %d\n"), error);
		closeClient();
		return 1;
	}

	// Creation of the client socket
	if ((clientSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET){
		_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
		closeClient();
		return 1;
	}

	_tcout << "Socket created." << endl;

	// Description of server socket
	serverSocket.sin_family       = AF_INET;
	serverSocket.sin_addr.s_addr  = inet_addr("127.0.0.1");
	serverSocket.sin_port         = htons(1234);
	memset(&serverSocket.sin_zero, '\0', sizeof(serverSocket.sin_zero));

	// Connection of client to server
	if(connect(clientSocket, (SOCKADDR *)&serverSocket, sizeof(serverSocket)) == SOCKET_ERROR){ // TODO Remplacer le while par un if et affiner l'exception.
		_tprintf(_T("Unable to connect client to server, error %d\n"), WSAGetLastError());
		closeClient();
		return 1;
	}

	_tcout << "Connection established." << endl;

	// Start sendDataThread
	if(!(sendDataThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)sendingDataThread, NULL, NULL, &sendingDataThreadId))){
		_tprintf(_T("Unable to create socket receiving thread\n"));
		closeClient();
		return 1;
	}

	// Start receiveDataThread
	if(!(receiveDataThread = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)receivingDataThread, NULL, NULL, &receivingDataThreadId))){
		_tprintf(_T("Unable to create socket receiving thread\n"));
		closeClient();
		return 1;
	}

	closeClient();
	return 0;
}


DWORD WINAPI readingKeyboardThread(){

	// Variables
	HANDLE stdIn = GetStdHandle(STD_INPUT_HANDLE); // Handle to read from the console.
	_TCHAR commandBuffer[21]; // Buffer to received read data.
	DWORD nReadChars;

	// Initialisation of Standard Input Console
	if (stdIn == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("GetStdHandle() failed, error %d\n"), GetLastError());
		CloseHandle(stdIn);
		return 1;
	}
	if (!SetConsoleMode(stdIn, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))
	{
		_tprintf(_T("SetConsoleMode() failed, error %d\n"), GetLastError());
		CloseHandle(stdIn);
		return 1;
	}

	// Reading and processing loop
	while(true){
		if (!ReadConsole(stdIn, commandBuffer, 20, &nReadChars, NULL)) 
		{
			_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError());
			CloseHandle(stdIn);
			return 1; 
		}
		commandBuffer[nReadChars-2] = 0; // replace /return by /nul
		commandBuffer[nReadChars-1] = 0;
		if (!_tcsicmp(commandBuffer, _T("exit"))) { // Compare the received command with "exit".
			SetEvent(stopEvent);
			break;
		}
		else {
			_tcout << "Command " << commandBuffer << " not recognized" << endl;
		}
	}
	CloseHandle(stdIn);
	return 0;
}


DWORD WINAPI receivingDataThread(){

	char recvMessage[2048];
	WSABUF recvDataBuffer;  // Buffer for received data is a structure
	recvDataBuffer.buf = recvMessage;
	recvDataBuffer.len = 2048;
	DWORD nReceivedBytes = 0, ReceiveFlags = 0, waitResult;
	BOOL firstRecv = true;
	
	DWORD nWrittenBytes;
	file = CreateFile(_T("IAG0010Client_pohikiri.doc"),GENERIC_READ | GENERIC_WRITE, 0, NULL,CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		_tprintf(_T("Unable to create file, error %d\n"), GetLastError());
		return 1;
	}


	while (TRUE)
	{
		if (WSARecv(clientSocket, &recvDataBuffer, 1, &nReceivedBytes, &ReceiveFlags, &recvOverlapped, NULL) == SOCKET_ERROR)
		{
			if ((error = WSAGetLastError()) != WSA_IO_PENDING) {
				_tprintf(_T("WSARecv() failed, error %d\n"), error);
				return 1;
			}
			waitResult = WSAWaitForMultipleEvents(2, readySentEvents, false, WSA_INFINITE, false); // We wait that sending message complete
			if (waitResult == WSA_WAIT_FAILED) {
				_tprintf(_T("WSAWaitForMultipleEvents() failed for readySentEvents in receivingDataThread, error %d\n"), WSAGetLastError());
				return 1;
			}
			if (waitResult == WAIT_OBJECT_0)
				break; // stopEvent raised
			// We wait 5 seconde that receiving message complete.
			// If WSARecv don't complete into 5 secondes, it means that there is not data to receive.
			waitResult = WSAWaitForMultipleEvents(2, WSArecvCompletedEvents, false, 5000, false);
			if (waitResult == WSA_WAIT_FAILED)
			{
				_tprintf(_T("WSAWaitForMultipleEvents() failed for WSArecvCompletedEvents in receivingDataThread, error %d\n"), WSAGetLastError());
				return 1;
			}
			if (waitResult == WSA_WAIT_TIMEOUT) {// No more data.
				_tprintf(_T("No more data. All packets have been received.\n"));
				downloadingCompleted = true;
				break; // Waiting stop because the time was out.
			}
			if (waitResult == WAIT_OBJECT_0) // stopEvent has become signaled
				break;
			if (!WSAGetOverlappedResult(clientSocket, &recvOverlapped, &nReceivedBytes, FALSE, &ReceiveFlags)) {
				_tprintf(_T("WSAGetOverlappedResult(&recvOverlapped) failed, error %d\n"), GetLastError());
				return 1;
			}
		}
		else
		{
			if (!nReceivedBytes){
				_tprintf(_T("Server has closed the connection\n"));
				return 1;
			}
		}
		// Events management
		ResetEvent(readySentEvents[1]);
		WSAResetEvent(WSASendCompletedEvents[1]);
		SetEvent(packetReceivedEvents[1]);

		// Executed only the first time.
		if (firstRecv)
		{
			_TCHAR recvText[20];
			memcpy(recvText, &recvMessage[4], 40);
			_tcout << "Received message : " << (TCHAR*)recvText << endl;
			_tcout << "The downloading is gonna start." << endl;
			_tcout << "You can follow the evolution of the downloading." << endl;
			_tcout << "The downloaded file will be automatically launch in the default application." << endl;
			firstRecv = false;
			continue;
		}
		
		_tcout << nReceivedBytes << " bytes received" << endl;

		// Write in file
		if (!WriteFile(file, recvMessage+4, *(int*)&recvMessage[0], &nWrittenBytes, NULL)) {
			_tprintf(_T("Unable to write into file, error %d\n"), GetLastError());
			return 1;
		}
		memset(recvMessage,0,2048);
	}

	return 0;
}


DWORD WINAPI sendingDataThread(){

	// Declaration and initialization of sent packet.
	_TCHAR readyMessage[8] = _T("  Ready"); // UNICODE String
	readyMessage[0] = 6;
	readyMessage[1] = 0;
	_TCHAR sendMessage[22] = _T("  Hello IAG0010Server"); // UNICODE String
	sendMessage[0] = 40;
	sendMessage[1] = 0;

	// Declaration and initialization of WSABUF.
	WSABUF sendDataBuffer;
	sendDataBuffer.buf = (char*)sendMessage;
	sendDataBuffer.len = 44;
	DWORD nSentBytes = 0, sendFlags = 0, waitResult = 0;
	BOOL firstSent = true;

	WSAWaitForMultipleEvents(2, packetReceivedEvents, false, WSA_INFINITE, false);
	WSAResetEvent(packetReceivedEvents[1]);
	while(true){
		
		if (WSASend(clientSocket, &sendDataBuffer, 1, &nSentBytes, sendFlags, &sendOverlapped, NULL)== SOCKET_ERROR)
		{
			if ((error = WSAGetLastError()) != WSA_IO_PENDING) {
				_tprintf(_T("WSASend() failed, error %d\n"), error);
				return 1;
			}
			waitResult = WSAWaitForMultipleEvents(2, WSASendCompletedEvents, false, WSA_INFINITE, false);
			if (waitResult == WSA_WAIT_FAILED) {
				_tprintf(_T("WSAWaitForMultipleEvents() failed for WSASendCompletedEvents in sendingDataThread, error %d\n"), WSAGetLastError());
				return 1;
			}
			if (waitResult == WAIT_OBJECT_0) // stopEvent raised.
				break;
			if (!WSAGetOverlappedResult(clientSocket, &sendOverlapped, &nSentBytes, false, &sendFlags)) {
				_tprintf(_T("WSAGetOverlappedResult(&sendOverlapped) failed, error %d\n"), GetLastError());
				return 1;
			}
		}
		else
		{
			if (!nSentBytes) {
				_tprintf(_T("Server has closed the connection\n"));
				return 1;
			}
		}

		// Events management and synchronization
		WSAResetEvent(WSASendCompletedEvents[1]);
		SetEvent(readySentEvents[1]);
		waitResult = WSAWaitForMultipleEvents(2, packetReceivedEvents, false, WSA_INFINITE, false);
		if (waitResult == WSA_WAIT_FAILED) {
				_tprintf(_T("WSAWaitForMultipleEvents() failed for packetReceivedEvents in sendingDataThread, error %d\n"), WSAGetLastError());
				return 1;
		}
		if (waitResult == WAIT_OBJECT_0)
				break; // stopEvent raised
		ResetEvent(packetReceivedEvents[1]);

		// Executed only the first time.
		if (firstSent) {
			memset(sendDataBuffer.buf,0,40);
			sendDataBuffer.buf = (char*)readyMessage;
			sendDataBuffer.len = 16;
			firstSent = false;
			continue;
		}
	}

	return 0;
}