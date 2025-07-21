#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "iup.h"
#include "common.h"


#define PIPE_CMD_START 1
#define PIPE_CMD_STOP  2

typedef struct {
    char filter[256];
    int lag;
    int drop;
    int ood;
} PipeCmdStart;

#define PIPE_NAME "\\\\.\\pipe\\clumsy"
#define BUFFER_SIZE 1024

static PipeCmdStart _cmdStart;

// Pipe server thread for listening to incoming commands
DWORD WINAPI clientPipeServerThread(LPVOID lpParam) {
    HANDLE hPipe;
    BYTE cmd, ack;
    DWORD bytesRead, bytesWritten;
    BOOL connected = FALSE;
    
    while (TRUE) {
        // Create security descriptor to allow non-admin access
        SECURITY_ATTRIBUTES sa;
        SECURITY_DESCRIPTOR sd;
        char securityDescriptor[SECURITY_DESCRIPTOR_MIN_LENGTH];
        
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = FALSE;
        sa.lpSecurityDescriptor = &sd;
        
        if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
            LOG("InitializeSecurityDescriptor failed: %d", GetLastError());
            return 1;
        }
        
        // Allow everyone to access the pipe
        if (!SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE)) {
            LOG("SetSecurityDescriptorDacl failed: %d", GetLastError());
            return 1;
        }
        
        hPipe = CreateNamedPipe(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            1, 1, 0, &sa
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            LOG("CreateNamedPipe failed: %d", GetLastError());
            return 1;
        }
        
        LOG("Waiting for client...");

        connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        
        if (connected) {
            LOG("Client connected.");
            
            while (TRUE) {
                if (ReadFile(hPipe, &cmd, 1, &bytesRead, NULL) && bytesRead == 1) {
                    LOG("Received command: %d", cmd);
                    
                    switch (cmd) {
                        case PIPE_CMD_START: {
                            ZeroMemory(&_cmdStart, sizeof(PipeCmdStart));
                                
                            // Read filter (256 bytes)
                            if (!ReadFile(hPipe, &_cmdStart, sizeof(PipeCmdStart), &bytesRead, NULL) || bytesRead != sizeof(PipeCmdStart)) {
                                LOG("Wanted to read %d bytes, got %d", sizeof(PipeCmdStart), bytesRead);
                                ack = 1;
                                break;
                            }

                            IupPostMessage(lpParam, NULL, PIPE_CMD_START, 0, NULL);
                            ack = 0; // Success
                            break;
                        }
                            
                        case PIPE_CMD_STOP:
                            // Your stop logic here
                            IupPostMessage(lpParam, NULL, PIPE_CMD_STOP, 0, NULL);
                            ack = 0; // Success
                            break;
                            
                        default:
                            ack = 1; // Error
                            break;
                    }
                    
                    WriteFile(hPipe, &ack, 1, &bytesWritten, NULL);
                    FlushFileBuffers(hPipe);
                    
                } else {
                    break;
                }
            }
            
            LOG("Client disconnected.");
        }
        
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    
    return 0;
}


int pipeCommandCB(Ihandle* ih, char* s, int cmd, double d, void* p) {
    switch (cmd) {
        case PIPE_CMD_START: {
            // Your start logic here
            setLag(_cmdStart.lag);
            setDrop(_cmdStart.drop);
            setFilter(_cmdStart.filter);
            setOOD(_cmdStart.ood);
            setEnabled(TRUE);
            break;
        }
            
        case PIPE_CMD_STOP:
            // Your stop logic here
            setEnabled(FALSE);
            break;
    }
    
    return IUP_DEFAULT;
}


void startClientPipeServer(Ihandle * callbackTarget) {
    HANDLE hThread = CreateThread(NULL, 0, clientPipeServerThread, callbackTarget, 0, NULL);
    if (hThread == NULL) {
        LOG("Failed to create client pipe server thread: %d", GetLastError());
    }
    IupSetCallback(callbackTarget, "POSTMESSAGE_CB", (Icallback)pipeCommandCB);
}