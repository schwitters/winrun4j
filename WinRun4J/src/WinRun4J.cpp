/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "WinRun4J.h"
#include "launcher/SplashScreen.h"
#include "launcher/Shell.h"
#include "launcher/DDE.h"
#include "launcher/Service.h"
#include "launcher/EventLog.h"
#include "common/Registry.h"

#define ERROR_MESSAGES_JAVA_NOT_FOUND "ErrorMessages:java.not.found"

using namespace std;

static TCHAR *vmargs[MAX_PATH];
static int vmargsCount = 0;
static TCHAR *progargs[MAX_PATH];
static int progargsCount = 0;

void WinRun4J::SetWorkingDirectory(dictionary* ini)
{
	char* dir = iniparser_getstr(ini, WORKING_DIR);
	if(dir != NULL) {
		// First set the current directory to the module directory
		SetCurrentDirectory(iniparser_getstr(ini, INI_DIR));

		// Now set working directory to specified (this allows for a relative working directory)
		SetCurrentDirectory(dir);
	} 
}

bool WinRun4J::StrTrimInChars(LPSTR trimChars, char c)
{
	unsigned int len = strlen(trimChars);
	for(unsigned int i = 0; i < len; i++) {
		if(c == trimChars[i]) {
			return true;
		}
	}
	return false;
}

void WinRun4J::StrTrim(LPSTR str, LPSTR trimChars)
{
	unsigned int start = 0;
	unsigned int end = strlen(str) - 1;
	for(unsigned int i = 0; i < end; i++) {
		char c = str[i];
		if(!StrTrimInChars(trimChars, c)) {
			start = i;
			break;
		}
	}
	for(int i = end; i >= 0; i--) {
		char c = str[i];
		if(!StrTrimInChars(trimChars, c)) {
			end = i;
			break;
		}
	}
	if(start != 0 || end != strlen(str) - 1) {
		int k = 0;
		for(unsigned int i = start; i <= end; i++, k++) {
			str[k] = str[i];
		}
		str[k] = 0;
	}
}

void WinRun4J::ParseCommandLine(LPSTR lpCmdLine, TCHAR** args, int& count, bool includeFirst)
{
	StrTrim(lpCmdLine, " ");
	int len = strlen(lpCmdLine);
	if(len == 0) {
		return;
	}

	int start = 0;
	bool quote = false;
	bool first = true;
	TCHAR arg[4096];
	for(int i = 0; i < len; i++) {
		char c = lpCmdLine[i];
		if(c == '\"') {
			quote = !quote;
		} else if(!quote && c == ' ') {
			if(!first || includeFirst) {
				int k = 0;
				for(int j = start; j < i; j++, k++) {
					arg[k] = lpCmdLine[j];
				}
				arg[k] = 0;
				args[count] = strdup(arg);
				StrTrim(args[count], " ");
				StrTrim(args[count], "\"");
				count++;
			}
			start = i;
			first = false;
		}
	}

	// Add the last one
	if(!first || includeFirst) {
		int k = 0;
		for(int j = start; j < len; j++, k++) {
			arg[k] = lpCmdLine[j];
		}
		arg[k] = 0;
		args[count] = _strdup(arg);
		StrTrim(args[count], " ");
		StrTrim(args[count], "\"");
		count++;
	}
}

