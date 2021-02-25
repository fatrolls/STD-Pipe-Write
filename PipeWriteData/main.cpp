#include <windows.h> 
#include <tchar.h>
#include <stdio.h> 
#include <strsafe.h>
#include "Profile.h"

#ifdef _DEBUG
    #define RepeatSendRecvCount     100
    #define BUFSIZE                 1*4*1024*1024 
#else
    #ifdef BUILD_TEST_OVERHEAD
        #define RepeatSendRecvCount     100000
        #define BUFSIZE                 2
    #else
        #define RepeatSendRecvCount     1000
        #define BUFSIZE                 1*4*1024*1024 
    #endif
#endif

HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

HANDLE g_hInputFile = NULL;

void CreateChildProcess(void);
void WriteToPipe(void);
void ReadFromPipe(void);
void ErrorExit(LPCTSTR);

//allocate in order to not interfere with profiling
unsigned char* Buff = NULL;

int _tmain(int argc, TCHAR* argv[])
{
    SECURITY_ATTRIBUTES saAttr;

    Buff = (unsigned char* )malloc(BUFSIZE + 16);
    if (Buff == NULL)
        exit(1);

    printf("\n->Start of parent execution.\n");

    // Set the bInheritHandle flag so pipe handles are inherited. 

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT. 

    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
        ErrorExit(TEXT("StdoutRd CreatePipe"));

    // Ensure the read handle to the pipe for STDOUT is not inherited.

    if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
        ErrorExit(TEXT("Stdout SetHandleInformation"));

    // Create a pipe for the child process's STDIN. 

    if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
        ErrorExit(TEXT("Stdin CreatePipe"));

    // Ensure the write handle to the pipe for STDIN is not inherited. 

    if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
        ErrorExit(TEXT("Stdin SetHandleInformation"));

    // Create the child process. 

    CreateChildProcess();

    // Get a handle to an input file for the parent. 
    // This example assumes a plain text file and uses string output to verify data flow. 

 /*   if (argc == 1)
        ErrorExit(TEXT("Please specify an input file.\n"));

    g_hInputFile = CreateFile(
        argv[1],
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_READONLY,
        NULL);

    if (g_hInputFile == INVALID_HANDLE_VALUE)
        ErrorExit(TEXT("CreateFile"));*/

    // Write to the pipe that is the standard input for a child process. 
    // Data is written to the pipe's buffers, so it is not necessary to wait
    // until the child process is running before writing data.

    printf("Start pipe test\n");
    __int64 Start = GetTick();
    for (int i = 0; i < RepeatSendRecvCount; i++)
    {
        WriteToPipe();
#ifdef _DEBUG
        printf("%d)Data written\n", i);
#endif
        //   printf("\n->Contents of %S written to child STDIN pipe.\n", argv[1]);

           // Read from pipe that is the standard output for child process. 

       //    printf("\n->Contents of child process STDOUT:\n\n");
        ReadFromPipe();
#ifdef _DEBUG
        printf("%d)Data read\n", i);
#endif
    }
    double Duration = GetCounterDiff(Start);
#ifdef BUILD_TEST_OVERHEAD
    printf("This is a special test where pipe overhead is tested instead bandwidth. Transfering only %d bytes %d times\n", BUFSIZE, RepeatSendRecvCount);
#endif
    printf("Time (ms) the test it took : %f\n", (float)Duration);
    __int64 BytesWritten = __int64(BUFSIZE) * __int64(RepeatSendRecvCount);
    __int64 BytesRead = __int64(BUFSIZE) * __int64(RepeatSendRecvCount);
    printf("Bytes written = %lld + Bytes read = %lld\n", BytesWritten, BytesRead);
    __int64 TransferRate = __int64(((BytesWritten + BytesRead) * __int64(1000)) / Duration);
    printf("Transfer Speed %lld bytes/s = %lld kb/s = %lld mb/s = %lld gb/s\n", TransferRate, TransferRate / 1024, TransferRate/1024/1024, TransferRate/1024/1024/1024);

    printf("\n->End of parent execution.\n");

    // The remaining open handles are cleaned up when this process terminates. 
    // To avoid resource leaks in a larger application, close handles explicitly. 
    // Close the pipe handle so the child process stops reading. 
    if (!CloseHandle(g_hChildStd_IN_Wr))
        ErrorExit(TEXT("StdInWr CloseHandle"));

    free(Buff);
    Buff = NULL;

    return 0;
}

void CreateChildProcess()
// Create a child process that uses the previously created pipes for STDIN and STDOUT.
{
#ifdef _DEBUG
    TCHAR szCmdline[] = TEXT("../PipeConsumeNullRender/Debug/PipeConsumeNullRender.exe");
#else
    TCHAR szCmdline[] = TEXT("../PipeConsumeNullRender/Release/PipeConsumeNullRender.exe");
#endif
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bSuccess = FALSE;

    // Set up members of the PROCESS_INFORMATION structure. 

    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.

    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = g_hChildStd_OUT_Wr;
    siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
    siStartInfo.hStdInput = g_hChildStd_IN_Rd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Create the child process. 

    bSuccess = CreateProcess(NULL,
        szCmdline,     // command line 
        NULL,          // process security attributes 
        NULL,          // primary thread security attributes 
        TRUE,          // handles are inherited 
        0,             // creation flags 
        NULL,          // use parent's environment 
        NULL,          // use parent's current directory 
        &siStartInfo,  // STARTUPINFO pointer 
        &piProcInfo);  // receives PROCESS_INFORMATION 

     // If an error occurs, exit the application. 
    if (!bSuccess)
        ErrorExit(TEXT("CreateProcess"));
    else
    {
        // Close handles to the child process and its primary thread.
        // Some applications might keep these handles to monitor the status
        // of the child process, for example. 

        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);

        // Close handles to the stdin and stdout pipes no longer needed by the child process.
        // If they are not explicitly closed, there is no way to recognize that the child process has ended.

        CloseHandle(g_hChildStd_OUT_Wr);
        CloseHandle(g_hChildStd_IN_Rd);
    }
}

void WriteToPipe(void)

// Read from a file and write its contents to the pipe for the child's STDIN.
// Stop when there is no more data. 
{
    DWORD dwRead, dwWritten;
//    CHAR chBuf[BUFSIZE];
    BOOL bSuccess = FALSE;

//    for (;;)
    {
//        bSuccess = ReadFile(g_hInputFile, chBuf, BUFSIZE, &dwRead, NULL);
//        if (!bSuccess || dwRead == 0) break;

        dwRead = BUFSIZE;
        bSuccess = WriteFile(g_hChildStd_IN_Wr, Buff, dwRead, &dwWritten, NULL);
//        if (!bSuccess) break;
    }
}

void ReadFromPipe(void)

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT. 
// Stop when there is no more data. 
{
    DWORD dwRead;
 //   DWORD dwWritten;
//    CHAR chBuf[BUFSIZE];
    BOOL bSuccess = FALSE;
    HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

//    for (;;)
    {
        bSuccess = ReadFile(g_hChildStd_OUT_Rd, Buff, BUFSIZE, &dwRead, NULL);
//        if (!bSuccess || dwRead == 0) break;

//        bSuccess = WriteFile(hParentStdOut, chBuf, dwRead, &dwWritten, NULL);
//        if (!bSuccess) break;
    }
}

void ErrorExit(LPCTSTR lpszFunction)

// Format a readable error message, display a message box, 
// and exit from the application.
{
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"),
        lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(1);
}