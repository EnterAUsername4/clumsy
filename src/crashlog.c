#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <tchar.h>

#pragma comment(lib, "dbghelp.lib")

const char* GetExceptionName(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: return "ACCESS VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY BOUNDS EXCEEDED";
	case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND: return "FLOAT DENORMAL OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLOAT DIVIDE BY ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT: return "FLOAT INEXACT RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION: return "FLOAT INVALID OPERATION";
	case EXCEPTION_FLT_OVERFLOW: return "FLOAT OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK: return "FLOAT STACK CHECK";
	case EXCEPTION_FLT_UNDERFLOW: return "FLOAT UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR: return "IN PAGE ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT DIVIDE BY ZERO";
	case EXCEPTION_INT_OVERFLOW: return "INT OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION: return "INVALID DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION: return "PRIVILEGED INSTRUCTION";
	case EXCEPTION_SINGLE_STEP: return "SINGLE STEP";
	case EXCEPTION_STACK_OVERFLOW: return "STACK OVERFLOW";
	default: return "UNKNOWN";
	}
}

void log_registers(FILE* f, CONTEXT* ctx) {
#ifdef _M_X64
	fprintf(f, "RAX: 0x%016llX  RBX: 0x%016llX\n", ctx->Rax, ctx->Rbx);
	fprintf(f, "RCX: 0x%016llX  RDX: 0x%016llX\n", ctx->Rcx, ctx->Rdx);
	fprintf(f, "RSI: 0x%016llX  RDI: 0x%016llX\n", ctx->Rsi, ctx->Rdi);
	fprintf(f, "RSP: 0x%016llX  RBP: 0x%016llX\n", ctx->Rsp, ctx->Rbp);
	fprintf(f, "RIP: 0x%016llX\n", ctx->Rip);
#else
	fprintf(f, "EAX: 0x%08X  EBX: 0x%08X\n", ctx->Eax, ctx->Ebx);
	fprintf(f, "ECX: 0x%08X  EDX: 0x%08X\n", ctx->Ecx, ctx->Edx);
	fprintf(f, "ESI: 0x%08X  EDI: 0x%08X\n", ctx->Esi, ctx->Edi);
	fprintf(f, "ESP: 0x%08X  EBP: 0x%08X\n", ctx->Esp, ctx->Ebp);
	fprintf(f, "EIP: 0x%08X\n", ctx->Eip);
#endif
}

void log_stack_trace(FILE* f, CONTEXT* ctx) {
	SymInitialize(GetCurrentProcess(), NULL, TRUE);

	STACKFRAME64 stack = { 0 };
	DWORD machineType;

#ifdef _M_X64
	machineType = IMAGE_FILE_MACHINE_AMD64;
	stack.AddrPC.Offset = ctx->Rip;
	stack.AddrFrame.Offset = ctx->Rbp;
	stack.AddrStack.Offset = ctx->Rsp;
#else
	machineType = IMAGE_FILE_MACHINE_I386;
	stack.AddrPC.Offset = ctx->Eip;
	stack.AddrFrame.Offset = ctx->Ebp;
	stack.AddrStack.Offset = ctx->Esp;
#endif

	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrFrame.Mode = AddrModeFlat;
	stack.AddrStack.Mode = AddrModeFlat;

	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	fprintf(f, "\nStack Trace:\n");

	for (int i = 0; i < 32; ++i) {
		if (!StackWalk64(machineType, process, thread, &stack, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
			break;

		if (stack.AddrPC.Offset == 0)
			break;

		DWORD64 addr = stack.AddrPC.Offset;
		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
		PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;

		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = MAX_SYM_NAME;

		DWORD64 displacement;
		if (SymFromAddr(process, addr, &displacement, symbol)) {
			fprintf(f, "  %i: %s - 0x%p\n", i, symbol->Name, (void*)addr);
		} else {
			fprintf(f, "  %i: 0x%p\n", i, (void*)addr);
		}
	}

	SymCleanup(process);
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ExceptionInfo) {
	CreateDirectoryA("crashlogs", NULL);

	SYSTEMTIME st;
	GetLocalTime(&st);

	char filename[MAX_PATH];
	snprintf(
		filename, sizeof(filename),
		"crashlogs/crash_%04d%02d%02d_%02d%02d%02d.txt",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond
	);

	FILE* f = fopen(filename, "w");
	if (!f) return EXCEPTION_EXECUTE_HANDLER;

	EXCEPTION_RECORD* er = ExceptionInfo->ExceptionRecord;
	CONTEXT* ctx = ExceptionInfo->ContextRecord;

	fprintf(f, "==== CRASH REPORT ====\n");
	fprintf(f, "Time: %04d-%02d-%02d %02d:%02d:%02d\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	fprintf(f, "Exception Code: 0x%08X (%s)\n", er->ExceptionCode, GetExceptionName(er->ExceptionCode));
	fprintf(f, "Exception Address: 0x%p\n", er->ExceptionAddress);

	OSVERSIONINFOEX osvi = { sizeof(OSVERSIONINFOEX) };
	GetVersionEx((OSVERSIONINFO*)&osvi);
	fprintf(f, "OS Version: %lu.%lu (Build %lu)\n", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);

	log_registers(f, ctx);
	log_stack_trace(f, ctx);

	fclose(f);
	return EXCEPTION_EXECUTE_HANDLER;
}

void setup_crash_log_handler(void) {
	SetUnhandledExceptionFilter(CrashHandler);
}