int WinRun4J::DoBuiltInCommand(HINSTANCE hInstance, LPSTR lpCmdLine)
{
	// Check for SetIcon util request
	if(strncmp(lpCmdLine, "--WinRun4J:SetIcon", 20) == 0) {
		Icon::SetExeIcon(lpCmdLine);
		return 0;
	}

	// Check for RegisterDDE util request
	if(strncmp(lpCmdLine, "--WinRun4J:RegisterFileAssociations", 23) == 0) {
		DDE::RegisterFileAssociations(WinRun4J::LoadIniFile(hInstance), lpCmdLine);
		return 0;
	}

	// Check for UnregisterDDE util request
	if(strncmp(lpCmdLine, "--WinRun4J:UnregisterFileAssociations", 25) == 0) {
		DDE::UnregisterFileAssociations(WinRun4J::LoadIniFile(hInstance), lpCmdLine);
		return 0;
	}

	// Check for Register Service util request
	if(strncmp(lpCmdLine, "--WinRun4J:RegisterService", 27) == 0) {
		Service::Register(lpCmdLine);
		return 0;
	}

	// Check for Unregister Service util request
	if(strncmp(lpCmdLine, "--WinRun4J:UnregisterService", 29) == 0) {
		Service::Unregister(lpCmdLine);
		return 0;
	}

	if(strncmp(lpCmdLine, "--WinRun4J:ExecuteINI", 23) == 0) {
		return WinRun4J::ExecuteINI(hInstance, lpCmdLine);
	}

	Log::Error("Unrecognized command: %s", lpCmdLine);
	return 1;
}

dictionary* WinRun4J::LoadIniFile(HINSTANCE hInstance)
{
	dictionary* ini = INI::LoadIniFile(hInstance);
	if(ini == NULL) {
#ifdef CONSOLE
		Log::Error("Failed to find or load ini file.");
#else
		MessageBox(NULL, "Failed to find or load ini file.", "Startup Error", 0);
#endif
		Log::Close();
		return NULL;
	}

	return ini;
}

int WinRun4J::StartVM(LPSTR lpCmdLine, dictionary* ini)
{
	// Attempt to find an appropriate java VM
	char* vmlibrary = VM::FindJavaVMLibrary(ini);
	if(!vmlibrary) {
		char* javaNotFound = iniparser_getstr(ini, ERROR_MESSAGES_JAVA_NOT_FOUND);
		MessageBox(NULL, (javaNotFound == NULL ? "Failed to find Java VM." : javaNotFound), "Startup Error", 0);
		Log::Close();
		return 1;
	}

	// Collect the VM args from the INI file
	INI::GetNumberedKeysFromIni(ini, VM_ARG, vmargs, vmargsCount);

	// Build up the classpath and add to vm args
	Classpath::BuildClassPath(ini, vmargs, vmargsCount);

	// Extract the specific VM args
	VM::ExtractSpecificVMArgs(ini, vmargs, vmargsCount);

	// Log the VM args
	for(int i = 0; i < vmargsCount; i++) {
		Log::Info("vmarg.%d=%s\n", i, vmargs[i]);
	}

	// Collect the program arguments from the INI file
	INI::GetNumberedKeysFromIni(ini, PROG_ARG, progargs, progargsCount);

	// Add the args from commandline
	WinRun4J::ParseCommandLine(lpCmdLine, progargs, progargsCount);

	// Log the commandline args
	for(int i = 0; i < progargsCount; i++) {
		Log::Info("arg.%d=%s\n", i, progargs[i]);
	}

	// Make sure there is a NULL at the end of the args
	vmargs[vmargsCount] = NULL;
	progargs[progargsCount] = NULL;

	char* mainClass = iniparser_getstr(ini, MAIN_CLASS);

	// If we don't have a main class we might have a service class
	if(mainClass == NULL)
		mainClass = iniparser_getstr(ini, SERVICE_CLASS);

	// Fix main class - ie. replace x.y.z with x/y/z for use in jni
	if(mainClass != NULL) {
		int len = strlen(mainClass);
		for(int i = 0; i < len; i++) {
			if(mainClass[i] == '.') {
				mainClass[i] = '/';
			}
		}
	}
	if(mainClass == NULL) {
		Log::Error("ERROR: no main class specified\n");
		return 1;
	} else {
		Log::Info("Main Class: %s\n", mainClass);
	}

	// Start the VM
	if(VM::StartJavaVM(vmlibrary, vmargs) != 0) {
		Log::Error("Error starting java VM\n");
		return 1;
	}

	return 0;
}

void WinRun4J::FreeArgs()
{
	// Free vm args
	for(int i = 0; i < vmargsCount; i++) {
		free(vmargs[i]);
	}

	// Free program args
	for(int i = 0; i < progargsCount; i++) {
		free(progargs[i]);
	}
}

int WinRun4J::ExecuteINI(HINSTANCE hInstance, LPSTR lpCmdLine)
{
	TCHAR *tmpProgargs[MAX_PATH];
	int tmpProgargsCount = 0;
	ParseCommandLine(lpCmdLine, tmpProgargs, tmpProgargsCount, false);

	// The first arg should be the ini file
	if(tmpProgargsCount == 0) {
		Log::Error("INI file not specified");
		return 1;
	}

	dictionary* ini = INI::LoadIniFile(hInstance, tmpProgargs[0]);

	return 1;
}


int WinRun4J::ExecuteINI(HINSTANCE hInstance, dictionary* ini, LPSTR lpCmdLine)
{
	// Set the current working directory if specified
	WinRun4J::SetWorkingDirectory(ini);

	// Now initialise the logger using std streams + specified log dir
	Log::Init(hInstance, iniparser_getstr(ini, LOG_FILE), iniparser_getstr(ini, LOG_LEVEL));

	// Display the splash screen if present
	SplashScreen::ShowSplashImage(hInstance, ini);

	// Start vm
	int result = WinRun4J::StartVM(lpCmdLine, ini);
	if(result) {
		return result;
	}

	JNIEnv* env = VM::GetJNIEnv();

	// Register native methods
	Log::RegisterNatives(env);
	INI::RegisterNatives(env);
	SplashScreen::RegisterNatives(env);
	Registry::RegisterNatives(env);
	Shell::RegisterNatives(env);
	EventLog::RegisterNatives(env);

	// Startup DDE if requested
	bool ddeInit = DDE::Initialize(hInstance, env, ini);

	// Run the main class (or service class)
	char* serviceCls = iniparser_getstr(ini, SERVICE_CLASS);
	if(serviceCls != NULL)
		Service::Run(hInstance, ini, progargsCount, progargs);
	else
		JNI::RunMainClass(env, iniparser_getstr(ini, MAIN_CLASS), progargs);
	
	// Free the args memory
	WinRun4J::FreeArgs();

	// Close VM (This will block until all non-daemon java threads finish).
	result = VM::CleanupVM();

	// Close the log
	Log::Close();

	// Unitialize DDE
	if(ddeInit) DDE::Uninitialize();

	return result;
}

LPSTR StripArg0(LPSTR lpCmdLine)
{
	int len = strlen(lpCmdLine);
	bool found = false;
	int i = 0;
	for(; i < len; i++) {
		char c = lpCmdLine[i];
		if(c == '\"') {
			found = !found;
		} else if(c == ' ') {
			if(!found) break;
		}
	}
	return i == len ? &lpCmdLine[i] : &lpCmdLine[i + 1];
}

#ifdef CONSOLE
int main(int argc, char* argv[])
{
	LPSTR lpCmdLine = StripArg0(GetCommandLine());
	HINSTANCE hInstance = (HINSTANCE) GetModuleHandle(NULL);
#else
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) 
{
#endif
	lpCmdLine = StripArg0(GetCommandLine());

	// Initialise the logger using std streams
	Log::Init(hInstance, NULL, NULL);

	// Check for Builtin commands
	if(strncmp(lpCmdLine, "--WinRun4J:", 11) == 0) {
		WinRun4J::DoBuiltInCommand(hInstance, lpCmdLine);
		Log::Close();
		return 0;
	}

	// Load the INI file based on module name
	dictionary* ini = WinRun4J::LoadIniFile(hInstance);
	if(ini == NULL) {
		return 1;
	}
	
	return WinRun4J::ExecuteINI(hInstance, ini, lpCmdLine);
}

