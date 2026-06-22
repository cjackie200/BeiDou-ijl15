#pragma once
#include "AutoTypes.h"
//#pragma optimize("", off)
//notes from my knowledge as i have not used these kinds of codes practically well
//function replacement is when you replace the original function in the client with your own fake function, usually to add some extra functionality
//for more complex applications you would also need to define the client's variables and reinterpret_cast those (no void this time)
//you need the right calling convention (match client's original or use _fastcall, i havent tried it much)
//it would help to know the benefits and drawbacks of "reinterpret_cast", as well as how it is hooking to prevent accidents
//hooking to the original function will replace it at all times when it is called by the client
//i personally have not tried it more because it requires a very thorough understanding of how the client code works, re-making the parts here,
//and using it, all together, in a way that doesnt break anything
//it would be the best way to do it for very extensive client edits and if you need to replace entire functions in that context but
//code caving is generally easier for short term, one-time patchwork fixes	//thanks you teto for helping me on this learning journey
bool HookGetModuleFileName_initialized = true;
bool Hook_GetModuleFileNameW(bool bEnable) {
	static decltype(&GetModuleFileNameW) _GetModuleFileNameW = &GetModuleFileNameW;

	const decltype(&GetModuleFileNameW) GetModuleFileNameW_Hook = [](HMODULE hModule, LPWSTR lpFileName, DWORD dwSize) -> DWORD {
		if (HookGetModuleFileName_initialized)
		{
			std::cout << "HookGetModuleFileName started" << std::endl;
			HookGetModuleFileName_initialized = false;
		}
		auto len = _GetModuleFileNameW(hModule, lpFileName, dwSize);
		// Check to see if the length is invalid (zero)
		if (!len) {
			// Try again without the provided module for a fixed result
			len = _GetModuleFileNameW(nullptr, lpFileName, dwSize);
			std::cout << "HookGetModuleFileName null file name" << std::endl;
		}
		return len;
	};

	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_GetModuleFileNameW), GetModuleFileNameW_Hook);
}

/// <summary>
/// Creates a detour for the User32.dll CreateWindowExA function applying the following changes:
/// 1. Enable the window minimize box
/// </summary>
CreateWindowExA_t CreateWindowExA_Original = (CreateWindowExA_t)GetFuncAddress("USER32", "CreateWindowExA");
bool HookCreateWindowExA_initialized = true;
HWND WINAPI CreateWindowExA_Hook(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
	if(HookCreateWindowExA_initialized)//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
	{
		std::cout << "HookCreateWindowExA started" << std::endl;
		HookCreateWindowExA_initialized = false;
	}
	if(strstr(lpClassName, "MapleStoryClass"))
	{
		dwStyle |= WS_MINIMIZEBOX; // enable minimize button
		HWND ret  = CreateWindowExA_Original(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		return ret;
	}
	else if (strstr(lpClassName, "StartUpDlgClass"))
	{
		return NULL; //kill startup balloon
	}
	//if(Client::WindowedMode)
	//{ //unfortunately doesnt work, reverting to old window mode fix
	//	dwExStyle = 0;
	//}
	return CreateWindowExA_Original(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
};
bool Hook_CreateWindowExA(bool bEnable) {
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&CreateWindowExA_Original), CreateWindowExA_Hook);
}
bool CreateMutexA_initialized = true; ////credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
CreateMutexA_t CreateMutexA_Original = (CreateMutexA_t)GetFuncAddress("KERNEL32", "CreateMutexA");
HANDLE WINAPI CreateMutexA_Hook(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR	lpName) {
	if (CreateMutexA_initialized)
		{
			std::cout << "HookCreateMutexA started" << std::endl;
			CreateMutexA_initialized = false;
		}
	if (!CreateMutexA_Original)
	{
		std::cout << "Original CreateMutex pointer corrupted. Failed to return mutex value to calling function." << std::endl;
		return nullptr;
	}
	else if (lpName && strstr(lpName, "WvsClientMtx"))
	{
		// from https://github.com/pokiuwu/AuthHook-v203.4/blob/AuthHook-v203.4/Client176/WinHook.cpp
		char szMutex[128];	//multiclient
		int nPID = GetCurrentProcessId();
		sprintf_s(szMutex, "%s-%d", lpName, nPID);
		lpName = szMutex;
		return CreateMutexA_Original(lpMutexAttributes, bInitialOwner, lpName);
	}
	return CreateMutexA_Original(lpMutexAttributes, bInitialOwner, lpName);
}
bool Hook_CreateMutexA(bool bEnable)	//ty darter	//ty angel!
{
	//static auto _CreateMutexA = decltype(&CreateMutexA)(GetFuncAddress("KERNEL32", "CreateMutexA"));
	//decltype(&CreateMutexA) Hook = [](LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) -> HANDLE
	//{
	//	if (CreateMutexA_initialized)
	//	{
	//		std::cout << "HookCreateMutexA started" << std::endl;
	//		CreateMutexA_initialized = false;
	//	}
	//	//Multi-Client Check Removal
	//	if (lpName && strstr(lpName, "WvsClientMtx"))
	//	{
	//		return (HANDLE)0x0BADF00D;
	//		//char szMutex[128];
	//		//lpName = szMutex;
	//	}
	//	return _CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
	//};	//original
	//return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_CreateMutexA), Hook);
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&CreateMutexA_Original), CreateMutexA_Hook);
}
FindFirstFileA_t FindFirstFileA_Original = (FindFirstFileA_t)GetFuncAddress("KERNEL32", "FindFirstFileA");
bool FindFirstFileA_initialized = true; //ty joo for advice with this, check out their releases: https://github.com/OpenRustMS
HANDLE WINAPI FindFirstFileA_Hook(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
	if (FindFirstFileA_initialized)
	{
		std::cout << "HookFindFirstFileA started" << std::endl;
	}
	if (!FindFirstFileA_Original)
	{
		std::cout << "Original FindFirstFileA pointer corrupted. Failed to return ??? value to calling function." << std::endl;
		return nullptr;
	}
	else if (lpFileName && strstr(lpFileName, "*") && FindFirstFileA_initialized)
	{
		FindFirstFileA_initialized = false;
		//std::cout << "FindFirstFileA dinput8.dll spoofed" << std::endl;
		return FindFirstFileA_Original("*.wz", lpFindFileData);
	}
	//else if (FindFirstFileA_initialized)
	//{
	//	std::cout << "FindFirstFileA failed... trying again" << std::endl;
	//	Sleep(1); //keep trying to find the file instead of failing
	//	return FindFirstFileA_Hook;
	//}
	//std::cout << "FindFirstFileA failed... unable to try again" << lpFileName << std::endl;
	FindFirstFileA_initialized = false;
	return FindFirstFileA_Original(lpFileName, lpFindFileData);
}
bool Hook_FindFirstFileA(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&FindFirstFileA_Original), FindFirstFileA_Hook);
}
GetLastError_t GetLastError_Original = (GetLastError_t)GetFuncAddress("KERNEL32", "GetLastError");
bool GetLastError_initialized = true;
DWORD WINAPI GetLastError_Hook() {
	if (GetLastError_initialized)
	{
		std::cout << "HookGetLastError started" << std::endl;
		GetLastError_initialized = false;
	}
	DWORD error = GetLastError_Original();
	std::cout << "GetLastError: " << error << std::endl;
	return error;
}
bool Hook_GetLastError(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&GetLastError_Original), GetLastError_Hook);
}
sockaddr_in default_nXXXON_if;
#define WSAAddressToString  WSAAddressToStringA
bool WSPStartup_initialized = true; ////credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
INT WSPAPI WSPConnect_Hook(SOCKET s, const struct sockaddr* name, int namelen, LPWSABUF	lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS, LPINT lpErrno) {
	char szAddr[50];
	DWORD dwLen = 50;
	WSAAddressToString((sockaddr*)name, namelen, NULL, szAddr, &dwLen);

	sockaddr_in* service = (sockaddr_in*)name;
	//hardcoded NXXON IP addies in default client
	if (strstr(szAddr, "63.251.217.2") || strstr(szAddr, "63.251.217.3") || strstr(szAddr, "63.251.217.4"))
	{
		default_nXXXON_if = *service;
		service->sin_addr.S_un.S_addr = inet_addr(MainMain::m_sRedirectIP);
		//service->sin_port = htons(MainMain::porthere);
		MainMain::m_GameSock = s;
	}

	return MainMain::m_ProcTable.lpWSPConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS, lpErrno);
}
INT WSPAPI WSPGetPeerName_Hook(SOCKET s, struct sockaddr* name, LPINT namelen, LPINT lpErrno) {
	int nRet = MainMain::m_ProcTable.lpWSPGetPeerName(s, name, namelen, lpErrno);//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
	if (nRet != SOCKET_ERROR && s == MainMain::m_GameSock)
	{
		sockaddr_in* service = (sockaddr_in*)name; //suspecting this is for checking rather than actually connecting
		service->sin_addr.S_un.S_addr = default_nXXXON_if.sin_addr.S_un.S_addr;//inet_addr(MainMain::m_sRedirectIP)
		//service->sin_port = htons(MainMain::porthere);
	}
	return nRet;
}
INT WSPAPI WSPCloseSocket_Hook(SOCKET s, LPINT lpErrno) {//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
	int nRet = MainMain::m_ProcTable.lpWSPCloseSocket(s, lpErrno);
	if (s == MainMain::m_GameSock)
	{
		MainMain::m_GameSock = INVALID_SOCKET;
	}
	return nRet;
}
WSPStartup_t WSPStartup_Original = (WSPStartup_t)GetFuncAddress("MSWSOCK", "WSPStartup"); /*in this function we'll call the WSP Functions*/					const wchar_t* v42;
INT WSPAPI WSPStartup_Hook(WORD wVersionRequested, LPWSPDATA lpWSPData, LPWSAPROTOCOL_INFOW lpProtocolInfo, WSPUPCALLTABLE UpcallTable, LPWSPPROC_TABLE	lpProcTable) {
	if (WSPStartup_initialized)//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
	{
		std::cout << "HookWSPStartup started" << std::endl;
		WSPStartup_initialized = false;
	}
	int nRet = WSPStartup_Original(wVersionRequested, lpWSPData, lpProtocolInfo, UpcallTable, lpProcTable);

	if (nRet == NO_ERROR)
	{
		MainMain::m_GameSock = INVALID_SOCKET;
		MainMain::m_ProcTable = *lpProcTable;

		lpProcTable->lpWSPConnect = WSPConnect_Hook;
		lpProcTable->lpWSPGetPeerName = WSPGetPeerName_Hook;
		lpProcTable->lpWSPCloseSocket = WSPCloseSocket_Hook;
	}
	else
	{
		std::cout << "WSPStartup Error Code: " + nRet << std::endl;
	}
	return nRet;
}
bool Hook_WSPStartup(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&WSPStartup_Original), WSPStartup_Hook);
}
#define x86CMPEAX 0x3D
bool GetACP_initialized = true; DWORD LocaleSpoofValue = 0;//choose value from https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
GetACP_t GetACP_Original = (GetACP_t)GetFuncAddress("KERNEL32", "GetACP");
UINT WINAPI GetACP_Hook() { // AOB: FF 15 ?? ?? ?? ?? 3D ?? ?? ?? 00 00 74 <- library call inside winmain func
	if (GetACP_initialized){//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate	
		std::cout << "HookGetACP started" << std::endl;
		GetACP_initialized = false;	//NOTE: idk what this really does tbh, maybe it is custom locale value but more likely it is to skip a check
	}	//that some clients may have that restricts you based on locale; if it is not a check and instead logged by server feel free to feed bad data by disabling the part below
	UINT uiNewLocale = LocaleSpoofValue;
	if (uiNewLocale == 0) { return GetACP_Original(); } //we do hook in my version!//should not happen cuz we dont hook if value is zero
	DWORD dwRetAddr = reinterpret_cast<DWORD>(_ReturnAddress());
	// return address should be a cmp eax instruction because ret value is stored in eax
	// and nothing else should happen before the cmp
	if(ReadValue<BYTE>(dwRetAddr) == x86CMPEAX) {	//disable this if and else if you wanna always use spoof value (i.e. if server logs it)
			uiNewLocale = ReadValue<DWORD>(dwRetAddr + 1); // check value is always 4 bytes
			std::cout << "[GetACP] Found desired locale: " << uiNewLocale << std::endl; }
	else { std::cout << "[GetACP] Unable to automatically determine locale, using stored locale: " << uiNewLocale << std::endl; }
	std::cout << "[GetACP] Locale spoofed to: " << uiNewLocale << " unhooking. Calling address: " << dwRetAddr << std::endl;
	if (Memory::SetHook(FALSE, reinterpret_cast<void**>(&GetACP_Original), GetACP_Hook)) {
		std::cout << "Failed to unhook GetACP." << std::endl; }
	return uiNewLocale;
}
bool Hook_GetACP(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&GetACP_Original), GetACP_Hook);
}
//bool Hook_get_unknown(bool bEnable)
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_get_unknown), _get_unknown_Hook);
//}
//bool Hook_get_resource_object(bool bEnable)
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_get_resource_object), _get_resource_object_Hook);
//}
//bool Hook_com_ptr_t_IWzProperty__ctor(bool bEnable)
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_com_ptr_t_IWzProperty__ctor), _com_ptr_t_IWzProperty__ctor_Hook);
//}
//bool Hook_com_ptr_t_IWzProperty__dtor(bool bEnable)
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_com_ptr_t_IWzProperty__dtor), _com_ptr_t_IWzProperty__dtor_Hook);
//}
bool HookPcCreateObject_IWzResMan(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9FAF55), _PcCreateObject_IWzResMan_Hook);
}
bool HookPcCreateObject_IWzNameSpace(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9FAFBA), _PcCreateObject_IWzNameSpace_Hook);
}
bool HookPcCreateObject_IWzFileSystem(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9FB01F), _PcCreateObject_IWzFileSystem_Hook);
}
bool HookCWvsApp__Dir_BackSlashToSlash(bool bEnable)
{
	BYTE firstval = 0x56;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F95FE;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F95FE), _CWvsApp__Dir_BackSlashToSlash_rewrite);
}
bool HookCWvsApp__Dir_upDir(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F9644), _CWvsApp__Dir_upDir_Hook);
}
bool Hookbstr_ctor(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_406301), _bstr_ctor_Hook);
}
bool HookIWzNameSpace__Mount(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F790A), _IWzNameSpace__Mount_Hook);
}
//void loadMyShA() {	//partial credits to blackwings v95
//	void* pDataFileSystem = nullptr;
//	void* pThePackage = nullptr; //9FB0E9
//	//@_com_ptr_t<_com_IIID<IWzNameSpace,&_GUID_2aeeeb36_a4e1_4e2b_8f6f_2e7bdec5c53d> > g_root
//	//_sub_9FAFBA(L"NameSpace", g_root, NULL);//void __cdecl PcCreateObject(const wchar_t* sUOL, _com_ptr_t<_com_IIID<IWzNameSpace, &_GUID_2aeeeb36_a4e1_4e2b_8f6f_2e7bdec5c53d> > *pObj, IUnknown * pUnkOuter)
//
//	void* pIWzNameSpace_Instance = g_root; //partial credits to https://github.com/MapleMyth/ClientImageLoader
//	//auto PcSetRootNameSpace = (void(__cdecl*)(void*, int)) * (int*)pNameSpace;//Hard Coded//HRESULT __cdecl PcSetRootNameSpace(IUnknown *pNameSpace)
//	//PcSetRootNameSpace(pIWzNameSpace_Instance, 1);
//
//	char sStartPath[MAX_PATH];
//	GetModuleFileNameA(NULL, sStartPath, MAX_PATH);
//	_CWvsApp__Dir_BackSlashToSlash_rewrite(sStartPath);	//_sub_9F95FE
//	_sub_9F9644(sStartPath);//_CWvsApp__Dir_upDir
//
//	strcat(sStartPath, "/Ezorsia_v2_files");//sStartPath += "./Ezorsia_v2_files";
//	//char sStartPath2[MAX_PATH]; strcpy(sStartPath2, sStartPath);
//	//strcat(sStartPath2, "/");//sStartPath += "./Ezorsia_v2_files";
//
//	Ztl_bstr_t BsStartPath = Ztl_bstr_t();
//	_sub_425ADD(&BsStartPath, nullptr, sStartPath);//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//	//Ztl_bstr_t BsStartPath2 = Ztl_bstr_t();
//	//_sub_425ADD(&BsStartPath2, nullptr, "/");//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//
//	_sub_9FB01F(L"NameSpace#FileSystem", &pDataFileSystem, NULL);//void __cdecl PcCreateObject(const wchar_t *sUOL, _com_ptr_t<_com_IIID<IWzFileSystem,&_GUID_352d8655_51e4_4668_8ce4_0866e2b6a5b5> > *pObj, IUnknown *pUnkOuter)
//	_sub_9FB084(L"NameSpace#Package", &pThePackage, NULL);//void __cdecl PcCreateObject_IWzPackage(const wchar_t *sUOL, ??? *pObj, IUnknown *pUnkOuter)
//	HRESULT v0 =_sub_9F7964(pDataFileSystem, nullptr, BsStartPath);//HRESULT __thiscall IWzFileSystem::Init(IWzFileSystem *this, Ztl_bstr_t sPath)
//	std::cout << v0 << " Hook_sub_9F7159 HRESULT 1: " << BsStartPath.m_Data << "   " << sStartPath << std::endl;
//	
//	const char* myWzPath = "EzorsiaV2_wz_file.wz";
//	Ztl_bstr_t BmyWzPath = Ztl_bstr_t();
//	_sub_425ADD(&BmyWzPath, nullptr, myWzPath);//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//
//	Ztl_variant_t pBaseData = Ztl_variant_t();
//	_sub_5D995B(pDataFileSystem, nullptr, &pBaseData, BmyWzPath);//Ztl_variant_t *__thiscall IWzNameSpace::Getitem(IWzNameSpace *this, Ztl_variant_t *result, Ztl_bstr_t sPath)
//
//	const char* mysKey = "83";
//	Ztl_bstr_t BmysKey = Ztl_bstr_t();
//	_sub_425ADD(&BmysKey, nullptr, mysKey);//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//	const char* mysBaseUOL = "/";
//	Ztl_bstr_t BmysBaseUOL = Ztl_bstr_t();
//	_sub_425ADD(&BmysBaseUOL, nullptr, mysBaseUOL);//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//	_sub_9F79B8(pThePackage, nullptr, BmysKey, BmysBaseUOL, pCustomArchive);//HRESULT __thiscall IWzPackage::Init(IWzPackage *this, Ztl_bstr_t sKey, Ztl_bstr_t sBaseUOL, IWzSeekableArchive *pArchive)
//
//
//
//	_sub_425ADD(&BsStartPath, nullptr, "/");//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s) //Ztl_bstr_t ctor
//	HRESULT v1 = _sub_9F790A(pIWzNameSpace_Instance, nullptr, BsStartPath, pThePackage, 0); //HRESULT __thiscall IWzNameSpace::Mount(IWzNameSpace *this, Ztl_bstr_t sPath, IWzNameSpace *pDown, int nPriority)
//	std::cout << v1 << " Hook_sub_9F7159 HRESULT 2: " << BsStartPath.m_Data << "   " << sStartPath << std::endl;
//
//
//
//
//	//void __thiscall _com_ptr_t<_com_IIID<IWzSeekableArchive == v95 9C4830 v83 9F7367
//} 
//bool Hook_sub_9F7159_initialized = true;
bool resmanSTARTED = false;
static _CWvsApp__InitializeResMan_t _sub_9F7159_append = [](CWvsApp* pThis, void* edx) {
	//-> void {_CWvsApp__InitializeResMan(pThis, edx);
	//if (Hook_sub_9F7159_initialized)
	//{
	//	std::cout << "_sub_9F7159 started" << std::endl;
	//	Hook_sub_9F7159_initialized = false;
	//}
	resmanSTARTED = true;
	//loadMyShA();
	//void* pData = nullptr;
	//void* pFileSystem = nullptr;
	//void* pUnkOuter = 0;
	//void* nPriority = 0;
	//void* sPath;
	//edx = nullptr
	// 
	//// Resman
	//_PcCreateObject_IWzResMan(L"ResMan", &g_rm, pUnkOuter);	//?(void*) //?&g

	//void* pIWzResMan_Instance = *&g_rm;	//?&g	//custom added, find existing instance
	//!!auto IWzResMan__SetResManParam = *(void(__fastcall**)(void*, void*, void*, int, int, int))((*(int*)pIWzResMan_Instance) + 20); // Hard Coded
	//!!IWzResMan__SetResManParam(nullptr, nullptr, pIWzResMan_Instance, RC_AUTO_REPARSE | RC_AUTO_SERIALIZE, -1, -1);

	//// NameSpace
	//_PcCreateObject_IWzNameSpace(L"NameSpace", &g_root, pUnkOuter);

	//void* pIWzNameSpace_Instance = &g_root;
	//auto PcSetRootNameSpace = (void(__cdecl*)(void*, int)) * (int*)pNameSpace; // Hard Coded
	//PcSetRootNameSpace(pIWzNameSpace_Instance, 1);

	//// Game FileSystem
	//_PcCreateObject_IWzFileSystem(L"NameSpace#FileSystem", &pFileSystem, pUnkOuter);
	_sub_9F7159(pThis, nullptr);	//comment this out and uncomment below if testing, supposed to load from .img files in folders but i never got to test it
	resmanSTARTED = false;
	//char sStartPath[MAX_PATH];
	//GetModuleFileNameA(NULL, sStartPath, MAX_PATH);
	//_CWvsApp__Dir_BackSlashToSlash(sStartPath);
	//_CWvsApp__Dir_upDir(sStartPath);

	//_bstr_ctor(&sPath, pData, sStartPath);

	//auto iGameFS = _IWzFileSystem__Init(pFileSystem, pData, sPath);

	//_bstr_ctor(&sPath, pData, "/");

	//auto mGameFS = _IWzNameSpace__Mount(*&g_root, pData, sPath, pFileSystem, (int)nPriority);

	//// Data FileSystem
	//_PcCreateObject_IWzFileSystem(L"NameSpace#FileSystem", &pFileSystem, pUnkOuter);

	//_bstr_ctor(&sPath, pData, "./Data");

	//auto iDataFS = _IWzFileSystem__Init(pFileSystem, pData, sPath);

	//_bstr_ctor(&sPath, pData, "/");

	//auto mDataFS = _IWzNameSpace__Mount(*&g_root, pData, sPath, pFileSystem, (int)nPriority);
	};
bool Hook_sub_9F7159(bool bEnable)	//resman hook that does nothing, kept for analysis and referrence //not skilled enough to rewrite to load custom wz files
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F7159;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F7159), _sub_9F7159_append);
}
struct KeyValuePair {
	int key;
	std::string value;
};
KeyValuePair newKeyValuePairs[] = {
    {11, "设置"},
    {12, "新手"},
    {13, "战士"},
    {14, "弓箭手"},
    {15, "飞侠"},
    {16, "海盗"},
    {17, "管理员"},
    {18, "骑士团"},
    {19, "可以"},
    {20, "管理员"},
    {22, "战士"},
    {23, "剑客"},
    {24, "勇士"},
    {25, "英雄"},
    {26, "准骑士"},
    {27, "骑士"},
    {28, "圣骑士"},
    {29, "枪战士"},
    {30, "龙骑士"},
    {31, "黑骑士"},
    {32, "法师"},
    {33, "巫师"},
    {34, "魔导师"},
    {35, "法师(火,毒)"},
    {36, "巫师(火,毒)"},
    {37, "魔导师(火,毒)"},
    {38, "法师(冰,雷)"},
    {39, "巫师(冰,雷)"},
    {40, "魔导师(冰,雷)"},
    {41, "牧师"},
    {42, "祭司"},
    {43, "主教"},
    {44, "弓箭手"},
    {45, "猎人"},
    {46, "射手"},
    {47, "神射手"},
    {48, "弩弓手"},
    {49, "游侠"},
    {50, "箭神"},
    {51, "飞侠"},
    {52, "刺客"},
    {53, "无影人"},
    {54, "隐士"},
    {55, "侠客"},
    {56, "独行客"},
    {57, "侠盗"},
    {58, "拳手"},
    {59, "斗士"},
    {60, "冲锋队长"},
    {61, "火枪手"},
    {62, "大副"},
    {63, "船长"},
    {64, "初心者"},
    {65, "服务器人数已满\r\n请稍后再试！"},
    {66, "死亡状态中不能执行该操作"},
    {67, "想返回开始界面吗？"},
    {68, "先按确定键确认是否可用该角色名。"},
    {69, "身份证号码不正确\r\n请重新输入！"},
    {70, "不能再创建新角色。"},
    {71, "是否删除该角色？"},
    {72, "[创建角色注意事项]\r\n请使用文明用语，否则将被系统限制！"},
    {73, "'%s'是已使用的角色名"},
    {74, "'%s'是可以使用的角色名"},
    {75, "发生未知错误\r\n不能做角色名检测。"},
    {76, "发生未知错误\r\n无法创建新角色。"},
    {77, "无法连接游戏服务器\r\n请稍等后再试。"},
    {78, "与游戏服务器连接中断\r\n请稍等后再试。"},
    {79, "与服务器连接发生错误\r\n请稍等后再试。"},
    {80, "无法登陆服务器。\r\n详情查看冒险岛ONLINE官方网站。"},
    {82, "与登录服务器连接中断\r\n请稍等后再试。"},
    {83, "本机内存不足\r\n详细的解决办法请访问官方网站。"},
    {84, "没有数据文件\r\n请下载最新客户端。"},
    {85, "不正确的游戏Data\r\n请下载最新客户端。"},
    {86, "不正确的游戏数据\r\n请下载最新客户端。"},
    {87, "不正确的客户端\r\n请下载最新客户端。"},
    {88, "与服务器连接发现错误\r\n请稍等后再试。"},
    {89, "与其他软件发生冲突\r\n请关闭其它软件后再试。如还发生该问题，请重新启动电脑再试或访问官方网站。"},
    {90, "与其他软件发生冲突。\r\n可能是电脑病毒，请先使用杀毒软件清除病毒后再试！"},
    {91, "该IP地址被禁止\r\n或点数不足。"},
    {92, "请登录MapleStory主页并按\r\n开始游戏按钮。"},
    {93, "请下载完整的冒险岛客户端。"},
    {122, "/悄悄话"},
    {123, "/组队"},
    {124, "/好友"},
    {125, "/家族"},
    {126, "/联盟"},
    {127, "/夫妻"},
    {128, "/所有人"},
    {129, "/找人"},
    {130, "/交易"},
    {131, "/商店"},
    {132, "/物品移动"},
    {133, "/组队状况"},
    {134, "/组队开启"},
    {135, "/组队离开"},
    {136, "/组队邀请"},
    {137, "/组队退出"},
    {138, "/组队搜索"},
    {139, "/公会信息"},
    {140, "/退出公会"},
    {141, "/公会邀请"},
    {142, "/踢出公会"},
    {143, "/呕吐"},
    {144, "/哎呀"},
    {145, "/招待"},
    {146, "/宠物"},
    {147, "正在处理别的事情。"},
    {148, "管理帐户无法进行。"},
    {149, "MWLB帐户无法进行。"},
    {150, "不能与别的操作同时进行。"},
    {151, "向'%s'玩家传送了悄悄话。"},
    {152, "找不到'%s'玩家。"},
    {153, "'%s'玩家在'%s'里。"},
    {154, "'%s'玩家在冒险岛ONLINE商城中"},
    {155, "'%s'玩家的现在位置是'%s'"},
    {156, "'%s'玩家在特别活动地区里"},
    {157, "'%s'玩家现在拒绝悄悄话的状态。"},
    {158, "你未加入组或没有在线的队员。"},
    {159, "你还未结婚或配偶没有在线。"},
    {160, "没有可对话的好友。"},
    {161, "没有加入家族或没有在线的家族成员"},
    {162, "成功停止了。"},
    {163, "失败停止了。"},
    {164, "成功解止了。"},
    {165, "提示语语法上有问题。请再确认。"},
    {166, "失败解止了。"},
    {167, "在排行榜上解除成功了。"},
    {168, "不正确的角色名。"},
    {169, "在排行榜上解除失败了。"},
    {170, "是不正确的NPC名或变数名。"},
    {171, "请求失败"},
    {172, "修改成功"},
    {173, "修改失败 - 找不到所雇用的商人！"},
    {174, "雇用商人位置 : %s"},
    {175, "找不到所雇用的商人！"},
    {247, "你好。我是冒险岛管理员。现在被解封的怪物侵略射手村。勇士们快来保护射手村"},
    {248, "你好。我是冒险岛管理员。现在被解封的怪物侵略魔法密林。勇士们快来保护魔法密林"},
    {249, "你好。我是冒险岛管理员。现在被解封的怪物侵略勇士部落。勇士们快来保护勇士部落"},
    {250, "你好。我是冒险岛管理员。现在被解封的怪物侵略废落都市。勇士们快来保护废落都市"},
    {251, "你好。我是冒险岛管理员。现在被解封的怪物侵略明珠港。勇士们快来保护明珠港"},
    {252, "你好。我是冒险岛管理员。现在被解封的怪物侵略天空之城。勇士们快来保护天空之城"},
    {253, "你好。我是冒险岛管理员。现在被解封的怪物侵略玩具城。勇士们快来保护玩具城"},
    {254, "你好。我是冒险岛管理员。现在被解封的怪物侵略童话村。勇士们快来保护童话村"},
    {257, "怪物在侵略村落。拜托大家来保护村落"},
    {258, "从怪物的袭击中成功解救了村庄. 获得'恶魔气息'后找NPC'冒险岛运营员'或者已经封印 '恶魔气息'的人请直接去找NPC '记忆者'."},
    {266, "现在无法操作。\r\n请稍等后再试。"},
    {267, "你现在所处的位置。\r\n不能查看大地图！"},
    {270, "该NPC所在世界地图无法显示。"},
    {271, "请输入用喇叭想说的内容。"},
    {272, "不能使用禁止语"},
    {273, "使用喇叭的话最大可以写英文字母60字，汉字30字．"},
    {275, "在彩虹岛里不能使用。"},
    {276, "在这里做不了。"},
    {277, "在这个地图做不了。"},
    {278, "不能到别的岛去。"},
    {279, "过分的好奇心会妨碍升级\r\n\r\n真愿意打开包吗？"},
    {280, "现在不能聊天。"},
    {281, "得到经验值 (+%d)"},
    {282, "网吧特别经验值(+%d)"},
    {283, "连续击杀额外奖励经验(+%d)"},
    {284, "结婚奖励经验值(+%d)"},
    {285, "道具佩戴附加奖励经验值(+%d)"},
    {286, "得到了""天天活动""奖励经验值。(+%d)"},
    {287, "以后%d次为止完成的任务的奖励经验值会增加活动奖励经验值。"},
    {288, "获得人气 (+%d)"},
    {289, "人气度减少了 (%d)"},
    {290, "获取金币 (+%d)"},
    {291, "网吧特别金币 (+%d)"},
    {292, "金币减少 (%d)"},
    {293, "丢弃物品 (%s %d个)"},
    {294, "丢弃物品 (%s)"},
    {295, "不能拣取物品。"},
    {296, "现金道具[%s]已过期\r\n道具被清除了。"},
    {297, "正在处理别的请求。\r\n请稍后再试。"},
    {298, "增加'%s'的人气度。"},
    {299, "降低了'%s'的人气度。"},
    {300, "角色名不正确。"},
    {301, "等级不满15级的角色不能使用人气度功能。"},
    {302, "今天不能再使用人气度功能。"},
    {303, "这个月不能再操作特定角色的人气度。"},
    {304, "'%s'玩家提高了'%s'玩家的人气度。"},
    {305, "'%s'玩家降低了'%s'玩家的人气度。"},
    {306, "未知原因操作人气度失败。"},
    {307, "请你稍后再试。"},
    {308, "%s玩家处于拒绝组队状态。"},
    {309, "%s玩家拒绝了组队招待。"},
    {310, "开新组队。"},
    {311, "从组队被退出了。"},
    {312, "组队结束。"},
    {313, "'%s'玩家从组队被退出了。"},
    {314, "'%s'玩家离开组队。"},
    {315, "队长退出，组队结束了。"},
    {316, "队长解散组队，离开了组队。"},
    {317, "加入组队。"},
    {318, "'%s'玩家参加到组队。"},
    {319, "已经加入其他组。"},
    {320, "新手不能开启组队。"},
    {321, "没有参加的组队。"},
    {322, "组队成员已满"},
    {323, "不是队长"},
    {324, "无法招待自己。"},
    {325, "'%s'玩家不是该队的队员"},
    {326, "管理员不能开组队"},
    {327, "管理员不能加入组队"},
    {328, "不能加入队伍"},
    {329, "发生未知错误，\r\n不能处理组队邀请。"},
    {330, "在这频道，找不到角色。"},
    {331, "组员:"},
    {332, "你是队长。"},
    {333, "超过限制时间拒绝了组队的邀请。"},
    {334, "%s玩家拒绝加入家族状态"},
    {335, "%s玩家拒绝了家族邀请"},
    {336, "加入了%s家族"},
    {337, "被退出家族"},
    {338, "离开家族。"},
    {339, "'%s'玩家被退出了家族"},
    {340, "被删除的角色自动退出了家族。"},
    {341, "'%s'退出了家族"},
    {342, "你想把'%s'玩家退出家族吗？"},
    {343, "你想离开家族吗？"},
    {344, "'%s'退出了家族"},
    {345, "家族解散了"},
    {346, "加入了家族"},
    {347, "'%s'玩家加入了家族"},
    {348, "已经有家族了"},
    {349, "等级太低不能建立家族"},
    {350, "没有参加的家族"},
    {351, "族长不能退出家族。"},
    {352, "家族人数已满"},
    {353, "新手不能当族长"},
    {354, "不是家族的族长"},
    {355, "不能邀请自己"},
    {356, "'%s'玩家不是家族成员"},
    {357, "只有家族长才有权限除去副家族长。"},
    {358, "家族长无法除去。"},
    {359, "管理员不能开家族"},
    {360, "不能再邀请家族成员了。"},
    {361, "发生未知错误无法处理有关家族的邀请"},
    {362, "当前频道找不到该角色"},
    {363, "成员:"},
    {364, "你是族长"},
    {365, "家族人数上限增加到%d了。"},
    {366, "找不到该角色。"},
    {367, "已加入的好友。"},
    {368, "不能把自己加入到好友。"},
    {369, "未加入好友。"},
    {370, "没做登录邀请的好友"},
    {371, "现在关闭了缩地门。"},
    {372, "不能去别的岛"},
    {373, "不正确的频道号"},
    {374, "这里是优秀会员专用区。\r\n在这里你可享受些\r\n特别的服务。"},
    {375, "\r\n在Windows98也会发生游戏进行不太顺利的情况。\r\n请你利用XP或2000系统"},
    {376, "哦哦。。。你想领养我创造的宠物啊！有些事情我必须告诉你！我是开采生命水的魔法师。\r\n\r\n那些宠物是我在娃娃身上加上生命水后，再用魔法创造的魔法生命。我研究有关生命的魔法已经很长时间了，终于发现了创造生命的魔法。但还是有缺点，那就是－－没有生命水，这魔法也会消失。而且生命水每次采集的数量很少。所以那些宠物，那些宠物每隔一段时间都需要复活一次。。。"},
    {377, "当生命水消耗完的时候，它就会回到娃娃的状态。虽然这是很悲伤的事情，但也不必太伤心。用生命水就可以使它复活。为了能让大家一直拥有可爱的宠物，我现在每天都在很深的地下开采生命水。如果以后你需要生命水的话，去找我的徒弟吧。你可别忘记，宠物的日子只有”90”天罢了。你千万要珍惜它。。。"},
    {378, "结束了魔法的时间，不能动作下去。"},
    {379, "该宠物无法复活。\r\n在商城可购买新的宠物。\r\n请问要移动么？"},
    {380, "亲密度增加 (+%d)"},
    {381, "亲密度下降 (%d)"},
    {382, "宠物肚子饿，刚才回家了。"},
    {383, "宠物耗干生命水变了娃娃。"},
    {384, "已超过可同时召唤的最大上限！\r\n请将宠物转换为娃娃后再试！"},
    {385, "您要将此宠物设为主要宠物吗？\r\n（移动速度，跳跃力，\r\n不拣取特定道具功能随主要宠物的能力。）"},
    {386, "请选择要安装装备的宠物！"},
    {387, "卷轴发出一道亮光，在道具上添加了某种神秘的能力。"},
    {388, "卷轴发出一道亮光，但是道具没有任何变化。"},
    {389, "因为卷轴的力量，道具消失了。"},
    {390, "想用<%s>音乐箱奏曲吗？"},
    {392, "航班时刻表如下。"},
    {393, "出发前5分钟可以进入候车室。"},
    {394, "已经关闭了。"},
    {395, "已经满员了。现在不能进去。"},
    {396, "正在做别的事情。"},
    {397, "死亡状态中不能执行该操作。"},
    {398, "在特别活动中不能执行该操作。"},
    {399, "该角色不能执行该操作。"},
    {400, "不可能做另外交易。"},
    {401, "在这里不能开办店铺。"},
    {402, "在同一个地图里\r\n才可以做交易。"},
    {403, "'%s'玩家正在做别的事。"},
    {404, "'%s'玩家拒绝了你的邀请。"},
    {405, "'%s'玩家是拒绝邀请的状态。"},
    {406, "对方中止交易。"},
    {407, "交易成功了。\r\n请再确认交易的结果。"},
    {408, "扣除手续费后，获得了%d金币。\r\n请确认。"},
    {409, "交易失败了。"},
    {410, "因部分道具有数量限制只能拥有一个\r\n交易失败了。"},
    {411, "双方在不同的地图不能交易。"},
    {412, "要登记多少?"},
    {413, "真要做交易吗?"},
    {414, "请输入要开设的店铺名\r\n\r\n开店铺前移动到别的地方\r\n就不能开设店铺了。"},
    {415, "每%d个当%s金币"},
    {416, "%s金币"},
    {417, "店铺关门"},
    {418, "该物品已售完"},
    {419, "物品的数量应该是销售单位的倍数。"},
    {420, "总价是%d金币。\r\n真愿意买物品吗？"},
    {421, "不能加卖物品。"},
    {422, "单位销售"},
    {423, "单价"},
    {424, "物价"},
    {425, "要卖的物品总量"},
    {426, "每单位的物品数量"},
    {427, "该商品已经售完。"},
    {428, "无法进入该店铺"},
    {429, "你想将%s玩家踢出去吗？"},
    {430, "被踢开了。"},
    {431, "超过时间限制被自动踢出了。\r\n无法再次进入。"},
    {432, "请输入要开的游戏名"},
    {433, "因为你的金币不足,你不能参加。"},
    {434, "在这里不能开游戏房。"},
    {435, "个人商店只在自由市场上\r\n可以开设."},
    {436, "只限位于自由市场入口2楼以上的\r\n房间(7号~22号)中使用。"},
    {437, "[%s]加入房间。"},
    {438, "[%s]退出房间。"},
    {439, "[%s]玩家申请退出房间。"},
    {440, "[%s]玩家取消退出申请。"},
    {441, "[%s]玩家强行退出房间。"},
    {442, "你被强制退出房间。"},
    {443, "[%s]玩家弃权了。"},
    {444, "[%s]玩家的让步要求被同意了。"},
    {445, "从游戏房被踢出了。"},
    {446, "本轮由[%s]玩家翻牌。"},
    {447, "[%s]玩家成功了，继续进行。"},
    {448, "因[%s]玩家金币不足，不能开始游戏。"},
    {449, "开始了。"},
    {450, "游戏结束了。\r\n10秒后自动关房。"},
    {451, "剩下10秒"},
    {452, "关房了。"},
    {453, "赢了。"},
    {454, "平局。"},
    {455, "输了。"},
    {456, "真的弃权吗？"},
    {457, "要退出对方吗？"},
    {458, "对方请求平局。\r\n同意吗？"},
    {459, "要向对方请求平局吗？"},
    {460, "对方拒绝了平局请求。"},
    {461, "让步请求每一局只可以一次。"},
    {462, "对方请求让步一手。\r\n同意吗？"},
    {463, "要向对方请求让步一手吗？"},
    {464, "对方拒绝了你的让步请求。"},
    {465, "要预定退房吗？"},
    {466, "要取消预定退房吗？"},
    {467, "剩下时间 : %d 秒"},
    {468, "[ %s ]玩家的顺序。"},
    {469, "真要退房吗？"},
    {470, "密码不对了。"},
    {471, "愿意非公开游戏吗？"},
    {472, "不能进入锦标赛房。"},
    {473, "双活三位置。"},
    {474, "不能在这里放下。"},
    {475, "不能飘在这里"},
    {476, "接受物品的角色名不正确。"},
    {477, "操作成功。请再确认结果。"},
    {478, "正在执行其他操作。"},
    {479, "不能搬到同账户的角色。"},
    {480, "对方正在连接游戏。"},
    {481, "对方的物品背包里没有空间。"},
    {482, "没有要搬运的物品或金钱。"},
    {483, "未登录的角色。"},
    {484, "未知原因，请求失败。"},
    {485, "如果选择换购该商品\r\n该商品就会在物品框中消失\r\n其现在价格的%d％(%d)将转化为你的抵用券\r\n用抵用券也可以购买商品。\r\n确实要换购该商品吗？"},
    {486, "删除被选的道具\r\n不会转换成抵用卷\r\n还想删除该道具吗？\r\n "},
    {487, "%s增加仓库窗。（%d点券）\r\n确定是否购买？"},
    {488, "增加仓库窗（%d点券）\r\n确实要增加吗？"},
    {489, "角色增加会员卡(%d点券)\r\n增加后无法取消或退款。\r\n是否增加角色槽个数？"},
    {490, "购买后，可以在90天内装备额外物品\r\n。\r\n购买后无法取消或退款。\r\n是否购买？"},
    {491, "不能增加被选的背包空格数。"},
    {492, "不能增加扩充仓库空间。"},
    {493, "无法增加角色栏个数。"},
    {494, "由职业限制不能使用该道具。\r\n还要买道具吗？"},
    {495, "等级不够无法使用该道具。\r\n还要买道具吗？"},
    {496, "人气度不足无法使用该道具。\r\n还要买道具吗？"},
    {497, "(因为有需要的能力要求\r\n不能用有些道具。)"},
    {498, "该道具已存在。\r\n真要买道具吗？"},
    {499, "现在你装备的武器\r\n不能使用这种道具。\r\n你真想要购买吗？"},
    {500, "只有鲁道夫宠物可以装备该物品."},
    {501, "你现在的宠物不能装备\r\n该道具。还想购买吗？"},
    {502, "这些装备只有宠物能用。\r\n还想购买吗？"},
    {503, "没有宠物就无法使用的道具\r\n宠物变成娃娃的时候也无法使用.\r\n还要购买吗？"},
    {504, "购买后无法取消或退款。\r\n是否购买该物品？"},
    {505, "目前你的宠物\r\n无法使用的道具.\r\n还要购买吗？"},
    {506, "购买后无法取消或退款。是否购买上述物品？"},
    {507, "一共 %d点券"},
    {508, "确定购买吗？"},
    {509, "购买失败"},
    {510, "买好了你选的道具。\r\n(但是买不到有些道具。)"},
    {511, "请输入要增送礼物的角色名\r\n只能赠送给同一世界的玩家。"},
    {512, "%s(%d现金)\r\n赠送后无法取消或退款。\r\n是否将物品赠送给“%s”\r\n？"},
    {513, "请确认身份证号后\r\n再试。"},
    {514, "请确认出生日期后\r\n再试。"},
    {515, "请确认帐户后\r\n再试。"},
    {516, "该名字不能使用\r\n请再确认。"},
    {517, "该名字已被使用\r\n请输入其他名字。"},
    {518, "可以使用此名字，请按确定。"},
    {519, "该名字已被使用，\r\n请输入其他名字。"},
    {521, "该道具已被购买，名称更改已提交。"},
    {523, "这适用于请求的限制。\r\n请检查您是否在一个月内已经\r\n更改角色名称。 "},
    {527, "在关注物品目录上已经有同样的道具。"},
    {528, "关注商品目录已满了。"},
    {529, "买好了。"},
    {530, "送给'%s'\r\n%d个[%s]。"},
    {531, "增加了背包的容量。"},
    {532, "已经增加角色栏个数。"},
    {533, "删除了现金道具。"},
    {534, "超过了工作时间。\r\n请稍后再试。"},
    {535, "点券余额不足。"},
    {536, "未满14岁的用户不能\r\n赠送现金道具。"},
    {537, "超过了可以送礼物的限界额。"},
    {538, "请确认是否超过\r\n可以保有的现金道具数量。"},
    {539, "请确认角色名是否正确\r\n或有性别限制。"},
    {540, "超过了该道具的每日购买限额，\r\n无法购买。"},
    {541, "你的性别不适合这种领奖卡。"},
    {542, "已经使用过的领奖号"},
    {543, "已经过期的领奖号"},
    {547, "请再确认领奖卡号码是否正确。"},
    {549, "领奖卡已过期作废。"},
    {550, "领奖卡已经领过奖。"},
    {551, "这种领奖卡是专用道具。\r\n所以你不能赠送别人。"},
    {552, "此券是冒险岛专用抵用券无法送给\r\n他人。"},
    {553, "这种道具只在优秀会员网吧\r\n买得到。"},
    {554, "恋人道具只能赠送给相同频道的\r\n不同性别的角色。请确认\r\n是否你要送礼物的角色\r\n在同一频道或性别不同。"},
    {555, "超过了点卷购买限制额。"},
    {556, "现金商店不可用\r\n在beta测试阶段。\r\n对于给您带来的不便，我们深表歉意。"},
    {557, "因为发生未知错误。\r\n不能进入到MapleStory商城。"},
    {558, "用抵用券买道具。"},
    {559, "已有重复角色名"},
    {560, "你确认要送礼物给%d个人吗?"},
    {561, "所有的礼物都已送出.\r\n消耗了%d点券."},
    {562, "该礼物无法送出。"},
    {563, "[ %s ]\r\n已成功送达以上角色.\r\n(有可能无法送给部分人.)消耗了\r\n%d点券."},
    {564, "我要送礼物给别人。"},
    {565, "除了镖以外的没有期限的道具才可以换购。"},
    {566, "这种道具不能换购。\r\n有期限的和按次消耗的道具\r\n都不能换购。"},
    {567, "现金道具换购成功。\r\n(增加%d 抵用券)"},
    {568, "目前选上来的关注物品目录"},
    {569, "欢迎您来到MapleStory商城。"},
    {570, "正在准备商品。带来不便请大家谅解。"},
    {571, " 点券"},
    {572, "(%d个)"},
    {573, "加在关注物品目录上。"},
    {574, "[宠物]%s"},
    {575, "仓库增加。"},
    {576, "为仓库添加%d个格子（当前：%d个）\r\n最多48格。%d 点券"},
    {577, "为仓库添加%d个格子（当前：%d个）\r\n最多48格。"},
    {578, "增加%s背包空间"},
    {579, "增加%d个%s背包格子. (当前: %d 个)\r\n最多%d格.%d 点券."},
    {580, "增加%d个%s背包格子. (当前: %d 个)\r\n最多%d格"},
    {581, "增加角色栏（现在%d个）\r\n 每次可增加1个角色，最多可以增加到%d个角色。"},
    {582, "请你正确输入领奖卡号码30位。"},
    {583, "请你正确输入要送礼物的角色名。"},
    {584, "要送礼物的角色名"},
    {585, "收到了"},
    {586, "\r\n%s %d个"},
    {587, "抵用券 %d点"},
    {588, "%d金币"},
    {589, "向%s玩家"},
    {590, "充值"},
    {591, "现在不销售的商品。"},
    {592, "您想要离开商城吗？"},
    {593, "你的信用卡额度不够"},
    {594, "发生了未知错误，\r\n无法确认在线商店购买信息。\r\n游戏商店可以正常使用。"},
    {595, "拥有的金币道具\r\n发生错误，\r\n无法确认在线商店购买信息。\r\n游戏商店可以正常使用。"},
    {596, "无法拿到在线商店中购买的商品\r\n。\r\n请确认可拥有的金币道具个数\r\n没有超限。\r\n游戏商店可以正常使用。"},
    {597, "在线商店有错误订单。\r\n游戏商店可以正常使用。"},
    {598, "强化宝石"},
    {599, "怪物结晶"},
    {600, "分解装备"},
    {601, "单手剑"},
    {602, "单手斧"},
    {603, "单手钝器"},
    {604, "短刀"},
    {605, "短杖"},
    {606, "长杖"},
    {607, "双手剑"},
    {608, "双手斧"},
    {609, "双手钝器"},
    {610, "枪"},
    {611, "矛"},
    {612, "弓"},
    {613, "弩"},
    {614, "拳套"},
    {615, "指节"},
    {616, "短枪"},
    {617, "帽子"},
    {618, "脸饰"},
    {619, "眼饰"},
    {620, "耳环"},
    {621, "套服"},
    {622, "上衣"},
    {623, "裤/裙"},
    {624, "鞋子"},
    {625, "手套"},
    {626, "披风"},
    {627, "盾牌"},
    {628, "戒指"},
    {629, "坠子"},
    {630, "勋章"},
    {631, "腰带"},
    {632, "全身铠甲"},
    {633, "武器"},
    {634, "单手武器"},
    {635, "双手武器"},
    {636, "药水类"},
    {637, "回城卷轴"},
    {638, "武器及防具卷轴"},
    {639, "弓箭，标枪和小球"},
    {640, "战利品"},
    {641, "母矿类"},
    {642, "游戏"},
    {643, "促进剂及制作法"},
    {644, "非常快"},
    {645, "比较快"},
    {646, "快"},
    {647, "普通"},
    {648, "慢"},
    {649, "比较慢"},
    {650, "非常慢"},
    {651, "'%s'送给你的礼物。"},
    {652, "可以用%d天"},
    {653, "可以用4小时"},
    {654, "可以用%d小时"},
    {655, "使用期限到%04d年%d月%d日 %02d:%02d"},
    {656, "攻击速度 :"},
    {657, "攻击力 :"},
    {658, "魔法力 :"},
    {659, "防御力 :"},
    {660, "魔法防御力 :"},
    {661, "命中率 :"},
    {662, "回避率 :"},
    {663, "手技 :"},
    {664, "移动速度 :"},
    {665, "跳跃力 :"},
    {666, "游泳速度 : +%d"},
    {667, "直接攻击时发生后退现象的几率 :"},
    {668, "可升级次数 :"},
    {669, "添加防滑"},
    {670, "添加防寒"},
    {671, "'%s'玩家送给你的礼物"},
    {672, "是与%s玩家之间的恋人道具"},
    {673, "'%s'玩家的挚友道具。"},
    {676, "魔法时间 : %d"},
    {677, "魔法时间 : %d小时 %d分钟"},
    {678, "结束了魔法时间。"},
    {679, "%04d年%02d月%02d日%02d时前可用"},
    {680, "%s / %s /%s / 优秀"},
    {681, "在线好友数 / 总好友数"},
    {682, "离线好友数 / 总好友数"},
    {683, "[ 已登录好友数 / 可登录最大好友数 ]"},
    {684, "在线族人 / 总家族成员"},
    {685, "离线族人 / 总家族成员"},
    {686, "以名字顺序排列"},
    {687, "以职业顺序排列"},
    {688, "以级别顺序排列"},
    {689, "以职位顺序排列"},
    {690, "已登录人员 / 可登录人员"},
    {691, "在别的地方"},
    {692, "固有道具"},
    {693, "任务道具"},
    {694, "组队任务道具"},
    {695, "不可交换"},
    {696, "可交换1次(交易后不可交换)"},
    {697, "集合道具"},
    {698, "以种类排列"},
    {699, "装备后无法交换"},
    {708, "现在的好友数  [%d/%d]"},
    {709, "目前位置 - %s"},
    {710, "%s'的位置 - %s"},
    {711, "请输入朋友的角色名。\r\n\r"},
    {712, "输入组队想邀请的角色名。"},
    {713, "真要把'%s'玩家从好友目录\r\n上删除？"},
    {714, "%s玩家"},
    {715, "连接了"},
    {716, "是同频道"},
    {717, "与%s玩家"},
    {718, "冒险岛ONLINE商城"},
    {719, "因为发生未知错误，你的请求无法处理"},
    {720, "好友目录已满了。"},
    {721, "对方的好友目录已满了。"},
    {722, "已经是好友。"},
    {723, "没登录的角色。"},
    {724, "不能把管理员加为好友。"},
    {725, "请输入要加入黑名单的角色名。"},
    {726, "无法把自己放到黑名单上"},
    {727, "这不是角色名\r\n请再次确认。"},
    {728, "该角色已经登陆过\r\n请你在确认下。"},
    {729, "购买商城短消息道具后才可以使用。"},
    {730, "组队成员可输入2~6人。"},
    {731, "已招募组队成员%d人以上。"},
    {732, "等级范围最高为30级。"},
    {733, "目前MapleStory的最高等级为\r\n200级！"},
    {734, "本人的等级必须在搜索等级的范围之内。"},
    {735, "搜索等级的最大值要比最小值大！"},
    {736, "请选择希望组成组队的职业。"},
    {737, "请先选择想要制作的道具！"},
    {738, "包裹中空间不足。"},
    {739, "无法添加的道具。"},
    {740, "相同的道具已经登录。"},
    {741, "无法登录更多的材料。"},
    {742, "只能登录强化宝石。"},
    {743, "无法登录更多的强化宝石。"},
    {744, "相同种类的强化宝石只能登录1个。"},
    {745, "此强化宝石只能用在武器上。"},
    {746, "只能登录催化剂。"},
    {747, "催化剂只能登录1个。"},
    {748, "无法登录的催化剂。"},
    {749, "无法用作怪物结晶的材料。"},
    {750, "由于未知错误，道具制作失败。"},
    {751, "制作了%d个%s。"},
    {752, "%s已成功分解。"},
    {753, "制作失败。"},
    {754, "道具已破坏。"},
    {755, "您要登录几个呢？"},
    {756, "您要制作吗？"},
    {757, "您要制作怪物结晶吗？"},
    {758, "你要分解装备吗？"},
    {759, "您要制作吗？"},
    {760, "可以在此登录怪物战利品\r\n，制作成怪物结晶。"},
    {761, "将需要分解的装备放到这里，\r\n可以获得怪物结晶。"},
    {762, "请你输入要招待的角色名。"},
    {763, "'%s'玩家的"},
    {764, "招待邀请。"},
    {765, "招待好友。"},
    {766, "交易申请。"},
    {767, "组队邀请。"},
    {768, "家族邀请。"},
    {769, "邀请加入冒险学院。"},
    {770, "新的任务！"},
    {771, "任务完成！"},
    {772, "彩虹岛"},
    {773, "金银岛"},
    {774, "没有有关任务的信息。"},
    {775, "管理账号不能扔掉金币。"},
    {776, "- 向'%s'玩家传递了招待邀请。"},
    {777, "- 找不到'%s'玩家。"},
    {778, "'%s'玩家现在是拒绝聊天状态。"},
    {779, "'%s'玩家拒绝了邀请。"},
    {780, "- 向'%s'玩家发出聊天邀请。"},
    {781, "‘%s'玩家邀请你\r\n聊天。是否同意？"},
    {782, "请输入悄悄话的对象。"},
    {783, "账户不对。"},
    {784, "不能和自己说悄悄话。"},
    {785, "请选择对话的群。\r\n\r"},
    {786, "对所有人"},
    {787, "对组队"},
    {788, "对好友"},
    {789, "对夫妻"},
    {790, "对家族"},
    {791, "对群"},
    {792, "请输入想传递的内容。"},
    {793, "该名字已经存在\r\n换名字吗？"},
    {794, "请给宠物起名字。"},
    {795, "不可用该名字。"},
    {796, "%s\r\n\r\n这名字对吗？"},
    {797, "你想用金币包\r\n来获取 %d金币么?"},
    {798, "你已使用金币包\r\n获取了 %d 金币。"},
    {799, "使用金币包失败。"},
    {800, "[ MapleStory通讯 帮助 ]"},
    {801, "招待 ：/招待 角色名"},
    {802, "结束 ：/q"},
    {803, "- ‘%s'玩家入场了。"},
    {804, "- 不能参与受招待的聊天房。"},
    {805, "- ‘%s'玩家退出房间。"},
    {806, "- 请输入另外角色名。"},
    {807, "- 角色名不正确。"},
    {808, " 玩家正在输入"},
    {809, "角色名长度应在%d字符以上。"},
    {810, "角色名长度应在%d字符以下。"},
    {811, "角色名里不能包含空格。"},
    {812, "在角色名包含\r\n非法字符。"},
    {813, "在角色名包含\r\n限制字符。"},
    {814, "对角色名不正确。"},
    {815, "MapleStory文件夹"},
    {816, "桌面"},
    {817, "只能输入数字。"},
    {818, "只有%d以上的数字可以输入"},
    {819, "只有%d以下的数字可以输入"},
    {820, "必须输入数字%d位以上。"},
    {821, "必须输入%d字以上。"},
    {822, "请输入身份证号。"},
    {823, "请输入家族名"},
    {824, "该名字不能使用\r\n请再确认。"},
    {825, "该名字已被使用\r\n请输入其他名字。"},
    {826, "为保证帐户的安全,\r\n请输入8位数字生日代码。\r\n例：) 年/月/日 -> 20200220"},
    {827, "个人信息"},
    {828, "人气度 +"},
    {829, "人气度 -"},
    {830, "交易申请"},
    {831, "组队邀请"},
    {832, "储存名字"},
    {833, "由于系统错误，无法连接。"},
    {834, "此时将删除或限制ID的使用。"},
    {835, "ID未注册。"},
    {836, "ID已联机，\r\n或者服务器正在检查中。"},
    {837, "保存电子邮件地址"},
    {838, "电子邮件地址至少需要%d个字母。"},
    {839, "电子邮件地址的长度不能超过%d个字母。"},
    {840, "所有电子邮件地址都必须包含“@”。\r\n（例如）maple@wizet.com"},
    {841, "所有电子邮件地址都必须包含“.”\r\n（ex）maple@wizet.com"},
    {842, "密码长度不能超过%d个字母。"},
    {843, "你必须年满20岁才能进入。"},
    {844, "你不在授权的网吧里。"},
    {845, "已超出此IP地址允许的最大用户数。"},
    {846, "IP不在服务范围内"},
    {847, "现在不是玩这个游戏的时间。"},
    {848, "真要买吗？"},
    {849, "买多少？"},
    {850, "真要卖吗？"},
    {851, "卖多少？"},
    {852, "物品不够。"},
    {853, "请确认是不是你的\r\n背包的空间不够。"},
    {854, "这种道具不能拿两个以上。"},
    {855, "发生未知错误，不能交易。"},
    {856, "%s 金币 / 充值 : %d"},
    {857, "真充值吗？"},
    {858, "真要取去吗？"},
    {859, "取出一次需要%d金币\r\n真想取出吗？"},
    {860, "真要保管吗？"},
    {861, "保管一次需要%d金币\r\n真想保管吗？"},
    {862, "保管几个？"},
    {863, "取出多少？"},
    {864, "保管多少？"},
    {865, "仓库已满。"},
    {867, "此装备在装备后将无法交易。\r\n您要装备吗？"},
    {868, "能力值不足，不能使用该道具。"},
    {869, "恋人戒指每个人只能使用一个。"},
    {870, "挚友戒指一个人只能使用一个"},
    {871, "你基本上应该装备某种道具\r\n才可以使用。"},
    {872, "你一定要装备盾牌\r\n才可以使用。"},
    {873, "需要有驯养的怪物，\r\n才可以装备。"},
    {874, "需要有基本装备的鞍子，\r\n才可以装备。"},
    {875, "此道具不适用于您当前使用的武器。"},
    {876, "您不能将该装备与您当前装备的\r\n现金道具一起使用。\r\n请取消装备现金道具，然后重试。"},
    {877, "此物品不能装备给您当前的宠物。"},
    {878, "你不能在这个地图里更换装备。"},
    {879, "您当前没有一个活跃的宠物。"},
    {880, "目前的宠物无法装备的道具。"},
    {881, "管理帐户\r\n不能扔掉道具。"},
    {882, "这种道具不能扔掉。"},
    {883, "在当前地图中无法变更属性。"},
    {884, "该技能无法在当前地图使用。"},
    {885, "扔掉多少？"},
    {886, "这种道具不能给别人。"},
    {887, "提出多少？"},
    {888, "[欢迎] 欢迎光临冒险岛ONLINE世界。"},
    {889, "[冒险岛ONLINE 帮助]"},
    {891, "请文明游戏\r\n禁止刷屏！"},
    {892, "你的话太多了\r\n别人会讨厌的。"},
    {893, "目前处于禁言状态。无法进行聊天。"},
    {894, "一月"},
    {895, "二月"},
    {896, "三月"},
    {897, "四月"},
    {898, "五月"},
    {899, "六月"},
    {900, "七月"},
    {901, "八月"},
    {902, "九月"},
    {903, "十月"},
    {904, "十一月"},
    {905, "十二月"},
    {906, "不能连接。"},
    {907, "剩下时间: %02d分 %02d秒"},
    {908, "在特别活动用地图上不能说悄悄话。"},
    {909, "[答案:"},
    {910, "用户太少不能开始。"},
    {911, "还没设定奖品。"},
    {912, "恭喜你！晋级%d强战。"},
    {913, "恭喜你！晋级决赛。"},
    {914, "恭喜你成为冠军。"},
    {915, "成功地设定了奖品。"},
    {916, "奖品设定失败。\r\n请再确认物品号码。"},
    {917, "--------奖品--------\r\n第1名 : %s\r\n 第2名 : %s"},
    {918, "%s %02d时 %02d分 %02d秒"},
    {919, "自动更新发生错误，请你下载最新的客户端。"},
    {920, "在冒险岛世界过得愉快了吗？\r\n\r\n如果画面便成黑色和什么也看不上，\r\n或者在画面上表出频率范围超过等警告，\r\n\r\n实行[开始程序->Maplestory->Setup]\r\n同时调整频率范围吧。"},
    {921, "#e 冒险岛ONLINE 基本操作法#n\r\n\r\n#fUI/UIWindow.img/UtilDlgEx/notice#"},
    {922, "----------------- 帮助 -----------------"},
    {923, "/组队情报 : 查看现在组队的情况。"},
    {924, "/组队开启 : 开新的组队。"},
    {925, "/组队离开 : 从现在参加的组队脱出。"},
    {926, "/组队邀请 角色名 :邀请参加组队。(只有队长能做）"},
    {927, "/组队退出 角色名 : 从组队退出。(只有队长能做）"},
    {928, "/家族情报 : 查看现在家族情报"},
    {929, "/家族离开 : 从现在参加的家族出去"},
    {930, "/家族邀请 角色名 :邀请进入家族。(只有族长能做）"},
    {931, "/家族退出 角色名 : 从家族退出。(只有族长能做）"},
    {932, "/找人 角色名 : 查看特定角色的连接状态和位置。"},
    {933, "/游戏交换 角色名：申请交换游戏物品。"},
    {934, "/现金交换 角色名：申请交换现金物品。"},
    {935, "/好友: 设置为与在线好友对话的状态。"},
    {936, "/组队: 设置为与在线队员对话的状态。"},
    {937, "/家族: 设置为与在线家族员对话的状态。"},
    {938, "/悄悄话 角色名：设置为悄悄话状态(省略角色名时，向最近对象发送)"},
    {939, "/所有人: 设置为非悄悄话的一般对话"},
    {940, "*打开聊天输入窗口，按Tab键，或关闭聊天输入窗口"},
    {941, "请选择...."},
    {942, "男"},
    {943, "女"},
    {1163, "北斗"},
    {1369, "您所选择的游戏区人数较多，建议您选择其他区创建角色或进行游戏"},
    {1370, "您所选择的游戏区已经人满，请您选择其他区创建角色或进行游戏"},
    {1392, "广告窗被关掉。"},
    {1393, "这里是无法使用广告窗的地方。"},
    {1661, "奔覆"},
    {1662, "蹈框"},
    {1663, "官帕"},
    {1664, "泵辑"},
    {1753, "装备"},
    {1757, "其他"},
    {1763, "消耗"},
    {1886, "表示现在所充的能量值。\n可以用鼠标移动位置。"},
    {1951, "    AP如下分配。\r\n\r\n "},
    {2006, "等级:%d"},
    {2007, "[要求]"},
    {2008, "等级%d"},
    {2042, "力量 :"},
    {2043, "敏捷 :"},
    {2044, "智力 :"},
    {2045, "运气 :"},
    {2401, "键盘设定"},
    {2402, "装备(%s)"},
    {2403, "道具(%s)"},
    {2404, "能力值(%s)"},
    {2405, "技能(%s)"},
    {2495, "是%n强战竞赛。"},
    {2496, "是半决赛。"},
    {2497, "是决赛。"},
    {2505, "现在不是销售时刻。"},
    {2506, "这种商品已经卖完了。"},
    {2509, "以不战而胜进入到%n强战。"},
    {2510, "以不战而胜进入到半决赛。"},
    {2511, "以不战而胜进入到决赛。"},
    {2550, "此客户端只能在朝鲜语代码页中执行"},
    {2551, "初始化COM失败"},
    {2596, "[%s]已成功登记到怪物手册。"},
    {2597, "是已经登记到怪兽书的卡。获取的卡已消失。"},
    {2625, "购买人 : %s"},
    {2626, "购买的数量 : %d"},
    {2627, "总销售额 / 总收入额"},
    {2639, "关闭消息窗，所有消息将被清除\r\n你要继续吗？"},
    {2640, "条消息来了。"},
    {2641, "收到了"},
    {2642, "新消息."},
    {2651, "对方现在连接了游戏\r\n请你利用悄悄话功能。"},
    {2652, "请检查收信者的姓名。"},
    {2653, "对方的消息保管箱已满了。\r\n等以后再试。"},
    {2654, "你可以发送最大100字(汉字 50字)的消息"},
    {2695, "%d点券"},
    {2696, "%d %d点券"},
    {2710, "这里没有地方落脚。"},
    {2711, "请输入消息的内容。"},
    {2712, "不能发送消息给自己。"},
    {2713, "消息发送了。"},
    {2718, "因为对方现在拒绝悄悄话的状态。你可能不会收到回复。"},
    {2723, "'%s'玩家正在做别的事情"},
    {2769, "找不到说“悄悄话”的对象"},
    {2770, "⒏"},
    {2772, " ⒑"},
    {2773, "收到'%s'[%d个],来自[%s]的礼物"},
    {2806, "被赠方角色名"},
    {2811, "密码"},
    {2812, "现金道具[%s]\r\n已经过期\r\n该道具已经消失。"},
    {2837, "%s月%2d日%4d年 %2d:%02d %s以后\r\n才可以登录游戏"},
    {2838, "您的HP不够，不能使用该技能。"},
    {2839, "您的MP不够，不能使用该技能。"},
    {2840, "您的金币不够，不能使用该技能。"},
    {2841, "您的%s数量不够，不能使用该技能。"},
    {2842, "飞镖不足，无法使用该技能。"},
    {2843, "您的弹药已经用光了！ 使用技能所需道具不足。"},
    {2844, "你现在装备的武器，不能使用该技能。"},
    {2845, "当前能力值不足，不能使用装备。"},
    {2846, "没有装备武器，无法进行攻击。"},
    {2847, "只限使用HP恢复道具。"},
    {2848, "只限使用MP恢复道具。"},
    {2849, "您的箭已经用光了！"},
    {2850, "您的飞镖已经扔光了！"},
    {2851, "请佩戴手枪。"},
    {2864, "礼包商品，不能使用“购买试穿产品”功能。"},
    {2874, "这个程序不能和冒险岛Online一起运行！如果结束这个程序，该程序的状态或相关的数据将会丢失，你要结束这个程序吗？\r\n\r"},
    {2875, "被永久禁止登陆的账户，不能使用。"},
    {2881, "按价格排序"},
    {2882, "按人气排序"},
    {2883, "按推出时间的先后排序"},
    {2886, "该道具[%s]已经过期消失了。"},
    {2891, "现在不能截图。请稍后再试。"},
    {2906, "你的技能被封印不能使用。"},
    {2907, "你处于虚弱状态，不能跳跃。"},
    {2908, "你处于昏迷状态，不能移动。"},
    {2909, "你被诅咒了，获得的经验值减少。"},
    {2910, "你处于暗黑状态，命中率减少了。"},
    {2911, "你已经中毒了，HP逐渐减少。"},
    {2923, "如果开店前移动到别的地方，就不能开设店铺。"},
    {2924, "超过对方的最大金额\r\n无法交易．"},
    {2925, "该商品的价格太贵。你买不起"},
    {2949, "现在找不到%s玩家的位置。\r\n不能作瞬间移动。"},
    {2950, "不能瞬间移动的地图。"},
    {2951, "你不能设定自己为瞬间移动的对象。"},
    {2952, "不能登录的地图。"},
    {2953, "你的地图目录已经满了。请你删除以前的地图名。"},
    {2954, "已经登录的地图"},
    {2955, "你现在位置的地图。"},
    {2956, "7级以下玩家不能离开彩虹岛"},
    {2957, "你要将下面地图增加\r\n在地图目录？\r\n[%s]"},
    {2958, "你在地图目录\r\n删除下面的地图？\r\n[%s]"},
    {2959, "你确定要移动到以下的地图吗？\r\n[%s]"},
    {2964, "你已经有这种道具\r\n不能购买。"},
    {2965, "无法移动的物品。"},
    {2966, "消耗1个原地复活术，在当前地图复活了。(剩余%d 天/%d 小时)"},
    {2967, "消费一次%s道具，经验值不变。"},
    {2968, "巧克力棍子"},
    {2976, "        排行不可用"},
    {2977, "%d名"},
    {2983, "不能拣取该物品。"},
    {2984, "不能去那里"},
    {2985, "无法进行瞬间移动的地区。"},
    {2986, "因为有地气阻挡，无法移动。移动到就近的村庄。"},
    {2996, "请你输入要举报的角色名。"},
    {2997, "发送信息成功。"},
    {2998, "黑客"},
    {2999, "挂机"},
    {3000, "诈骗"},
    {3001, "冒充GM"},
    {3002, "骚扰"},
    {3003, "你成功举报了该角色。"},
    {3004, "找不到该角色。"},
    {3005, "每人每天只能举报10次。"},
    {3006, "你被举报了。"},
    {3007, "因未知的错误，无法举报！"},
    {3012, "对不起，正在准备MapleStory商城"},
    {3029, "#e《冒险岛》最终用户使用许可协议#n\r\n \r\n本#e《最终用户使用许可协议》#n（以下称《协议》，包括《用户服务条款》、《用户须知》、《用户守则》、《版权声明》）是用户（个人或单一实体，以下或称“您”）与#e上海数龙科技有限公司#n、（以下简称“盛大游戏”）之间有关本网络游戏软件产品\r\n（即网络游戏#e《冒险岛》#n，英文名#e《maplestory》#n，以下简称“软件产品”）使用的法律协议。上海盛付通电子商务有限公司应用的全付通在线电子商务服务平台共同提供#e《冒险岛》#n的在线服务\r\n本“软件产品”包括计算机软件，并可能包括相关网络服务器、网站（包括但不限于#e游戏官方网站：http://mxd.sdo.com/#n）、电子媒体、印刷材料和“联机”或电子文档。您一旦安装、复制、访问网站、充值、运行客户端软件或以其它方式使用“软件产品”，\r\n "},
    {3030, "即表示您同意接受本《协议》各项条款的约束。如您不同意本《协议》中的条款，请不要以上述任何一种方式使用“软件产品”。\r\n \r\n盛大游戏在此特别提醒您认真阅读本协议的全部条款，特别是其中免除或者限制盛大游戏责任的免责声明条款（该等条款通常含有“不负任何责任”、“无义务”等词汇）以及用户须遵守的《用户服务条款》、《用户须知》、《用户守则》、《版权声明》中的条款和\r\n其它限制用户权利的条款（该等条款通常含有“不得”等词汇），这些条款应在中国法律所允许的范围内最大程度地适用。除非用户接受本协议的全部条款，否则无权安装、复制、访问网站、充值、运行客户端软件或以其它方式使用“软件产品”。\r\n \r\n "},
    {3031, "本协议（包括《用户服务条款》、《用户须知》、《用户守则》、《版权声明》）下的条款可由盛大游戏随时变更，用户须定期审阅本协议。协议条款一旦发生变动，盛大游戏将会在盛大游戏相关的页面上提示变更内容。变更后的协议一旦在相关的页面上公布即有效代替\r\n原来的协议。如用户不同意盛大游戏对本协议的所作的任何变更，用户应立即停止使用盛大游戏的线上游戏。如用户在本协议变更后继续使用盛大游戏的线上游戏，即视作用户已完全同意变更后的协议。\r\n \r\n盛大游戏特别申明，未成年人应在法定监护人的陪同下审阅和接受本协议。对于未满十四周岁的未成年人，必须由其法定监护人以法定监护人的名义申请注册。未成年人用户应当在合理程度内使用“软件产品”及进行线上游戏，不得因使用“软件产品”及进行线上游戏\r\n而影响了日常的学习生活。用户理解盛大游戏无义务对本款前述事项进行任何形式的审查和确认。\r\n \r\n "},
    {3032, "1 许可权利的授予。本《协议》授予您下列权利：#e《冒险岛》#n网络游戏客户端软件的安装、运行。您可在许可生效的时间内将#e《冒险岛》#n网络游戏客户端软件安装在自己使用的联网计算机上，并以客户端软件指定的方式运行本“软件产品”的一份副本。\r\n其他任何形式的未经许可的安装、使用、访问、显示、运行以及转让，都将被视为对《协议》的侵犯。\r\n \r\n2 依本合同规定，著作权人许可您可以通过官方网站许可的方式获取仅授权本产品的客户端软件在一台计算机上使用。产品可以载入个人电脑或永久存取装置的方式使用。本产品可由一部个人电脑移转到另一部个人电脑使用，但不得同时存在于两部电脑中。\r\n依本合同第9条移转给他人时，使用者载入个人电脑的部分应予以删除，不得留存使用。\r\n \r\n3 禁止性行为或活动。\r\n \r\n "},
    {3033, "3.1 禁止用户发生以下侵害本网络游戏公平性的行为，包括但不限于：\r\n3.1.1 利用反向工程、编译或反向编译、反汇编等技术手段制作软件对游戏进行分析、修改、攻击，最终达到作弊的目的；\r\n3.1.2 使用任何外挂程序或游戏修改程序（本协议所称“外挂程序”是指独立于游戏软件之外的，能够在游戏运行的同时影响游戏操作的所有程序，包括但不限于模拟键盘鼠标操作、改变操作环境、修改数据等一切类型。\r\n如国家有管法律、法规及政府主管部门的规章或规范性文件规定的外挂定义与本协议有冲突，\r\n则以法律、法规、部门规章或规范性文件规定的为准），对本网络游戏软件进行还原工程、编译、译码或修改，包括但不限于修改本软件所使用的任何专有通讯协议、对动态随机存取内存（RAM）中资料进行修改或锁定；\r\n "},
    {3034, "3.1.3 使用异常的方法登录游戏、使用网络加速器等外挂软件或机器人程式等恶意破坏服务设施、扰乱正常服务秩序的行为；\r\n3.1.4 制作、传播或使用外挂、封包、加速软件，及其它各种作弊程序，或组织、教唆他人使用此类软件程序，或销售此类软件程序而为私人或组织谋取经济利益；\r\n3.1.5 使用任何方式或方法，试图攻击提供游戏服务的相关服务器、路由器、交换机以及其他设备，以达到非法获得或修改未经授权的数据资料、影响正常游戏服务，以及其他危害性目的的任何行为；\r\n3.1.6 利用线上游戏系统可能存在的技术缺陷或漏洞而以各种形式为自己及他人牟利（包括但不限于复制游戏中的虚拟物品等）。\r\n3.2 一旦盛大游戏通过内部的监测程序发现或经其他用户举报而发现您有可能正在从事上述行为，则盛大游戏有权采取相应的措施进行弥补，该措施包括但不限于限制您账号的登陆、限制您在游戏中的活动、\r\n "},
    {3035, "删除与复制有关的物品（包括复制出的虚拟物品和参与复制的虚拟物品）、删除您的账号和要求您赔偿因您从事上述行为而给盛大游戏造成的损失等。\r\n \r\n4 对反向工程(ReverseEngineering)、反向编译(Decompilation)、反汇编(Disassembly)的禁止。您不得对本“软件产品”进行反向工程(ReverseEngineering)、反向编译(Decompile)或反汇编(Disassemble)，您亦同意放弃行使适用法律允许上述活动之权利。\r\n \r\n5 服务条款的确认和接纳。本“软件产品”的运营权归盛大游戏。盛大游戏提供的服务完全按照其发布的章程、服务条款和操作规则严格执行。您应该遵守盛大游戏制定的上述管理规定。如果您发生违反此类管理规定的行为，\r\n盛大游戏可终止本《协议》并停止您使用“软件产品”的权力。如此类情况发生，您必须立即销毁“软件产品”的所有副本及其所有组成部分。\r\n\r\n6 用户必须提供的设备和须提供的信息。盛大游戏提供可使用的“软件产品”，并运用自己的网络系统通过国际互联网络（Inte\r\net）为用户提供服务。同时，用户必须：\r\n "},
    {3036, "(1)自行配备上网的所需设备，包括个人电脑、调制解调器或其他必备上网装置。\r\n(2)自行负担个人上网所支付的与此服务有关的电话费用、网络费用。\r\n基于盛大游戏提供服务的重要性，用户应同意：\r\n \r\n(1)提供详尽、准确的个人资料。\r\n(2)不断更新注册资料，符合及时、详尽、准确的要求。\r\n(3)牢记您填写的注册资料、历史信息。\r\n盛大游戏在为您提供相关客户服务的前提是您能表明您是账号的所有人，这可能需要您提供相关信息（包括但不限于注册信息、历史密码等），如果用户没有牢记自己填写的注册资料及相关历史信息或未及时更新相关注册资料，\r\n您的相关问题（包括但不限于密码找回等）将得不到解决，对此盛大游戏不承担任何责任。\r\n \r\n "},
    {3037, "7 拒绝提供担保。用户个人对网络服务的使用承担风险。盛大游戏对以下事宜不作任何类型的担保，不论是明确的或隐含的：\r\n(1)本协议项下的“软件产品”及盛大游戏提供的相关服务将符合用户的要求；\r\n(2)本协议项下的“软件产品”及盛大游戏提供的相关服务将不受不可抗力、计算机病毒、黑客攻击、系统不稳定、用户所在位置、用户关机、电信部门原因及其他任何网络、技术、通信线路等外界或人为因素的影响；\r\n(3)安装、复制、访问网站、充值、运行客户端软件或以其它方式使用“软件产品”及/或接受盛大游戏提供的相关服务与任何其他软件不存在任何冲突；\r\n(4)通过盛大游戏网站、游戏官方网站及其他相关网络上的链接和标签所指向的第三方的商业信誉及其提供服务的质量。\r\n \r\n8 免除责任。盛大游戏对任何直接、间接、偶然、特殊及继起的损害不负责任，这些损害可能来自：不正当使用网络服务，非法使用网络服务或用户传送的信息有所变动等方面。用户明确同意其安装、复制、访问网站、充值、运行客户端软件或以其它方式使用“软件产品”及/或\r\n接受盛大游戏提供的相关服务所存在的风险将完全由其自己承担。\r\n \r\n "},
    {3038, "因其安装、复制、访问网站、充值、运行客户端软件或以其它方式使用“软件产品”及/或接受盛大游戏提供的相关服务而产生的一切后果也由其自己承担，盛大游戏对用户不承担任何责任。\r\n \r\n9 使用者可以移转本产品和所授之权利（不包括用户的账号、密码及线上游戏中的虚拟物品等）予他人，但是移转时应连同所附产品使用手册、授权合同等一并移转，不得留存任一样使用。且该受让人接受本产品和所授之权利时即视同接受遵守本协议\r\n（包括《用户服务条款》、《用户须知》、《玩家守则》、《版权声明》）的全部条款。\r\n \r\n10 除本合同有其他规定外，未经盛大游戏书面同意，使用者严格禁止有下列行为（无论是有偿的还是无偿的）：\r\n \r\n10.1 复制、翻拷、传播和在网络上陈列本产品的程序、使用手册和其它图文音像资料的全部或部分内容。\r\n \r\n "},
    {3039, "10.2 公开展示和播放本产品的全部或部分内容。\r\n \r\n10.3 出租本产品于他人。\r\n \r\n10.4 对本产品的程序、图像、动画和音乐进行还原、反编译、反汇编、剪辑、翻译和改编等任何修改行为。\r\n \r\n10.5 修改或遮盖本产品程序、图像、动画、包装和手册等内容上的产品名称、公司标志、版权信息等内容。\r\n \r\n10.6 以本产品作为营业使用。\r\n \r\n "},
    {3040, "10.7 其它违反著作权法、计算机软件保护条例和相关法规的行为。\r\n \r\n一旦用户有实施违反上述内容的行为，本产品的授权协议将立即停止。盛大游戏有权通过各种合法途径向因违反上述条款而对盛大游戏造成损害的用户追究法律责任。\r\n \r\n11 违约赔偿\r\n \r\n11.1 用户同意保障和维护盛大游戏及其他用户的利益，如因用户违反有关法律、法规或本协议项下的任何条款而给盛大游戏造成损害，用户同意承担由此造成的损害赔偿责任，该等责任包括但不限于给盛大游戏造成的任何直接或间接损失。\r\n \r\n11.2 因用户违反有关法律、法规或本协议项下的任何条款导致任何第三方向盛大游戏主张任何索赔、要求或损失的，用户同意赔偿盛大游戏由此产生的任何直接或间接损失。\r\n \r\n "},
    {3041, "12 法律适用和争议解决\r\n \r\n12.1 本协议的订立、履行、解释及争议的解决均应适用中国法律。\r\n \r\n12.2 如双方就本协议内容或其执行发生任何争议，双方应尽量友好协商解决；协商不成时，任何一方均应向盛大游戏所在地的人民法院提起诉讼。\r\n \r\n13 通知和送达。本协议项下盛大游戏的所有通知均可通过重要页面公告、电子邮件或常规的信件传送等方式进行；该等通知于发送之日视为已送达收件人。\r\n \r\n "},
    {3042, "14 其他规定\r\n \r\n14.1 本“软件产品”受著作权法及国际著作权条约和其它知识产权法和条约的保护；本“软件产品”只许可在给定范围和时间内使用，而不出售其原代码和其他任何相关知识产权权利。\r\n \r\n14.2 关于本“软件产品”的《用户服务条款》、《用户须知》、《玩家守则》、《版权声明》等文件均属于本《协议》不可分割的一部分，与本《协议》同样有效。\r\n \r\n "},
    {3043, "14.3 本协议构成双方对本协议之约定事项及其他有关事宜的完整协议，除本协议规定的之外，未赋予本协议各方其他权利。\r\n \r\n14.4 如本协议中的任何条款无论因何种原因完全或部分无效或不具有执行力，在此情况下，该无效或不具有执行力的部分将被最接近原条款意图的一项有效并可执行的条款所替代，并且本协议的其余条款仍应有效并且有约束力。\r\n \r\n "},
    {3044, "14.5 本协议中的标题仅为方便而设，不会对本协议的其他条款有限制作用，也不具有任何法律效力。\r\n \r\n#e上海数龙科技有限公司#n\r\n \r\n "},
    {3045, "二○○九年四月\r\n \r\n "},
    {3062, "『椰子比赛』活动开始了"},
    {3063, "『椰子比赛』活动已经结束了。你将被送到下一个地图。请你稍等。"},
    {3065, "总分数:%d 彩虹:%d 神秘:%d"},
    {3066, "两队的数量相同，增加了2分钟的时间"},
    {3067, "2分钟后还平分的话，两个队都被处理为输，都没有奖品。"},
    {3068, "『彩虹』组和『神秘』组的分数统一。这场比赛平局了."},
    {3069, "『彩虹』队赢了"},
    {3070, "『神秘』队赢了"},
    {3071, "OX问答中，有 %d 玩家答错了，请再接再厉。"},
    {3072, "『雪球赛』活动已经结束。你将被移动到别的地方。请稍等。"},
    {3081, "的"},
    {3082, "管理员生成"},
    {3109, "请你输入要留言的角色名。"},
    {3110, "你不能向自己送礼物"},
    {3111, "你可以留言最多72字符。"},
    {3112, "请你输入留言的内容。"},
    {3113, "给你的礼物"},
    {3114, "在线商店\r\n购买成功。"},
    {3115, "先生通过在线商店送出的礼物。"},
    {3116, "请你输入心中想说的。"},
    {3117, "原价：%d 点卷"},
    {3118, "打折："},
    {3138, "%04d年%02d月%02d日%02d时%02d分前可用"},
    {3140, "你好，我是冒险岛GM"},
    {3141, "我正在查找非法自动打猎程序的使用者。请停止打猎，并按照GM的指示去做"},
    {3142, "如果不遵照GM的指示，你将会被视作非法自动打猎程序使用者，并受到相应的处罚"},
    {3143, "你的帐号已作为非法自动打猎程序使用者处理。"},
    {3144, "详细内容请咨询客服中心"},
    {3145, "祝你在MapleStory度过美好的一天"},
    {3158, "找不到该角色"},
    {3159, "对不进行攻击的角色不能使用"},
    {3160, "已经被使用过测谎仪的角色"},
    {3161, "测谎仪正在查询的角色"},
    {3162, "谢谢你的帮助。你没有使用非法程序。我们已给予你5000金币奖励。"},
    {3163, "使用测谎仪后发现你使用过非法程序。再次被发现你会被封闭账号。"},
    {3165, "亲爱的%s，我是MapleStory GM。我正在查找非法自动打猎程序的使用者。请停止打猎，并按照GM的指示去做。如果没有应答或回避应答，你将会被视作非法自动打猎程序使用者，并受到相应的处罚"},
    {3166, "我是MapleStory GM。%s，你已被运营者限制使用。请查看上方通知"},
    {3167, "对%s玩家使用了测谎仪。"},
    {3168, "%s_截图已保存。已通知限制宏的使用。"},
    {3169, "%s_截图已保存。测谎仪已发动。"},
    {3170, "%s_你通过了测谎仪的测试。"},
    {3171, "%s_截图已保存。探测发现使用非法程序。"},
    {3172, "被使用测谎仪时无法操作。"},
    {3173, "无法使用测谎仪的地区。"},
    {3176, "等级过低，因此无法消耗您所选的道具。"},
    {3177, "等级太高，因此无法消耗您所选的道具。"},
    {3178, "测谎仪检查的结果发现该角色使用过非法程序。你获得了对方所有金币中的7000金币。"},
    {3179, "成功通过测谎仪的测试，谢谢配合。祝你冒险愉快！！"},
    {3180, "经过运营人员的核查，你被认定为非法自动打猎程序使用者，并将受到相应的处罚。"},
    {3181, "门口附近不能使用时空门技能"},
    {3202, "\r\n#b#L%d# #i%07d# %s %d张#l"},
    {3204, "#L%d##v%d:# #t%d:# %d个#l\rn"},
    {3206, "#v%d:# #t%d:# %d个\rn"},
    {3208, "#fUI/UIWindow.img/QuestIcon/7/0# %d 金币\rn"},
    {3209, "#fUI/UIWindow.img/QuestIcon/8/0# %d 经验\rn"},
    {3237, "没有限制"},
    {3238, "等级 %d以上"},
    {3239, "%s等级 %d以下"},
    {3240, "全职业都可以"},
    {3241, "全职业中只有新手不可能"},
    {3242, "全职业中只有%s不可能"},
    {3243, "第%d转"},
    {3244, "1转职业"},
    {3245, "2转职业"},
    {3246, "3转职业"},
    {3247, "4转职业"},
    {3248, "%d转以上"},
    {3249, "1转以上"},
    {3250, "2转以上"},
    {3251, "3转以上"},
    {3252, "4转以上"},
    {3253, "%d年 %d月 %d日 %02d时 完成"},
    {3257, "因未知错误，\r\n任务执行失败。\r\n请确认执行条件。"},
    {3258, "你真想放弃吗？"},
    {3259, "%s窗的空间不够"},
    {3260, " 或"},
    {3263, "族长"},
    {3264, "副族长"},
    {3265, "成员"},
    {3268, "%d时"},
    {3269, "%d分"},
    {3270, "网吧定量制时间剩余 %s%s"},
    {3271, "网吧定量制时间剩余 %s%s。(目前游戏使用者 %d人) 若剩余时间消耗完毕，则游戏将结束。"},
    {3275, "此会员卡只限于新购买\r\n现金道具用户使用。"},
    {3276, "好。我现在要问一下你的朋友们是否同意开设家族。所有人都同意了才能开设家族。你稍等一会儿吧。"},
    {3277, "有人不同意，请你以后再来吧。必须要所有人都同意，才可以开设家族。"},
    {3278, "该名字已经有别的家族使用。。。请你输入其他名字。"},
    {3279, "恭喜！%s 家族登记成立了。祝你们家族发展顺利。"},
    {3280, "家族已经解散了。。。如果你想再开设家族，随时欢迎来找我。"},
    {3281, "恭喜~ 你的家族人数限制增加到%d名了。祝你们以后发展顺利。"},
    {3282, "询问中出现异常。请重新开始。"},
    {3283, "开设家族过程中出现异常。请重新开始。"},
    {3284, "解散家族时出现异常。请重新开始。"},
    {3285, "增加家族人数上限时出现异常。请重新开始。"},
    {3286, "该道具已经有固有名字。"},
    {3287, "你愿意在%s上刻上你的名字吗?"},
    {3295, "[%s]玩家的地位变更为[%s]了。"},
    {3296, "保存好了。"},
    {3299, "请你输入要变更的地位名。"},
    {3300, "存在比这个地位低的地位\r\n就不能删除。"},
    {3301, "有这个地位的角色，\r\n就无法删除。"},
    {3302, "保存修改的内容吗？"},
    {3315, "      动物"},
    {3316, "      植物"},
    {3317, "      纹样"},
    {3318, "      文字"},
    {3319, "      其他"},
    {3339, "你确定作为家族标志吗？"},
    {3340, "保存好了"},
    {3343, "现在无法进入该频道。请稍后再尝试。"},
    {3344, "现在无法进入MapleStory商城。请稍后再尝试。"},
    {3347, "[%s]任务超过限制时间，已从目录中删除。"},
    {3348, "* 家族公告"},
    {3349, "* 家族公告 : %s"},
    {3350, "请你输入家族公告事项。"},
    {3366, "玩家收到了礼物"},
    {3367, "玩家的人气度增加1。"},
    {3374, "骂人/羞辱他人"},
    {3375, "欺诈"},
    {3376, "现金交易"},
    {3377, "谎称GM"},
    {3378, "泄漏个人资料"},
    {3379, "使用非法程式检举"},
    {3380, "您已成功注册。"},
    {3384, "你被检举了！"},
    {3385, "检举系统目前维护中！"},
    {3386, "请稍后重新再试。"},
    {3387, "请确认角色名称后重新再试。"},
    {3388, "检举所需金币不足！"},
    {3389, "无法连接服务器！"},
    {3390, "你已超过可检举次数。"},
    {3391, "无可检举的角色。"},
    {3392, "因目前是试用期间，\r\n可能在无事先告知的情况下，终止该项服务。"},
    {3393, "%04d年 %d月 %d日以后起\r\n可进行对话。"},
    {3394, "请输入状况说明。"},
    {3395, "只限%d点至%d点可进行检举。"},
    {3396, "该角色并无执行任何动作，无法检举该角色。"},
    {3397, "诬告的关系官方已经惩罚处理\r\n无法申告。"},
    {3398, "请选择"},
    {3399, "请选择要举报的角色。"},
    {3413, "%s队的雪球通过了%d区域。"},
    {3414, "因%s队攻击雪人，而使%s队的雪球停止运转。"},
    {3415, "%s队的雪球重新运转！"},
    {3416, "已封印的道具无法贩卖、交换或丢弃。"},
    {3417, "该道具无法封印！"},
    {3418, "请先选择所要封印的道具。"},
    {3419, "若封印该道具，便无法进行消耗。"},
    {3420, "%s\r\n确定要封印该道具吗？"},
    {3425, "该物品无法孵化。"},
    {3426, "请选择可以孵化的物品。"},
    {3427, "至少要有装备栏1个,消费栏3个,\r\n其他栏1个空间。\r\n您要孵化飞天猪的蛋么?"},
    {3431, "清单上没有空的栏位。\r\n请稍后再试。"},
    {3432, "从飞天猪的蛋中意外地获得了道具。"},
    {3433, "宠物所要消耗的HP药水不足！"},
    {3434, "宠物所要消耗的MP药水不足！"},
    {3435, "当系统设定中的HP警告标志闪烁时，便会服用药水。"},
    {3436, "当系统设定中的MP警告标志闪烁时，便会服用药水。"},
    {3437, "需要先解除封印。"},
    {3438, "为了完成任务，需解除对任务道具的封印。"},
    {3441, "购买成功！\r\n您获得了%d点的抵用券。"},
    {3442, "您送礼给'%s'\r\n [%s]\r\n %d个。\r\n获得了抵用券%d点。"},
    {3444, "%d点"},
    {3445, "因您正装备着该道具，无法回收！"},
    {3446, "该道具无法同时拥有数个！"},
    {3451, "雇用商人"},
    {3460, "%s的雇用商人"},
    {3462, "%s的雇用商人 : %s"},
    {3463, "已超过营业时间而关闭商店！"},
    {3464, "您的个人商店被GM关闭。\r\n请你去弗兰德里，跟他收回您的道具。"},
    {3465, "雇佣商店未打开，无法使用。"},
    {3466, "若您的背包里无空间可容纳个人商店的道具，\r\n则须请您至商店承租商NPC弗兰德里那取回。\r\n您确定要关闭商店吗？"},
    {3467, "管理中无法贩卖物品！\r\n您要开始商店管理吗？"},
    {3468, "商店主人正在整理物品。\r\n请稍后再度光临！"},
    {3469, "地图已移动，远程攻击\r\n中断。请稍候重新使用。"},
    {3470, "商店结束 / %02d : %02d"},
    {3471, "您已在第%s频道自由市场%d内开设商店，\r\n请先将该商店关闭后再重新使用。"},
    {3472, "%s频道开设有商店。\r\n您想要移动到该频道吗？"},
    {3473, "目前有其它角色正在使用中。\r\n请以该角色登入后关闭商店，\r\n或将商店仓库清空。"},
    {3474, "目前无法开设商店！"},
    {3475, "传送点附近无法开设商店！"},
    {3476, "请向自由市场入口处的弗兰德里\r\n领取物品后，重新再试。"},
    {3477, "将售出的物品从清单中删除，\r\n并收取售出金额。"},
    {3478, "道具或金币已成功取回。 "},
    {3479, "商店内的金币过多，\r\n未能领取金币与道具。\r\n请联系自由市场入口的\r\n弗兰德里。"},
    {3480, "已领取金币，但因部分道具有数量限制，\r\n未能取回道具！\r\n请洽自由市场入口处的弗兰德里。"},
    {3481, "已领取金币，但因背包位不足，\r\n未能取回道具！\r\n请洽自由市场入口处的弗兰德里。"},
    {3482, "已领取金币，但因不明原因，\r\n未能取回道具！\r\n请洽自由市场入口处的弗兰德里。"},
    {3483, "该道具限制的上架数量为一个。"},
    {3484, "该道具限制的拥有数量为一个。"},
    {3485, "您要将%s名称\r\n加以变更吗？"},
    {3486, "祝您的冒险愉快"},
    {3487, "看来你并没有可以领取的道具或金币。\r\n你在这里可取回未能在雇用商人那边领取的道具或金币，可是来见我时，记得要以开设商店的角色来找我喔！"},
    {3488, "你所雇用的商店开设在#b第%s频道自由市场%d#k内，\r\n若将商店关闭后有任何的需要时，请再来找我吧！"},
    {3489, "目前无法使用！\r\n请稍后再试！"},
    {3490, "因超过%d天，需支付全体的%d%%，即\r\n%d金币为手续费。\r\n确定要领回吗？"},
    {3491, "确定要领回吗？"},
    {3492, "你已经取回了道具或金币。"},
    {3493, "因商店仓库内的金额过多，\r\n未能领取金币与道具。"},
    {3494, "因道具有数量的限制，\r\n未能领取金币与道具。"},
    {3495, "因手续费不足，\r\n未能领取金币与道具。"},
    {3496, "因背包位不足，\r\n未能领取金币与道具。"},
    {3497, "请通过弗兰德里领取物品。"},
    {3498, "您好~我是雇用商人委员会的代表弗兰德里。雇用商人的使用方法与一般商店大致相同，只需注意几点注意事项，我把相关内容整理出来了，请确认一下的同意书后，再使用雇用商人功能。\r\n\r\n雇用商人同意书\r\n1) 在下列情况下，雇用商人会将金币与道具寄托到商店银行后消失。\r\n  - 雇用商人 #r已达有效期限#k\r\n  -在同一地点开设雇用商人 #r超过24小时#k \r\n  - 雇用商人撤销时，#r背包空间不足#k(将提醒1次)\r\n2)并且雇用商人消失时，需透过自由市场入口处的#r弗兰德里#k领取金币与道具\r\n3)在回收金币与道具前，无法再使用雇用商人的服务\r\n4)将金币与物品交托给商店银行后，经过#r24小时#k便开始征收每日销售金额与物品原价 1%的手续费\r\n5)当手续费超过100%时，便将此充公用作商店街发展委员会的经费\r\n6)若开设的商店内，包含脏话与不雅文字时，管理者可在无预警的情况下变更商店名称。"},
    {3499, "雇佣商店售出%s %d个。"},
    {3506, "没有宠物就无法完成任务\r\n请你召唤宠物后再来."},
    {3507, "宠物亲密度太低，或当前召唤的宠物\r\n无法完成任务。"},
    {3509, "1转或2转技能不足"},
    {3510, "1转技能不足"},
    {3511, "1转或2转或3转技能不够"},
    {3513, "内容"},
    {3528, "请重新确认电话号码！"},
    {3529, "请重新确认身份证号码！"},
    {3531, "请重新确认认证编码！"},
    {3535, "在变身状态上无法进行。"},
    {3536, "变身时无法进行。右击右上方图标，可以解除变身。"},
    {3537, "必须在能量全满的状态下使用的技能。"},
    {3538, "只能在变身状态下使用的技能。"},
    {3539, "只能在搭乘战舰的时候使用的技能。"},
    {3540, "得到了家族点 (+%d)"},
    {3541, "丢失了家族点 (%d)"},
    {3543, "连接后已经经过了%d个小时"},
    {3544, "连接后已经经过了%d个小时。您要休息点"},
    {3545, "因为剩下的人员不到6个人，就无法进行家族对抗赛。5秒后将结束。"},
    {3546, "因为申请者离开了游戏，就无法进行家族对抗赛。5秒后将结束"},
    {3547, "现在通过%s频道的家族对抗赛NPC可入场。（按顺序30名可入场）"},
    {3548, "您是在下次可以参加的家族。请您去%s频道等待"},
    {3549, "现在一个家族在进行对抗赛。该家族的顺序等%d次后就回来"},
    {3550, "家族对抗赛"},
    {3557, "家族对抗赛待号:%d"},
    {3558, "已经报名"},
    {3559, "猜猜看活动报名是\r\n先猜10个道具后\r\n抽最接近33,333元者后\r\n赠送丰富的奖品。\r\n猜的道具数量不足。\r\n请再确认。"},
    {3560, "抽最接近33,333元者后\r\n将得到丰富的奖品。猜猜看活动\r\n上要报名吗？\r\n总价格 : %d 点卷"},
    {3561, "认证号码传送到您的手机SMS上。"},
    {3562, "恭喜您加入会员商场。\r\n透过""传送到你的手机的SMS""\r\n可以下载会员商场。"},
    {3563, "服务器连接失败。\r\n请稍后再试。"},
    {3564, "电话号码输入错误。\r\n请确认后再试。"},
    {3565, "手机使用者资料和\r\n输入资料不一致。\r\n请确认后再试。"},
    {3566, "认证过程当中发生错误。\r\n请稍后再试。"},
    {3567, "您已经加入了会员商场。"},
    {3568, "认证号码输入错误。\r\n请确认后再试。"},
    {3569, "超过了邀请处理时间。\r\n请你稍等后再试。"},
    {3570, "发生了未知错误。"},
    {3571, "捡取道具"},
    {3572, "HP回复"},
    {3573, "MP回复"},
    {3574, "扩大移动范围"},
    {3575, "自动捡取"},
    {3576, "捡取没有所有权的道具和金币"},
    {3577, "宠物召唤"},
    {3578, "不捡取特定道具技能"},
    {3579, " 请你用左右方向键选择愿意使用卷轴的宠物"},
    {3580, "你要离开聊天窗吗？"},
    {3597, "还无法使用技能"},
    {3598, "装备单手武器的状态上才可以使用技能。"},
    {3599, "在宠物%s%s装备了捡取道具技能。"},
    {3600, "在宠物%s%s装备了扩大移动范围技能。"},
    {3601, "在宠物%s%s装备了自动捡取技能。"},
    {3602, "在宠物%s%s装备了捡取没有所有权的道具和金币技能。"},
    {3603, "在宠物%s%s装备了HP恢复技能。"},
    {3604, "在宠物%s%s装备了MP恢复技能。"},
    {3605, "在宠物%s%s装备了不捡取特定道具的技能"},
    {3606, "祝贺你.宠物召唤技能强化成功了."},
    {3607, "恭喜恭喜.宠物自言自语技能强化成功了."},
    {3608, "追加"},
    {3609, "删除"},
    {3619, "第4次转职"},
    {3620, "任务告知(%d/5)"},
    {3629, "[%s]\r\n你要搜索？\r\n按确定键后有搜索结果\r\n%s\r\n商店搜索器就消失。"},
    {3630, "显示从最低价格开始200个道具"},
    {3631, "显示从最高价格开始200个道具"},
    {3632, "输入的"},
    {3633, "的搜索结果"},
    {3634, "一共"},
    {3635, "个被搜索了"},
    {3636, "频道不同或商店关闭的话无法使用快捷键功能"},
    {3637, "找不到你输入的道具"},
    {3638, "商店关闭"},
    {3639, "无法移动"},
    {3640, "商店开设在房间%d，频道%s"},
    {3641, "在自由市场内才可以使用"},
    {3642, "无法在自由市场内使用。"},
    {3650, "单位数量"},
    {3651, "单位价格"},
    {3652, "销售总数量"},
    {3675, "#e#b剪刀石头布注意事项#n#k\r\n参加剪刀石头布游戏费用是 #r1000金币#k。\r\n游戏内获得胜利时，可以取得名获胜的连胜证明，但是失败连胜挑战时无法取得连胜证明。\r\n取得的连胜证明可以交换成 NPC 珀尔，江，马提呢，东尼。"},
    {3676, "按‘开始‘按键时游戏会开始。"},
    {3677, "%d秒内选择剪刀石头布其中一样。"},
    {3678, "%d要连胜挑战时，请选择'继续'。失败挑战时无法获得商品。"},
    {3679, "恭喜您成功挑战10连胜。"},
    {3680, "限制时间已超过，游戏判定为输。想要再挑战时，请选择'再次挑战'。再挑战需要1000金币的参加费用。"},
    {3681, "获得安慰奖500金币。想要再挑战时请按'再次挑战'按键。再挑战需要1000金币的参加费用。"},
    {3682, "想要再挑战时请按'再次挑战'按键。再挑战需要1000金币的参加费用。"},
    {3683, "道具其他栏位"},
    {3684, "缺少游戏参加费用(1000金币)。"},
    {3685, "一天只能参加 %d回合的游戏。"},
    {3686, "#k暑假活动\r#b下午 1点~7点\r#r经验值 %d倍 暴率 %d倍"},
    {3693, "%4d年%2d月%2d日开始"},
    {3694, "%4d年%2d年%2d日结束"},
    {3695, "[一]"},
    {3696, "[二]"},
    {3697, "[三]"},
    {3698, "[四]"},
    {3699, "[五]"},
    {3700, "[六]"},
    {3701, "[日]"},
    {3702, "只限在星期"},
    {3703, "上午 %2d 点开始"},
    {3704, "下午 %2d 点开始"},
    {3705, "上午 %2d 点为止"},
    {3706, "下午 %2d 点为止"},
    {3708, "目前只剩下 %d 个."},
    {3709, "在这期间限定销售"},
    {3710, "快抓紧机会噢"},
    {3711, "[ 限定销售介绍 ]"},
    {3721, "请输入内容"},
    {3722, "确定要删除吗？"},
    {3723, "请删除目前的公告后重新再试！"},
    {3756, "亿"},
    {3757, "万"},
    {3759, " %d十"},
    {3760, " %d百"},
    {3761, " %d千"},
    {3762, " %d千万"},
    {3765, "组队加经验(+%d)"},
    {3766, "活动组队奖励经验值(+%d) x%d"},
    {3767, "%d以上打猎后的特别经验值(+%d)"},
    {3768, "每次打3个怪物时给你特别经验值%d"},
    {3775, "保护月妙失败！将移动到流放地。"},
    {3776, "护卫泰勒斯失败"},
    {3777, "野猪死掉了"},
    {3778, "因受到诱惑，而使行动受限。"},
    {3779, "变身成不死族，恢复时受到伤害，恢复药剂效果减半。"},
    {3785, "经过无数次的挑战，终于击破了暗黑龙王的远征队！你们才是龙之林的真正英雄~"},
    {3786, "借永不疲倦的热情打败品克缤的远征队啊！你们是真正的时间的胜者！"},
    {3789, "月妙只做了第%d个年糕。"},
    {3790, "月妙受了伤…请保护他避免受到攻击,让他能够专心制作年糕！"},
    {3791, "泰勒斯很危险，请严加护卫。"},
    {3792, "野猪很危险，请细心照顾。"},
    {3793, "宠物不能吃这种饲料.\r\n请确认一下。"},
    {3797, " 要使用%s吗？"},
    {3798, "%s已使用。"},
    {3799, "已经配戴了同样颜色的隐形眼镜了\r\n不能再配戴了。"},
    {3800, "没有宠物，不能使用。"},
    {3806, "驯服的怪物"},
    {3807, "鞍子"},
    {3810, "怪物的装备。"},
    {3811, "宠物的装备."},
    {3812, "宠物的戒指"},
    {3813, "给驯服的怪物配戴上鞍子才能使用该技能。"},
    {3814, "搭乘中无法使用。"},
    {3815, "骑在驯服的怪物上不能使用。"},
    {3817, "无法骑上驯服的怪物。"},
    {3818, "需要同时装备驯养的现金怪物和现金鞍子，才可以使用。"},
    {3825, "装备道具空格不足。"},
    {3826, "其他道具栏不够。"},
    {3827, "成功的驯服了怪物。"},
    {3828, "周围没有可驯服的怪物。"},
    {3845, "能力册"},
    {3846, "技能册"},
    {3847, "不能使用%s。"},
    {3848, "使用了%s，但是没有效果。"},
    {3849, "能力册发出了光芒，能力升级了。"},
    {3850, "技能册发出了光芒，产生了新的技能。"},
    {3852, "[能力 级别：%d]"},
    {3853, "%s的持续时间已经结束并且消失。"},
    {3854, "%s消失。"},
    {3855, "未知"},
    {3870, "在当前地图中无法接收。\r\n请到送货员杜宜那里领取。"},
    {3871, "无法送出该道具"},
    {3872, "请到送货NPC那里确认后领取东东。"},
    {3873, "快递发送/发送的包裹"},
    {3874, "到达"},
    {3875, "包裹到达！"},
    {3876, "特快包裹到达！"},
    {3878, "[%s]发送的\r\n包裹到期了.\r\n "},
    {3879, "\r\n金币 : %d"},
    {3880, "\r\n道具 : %s"},
    {3881, "发送中"},
    {3882, "%d日"},
    {3883, "快到期了"},
    {3884, "%d Lv以上"},
    {3885, "没有等级限制"},
    {3886, "没有内容．"},
    {3887, "选择要接收的快递．"},
    {3888, "目前还在发送当中.\r\n请稍后再试．"},
    {3889, "发送的包裹中包含的\r\n道具或者金币会永久删除．\r\n要删除吗？"},
    {3890, "点击这里输入要发送的内容．"},
    {3891, "请输入．(汉字50字)"},
    {3892, "想取消道具登陆吗？"},
    {3893, "没有特快使用券．"},
    {3894, "必须要选择金币或者道具．"},
    {3895, "请你输入接收人姓名．"},
    {3896, "手续费需要交\r\n%d金币.\r\n另外会消耗 [特快使用券] 1个.\r\n确定要发送吗？"},
    {3897, "运送及手续费需交\r\n%d 金币.\r\n确定要发送吗？"},
    {3898, "想发送几个？"},
    {3899, "成功删除．"},
    {3900, "成功接收．"},
    {3901, "成功发送．"},
    {3902, "新的快递已发送．"},
    {3903, "这是错误的邀请．"},
    {3904, "请重新确认接收人的姓名．"},
    {3905, "不能发送给同一账号的角色．"},
    {3906, "接收人的快递栏已满．"},
    {3907, "该角色无法接收快递．"},
    {3908, "只能拥有一个的道具已经在该\r\n角色的快递栏里．"},
    {3909, "出现不明错误．"},
    {3910, "请查看是否有空间．"},
    {3911, "因为有只能拥有一个的道具\r\n无法找到金币和道具．"},
    {3916, "问题："},
    {3917, "提示："},
    {3918, "[输入答案]"},
    {3919, "请在限定的时间内，输入答案。"},
    {3920, "输入的汉字最少要%d字。"},
    {3921, "输入的汉字不能超出%d字。"},
    {3922, "回答问题期间不能做其他事情哦！"},
    {3923, "超过时间！！！"},
    {3957, "无法发送GM专用的公告。"},
    {3958, "你得等1个小时以上。"},
    {3959, "名字输入错误。"},
    {3960, "请输入收信者的姓名。"},
    {3961, "这个公告通过\r\n冒险岛TV%d秒钟以后发送。\r\n 你要继续吗？"},
    {3962, "收信者不在线。"},
    {3963, "你想使用祝福卷轴吗？"},
    {3964, "消耗了祝福卷轴。"},
    {3965, "卷轴发出一道光亮，道具慢慢升起。\r\n但是道具没有任何变化。\r\n消耗了祝福卷轴，因此武器的可升级次数没有减少。"},
    {3966, "卷轴发出一道亮光, 在道具上添加了某种神秘的能力。\r\n消耗了祝福卷轴。"},
    {3972, "等候时间超过15秒。\r\n请稍后再试．"},
    {3977, "15级及以下的玩家每天只能交易100万金币。\r\n您今天已达到限制，\r\n请明天再试。"},
    {3988, "找不到任何玩家。"},
    {3989, "您已经连接到服务器。"},
    {3990, "未知错误：查看所有角色。"},
    {3991, "你想把这个东西送给别人吗？\r\n[%s]"},
    {4031, "您可将角色名称设定为"},
    {4032, "，"},
    {4033, "只限"},
    {4034, "1次"},
    {4045, "你想把组队长让给\r\n%s吗?"},
    {4046, "%s现在是组队长。"},
    {4047, "组队长结束了组队, %s成了新的组队长。"},
    {4048, "能转让给同一个场地的组队成员。"},
    {4049, "能转让给同一个频道的组队成员。"},
    {4050, "没有与组队长同一地图的组队成员。"},
    {4065, "任务开始，双击角色头顶的图标。"},
    {4075, "剩余时间 %d小时%d分%d秒"},
    {4076, "已过限定时间,[%s]任务失败，已从目录中删除。"},
    {4077, "在这里无法使用该道具。"},
    {4078, "冒险岛红队"},
    {4079, "冒险岛蓝队"},
    {4080, "[%s]目前是无法战斗的状态,所以[%s]队损失了CP %d ."},
    {4081, "[%s]目前是无法战斗的状态,但是没有剩余的CP所以[%s]队的CP不减少."},
    {4082, "CP不足所以无法执行"},
    {4083, "已经召唤过的怪物"},
    {4084, "你不能再使用召唤物了。"},
    {4085, "这个生物已经被召唤了。"},
    {4086, "因为无法得知的原因,请求失败."},
    {4087, "怪物嘉年华胜利了.马上会移动到其他地方请耐心等待."},
    {4088, "怪物嘉年华失败了.马上会移动到其他地方请耐心等待."},
    {4089, "延长战结束还是未分胜败. 马上会移动到其他地方请耐心等待."},
    {4090, "对方队伍过早退场所以终止怪物嘉年华. 马上会移动到其他地方请耐心等待."},
    {4091, "[%s]召唤了召唤兽. [%s]"},
    {4092, "[%s]使用了技能. [%s]"},
    {4093, "[%s]召唤了召唤物. [%s]"},
    {4094, "开始怪物嘉年华!!"},
    {4095, "胜负未定，延长比赛2分钟."},
    {4096, "[%s]队的[%s]中断了怪物嘉年华."},
    {4097, "[%s]队的组队长中断了怪物嘉年华,所以[%s]成了新的组队长."},
    {4110, "组队成员: %s"},
    {4114, "[祝贺]%s终于达到了200级. 大家一起祝贺下吧。"},
    {4125, "请重新输入。"},
    {4128, "我获得并可以使用的CP."},
    {4129, "剩余 CP : %d / 总获得 CP : %d"},
    {4130, "以我们队的CP状态,比较总获得CP判断胜负."},
    {4131, "组队长可以使用组队成员的CP"},
    {4132, "以对方队的CP状态,比较总获得CP判断胜负."},
    {4133, "允许好友对话向%s公开。"},
    {4134, "不允许好友对话向%s公开。"},
    {4135, "%s的好友对话公开。"},
    {4136, "%s的好友对话不公开。"},
    {4137, "我与好友说的对话内容 ""%s""无法看到。"},
    {4138, "我与好友说的对话内容 ""%s""可以看到。"},
    {4139, "无法看到""%s""与好友说的对话内容。"},
    {4140, "可以看到""%s""与好友说的对话内容。"},
    {4141, "无法看到""%s""与好友说的对话内容。"},
    {4142, "可以看到""%s""与好友说的对话内容。"},
    {4143, "无法和%s进行好友对话。"},
    {4144, "可以和%s进行好友对话。"},
    {4150, "补助武器"},
    {4167, "被从自动提示上排除,所以重新连接前无法自动登陆。[%s]"},
    {4168, "任务开始了,但没有登陆到提示窗上。[%s]"},
    {4171, "冒险岛上有人要给你传话。"},
    {4176, "禁止自动关闭。"},
    {4177, "允许自动关闭。"},
    {4178, "打开自动提示窗。"},
    {4179, "关闭自动提示窗。"},
    {4180, "点击时进行中的任务会自动登陆, 要是10分钟不进行的话会消失。"},
    {4181, "点击时任进行中的任务不自动登陆。"},
    {4182, "%d分钟不召唤扎昆的话，会关闭扎昆的祭台．"},
    {4183, "离扎昆关闭时间还有 %d分钟．"},
    {4184, "扎昆的祭台已关闭．"},
    {4185, "%d分钟不召唤暗黑龙王的话，会关闭暗黑龙王洞穴。"},
    {4186, "暗黑龙王洞穴将在%d分钟后关闭。"},
    {4187, "暗黑龙王洞穴已关闭。"},
    {4188, "不显示"},
    {4189, "只显示名字"},
    {4190, "只显示HP"},
    {4191, "名字和HP都显示"},
    {4192, "成功参与．"},
    {4195, "无法使用卷轴的道具．"},
    {4200, "您与 %s结婚了．"},
    {4201, "您与 %s订婚了．"},
    {4204, "想给这对情侣祝福吗？"},
    {4207, "在等对方的回应．"},
    {4216, "登陆目录"},
    {4223, "一次只能登陆一个．"},
    {4231, "真的要退场吗？"},
    {4233, "%s向您求婚了.\r\n要答应吗？"},
    {4235, "祝贺你订婚．"},
    {4236, "祝贺你结婚．"},
    {4237, "对方郑重地拒绝了您的求婚．"},
    {4238, "订婚失败．"},
    {4239, "离婚失败．"},
    {4240, "该名为错误的角色名．"},
    {4241, "对方不在同一地图．"},
    {4242, "对方的道具栏已满．"},
    {4243, "同性不能结婚．"},
    {4244, "您已经是订婚的状态．"},
    {4245, "您已经是结婚的状态．"},
    {4246, "对方已经是订婚的状态．"},
    {4247, "对方已经是结婚的状态．"},
    {4248, "您处于不能求婚的状态．"},
    {4249, "对方处于无法接受求婚的状态．"},
    {4250, "很遗憾对方取消了您的求婚请求．"},
    {4251, "预约后无法取消结婚典礼．"},
    {4252, "已成功取消预约．请以后再试．"},
    {4253, "此请帖无效．"},
    {4254, "结婚典礼预约已成功接收．"},
    {4255, "道具栏已满．\r\n请整理其他窗口．"},
    {4256, "请输入求婚对象的角色名．"},
    {4257, "请输入想招待的人的名字．"},
    {4258, "无法给结婚当事者\r\n请帖．"},
    {4259, "丢弃该道具，订婚就会被取消.\r\n还想丢弃该道具？"},
    {4260, "无法接收结婚戒指.\r\n没关系吗？"},
    {4261, "无法接收请帖．\r\n请留其他窗口．"},
    {4262, "祝贺您! 您成功邀请到结婚典礼！请确认其他窗口．"},
    {4263, "%s和 %s的婚宴将在%d频道宴客堂举行．"},
    {4264, "%s和 %s的婚宴将在 %d频道大教堂举行．"},
    {4265, "%s和 %s的婚宴将在20岁以上%d频道大教堂进行．"},
    {4266, "结婚戒指只能佩戴一个．"},
    {4267, "[配偶]"},
    {4268, "超过限定数字．"},
    {4269, "在这里使用不大适合．"},
    {4270, "想送该道具吗？"},
    {4271, "已经送出了道具．"},
    {4272, "发送道具失败．"},
    {4280, "目前还没有发芽．"},
    {4281, "1阶段: 很小的圣诞花盆叶芽．"},
    {4282, "2阶段: 圣诞花盆叶芽稍微长大了．"},
    {4283, "3阶段: 圣诞花盆叶芽又稍微长大了．"},
    {4284, "4阶段: 圣诞花盆叶芽长了很多．"},
    {4285, "5阶段: 圣诞花盆叶芽长大了很多很多．"},
    {4286, "6阶段: 好像马上就要结果实了．"},
    {4287, "7阶段: 终于长出礼品盒子了．"},
    {4296, "未开始"},
    {4297, "进行中"},
    {4298, "已结束"},
    {4299, "可完成"},
    {4301, "跳跃中无法开设商店。"},
    {4302, "推荐职业："},
    {4303, "以强健的身体和意志力直面困难并突破的职业。"},
    {4304, "以强健的身体和力量直面困难并突破的职业。"},
    {4305, "受力量和敏捷的影响。"},
    {4306, "窜访等 脚眉客 塞栏肺 葛电 矫访阑 沥搁栏肺 嘎辑"},
    {4307, "受力量和敏捷的影响。"},
    {4308, "激发潜在的魔力，探寻真理的职业。"},
    {4309, "可以进攻也善于防守"},
    {4310, "受运气和智力的影响。"},
    {4311, "以高集中力为基础，熟练使用弓和弩的职业。"},
    {4312, "以高集中力为基础，熟练使用远程武器的职业。"},
    {4313, "受敏捷和力量的影响。"},
    {4314, "槛访等 笼吝仿阑 官帕栏肺 盔芭府 公扁甫 磊蜡磊犁肺"},
    {4315, "促风绰 流诀. DEX客 STR狼 康氢阑 罐绰促."},
    {4316, "这个职业拥有最高的速度和隐形战术"},
    {4317, "以快捷的身手，纵横天下，喜欢冒险的职业。"},
    {4318, "受运气和敏捷的影响。"},
    {4319, "没有"},
    {4320, "现在的状态，没有合适的职业推荐。"},
    {4321, "如果想随机生成一个，请转动骰子。"},
    {4323, "转动"},
    {4324, "把鼠标移到"},
    {4325, "技能图表上"},
    {4326, "就可以看到"},
    {4327, "仔细的说明。"},
    {4328, "为了提高"},
    {4329, "技能级别，"},
    {4330, "请点击"},
    {4331, "按钮。"},
    {4332, "拥有多少"},
    {4333, "技能点数，"},
    {4334, "就可以升级"},
    {4335, "多少技能。"},
    {4336, "注册常用技能，只需一个按钮即可快速访问。"},
    {4341, "可以打开或折叠属于我自己的技能窗。"},
    {4342, "可以把想要组合的\r\n技能道具拖到左侧的技能清单登记。"},
    {4343, "储存技能名称。"},
    {4348, "无法取消变身。"},
    {4349, "目标怪兽体力太强，无法捕获。"},
    {4350, "速成石不能连续使用。"},
    {4351, "幽灵离得太远，无法抓住。"},
    {4352, "怪兽离得太远，无法抓住。"},
    {4362, "无法使用的道具。"},
    {4363, "总金额超过了一次销售\r\n限度。"},
    {4369, "因没有其他角色参与，无法进行下去。竞技赛终止！"},
    {4374, "佩戴的武器无法使用此技能。"},
    {4375, "在这里无法使用。"},
    {4376, "根据伴侣的要求，离婚成立，失去效力的结婚戒指消失。"},
    {4377, "%4d 个"},
    {4378, "如果想孵化矮人蛋，需要在商店购买\r\n孵化器。"},
    {4418, "没有可以对话的对象。"},
    {4421, "可以用Tab对菜单进行操作。"},
    {4431, "部分卷轴可导致\r\n封印的道具损坏。\r\n您想要使用卷轴吗？"},
    {4432, "它不能再在这个装备上继续使用了"},
    {4433, "显示队伍血量"},
    {4434, "您没有把道具放在上面。\r\n您还是想要使用吗？"},
    {4442, "文字不能超过20行。"},
    {4443, "只能发送300字(韩文150字)以内的文字。"},
    {4444, "标题最多可以写34字(汉字17字)。"},
    {4446, "回帖最多可以写68字(汉字34字)。"},
    {4447, "%s家族拒绝了联盟邀请。"},
    {4448, "%s家族拒绝了联盟邀请。"},
    {4449, "已邀请%s家族加入联盟。"},
    {4450, "还未加入任何联盟。"},
    {4451, "联盟公告"},
    {4453, "请输入邀请加入联盟的家族名称\r\n。"},
    {4454, "真的要退出吗？"},
    {4455, "您要将[%s]家族\r\n逐出联盟吗？"},
    {4456, "请输入联盟公告内容。"},
    {4457, "已超过每个帐号可以使用该优惠券的\r\n限制次数。\r\n详细内容请参考优惠券说明。"},
    {4458, "你好！"},
    {4459, "该职位有相关联盟成员担任，\r\n无法删除。"},
    {4460, "连接世界的门已关闭。目前无法使用。"},
    {4466, "缤纷喇叭预览。"},
    {4467, "还有行没有输入内容。\r\n您还要发送么？"},
    {4493, "获得"},
    {4494, "未获得"},
    {4496, "组队任务"},
    {4497, "连续任务"},
    {4501, "扩展道具栏"},
    {4502, "关闭道具栏"},
    {4504, "可以用现金道具扩充道具栏。"},
    {4526, "您真的要删除吗？"},
    {4527, "网络繁忙，连接延迟。\r\n请稍后重新尝试。"},
    {4528, "转职时，可以增加%d个空格。\r\n即使会有损失，您还是要现在扩充吗？"},
    {4531, "共搜索到%d张地图。"},
    {4532, "没有搜索的地图。"},
    {4533, "[%d]移动到%s。"},
    {4535, "%s世界的%s中奖获得了%s。大家祝贺他！"},
    {4536, "金币不足或者%s"},
    {4537, "无法向本人的账号赠送礼物。\r\n请用该角色登录，然后购买。"},
    {4538, "请确认角色名是否错误。"},
    {4539, "此道具对性别有限制。\r\n请确认接收人的性别。"},
    {4540, "接收礼物的人的保管箱已满，\r\n无法发送礼物。"},
    {4541, "包含超过有效期限后会消失的道具。\r\n您确定要购买吗？"},
    {4542, "的勋章"},
    {4543, "由于丧失称号，%s已移送给新的主人。"},
    {4558, "这里无法丢弃物品。"},
    {4559, "无法分解的物品。"},
    {4563, "用在%s上，可以将有效时间延长%s。你确定要使用吗？"},
    {4564, "有效时间无法增加到%d天以上。"},
    {4565, "%d天"},
    {4566, "%d小时"},
    {4567, "%d分钟"},
    {4568, "无法获得物品效果。"},
    {4569, "由于装备了%s，打猎时额外获得%d%%的经验值奖励。"},
    {4570, "装备%s后经过了%d小时，打猎时额外获得%d%%的经验值奖励。"},
    {4585, "请输入登录为同学的角色名。必须在同一频道的同一地图中，才可以登录。"},
    {4586, "请输入诀别的角色名。"},
    {4587, "请输入冒险学院的口号"},
    {4588, "(没有冒险学院成员)"},
    {4589, "(没有老师)"},
    {4590, "(没有远征)"},
    {4591, "(还没有冒险学院)"},
    {4592, "[请登录同学]"},
    {4593, "%s冒险学院"},
    {4594, "上级(%d名)"},
    {4595, "下级(%d名)"},
    {4596, "角色不在线，或角色名不正确。"},
    {4597, "无法登录为同学的角色。"},
    {4598, "是同一冒险学院。"},
    {4599, "非同一冒险学院。"},
    {4600, "输入的角色不存在。"},
    {4601, "只有在同一地图中的角色才能\r\n登录为同学。"},
    {4602, "已经是其他角色的同学。"},
    {4603, "只能将比自己等级低的角色\r\n登录为同学。"},
    {4604, "等级差异超过20，无法\r\n登录为同学。"},
    {4605, "只有10级以上的角色才能添加为\r\n同学。"},
    {4606, "正在邀请其他角色登录为同学。\r\n请稍候重新尝试。"},
    {4607, "其他角色已请求召唤。\r\n请稍候重新尝试。"},
    {4608, "只有关系图中可以查看到的下级冒险学院成员有6人以上在线时才可以使用。"},
    {4609, "召唤失败。该地区无法召唤，或处于无法召唤的状态。"},
    {4610, "无法登录为同学。冒险学院的规模上下不能超过1000级。"},
    {4611, "同学已满，无法将%登录为同学。"},
    {4612, "%s你的同学已满，无法登录更多的同学。"},
    {4613, "%s\r\n希望成为你的老师。\r\n你想成为%s的同学吗？"},
    {4614, "%s拒绝登录为同学。"},
    {4615, "已将%s登录为同学。希望你尽可能地帮助同学进行游戏。"},
    {4616, "%s成为了你的老师。在游戏过程中你可以获得各种的帮助。"},
    {4617, "已和%s诀别。\r\n冒险学院关系已结束。"},
    {4618, "已和%s诀别。\r\n冒险学院关系已结束。"},
    {4619, "名声度提高了(%s, +%d)"},
    {4620, "消费了%d的名声度。"},
    {4621, "%s使用了“%s”特权。"},
    {4622, "本人的名声度将会减少，\r\n下属所有学员成员都会受到影响。\r\n是否和所选角色诀别？"},
    {4623, "请输入对象冒险学院成员的名字。"},
    {4624, "%s的%s请求召唤你。你要立即移动过去吗？"},
    {4625, "%s拒绝了召唤请求。"},
    {4626, "超过了%s你的特权使用限度。"},
    {4627, "名声度以[当前名声度/总名声度]的形态显示。\r\n消费当前名声度，可以使用[名人冒险家的特权]。"},
    {4628, "同学按[当前同学人数/最大同学人数]的形态显示。\r\n自己登录的同学获得经验值或升级时，自己的名声度也会随之上升。\r\n在同学升级的过程中提供的帮助越多，自己的名声度上升的就越快。"},
    {4629, "同学登录格还有空格的话，就可以登录同学，以提高自身的名声。"},
    {4630, "可以和老师诀别，自己成为院长，结成新的冒险学院。\r\n你的下级冒险学院成员全都会成为你的冒险学院成员。"},
    {4631, "和同学诀别。\r\n同学和其下级冒险学院成员全部退出冒险学院，组成新的冒险学院。"},
    {4632, "指属于冒险学院的所有成员。\r\n可能会比关系图上显示的人数多。"},
    {4633, "今天使用了%d次\r\n今天还可以使用%d次"},
    {4634, "今天无法继续使用此项特权。"},
    {4635, "#c※超越老师的等级后，名声度的累积值会减半#\r\n#c。请帮助老师升级！#\r\n"},
    {4636, "#c※等级被同学超越后，名声度的累积值会#\r\n#c减半。请努力升级！#\r\n "},
    {4637, "今天老师累积的名声度：%s\r\n "},
    {4638, "正在使用的特权：\r\n "},
    {4639, "今天使用的特权：\r\n "},
    {4640, " %s - %d次\r\n "},
    {4641, "今天累积的名声度：%s\r\n "},
    {4642, "在线时间：%d小时%d分钟\r\n "},
    {4643, "当前位置：%s\r\n "},
    {4644, "当前名声度：%s\r\n总名声度：%s\r\n "},
    {4646, "你还有其他想要的东西吗？我可以做一个参考。"},
    {4647, "现在无法交易。我也要休息一下，不是吗？"},
    {4648, "这种商品好像目前还没有。我再把目录给你看一下，你再挑选一个。"},
    {4649, "你是在求我吗？"},
    {4650, "你干嘛带这么多的现金啊？先把口袋清空之后再来找我吧。"},
    {4651, "怎么能这么不上心呢？这个东西已经停止交易了。"},
    {4652, "这个东西今天已经停止交易。你明天再来吧。"},
    {4653, "你怎么对这件东西这么执着啊？我说过不能交易了。"},
    {4654, "你还真贪心。请先减少一些数量。"},
    {4655, "现在我很忙，以后再来找我吧。"},
    {4656, "没有相应的物品。"},
    {4658, "丢弃物品后获得的话，会变成无法交易状态。\r\n你确认要丢弃吗？"},
    {4659, "购买后该物品会变成无法交易状态\r\n。你确认要购买吗？"},
    {4660, "物品保存到仓库后，会变成无法交换状态。\r\n取出物品的角色无法进行\r\n交换。\r\n你确认好保存吗？"},
    {4661, "取出物品后，会变成无法交换状态\r\n。你确认要取出吗？"},
    {4662, "接受交换的物品中一部分之后，\r\n会变成无法交换状态。\r\n你确认要交换吗？"},
    {4663, "该物品无法使用。"},
    {4664, "使用宿命剪刀，可以使物品交易1次。"},
    {4665, "#c使用宿命剪刀，可以使物品交易1次。"},
    {4666, "当前持有的物品中，没有\r\n可以使用宿命剪刀的物品。\r\n你确定要购买吗？"},
    {4667, "\r\n-#c可以使用的我的物品#-"},
    {4668, "你成功创建了骑士团角色.\r\n你现在可以选择骑士团角色了\r\n你可以重新登录了"},
    {4669, "未知错误无法创建骑士团."},
    {4670, "当前名称已存在."},
    {4671, "你的背包已经满了,请前往商城进行扩充背包."},
    {4672, "当前名称无法使用."},
    {4673, "角色必须在转职后或\r\n10级以上才可以查看排名信息。"},
    {4677, "<%s> %s已达到%d级。"},
    {4678, "- 从%s处获得的名声度减半。"},
    {4686, "全部"},
    {4730, "设置"},
    {4740, "目前无法使用拍卖系统，请稍候！"},
    {4741, "目前无物品登记！"},
    {4742, "道具数量不足！\r\n请重新确认。"},
    {4743, "本人登记的物品无法进行交易。"},
    {4744, "目前无法进入，请玩家稍后再试."},
    {4746, "准备开始进行冒险之旅了？开始下载客户端吧！"},
    {4747, "目前无法使用该功能。"},
    {4748, "将扣除手续费5%的金额，告知卖方"},
    {4749, "请问要以直接购买价%d抵用券来进行购买吗？"},
    {4750, "已出售"},
    {4751, "已购买"},
    {4752, "流拍"},
    {4753, "取消"},
    {4754, "展示"},
    {4755, "竞价"},
    {4756, "目前下标价：%d"},
    {4757, "剩余时间：%d小时%d分"},
    {4758, "出价增额：%d"},
    {4759, "直接购买价：%d"},
    {4760, "无法连续竞标"},
    {4761, "%s的%s以\r\n%d抵用券售出。"},
    {4762, "%s的%s以\r\n%d抵用券拍得。"},
    {4763, "个别道具价格：%d"},
    {4764, "%d小时%d分"},
    {4765, "直接购买价低于原始价格！"},
    {4766, "出价次数：%d"},
    {4767, "数量：%d"},
    {4768, "已有1件以上的出价纪录！\r\n因此无法取消贩卖登记！"},
    {4769, "可在24~168小时的期限内，\r\n以1小时为单位进行设定。"},
    {4770, "系统准备中！"},
    {4771, "贩卖"},
    {4772, "购买"},
    {4773, "竞标"},
    {4774, "全部"},
    {4775, "角色名称"},
    {4776, "请输入数字"},
    {4777, "您确定要取消贩卖吗？"},
    {4778, "超出数量上限！需在%d个以内"},
    {4779, "贩卖登记成功！"},
    {4780, "您确定要贩卖吗？"},
    {4781, "贩卖申请成功！"},
    {4782, "贩卖申请失败！"},
    {4783, "抵用券不足！"},
    {4784, "抵用券使用中发生错误！"},
    {4785, "列表确认失败！"},
    {4786, "贩卖取消成功！"},
    {4787, "道具已移转。"},
    {4788, "放入购物车成功！"},
    {4789, "无法重复放入购物车！"},
    {4790, "已从购物车列表中删除。"},
    {4791, "购物车列表删除失败！"},
    {4792, "无登记内容…"},
    {4793, "购买列表确认失败！"},
    {4794, "购买申请成功！"},
    {4795, "购买申请失败！"},
    {4796, "购买申请已取消。"},
    {4797, "购买申请取消失败！"},
    {4798, "已登记到[购买]。"},
    {4799, "登记到[购买]失败！"},
    {4800, "(7天)"},
    {4801, "请至少要输入一个字以上。"},
    {4802, "无搜寻内容！"},
    {4803, "您确定要购买吗？"},
    {4804, "销价: %d点"},
    {4805, "交易价格: %d"},
    {4806, "此交易已被取消或者道具已被卖出! \r\n请重新确认!"},
    {4807, "交易结束的道具共有%d个. \r\n根据登记须序会显示%d个, \r\n当您领取道具后,其他道具会依序显示出来."},
    {4808, "因为发生未知错误。\r\n不能进入到MapleStory拍卖系统。"},
    {4809, "目前拍卖系统拥塞中,请稍后再试!"},
    {4810, "该道具无法登记!"},
    {4811, "您不符合访问交易商店的最低级别要求。"},
    {4812, "您是涉嫌复制道具的用户，无法进行登记。"},
    {4813, "您不能在此地图上使用密涅瓦的猫头鹰。"},
    {4814, "该道具已售出.\r\n您不能取消已购买产品的销售."},
    {4815, "想要的道具\r\n已过期.\r\n%d 道具和 %d 点券\r\n已经退还你的拍卖商城."},
    {4816, "飞镖不可交易."},
    {4837, "你必须达到10级才能\r\n在拍卖进行道具销售."},
    {4838, "您的屏幕分辨率不支持窗口模式。 请选择更高的屏幕分辨率以使用窗口模式."},
    {4839, "你已经被 #b管理员 %s 因 %s 原因封禁.#k"},
    {4842, "这是你的搜索结果."},
    {4846, "已经有一个特效道具在使用中. \r\n请稍后再试."},
    {4859, "雪人吸收了雪的活力!"},
    {4860, "您需要使用所有剩余的EXP才能使用新的所罗门令状."},
    {4861, "您是否要收取\r\n《所罗门令》中学习的经验值？"},
    {4862, "你确定要发送一份\r\n空的新年邮票 ?"},
    {4863, "您无法向自己发送卡 !"},
    {4864, "您没有可用的空间来存储卡片.\r\n请稍后再试."},
    {4865, "你没有可发送的卡片."},
    {4866, "错误背包信息 !"},
    {4867, "无法找到该角色 !"},
    {4868, "数据不一致!"},
    {4869, "DB操作期间发生错误."},
    {4870, "未知错误 !"},
    {4871, "成功发送一封新年卡片\r\n 给 %s."},
    {4872, "成功收到一封新年卡片."},
    {4873, "成功删除一封新年卡片."},
    {4875, "你想扔掉\r\n这个卡片?"},
    {4876, "您已将%s转换为其他形式."},
    {4877, "%s 使您看起来有些奇怪."},
    {4878, "找不到用户 %s."},
    {4879, " 您只能在城镇中使用随机变换药水."},
    {4907, "Error occurs when game client is executed in a general user account' s authority, not in the Windows NT system's administrator account. In order to use the functions provided by HackShield with a general user accoun'ts authority, a HackShield Shadow account must first be created. Refer to ‘User Authority Execution Function'."},
    {4988, "%s : 获得了一个 "},
    {4989, "%s %s  :  %s   %s  祝贺他(她)!"},
    {4990, "%s %s  :  %s  -- 商城惊喜!  祝贺他(她)!"},
    {5013, "%s将会在%04d-%02d-%02d %02d:%02d前被锁定.\r\n要使用封印之锁吗?"},
    {5015, "%s的封印过期了."},
    {5052, "该道具不能提高强化次数。"},
    {5053, "2次强化次数\r\n已提高完毕。"},
    {5054, "强化次数提高1次. 还剩\r\n%d 次强化提高次数。"},
    {5055, "金锤子不能用于\r黑龙项环。"},
    {5056, "金锤子已提高的强化次数: %d"},
    {5057, "金锤子已提高的强化次数: 2(MAX)"},
    {5217, "[GM]您已收到管理员的来信.请点击右上角的信封."},
    {5220, "仅在登机时才可用 %s."},
    {5223, "该道具只能装备一个。"},
    {5237, "Mu Young: That was a close call!! I can't believe you tried to fight Balrog when you are so weakˇ Draw Balrog's attention and continue hitting him for 10 minutes while I seal him up."},
    {5238, "Mu Young: That was a close call!! I can't believe you tried to fight Balrog when you are so weakˇ Draw Balrog's attention and continue hitting him for 5 minutes while I seal him up."},
    {5241, "消耗1个原地复活术，在当前地图复活了。(剩余%d个)"},
    {5254, "战童"},
    {5260, "'%s'%s转职为%s。"},
    {5261, "'%s'与'%s'结婚了。大家祝福下他（她）吧！"},
    {5262, "已完成"},
    {5263, "未完成"},
    {5273, "封印至 %04d年%d月%d日 %02d:%02d"},
    {5280, "扔掉多少？"},
    {5281, "点击箭头，可以提升相应的属性。"},
    {5282, "对战士而言，最重要的属性是力量，此外"},
    {5283, "对魔法师而言，最重要的属性是智力，此外"},
    {5284, "需要一定的运气。"},
    {5285, "对弓箭手而言，最重要的属性是敏捷，此外"},
    {5286, "还需要一些力量。"},
    {5287, "对飞侠而言，最重要的属性是运气，此外"},
    {5288, "对使用拳甲的海盗而言，最重要的属性是力量，"},
    {5289, "此外，还需要一些敏捷。"},
    {5290, "对使用短枪的海盗而言，最重要的属性是敏捷，"},
    {5291, "此外，还需要一些力量。"},
    {5292, "对魂骑士而言，最重要的属性是力量，"},
    {5293, "对炎术士而言，最重要的属性是智力，"},
    {5294, "此外，还需要一些运气。"},
    {5295, "对风灵使者而言，最重要的属性是敏捷，"},
    {5296, "对夜行者而言，最重要的属性是运气，"},
    {5297, "对奇袭者而言，最重要的属性是力量，"},
    {5298, "点击自动分配按钮，可以对能力点"},
    {5299, "进行自动分配。"},
    {5300, "获得过的勋章个数:%d个"},
    {5301, "可以获得的称号"},
    {5302, "挑战中的称号"},
    {5306, "/帮助"},
    {5319, "道具过期了."},
    {5320, "新手、初心者、战童无法使用"},
    {5421, "龙神"},
    {5435, "如果您升级后一直不使用属性点，\r\n您将不再获得属性点！"},
    {5437, "获取%s栏物品 (%s %d个)"},
    {5438, "获取%s栏物品 (%s)"},
    {5445, "%s在购买后可以使用"},
    {5446, "%d天"},
    {5447, "%d小时"},
    {5448, "%d分钟"},
    {5449, "%d级以上可以购买"},
    {5450, "%d级以下可以购买"},
    {5451, "这是一个时间限制道具\r\n"},
    {5452, "确认要购买吗？\r\n你现在有%d个%s"},
    {5453, "（英文应留空）"},
    {5454, "此物品只能装备在龙神身上。"},
    {5510, " 金币"},
    {5526, "战神"},
    {5529, "炎术士"},
    {5530, "装备分类 :"},
    {5531, "性别不符，无法交换。"},
    {5532, "现金"},
    {5535, "魂骑士"},
    {5539, "购买道具失败."},
    {5542, "家族"},
    {5544, "还需要一些敏捷。"},
    {5545, "道具名称"},
    {5548, "魔法师"},
    {5550, "金币"},
    {5552, "夜行者"},
    {5554, "在线组员 / 总组员"},
    {5559, "请输入内容。"},
    {5560, "请输入110抵用券以上的价格。"},
    {5561, "请输入标题。"},
    {5565, "谢谢合作。"},
    {5566, "游戏文件损坏，无法持有物品。请重新安装游戏后，再重新尝试。"},
    {5567, "你的密码错误，请重试。"},
    {5568, "密码至少要 %d位以上。"},
    {5571, "这种道具丢弃后无法再次获得\r\n还想丢弃吗？"},
    {5572, "这件物品无法使用"},
    {5573, "未满7岁的人无法购买\r\n该物品。"},
    {5574, "未满7岁的人无法接收\r\n该物品。"},
    {5575, "奇袭者"},
    {5592, "因为有地气阻挡，无法接近。"},
    {5595, "未知错误 (%d)."},
    {5597, "悄悄话"},
    {5598, "风灵使者"},
    {5599, "您的金币不够．"},
    {5600, "被家族除名了。"},
    {5601, "送到了。"},
    {5602, "道具购买成功！"},
    {5603, "背包已满"},
    {5617, "和 \r"},
    {5639, " 金币"},
};bool Hook_StringPool__GetString_initialized = true;
_StringPool__GetString_t _StringPool__GetString_rewrite = [](void* pThis, void* edx, ZXString<char>* result, unsigned int nIdx, char formal) ->  ZXString<char>*
{
	if (Hook_StringPool__GetString_initialized)
	{
		std::cout << "Hook_StringPool__GetString started" << std::endl;
		Hook_StringPool__GetString_initialized = false;
	}
	auto ret = _sub_79E993(pThis, nullptr, result, nIdx, formal);//_StringPool__GetString_t
	switch (nIdx)
	{
	case 1307:	//1307_UI_LOGINIMG_COMMON_FRAME = 51Bh
		if (MainMain::EzorsiaV2WzIncluded && !MainMain::ownLoginFrame) {
			switch (Client::m_nGameWidth)
			{
			case 1280:	//ty teto for the suggestion to use ZXString<char>::Assign and showing me available resources
				*ret = ("MapleEzorsiaV2wzfiles.img/Common/frame1280"); break;
			case 1366:
				*ret = ("MapleEzorsiaV2wzfiles.img/Common/frame1366"); break;
			case 1600:
				*ret = ("MapleEzorsiaV2wzfiles.img/Common/frame1600"); break;
			case 1920:
				*ret = ("MapleEzorsiaV2wzfiles.img/Common/frame1920"); break;
			case 1024:
				*ret = ("MapleEzorsiaV2wzfiles.img/Common/frame1024"); break;
			}
			break;
		}
	case 1301:	//1301_UI_CASHSHOPIMG_BASE_BACKGRND  = 515h
		if (MainMain::EzorsiaV2WzIncluded && !MainMain::ownCashShopFrame) { *ret = ("MapleEzorsiaV2wzfiles.img/Base/backgrnd"); } break;
	case 1302:	//1302_UI_CASHSHOPIMG_BASE_BACKGRND1 = 516h
		if (MainMain::EzorsiaV2WzIncluded && !MainMain::ownCashShopFrame) { *ret = ("MapleEzorsiaV2wzfiles.img/Base/backgrnd1"); } break;
	case 5361:	//5361_UI_CASHSHOPIMG_BASE_BACKGRND2  = 14F1h			
		if (MainMain::EzorsiaV2WzIncluded && !MainMain::ownCashShopFrame) { *ret = ("MapleEzorsiaV2wzfiles.img/Base/backgrnd2"); } break;
		//case 1302:	//BACKGRND??????
		//	if (EzorsiaV2WzIncluded && ownCashShopFrame) { *ret = ("MapleEzorsiaV2wzfiles.img/Base/backgrnd1"); } break;
		//case 5361:	//SP_1937_UI_UIWINDOWIMG_STAT_BACKGRND2  = 791h	
		//	if (EzorsiaV2WzIncluded && ownCashShopFrame) { *ret = ("MapleEzorsiaV2wzfiles.img/Base/backgrnd2"); } break;
			default:
			if (Client::SwitchChinese) {
				for (const auto& pair : newKeyValuePairs) {
					if (nIdx == pair.key) {
						*ret = pair.value.c_str();
						break;
					}
				}
			}
			break;
		}
	return ret;
};
bool Hook_StringPool__GetString(bool bEnable)	//hook stringpool modification //ty !! popcorn //ty darter
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x0079E993;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_79E993), _StringPool__GetString_rewrite);//_StringPool__GetString_t
}
bool HookMyTestHook(bool bEnable)
{ return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9DE4D2), _CWndCreateWnd_Hook); }

bool HookDetectLogin(bool bEnable)
{ return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_5F726D), _CLoginSendSelectCharPacket_Hook); }
bool Hook_lpfn_NextLevel_initialized = true;//__thiscall NEXTLEVEL::NEXTLEVEL(NEXTLEVEL *this)
const int maxLevel = 201;//determine the max level for characters in your setup here //your max level is the size of your array divded by operator size( currently int size)
static _sub_78C8A6_t _sub_78C8A6_rewrite = [](unsigned int NEXTLEVEL[maxLevel], void* edx) {
	if (Hook_lpfn_NextLevel_initialized)
	{
		std::cout << "Hook exptable@_sub_78C8A6 started" << std::endl;
		Hook_lpfn_NextLevel_initialized = false;
	}
	edx = nullptr;
	MainMain::useV62_ExpTable ? memcpy(NEXTLEVEL, MainMain::v62ArrayForCustomEXP, MainMain::expTableMemSize) : memcpy(NEXTLEVEL, MainMain::v83ArrayForCustomEXP, MainMain::expTableMemSize);
	NEXTLEVEL[maxLevel] = 0;//(pThis->n)[len / sizeof((pThis->n)[0])] = 0; //set the max level to 0
	return NEXTLEVEL;
	//if you want to use your own formula rewrite this function. generated numbers MUST MATCH server numbers
};
//void* __fastcall _lpfn_NextLevel_v62_Hook(int expTable[])	//formula for v62 exp table, kept for reference/example
//{															//if you want to use it remember to change the setting in Hook_lpfn_NextLevel
//	int level = 1;
//	while (level <= 5)
//	{
//		expTable[level] = level * (level * level / 2 + 15);
//		level++;
//	}
//	while (level <= 50)
//	{
//		expTable[level] = level * level / 3 * (level * level / 3 + 19);
//		level++;
//	}
//	while (level < 200)
//	{
//		expTable[level] = long(double(expTable[level - 1]) * 1.0548);
//		level++;
//	}
//	expTable[200] = 0;	//you need a MAX_INT checker for exp if you have levels over 200 and are not using a predefined array
//	return expTable;
//}
bool Hook_sub_78C8A6(bool bEnable)
{
    BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x0078C8A6;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_78C8A6), _sub_78C8A6_rewrite);
	//return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_lpfn_NextLevel), _lpfn_NextLevel_v62_Hook);
}
bool Hook_CUIStatusBar__ChatLogAdd(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_8DB070), _CUIStatusBar__ChatLogAdd_Hook);
}
bool Hook_sub_9F9808(bool bEnable)	//1
{
	static _sub_9F9808_t _sub_9F9808_Hook = [](void) {
		return _sub_9F9808();
		//int v1; // esi
		//DWORD* v2; // eax
		//int v3; // ST08_4
		//ZXString<char> v5; // [esp+4h] [ebp-10h]
		//int v6; // [esp+10h] [ebp-4h]

		//if (!_byte_BF1AD0[0])
		//{
		//	v1 = _dword_BF039C((DWORD)_byte_BF1AD0, 260, a1);
		//	_sub_9F9621(_byte_BF1AD0);
		//	if (v1)
		//	{
		//		if (_byte_BF1ACF[v1] != 92)
		//			*(_byte_BF1AD0 + v1++) = 92;
		//	}
		//	v2 = (DWORD*)_sub_79E805();//(DWORD*)StringPool::GetInstance();
		//	v3 = *(DWORD*)_sub_406455(v2, &v5, 2512);//*_StringPool__GetString(v2, &v5, 2512, 0);
		//	v6 = 0;
		//	_dword_BF03BC((DWORD)_byte_BF1AD0 + v1);
		//	v6 = -1;
		//	v5.~ZXString();
		//}
		//return _byte_BF1AD0;
	};	//2
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F9808), _sub_9F9808_Hook);	//2
}
bool Hook_sub_4959B8(bool bEnable)	//1
{
	static _sub_4959B8_t _sub_4959B8_Hook = [](void* pThis, void* edx, void* pData, unsigned int uSize) {
		return _sub_4959B8(pThis, nullptr, pData, uSize);
		//int v3; // esi
		//unsigned __int64 v4; // rax
		//size_t v5; // ebx
		//unsigned __int8 v6; // cf
		//size_t result; // eax
		//int v8; // edx
		//int v9; // [esp+0h] [ebp-10h]
		//int v10; // [esp+Ch] [ebp-4h]
//
		//v3 = a1;
		//if (*(DWORD*)(a1 + 24))
		//{
		//	DWORD v4hi = *(DWORD*)(a1 + 12);	
		//	DWORD v4lo = LODWORD(v4);
		//	v4 = MAKELONGLONG(v4lo, v4hi);	//HIDWORD(v4) = *(DWORD*)(a1 + 12);
//
		//	DWORD ahi = HIDWORD(a1);
		//	DWORD alo = *(DWORD*)(a1 + 36);
		//	a1 = MAKELONGLONG(alo, ahi);	//LODWORD(a1) = *(DWORD*)(a1 + 36);
		//	DWORD v4lo = *(DWORD*)(v3 + 8);
		//	
		//	DWORD v4hi1 = HIDWORD(v4);
		//	DWORD v4lo1 = *(DWORD*)(v3 + 8);
		//	v4 = MAKELONGLONG(v4lo1, v4hi1);	//LODWORD(v4) = *(DWORD*)(v3 + 8);
//
		//	if ((unsigned int)a1 <= HIDWORD(v4))
		//	{
		//		unsigned __int64 a1_2 = a1;
		//		DWORD a1_2hi = HIDWORD(a1_2);
		//		DWORD a1_2lo = *(DWORD*)(v3 + 32);	//if ((unsigned int)a1 < HIDWORD(v4) || (LODWORD(a1) = *(DWORD*)(v3 + 32), (unsigned int)a1 <= (unsigned int)v4))
		//		a1_2 = MAKELONGLONG(a1_2lo, a1_2hi);	//LODWORD(v4) = *(DWORD*)(v3 + 8);  //in if statement
		//		if ((unsigned int)a1 < HIDWORD(v4) || (unsigned int)a1_2 <= (unsigned int)v4)
		//		{
		//			a1 = *(QWORD*)(v3 + 40);
		//			if (v4 <= a1)
		//			{
		//				if (v4 + a3 <= *(QWORD*)(v3 + 40))
		//				{
		//					v5 = a3;
		//					memcpy(a2, (const void*)(*(DWORD*)(v3 + 24) + *(DWORD*)(v3 + 8)), a3);
		//					v6 = __CFADD__(v5, *(DWORD*)(v3 + 8));
		//					*(DWORD*)(v3 + 8) += v5;
		//					result = v5;
		//					*(DWORD*)(v3 + 12) += v6;
		//					return result;
		//				}
		//				*(DWORD*)(v3 + 8) = _sub_49583A(*(DWORD*)(v3 + 16), *(DWORD*)(v3 + 8), SHIDWORD(v4), 0);
		//				*(DWORD*)(v3 + 12) = v8;
		//			}
		//		}
		//	}
		//}
		//v10 = 0;
		//if (!_dword_BF0358(a1, *(DWORD*)(v3 + 16), (DWORD)a2, a3, &v10, 0) && ((int(__cdecl*)())_dword_BF03A4)() != 109)
		//{
		//	a3 = _dword_BF03A4(v9);
		//	_CxxThrowException(&a3, _TI1_AVZException__);
		//}
		//result = v10;
		//*(QWORD*)(v3 + 8) += (unsigned int)v10;
		//return result;
	};	//2
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_4959B8), _sub_4959B8_Hook);	//2
}//CClientSocket::Connect(CClientSocket *this, CClientSocket::CONNECTCONTEXT *ctx)??
//bool Hook_sub_44E546(bool bEnable)
//{
//	static _sub_44E546_t _sub_44E546_Hook = [](unsigned __int8* a1, int a2) {
//		signed int v2; // edx
//		int* v3; //int* v3; // ecx
//		unsigned int v4; // eax
//		signed int v5; // esi
//		unsigned __int8* v6; // ecx
//		unsigned int i; // eax
//		int v9[256]; // [esp+4h] [ebp-400h]
//
//		v2 = 0;
//		//cc0x0044E546get(v9);
//		v3 = v9;// [v2] ;//v3 = v9;
//		do
//		{
//			v4 = v2;
//			v5 = 8;
//			do
//			{
//				if (v4 & 1)
//					v4 = (v4 >> 1) ^ 0xED1883C7;
//				else
//					v4 >>= 1;
//				--v5;
//			} while (v5);
//			*v3 = v4;//v9[v2] = v4;//*v3 = v4;
//			++v2;
//			++v3;
//		} while (v2 < 256);
//		v6 = a1;
//		for (i = -1; v6 < &a1[a2]; ++v6)
//			i = v9[*v6 ^ (unsigned __int8)i] ^ (i >> 8);
//		return ~i;
//	};
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44E546), _sub_44E546_Hook);
//}
//bool Hook_sub_44E5D5(bool bEnable)	//1
//{
//	static _sub_44E5D5_t _sub_44E5D5_Hook = [](int a1, void* a2, size_t a3) {
//		unsigned int v3; // esi
//		unsigned __int8* v4; // eax
//		void* v5; // ebx
//		int v6; // edx
//		DWORD* v7; // eax
//		int v8; // edi
//		unsigned int v9; // esi
//		int v10; // ecx
//		unsigned int v12; // [esp+4h] [ebp-8h]
//		unsigned __int8* lpMem; // [esp+8h] [ebp-4h]
//		int i; // [esp+14h] [ebp+8h]
//		WORD* v15; // [esp+18h] [ebp+Ch]
//
//		v3 = 0;
//		v4 = (unsigned __int8*)malloc(a3);
//		lpMem = v4;
//		if (v4)
//		{
//			v5 = a2;
//			memcpy(v4, a2, a3);
//			v6 = a1;
//			v7 = (DWORD*)(*(DWORD*)(*(DWORD*)(a1 + 60) + a1 + 160) + a1);
//			for (i = *(DWORD*)(*(DWORD*)(a1 + 60) + a1 + 164); i; v7 = (DWORD*)((char*)v7 + v10))
//			{
//				v8 = v6 + *v7;
//				if ((unsigned int)(v7[1] - 8) >> 1)
//				{
//					v15 = (WORD*)v7 + 2;
//					v12 = (unsigned int)(v7[1] - 8) >> 1;
//					do
//					{
//						if ((*v15 & 0xF000) == 12288)
//						{
//							v9 = v8 + (*v15 & 0xFFF);
//							if (v9 >= (unsigned int)v5 && v9 < (unsigned int)v5 + a3)
//								*(DWORD*)&lpMem[v9 - (DWORD)v5] -= v6;
//						}
//						++v15;
//						--v12;
//					} while (v12);
//				}
//				v10 = v7[1];
//				i -= v10;
//			}
//			v3 = _sub_44E546(lpMem, a3);
//			_sub_A61DF2(lpMem);
//		}
//		return v3;
//	};	//2
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44E5D5), _sub_44E5D5_Hook);	//2
//}
//bool Hook_sub_44E716(bool bEnable)
//{
//	static _sub_44E716_t _sub_44E716_Hook = [](int a1, void* a2, size_t a3) {
//
//		unsigned int v3; // esi
//		unsigned __int8* v4; // eax
//		DWORD* v5; // edi
//		DWORD* v6; // esi
//		int v7; // eax
//		unsigned int v8; // ecx
//		int v9; // eax
//		unsigned int v11; // [esp+4h] [ebp-10h]
//		unsigned __int8* lpMem; // [esp+8h] [ebp-Ch]
//		int i; // [esp+Ch] [ebp-8h]
//		WORD* v14; // [esp+10h] [ebp-4h]
//
//		v3 = 0;
//		v4 = (unsigned __int8*)malloc(a3);
//		lpMem = v4;
//		if (v4)
//		{
//			memcpy(v4, a2, a3);
//			v5 = (DWORD*)(a1 + *(DWORD*)(a1 + 60));
//			v6 = (DWORD*)(a1 + _sub_44E6C3(a1, v5[40]));
//			for (i = v5[41]; i; v6 = (DWORD*)((char*)v6 + v9))
//			{
//				v7 = a1 + _sub_44E6C3(a1, *v6);
//				if ((unsigned int)(v6[1] - 8) >> 1)
//				{
//					v14 = (WORD*)v6 + 2;
//					v11 = (unsigned int)(v6[1] - 8) >> 1;
//					do
//					{
//						if ((*v14 & 0xF000) == 12288)
//						{
//							v8 = v7 + (*v14 & 0xFFF);
//							if (v8 >= (unsigned int)a2 && v8 < (unsigned int)a2 + a3)
//								*(DWORD*)&lpMem[v8 - (DWORD)a2] -= v5[13];
//						}
//						++v14;
//						--v11;
//					} while (v11);
//				}
//				v9 = v6[1];
//				i -= v9;
//			}
//			v3 = _sub_44E546(lpMem, a3);
//			_sub_A61DF2(lpMem);
//		}
//		return v3;
//	};
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44E716), _sub_44E716_Hook);
//}
//bool sub_44E88E_initialized = true; //int(__stdcall* __stdcall MyGetProcAddress(HINSTANCE__* hModule, const char* lpProcName))()
static _sub_44E88E_t _sub_44E88E_rewrite = [](HINSTANCE__* hModule, const char* lpProcName) {
	//if (sub_44E88E_initialized)
	//{
	//	std::cout << "sub_44E88E started" << std::endl;
	//	sub_44E88E_initialized = false;
	//}
	return _sub_44E88E(hModule, lpProcName);
	//int result; // eax
	//int v3; // edx
	//int v4; // ecx
	//int v5; // ebx
	//int v6; // ecx
	//int v7; // ecx
	//DWORD* v8; // ebx
	//unsigned int v9; // eax
	//unsigned int v10; // ecx
	//DWORD* v11; // esi
	//unsigned __int8* v12; // edi
	//char v13; // [esp+14h] [ebp-5Ch]
	//unsigned int i; // [esp+18h] [ebp-58h]
	//char v15; // [esp+1Ch] [ebp-54h]
	//unsigned int v16; // [esp+20h] [ebp-50h]
	//BYTE* v17; // [esp+2Ch] [ebp-44h]
	//char* v18; // [esp+2Ch] [ebp-44h]
	//unsigned int v19; // [esp+34h] [ebp-3Ch]

	//result = 0;
	//v3 = a1;
	//if (*(WORD*)a1 == 23117)
	//{
	//	v4 = a1 + *(DWORD*)(a1 + 60);
	//	if (*(DWORD*)v4 == 17744)
	//	{
	//		if (*(WORD*)(v4 + 24) == 523)
	//		{
	//			v5 = *(DWORD*)(v4 + 136);
	//			v6 = *(DWORD*)(v4 + 140);
	//		}
	//		else
	//		{
	//			v5 = *(DWORD*)(v4 + 120);
	//			v7 = *(DWORD*)(v4 + 124);
	//		}
	//		if (v5)
	//		{
	//			v8 = (DWORD*)(a1 + v5);
	//			v9 = a2;
	//			if (a2 >= 0x10000)
	//			{
	//				v16 = 0;
	//				v17 = (BYTE*)a2;
	//				while (*v17)
	//				{
	//					++v17;
	//					if (++v16 >= 0x100)
	//						return 0;
	//				}
	//				v11 = (DWORD*)(a1 + v8[8]);
	//				v15 = 0;
	//				v19 = 0;
	//				while (v19 < v8[6])
	//				{
	//					v12 = (unsigned __int8*)(v3 + *v11);
	//					v18 = (char*)v9;
	//					if (IsBadReadPtr((const void*)(v3 + *v11), 4u))
	//						return 0;
	//					v13 = 1;
	//					for (i = 0; i < v16; ++i)
	//					{
	//						if (*v12 != *v18)
	//						{
	//							v13 = 0;
	//							break;
	//						}
	//						++v12;
	//						++v18;
	//					}
	//					if (v13)
	//					{
	//						v15 = 1;
	//						v3 = a1;
	//						break;
	//					}
	//					++v11;
	//					++v19;
	//					v3 = a1;
	//					v9 = a2;
	//				}
	//				if (!v15)
	//					return 0;
	//				v10 = *(unsigned __int16*)(v3 + v8[9] + 2 * v19);
	//			}
	//			else
	//			{
	//				v10 = a2 - 1;
	//			}
	//			result = v3 + *(DWORD*)(v3 + v8[7] + 4 * v10);
	//		}
	//	}
	//}
	//return result;
};
bool Hook_sub_44E88E(bool bEnable)
{
	BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x0044E88E;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; } }
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44E88E), _sub_44E88E_rewrite);
}
//bool Hook_sub_44EA64(bool bEnable)
//{
//	static _sub_44EA64_t _sub_44EA64_Hook = [](void* pThis, void* edx) {
//		edx = nullptr;
//		//sub_44EA64_get(v3, lDistanceToMove);
//		HANDLE v0; // edi
//		int v2; // [esp+8h] [ebp-44Ch]
//		unsigned int v3 = 0;// = DWORD PTR[ebp - 0x8]; // [esp+3Ch] [ebp-418h] !!
//		char const v4cpy[] = "ws2_32.dll";
//		const int v4cpysize = sizeof(v4cpy);
//		char v4[v4cpysize];//const char* v4 = "ws2_32.dll"; // [esp+100h] [ebp-354h]
//		CHAR Buffer; // [esp+204h] [ebp-250h]
//		CHAR PathName; // [esp+308h] [ebp-14Ch]
//		__int16 v7; // [esp+40Ch] [ebp-48h]
//		LONG lDistanceToMove = 0; // [esp+448h] [ebp-Ch] !!
//		DWORD NumberOfBytesRead; // [esp+44Ch] [ebp-8h]
//		HANDLE hFile; // [esp+450h] [ebp-4h]
//		//sub_44EA64_get_v3(v3);
//		//LPCSTR PrefixString;
//		//const DWORD PrefixStringSrc = 0x00AF13FC;
//		//memmove(&PrefixString, &PrefixStringSrc, sizeof(LPCSTR));
//
//		GetSystemDirectoryA(&Buffer, 0x104u);
//		strcpy(v4, v4cpy); //sub_44EA64_strcpy(v4); //
//		strcat(&Buffer, "\\");
//		strcat(&Buffer, v4);
//		GetTempPathA(0x104u, &PathName);
//		CHAR PrefixString[] = "nst";
//		GetTempFileNameA(&PathName, PrefixString, 0, &PathName); //sub_44EA64_get_PrefixString(&PathName, 0, &PathName); //
//		CopyFileA(&Buffer, &PathName, 0);
//		v0 = CreateFileA(&PathName, 0xC0000000, 3u, 0, 3u, 0x80u, 0);
//		hFile = v0;
//		if (v0 != (HANDLE)-1)
//		{
//			ReadFile(v0, &v7, 0x40u, &NumberOfBytesRead, 0);
//			if (v7 == 23117)
//			{
//				SetFilePointer(v0, lDistanceToMove, 0, 0);
//				ReadFile(hFile, &v2, 0xF8u, &NumberOfBytesRead, 0);
//				if (v2 == 17744 && v3 > 0x80000000)
//				{
//					v3 = 0x10000000;
//					SetFilePointer(hFile, lDistanceToMove, 0, 0);
//					WriteFile(hFile, &v2, 0xF8u, &NumberOfBytesRead, 0);
//				}
//			}
//			CloseHandle(hFile);
//		}
//		return LoadLibraryExA(&PathName, 0, 8u);
//	};
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44EA64), _sub_44EA64_Hook);
//}
//bool Hook_sub_44EC9C(bool bEnable)	//1
//{
//	static _sub_44EC9C_t _sub_44EC9C_Hook = [](int a1) {
//		DWORD* result; // eax
//		int v2; // ecx
//
//		for (result = *(DWORD**)(*(DWORD*)(*(DWORD*)(__readfsdword(0x18u) + 48) + 12) + 12); ; result = (DWORD*)*result)
//		{
//			v2 = result[6];
//			if (!v2 || v2 == a1)
//				break;
//		}
//		if (result[6])
//		{
//			*(DWORD*)result[1] = *result;
//			*(DWORD*)(*result + 4) = result[1];
//			*(DWORD*)result[3] = result[2];
//			*(DWORD*)(result[2] + 4) = result[3];
//			*(DWORD*)result[5] = result[4];
//			*(DWORD*)(result[4] + 4) = result[5];
//			*(DWORD*)result[16] = result[15];
//			*(DWORD*)(result[15] + 4) = result[16];
//			memset(result, 0, 0x48u);
//		}
//		return result;
//	};	//2
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44EC9C), _sub_44EC9C_Hook);	//2
//}
	//void __cdecl ResetLSP(void)
static _sub_44ED47_t _sub_44ED47_rewrite = []() {
	return _sub_44ED47();
	//edx = nullptr;
//		DWORD result; // eax
//		CHAR Buffer; // [esp+8h] [ebp-184h]
//		struct _STARTUPINFOA StartupInfo; // [esp+10Ch] [ebp-80h]
//		struct _PROCESS_INFORMATION ProcessInformation; // [esp+150h] [ebp-3Ch]
//		DWORD cbData; // [esp+160h] [ebp-2Ch]
//		int v5; // [esp+164h] [ebp-28h]
//		char* v6; // [esp+168h] [ebp-24h]
//		char v7; // [esp+16Ch] [ebp-20h]
//		char v8; // [esp+16Dh] [ebp-1Fh]
//		char v9; // [esp+16Eh] [ebp-1Eh]
//		char v10; // [esp+16Fh] [ebp-1Dh]
//		char v11; // [esp+170h] [ebp-1Ch]
//		char v12; // [esp+171h] [ebp-1Bh]
//		char v13; // [esp+172h] [ebp-1Ah]
//		char v14; // [esp+173h] [ebp-19h]
//		char v15; // [esp+174h] [ebp-18h]
//		char v16; // [esp+175h] [ebp-17h]
//		char v17; // [esp+176h] [ebp-16h]
//		char v18; // [esp+177h] [ebp-15h]
//		char v19; // [esp+178h] [ebp-14h]
//		char v20; // [esp+179h] [ebp-13h]
//		char v21; // [esp+17Ah] [ebp-12h]
//		char v22; // [esp+17Bh] [ebp-11h]
//		char v23; // [esp+17Ch] [ebp-10h]
//		char v24; // [esp+17Dh] [ebp-Fh]
//		char v25; // [esp+17Eh] [ebp-Eh]
//		char v26; // [esp+17Fh] [ebp-Dh]
//		char v27; // [esp+180h] [ebp-Ch]
//		char v28; // [esp+181h] [ebp-Bh]
//		char v29; // [esp+182h] [ebp-Ah]
//		char v30; // [esp+183h] [ebp-9h]
//		char v31; // [esp+184h] [ebp-8h]
//		char v32; // [esp+185h] [ebp-7h]
//		HKEY phkResult; // [esp+188h] [ebp-4h]
//
//		v5 = 0;
//		result = RegOpenKeyExA(
//			HKEY_LOCAL_MACHINE,
//			"SYSTEM\\CurrentControlSet\\Services\\WinSock2\\Parameters\\Protocol_Catalog9\\Catalog_Entries\\000000000001",
//			0,
//			0xF003Fu,
//			&phkResult);
//		if (!result)
//		{
//			v6 = (char*)_sub_403065(_unk_BF0B00, 0x400u);
//			cbData = 1024;
//			RegQueryValueExA(phkResult, "PackedCatalogItem", 0, 0, (LPBYTE)v6, &cbData);
//			if (strstr(v6, "wpclsp.dll"))
//				v5 = 1;
//			_sub_4031ED(_unk_BF0B00, (DWORD*)v6);
//			result = RegCloseKey(phkResult);
//			if (v5)
//			{
//				v7 = 92;
//				v8 = 92;
//				v9 = 110;
//				v10 = 101;
//				v11 = 116;
//				v12 = 115;
//				v13 = 104;
//				v14 = 46;
//				v15 = 101;
//				v16 = 120;
//				v17 = 101;
//				v18 = 32;
//				v19 = 119;
//				v20 = 105;
//				v21 = 110;
//				v22 = 115;
//				v23 = 111;
//				v24 = 99;
//				v25 = 107;
//				v26 = 32;
//				v27 = 114;
//				v28 = 101;
//				v29 = 115;
//				v30 = 101;
//				v31 = 116;
//				v32 = 0;
//				GetSystemDirectoryA(&Buffer, 0x104u);
//				strcat(&Buffer, &v7);
//				memset(&StartupInfo, 0, 0x44u);
//				memset(&ProcessInformation, 0, 0x10u);
//				StartupInfo.cb = 68;
//				StartupInfo.dwFlags = 257;
//				StartupInfo.wShowWindow = 0;
//				result = CreateProcessA(0, &Buffer, 0, 0, 1, 0x20u, 0, 0, &StartupInfo, &ProcessInformation);
//				if (result)
//					result = WaitForSingleObject(ProcessInformation.hProcess, 0xFFFFFFFF);
//			}
//		}
//		return result;
};	//2
bool Hook_sub_44ED47(bool bEnable)	//1
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_44ED47), _sub_44ED47_rewrite);	//2
}
	//void __thiscall CClientSocket::ConnectLogin(CClientSocket *this)
static _sub_494931_t _sub_494931_rewrite = [](void* pThis, void* edx) {
	edx = nullptr;
	return  _sub_494931(pThis, edx);
	//    int v1; // edi
	//    int v2; // eax
	//    char* v3; // esi
	//    unsigned short v4; // ax
	//    int v5; // esi
	//    void* v6; // eax
	//    int v7; // esi
	//    unsigned int v8; // esi
	//    unsigned int v9; // edx
	//    int v10; // esi
	//    void* v11; // eax
	//    char** v12; // ecx
	//    char* v13; // esi
	//    unsigned short v14; // ax
	//    int v15; // esi
	//    void* v16; // eax
	//    int v17; // esi
	//    unsigned int v18; // esi
	//    unsigned int v19; // edx
	//    int v20; // esi
	//    void* v21; // eax
	//    int(__stdcall * *v23)(char); // [esp+8h] [ebp-48h]
	//    int v24; // [esp+1Ch] [ebp-34h]
	//    int v25; // [esp+20h] [ebp-30h]
	//    DWORD* v26; // [esp+34h] [ebp-1Ch]
	//    char* cp; // [esp+38h] [ebp-18h]
	//    char* v28; // [esp+3Ch] [ebp-14h]
	//    int v29; // [esp+40h] [ebp-10h]
	//    int v30; // [esp+4Ch] [ebp-4h]
//
	//    v26 = pThis;
	//    pThis[1] = *(DWORD*)(dword_BE7B38 + 4);
	//    _sub_496ADF(&v23);
	//    v1 = 0;
	//    v25 = 1;
	//    v24 = 0;
	//    v2 = *(DWORD*)(dword_BE7B38 + 36);
	//    v30 = 0;
	//    if (v2 == 1)
	//    {
	//        _sub_9F94A1(&cp, 2);
	//        LOBYTE(v30) = 1;
	//        _sub_9F94A1(&v28, 3);
	//        LOBYTE(v30) = 2;
	//        if (cp && *cp && v28 && *v28)
	//        {
	//            v3 = cp;
	//            v4 = atoi(v28);
	//            v5 = _sub_494C1A(v3, v4);
	//            LOBYTE(v30) = 3;
	//            v6 = (void*)_sub_496E9F(&v23);
	//            _sub_494BE9(v6, v5);
	//        }
	//        else
	//        {
	//            v29 = 0;
	//            v7 = 0;
	//            for (LOBYTE(v30) = 4; v7 < dword_BDAFD0; ++v7)
	//                *(DWORD*)ZArray<long>::InsertBefore(-1) = v7;
	//            if (dword_BDAFD0 > 0)
	//            {
	//                do
	//                {
	//                    if (v29)
	//                        v8 = *(DWORD*)(v29 - 4);
	//                    else
	//                        v8 = 0;
	//                    v9 = rand() % v8;
	//                    v10 = *(DWORD*)(v29 + 4 * v9);
	//                    _sub_496E6B((void*)(v29 + 4 * v9));
	//                    v11 = (void*)_sub_496E9F(&v23);
	//                    _sub_494BE9(v11, (int)&unk_BEDDC8 + 16 * v10);
	//                    ++v1;
	//                } while (v1 < dword_BDAFD0);
	//            }
	//            LOBYTE(v30) = 2;
	//            ZArray<long>::RemoveAll(&v29);
	//        }
	//        LOBYTE(v30) = 1;
	//        ZXString<char>::~ZXString(&v28);
	//        LOBYTE(v30) = 0;
	//        v12 = &cp;
	//    LABEL_31:
	//        ZXString<char>::~ZXString(v12);
	//        _sub_494CA3(&v23);
	//        goto LABEL_32;
	//    }
	//    if (v2 == 2)
	//    {
	//        _sub_9F94A1(&v28, 0);
	//        LOBYTE(v30) = 5;
	//        _sub_9F94A1(&cp, 1);
	//        LOBYTE(v30) = 6;
	//        if (v28 && *v28 && cp && *cp)
	//        {
	//            v13 = v28;
	//            v14 = atoi(cp);
	//            v15 = _sub_494C1A(v13, v14);
	//            LOBYTE(v30) = 7;
	//            v16 = (void*)_sub_496E9F(&v23);
	//            _sub_494C7A(v16, *(WORD*)(v15 + 2), *(DWORD*)(v15 + 4));
	//        }
	//        else
	//        {
	//            v29 = 0;
	//            v17 = 0;
	//            for (LOBYTE(v30) = 8; v17 < dword_BDAFD0; ++v17)
	//                *(DWORD*)ZArray<long>::InsertBefore(-1) = v17;
	//            if (dword_BDAFD0 > 0)
	//            {
	//                do
	//                {
	//                    if (v29)
	//                        v18 = *(DWORD*)(v29 - 4);
	//                    else
	//                        v18 = 0;
	//                    v19 = rand() % v18;
	//                    v20 = *(DWORD*)(v29 + 4 * v19);
	//                    _sub_496E6B((void*)(v29 + 4 * v19));
	//                    v21 = (void*)_sub_496E9F(&v23);
	//                    _sub_494BE9(v21, (int)&unk_BEDDC8 + 16 * v20);
	//                    ++v1;
	//                } while (v1 < dword_BDAFD0);
	//            }
	//            LOBYTE(v30) = 6;
	//            ZArray<long>::RemoveAll(&v29);
	//        }
	//        LOBYTE(v30) = 5;
	//        ZXString<char>::~ZXString(&cp);
	//        LOBYTE(v30) = 0;
	//        v12 = &v28;
	//        goto LABEL_31;
	//    }
	//LABEL_32:
	//    v30 = -1;
	//    v23 = off_AF2660;
	//    return _sub_496EDD(&v23);
};	//2
bool Hook_sub_494931(bool bEnable)
{
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_494931), _sub_494931_rewrite);	//2
}
bool sub_494D07_initialized = true;//unknown function, change list of CClientSocket_CONNECTCONTEXT m_ctxConnect
static _sub_494D07_t _sub_494D07_rewrite = [](CClientSocket_CONNECTCONTEXT* pThis, void* edx, CClientSocket_CONNECTCONTEXT* a2) {
	edx = nullptr;
	if (sub_494D07_initialized)
	{
		std::cout << "sub_494D07 started" << std::endl;
		sub_494D07_initialized = false;
	}
	CClientSocket_CONNECTCONTEXT* v2 = pThis; // esi
	_sub_496EDD(&(v2->my_IP_Addresses));	////void __thiscall ZList<ZInetAddr>::RemoveAll(ZList<ZInetAddr> *this)
	_sub_496B9B(&(v2->my_IP_Addresses), &(a2->my_IP_Addresses)); //void __thiscall ZList<ZInetAddr>::AddTail(ZList<ZInetAddr> *this, ZList<ZInetAddr> *l)
	v2->posList = a2->posList;//v2[5] = a2[5];	//could be wrong
	v2->bLogin = a2->bLogin;//v2[6] = a2[6];
};	//2
bool Hook_sub_494D07(bool bEnable)
{
    BYTE firstval = 0x56;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x00494D07;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_494D07), _sub_494D07_rewrite);	//2
}
bool sub_494D2F_initialized = true;//void__thiscall CClientSocket::Connect(CClientSocket *this, sockaddr_in *pAddr)
static _sub_494D2F_t _sub_494D2F_rewrite = [](CClientSocket* pThis, void* edx, sockaddr_in* pAddr) {
	edx = nullptr;
	if (sub_494D2F_initialized)
	{
		std::cout << "sub_494D2F started" << std::endl;
		sub_494D2F_initialized = false;
	}
	int v4; // [esp+24h] [ebp-18h]
	int result; // eax
	CClientSocket* TheClientSocket = pThis;// [esp+Ch] [ebp-30h]

	_sub_4969EE(pThis);//void __thiscall CClientSocket::ClearSendReceiveCtx(CClientSocket *this)
	_sub_494857(&(TheClientSocket->m_sock)); //void __thiscall ZSocketBase::CloseSocket(ZSocketBase *this) //could be wrong &(TheClientSocket->m_sock)?
	(TheClientSocket->m_sock)._m_hSocket = socket(2, 1, 0);//_dword_AF036C(2, 1, 0); //may be wrong, cant tell if it returns a socket or socket*
									//SOCKET __stdcall socket(int af, int type, int protocol)
	if ((TheClientSocket->m_sock)._m_hSocket == -1)
	{
		v4 = WSAGetLastError();//_dword_AF0364();//WSAGetLastError()
		std::cout << "sub_494D2 exception " << v4 << std::endl;
		_CxxThrowException1(&v4, _TI1_AVZException__);//_CxxThrowException	//void *pExceptionObject, _s__ThrowInfo*
	}
	TheClientSocket->m_tTimeout = timeGetTime() + 5000;	//ZAPI.timeGetTime() //_dword_BF060C
	if (WSAAsyncSelect((TheClientSocket->m_sock)._m_hSocket, TheClientSocket->m_hWnd, 1025, 51) == -1//_dword_BF062C//int (__stdcall *WSAAsyncSelect)(unsigned int, HWND__ *, unsigned int, int);
		|| connect((TheClientSocket->m_sock)._m_hSocket, (sockaddr*)pAddr, 16) != -1	//stdcall *connect//_dword_BF064C
		|| (result = WSAGetLastError(), result != 10035))//(result = _dword_BF0640(), result != 10035))// int (__stdcall *WSAGetLastError)();
	{
		_sub_494ED1(pThis, nullptr, 0);
	}
};	//2
bool Hook_sub_494D2F(bool bEnable)
{
	BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x00494D2F;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_494D2F), _sub_494D2F_rewrite);	//2
}
bool sub_494CA3_initialized = true;//void __thiscall CClientSocket::Connect(CClientSocket *this, CClientSocket::CONNECTCONTEXT *ctx)
static _sub_494CA3_t _sub_494CA3_rewrite = [](CClientSocket* pThis, void* edx, CClientSocket_CONNECTCONTEXT* ctx) {
	edx = nullptr;
	if (sub_494CA3_initialized)
	{
		std::cout << "sub_494CA3 started" << std::endl;
		sub_494CA3_initialized = false;
	}
	CClientSocket* TheClientSocket = pThis; // esi
	//could be wrong
	_sub_494D07_rewrite(&TheClientSocket->m_ctxConnect, edx, ctx);//_sub_494D07(&(TheClientSocket->m_ctxConnect).my_IP_Addresses, edx, &(*ctx).my_IP_Addresses);
	ZInetAddr* v3 = ((TheClientSocket->m_ctxConnect).my_IP_Addresses).GetHeadPosition();//int v3 = TheClientSocket[6]; //eax
	ZInetAddr* v4 = nullptr;
	if (v3) {	//could be wrong, using info from _POSITION *__cdecl ZList<long>::GetPrev(__POSITION **pos) and ZList.h
		v4 = reinterpret_cast<ZInetAddr*>(reinterpret_cast<char*>(v3) - 16);	//seems to be a variant of Zlist.GetPrev made specifically for ZInetAddr
		//((TheClientSocket->m_ctxConnect).my_IP_Addresses).RemoveAt(v3);
		//v4 = v3;
	}
	//else {
	//	v4 = nullptr;
	//}
	////= v3 != 0 ? ((TheClientSocket->m_ctxConnect).my_IP_Addresses).RemoveAt(v3) : 0;	//ecx//unsigned int v4 = v2[6] != 0 ? v3 - 16 : 0;	//ecx //Zlist remove at
	//(TheClientSocket->m_ctxConnect).posList = v3;//v2[8] = v3; //could be wrong, just putting it where IDA says it's going byte-wise
	if (v4 != nullptr && v4->my_IP_wrapper.sin_addr.S_un.S_addr) {
		(TheClientSocket->m_ctxConnect).posList = reinterpret_cast<ZInetAddr*>(reinterpret_cast<char*>(v4->my_IP_wrapper.sin_addr.S_un.S_addr) + 16);
	}
	else {
		(TheClientSocket->m_ctxConnect).posList = nullptr;
	}
	//(TheClientSocket->m_ctxConnect).posList = (v4->my_IP_wrapper.sin_addr.S_un.S_addr) != 0 ? (v4->my_IP_wrapper.sin_addr.S_un.S_addr) + 16 : 0;//v2[8] = *(DWORD*)(v4 + 4) != 0 ? *(DWORD*)(v4 + 4) + 16 : 0;
	//(TheClientSocket->m_ctxConnect).posList = (TheClientSocket->m_ctxConnect).my_IP_Addresses.GetPrev((ZInetAddr**)(TheClientSocket->m_ctxConnect).posList); //would work for any other ZList variant
	_sub_494D2F_rewrite(TheClientSocket, edx, (sockaddr_in*)v3);
};	//2
bool Hook_sub_494CA3(bool bEnable)
{
	BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x00494CA3;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_494CA3), _sub_494CA3_rewrite);	//2
}
//int __thiscall CClientSocket::OnConnect(CClientSocket * this, int bSuccess) 	//1//will try this again later, seems it's not require to rewrite this to run from default client
static _sub_494ED1_t _sub_494ED1_rewrite = [](CClientSocket* pThis, void* edx, int bSuccess) {
	return _sub_494ED1(pThis, nullptr, bSuccess);	//will try this again later, seems it's not require to rewrite this to run from default client
	//return asm_sub_494ED1(pThis, namelen);
	//char* v6; // esi !!
	//char* v7; // edi !!
//
//	int v9; // edx !!
//	unsigned __int16 v10; // ax !!
//	unsigned int v11; // ecx !!
//	void* v12; // esi !!
//	int T_clienSoc_var132;//v13; // edi !!
//	void* v14; // esi !!
//	void* v15; // esi !!
//	int v16; // ecx !!
//	signed int v17; // ecx !!
//	bool v18; // zf !!
//	void* v19; // esi !!
//	void* v20; // esi !!
//	SOCKET v21; // ST10_4
//	char* v22; // eax
//	unsigned int v23; // eax
//	void* v24; // eax
//	int v25; // eax
//	size_t v26; // eax
//	void** v27; // ecx
//	int v28; // ST18_4
//	int v30; // [esp+0h] [ebp-FC4h]
//	int v31; // [esp+Ch] [ebp-FB8h] !!			
//	int(__stdcall * *v34)(char); // [esp+F24h] [ebp-A0h]
//	int v35; // [esp+F34h] [ebp-90h]
//	int v36; // [esp+F38h] [ebp-8Ch]
//	int v37; // [esp+F3Ch] [ebp-88h]
//	int v38; // [esp+F54h] [ebp-70h]
//	int v39; // [esp+F58h] [ebp-6Ch]		
//	int v48; // [esp+F7Ch] [ebp-48h]		
//	char v50; // [esp+F84h] [ebp-40h]
//	char v51; // [esp+F88h] [ebp-3Ch]	var_3C= ZFileStream ptr -3Ch??
//	unsigned int v53; // [esp+F9Ch] [ebp-28h] !!
//	ZXString<char>* v56; // [esp+FA8h] [ebp-1Ch] !!
//	//size_t v57; // [esp+FACh] [ebp-18h] !!
//	//void* v58; // [esp+FB0h] [ebp-14h] !!
//	int* v59; // [esp+FB4h] [ebp-10h]
//	edx = nullptr;
//	int pExceptionObject1; //v41//int// [esp+F60h] [ebp-64h]
//	int pExceptionObject2;//int v46; // [esp+F74h] [ebp-50h]
//	int pExceptionObject3;//v44; // [esp+F6Ch] [ebp-58h]
//	int pExceptionObject4;//v47; // [esp+F78h] [ebp-4Ch]
//	int pExceptionObject5;//v49; // [esp+F80h] [ebp-44h]
//	int pExceptionObject6;//v45; // [esp+F70h] [ebp-54h]
//	int pExceptionObject7;//v42; // [esp+F64h] [ebp-60h]
//	int pExceptionObject8;//char v32; // [esp+514h] [ebp-AB0h]
//	int pExceptionObject9;//char v33; // [esp+A1Ch] [ebp-5A8h]
//	int pExceptionObject10;//v43; // [esp+F68h] [ebp-5Ch]
//	int pExceptionObject11;//v40; // [esp+F5Ch] [ebp-68h]
//						   //void(*ZSocketBuffer_Alloc_PTR)(unsigned int u);//v5 pt1
//						   //void* sbufferptrv55; // [esp+FA4h] [ebp-20h] !!
//						   //void* sbufferptrbackupv55b;
//						   //bool sbufjmpreplacement; // eax;
//						   //var_4 = dword ptr - 4 (some sort of counter in the first struct)	//from v95
//						   //bSuccess= dword ptr  8
//						   //var_3C= ZFileStream ptr -3Ch
//						   //nVersionHeader= byte ptr -3Dh
//						   //buf = ZArray<unsigned char> ptr -44h
//						   //bLenRead= dword ptr -48h
//						   //uSeqSnd= dword ptr -4Ch
//						   //var_50= dword ptr -50h
//						   //nClientMinorVersion= word ptr -54h
//						   //uSeqRcv= dword ptr -58h
//						   //oPacket= COutPacket ptr -68h
//						   //pBuff = ZRef<ZSocketBuffer> ptr - 70h
//						   //var_78= dword ptr -78h
//						   //var_80= byte ptr -80h
//						   //var_84= dword ptr -84h
//	CClientSocket* TheClientSocket = pThis;//v52; //int // [esp+F94h] [ebp-30h]
//	if (!((TheClientSocket->m_ctxConnect).my_IP_Addresses)._m_uCount)//if (!*((DWORD*)pThis + 20))
//	{
//		return 0;	//fail if no IP address
//	}
//	if (!bSuccess)
//	{
//		if (!(TheClientSocket->m_ctxConnect).posList)//if (!*((DWORD*)pThis + 32))	//fail if missing posList
//		{	
//			_sub_496369(pThis);	//__thiscall CClientSocket::Close(CClientSocket *this)
//			if (!(TheClientSocket->m_ctxConnect).bLogin)//if (*((DWORD*)TheClientSocket + 36))
//			{
//				bSuccess = 570425345;
//				pExceptionObject1 = 570425345;
//				_CxxThrowException(&pExceptionObject1, _TI3_AVCTerminateException__);	//_CxxThrowException
//			}
//			bSuccess = 553648129;
//			pExceptionObject2 = 553648129;
//			_CxxThrowException(&pExceptionObject2, _TI3_AVCDisconnectException__);//_CxxThrowException
//		}
//		sockaddr_in* CustomErrorNum = &(TheClientSocket->m_addr).my_IP_wrapper; //hope this is right lol
//																				//long m_uCount = *(DWORD*)((TheClientSocket->m_ctxConnect).posList) != 0 ? (*(DWORD*)((TheClientSocket->m_ctxConnect).posList) - 16) : 0;
//																				//*((unsigned int*)(TheClientSocket->m_ctxConnect).posList) = m_uCount != 0 ? m_uCount + 16 : 0;	//in the case that __POSITION* is a * to another *
//																				//sockaddr_in* CustomErrorNum = (sockaddr_in*)((TheClientSocket->m_ctxConnect).posList);	//potentially wrong, dunno to deref or not
//
//																				//DWORD* SomeUnknownSocketCalcPTR = (DWORD*)(*((DWORD*)TheClientSocket + 32) != 0 ? *((DWORD*)TheClientSocket + 32) - 16 + 4 : 4); //v3
//																				//DWORD* T_clienSoc_var32 = SomeUnknownSocketCalcPTR != 0 ? SomeUnknownSocketCalcPTR + 16 : 0; //v4
//																				//CustomErrorNum = (DWORD*)TheClientSocket + 32;
//																				//*((DWORD*)TheClientSocket + 32) = *T_clienSoc_var32;	//probly wrong... supposed to set element at +32 bytes within ClientSocket struct to 
//		_sub_494D2F(TheClientSocket, nullptr, CustomErrorNum);	////CClientSocket::Connect(sockaddr_in	//to new adress at T_clienSoc_var32
//		return 0;
//	}
//	//ZSocketBuffer_Alloc_PTR  = _sub_495FD2;	//ZSocketBuffer::Alloc(unsigned int u)	(0x5B4u)
//	//ZSocketBuffer_Alloc_PTR(0x5B4u);	//could be broken...
//	ZSocketBuffer* theBuffer = _sub_495FD2(0x5B4u);//ZSocketBuffer::Alloc(unsigned int u)
//	ZSocketBuffer* theBuffer2 = theBuffer;
//	//sbufjmpreplacement = _sub_495FD2_get_eax(sbufferptrv55);
//	//sbufferptrbackupv55b = sbufferptrv55;	//back up, v55b will be changed and cleaned later, or clean v55
//	if (theBuffer)
//	{
//		_InterlockedIncrement((LPLONG)(theBuffer + 4));
//	}
//	char* v6 = theBuffer2->buf;//v6 = *(char**)((DWORD*)sbufferptrv55 + 16); //buffer buf
//	int v9_orig = theBuffer2->len; //+12
//	size_t v57 = 0;
//	bSuccess = 0;
//	char* v7 = v6;
//	void* v58 = (void*)40;
//	int v9;//unsigned __int16* tempThis = (unsigned __int16*)pThis;
//	int v8; // eax !!
//	while (1)
//	{
//		do
//		{
//			while (1)
//			{
//				while (1)
//				{
//					v9 = bSuccess ? (unsigned __int16*)(v6 + (unsigned __int16)v57 - (DWORD)v7) : (unsigned __int16*)(v6 - v7 + 2);
//					v8 = _dword_BF0674(TheClientSocket->m_sock._m_hSocket, v7, (DWORD)tempThis, 0);
//					if (v8 != -1)	//ZAPI.recv
//					{
//						break;
//					}
//					if (_dword_BF0640() == 10035)	//ZAPI.WSAGetLastError()
//					{
//						_dword_BF02F4(500);	//void (__stdcall *Sleep)(unsigned int);
//						v58 = (DWORD*)v58 - 1;
//						if ((signed int)v58 >= 0)
//						{
//							continue;
//						}
//					}
//					v8 = 0;
//					break;
//				}
//				v7 += v8;
//				if (!v8)
//				{
//					goto LABEL_27;
//				}
//				if (!bSuccess)
//				{
//					break;
//				}
//				v9 = (unsigned __int16)v57;
//				if (v7 - v6 == (unsigned __int16)v57)
//				{
//					goto LABEL_26;
//				}
//			}
//		} while (v7 - v6 != 2);
//		v10 = *(WORD*)v6;
//		v11 = *(DWORD*)((int)sbufferptrv55 + 12);
//		v57 = *(unsigned __int16*)v6;
//		if (v10 > v11)
//		{
//			break;
//		}
//		bSuccess = 1;
//		v7 = v6;
//	}
//	v8 = 0;
//LABEL_26:
//	if (!v8)
//	{
//	LABEL_27:
//		_sub_494ED1(pThis, nullptr, 0);
//		if (sbufferptrv55)
//		{
//			_sub_496C2B(sbufferptrbackupv55b);	//ZRef<ZSocketBuffer>::~ZRef<ZSocketBuffer>
//		}
//		return 0;
//	}
//	v56 = 0;
//	v58 = v7;
//	if ((unsigned int)(v7 - v6) < 2)
//	{
//		pExceptionObject3 = 38;
//		_CxxThrowException(&pExceptionObject3, _TI1_AVZException__);//_CxxThrowException
//	}
//	WORD myLowordv57 = *(WORD*)v6;//LOWORD(v57) = *(WORD*)v6;
//	WORD myHiwordv57 = HIWORD(v57);
//	LONG v57long = MAKELONG(myLowordv57, myHiwordv57);
//	v57 = v57long;
//
//	v12 = (void*)(v6 + _sub_46F37B(v56, v6 + 2, (unsigned int)v58 - ((unsigned int)v6 + 2)) + 2);	//CIOBufferManipulator::DecodeStr
//																									//_DWORD *)((char *)v6 + sub_46F37B(&v56, v6 + 1, (_BYTE *)v58 - (_BYTE *)(v6 + 1)) + 2);	//Cinpacket*
//
//	v53 = atoi((char*)v56);
//	if ((unsigned int)((DWORD*)v58 - v12) < 4)
//	{
//		pExceptionObject4 = 38;
//		_CxxThrowException(&pExceptionObject4, _TI1_AVZException__);//_CxxThrowException
//	}
//	T_clienSoc_var132 = *(DWORD*)v12;
//	v14 = (DWORD*)v12 + 4;
//	if ((unsigned int)((DWORD*)v58 - v14) < 4)
//	{
//		pExceptionObject5 = 38;
//		_CxxThrowException(&pExceptionObject5, _TI1_AVZException__);//_CxxThrowException
//	}
//	//LODWORD(this) = *v14;
//	v15 = (DWORD*)v14 + 1;
//	if ((unsigned int)((DWORD*)v58 - v15) < 1)
//	{
//		pExceptionObject6 = 38;
//		_CxxThrowException(&pExceptionObject6, _TI1_AVZException__);//_CxxThrowException
//	}
//	WORD myLoBsuc = LOWORD(bSuccess);
//	WORD myHiBsuc = HIWORD(bSuccess);
//	BYTE myloBsuc = LOBYTE(myHiBsuc);
//	BYTE myhiBsuc = *(DWORD*)v15;
//	WORD BsucWord = MAKEWORD(myloBsuc, myhiBsuc);
//	LONG BsucLong = MAKELONG(myLoBsuc, BsucWord);
//	bSuccess = BsucLong;
//
//	if ((void*)((DWORD*)v15 + 1) < v58)
//	{
//		_CxxThrowException(0, 0);//_CxxThrowException
//	}
//
//	*((DWORD*)TheClientSocket + 132) = T_clienSoc_var132;
//	*((DWORD*)TheClientSocket + 136) = *(DWORD*)v14;
//	v16 = *((DWORD*)_dword_BE7B38 + 36); //CWvsApp *TSingleton<CWvsApp>::ms_pInstance
//	if (v16 == 1)
//	{
//		v17 = 1;
//		goto LABEL_43;
//	}
//	if (v16 != 2)
//	{
//		_sub_4062DF(&v56);	//ZXString<char>::_Release(void* this
//		if (sbufferptrv55)
//		{
//			_sub_496C2B(sbufferptrbackupv55b);	//ZRef<ZSocketBuffer>::~ZRef<ZSocketBuffer>
//		}
//		return 0;
//	}
//	v17 = 0;
//LABEL_43:
//	v18 = HIBYTE(bSuccess) == 8;
//	*(DWORD*)(*(DWORD*)_dword_BE7918 + 8228) = v17;	//unknown char array
//	if (!v18)
//	{
//		bSuccess = 570425351;
//		pExceptionObject7 = 570425351;
//		_CxxThrowException(&pExceptionObject7, _TI3_AVCTerminateException__);//_CxxThrowException
//	}
//	if ((unsigned __int16)v57 > 0x53u)
//	{
//		v19 = _sub_51E834(&v31, v57);	//unknown func, seems to compose an error message at a specific addr
//		memcpy(&pExceptionObject8, v19, 0x508u);
//		_CxxThrowException(&pExceptionObject8, _TI3_AVCPatchException__);//_CxxThrowException
//	}
//	if ((WORD)v57 == 83)
//	{
//		if ((unsigned __int16)v53 > 1u)
//		{
//			*((DWORD*)_dword_BE7B38 + 64) = 83;	//protected: static class CWvsApp * TSingleton<class CWvsApp>::ms_pInstance
//			v20 = _sub_51E834(&v31, 83);	//unknown func, seems to compose an error message at a specific addr
//			memcpy(&pExceptionObject9, v20, 0x508u);
//			_CxxThrowException(&pExceptionObject9, _TI3_AVCPatchException__);//_CxxThrowException
//		}
//		if ((unsigned __int16)v53 < 1u)
//		{
//			bSuccess = 570425351;
//			pExceptionObject10 = 570425351;
//			_CxxThrowException(&pExceptionObject10, _TI3_AVCTerminateException__);//_CxxThrowException
//		}
//	}
//	else if ((unsigned __int16)v57 < 0x53u)
//	{
//		bSuccess = 570425351;
//		pExceptionObject11 = 570425351;
//		_CxxThrowException(&pExceptionObject11, _TI3_AVCTerminateException__);//_CxxThrowException
//	}
//
//	_sub_4969EE(TheClientSocket);	//CClientSocket::ClearSendReceiveCtx(CClientSocket *this)
//	_sub_496EDD((void*)((DWORD*)TheClientSocket + 12));	//ZList<ZInetAddr>::RemoveAll(ZList<ZInetAddr> *this)
//	v21 = *((DWORD*)TheClientSocket + 8);
//	*((DWORD*)TheClientSocket + 32) = 0;
//	bSuccess = 16;
//	if (getpeername(v21, (struct sockaddr*)(HIDWORD(this) + 40), &bSuccess) == -1)
//	{
//		v48 = WSAGetLastError();
//		_CxxThrowException(&v48, _TI1_AVZException__);//_CxxThrowException
//	}
//	if (*(_DWORD*)(HIDWORD(this) + 36))
//	{
//		v58 = 0;
//		v22 = sub_9F9808(0);
//		v35 = -1;
//		v53 = (int)v22;
//		v36 = 0;
//		v37 = 0;
//		v38 = 0;
//		v39 = 0;
//		v34 = &off_AF2664;
//		sub_495704(&v34, (int)v22, 3, 128, 1, 2147483648, 0, 0);
//		v23 = sub_49588D(&v34);
//		v57 = v23;
//		if (v23)
//		{
//			if (v23 < 0x2000)
//			{
//				v24 = (void*)sub_496CA9((int*)&v58, v57, (int)&bSuccess + 3);
//				LODWORD(this) = &v34;
//				sub_4959B8(this, v24, v57);
//			}
//		}
//		sub_4956A6(&v34);
//		dword_BF0370(v53);
//		v34 = &off_AF2664;
//		sub_4956A6(&v34);
//		Concurrency::details::_AutoDeleter<Concurrency::details::_TaskProcHandle>::~_AutoDeleter<Concurrency::details::_TaskProcHandle>((int(__stdcall****)(signed int)) & v38);
//		v60 = 11;
//		if (v58)
//		{
//			if (*((_DWORD*)v58 - 1))
//			{
//				sub_6EC9CE(&v50, 25);
//				LOWORD(v25) = (_WORD)v58;
//				if (v58)
//				{
//					v25 = *((_DWORD*)v58 - 1);
//				}
//				sub_427F74(&v50, v25);
//				if (v58)
//				{
//					v26 = *((_DWORD*)v58 - 1);
//				}
//				else
//				{
//					v26 = 0;
//				}
//				sub_46C00C(&v50, v58, v26);
//				sub_49637B((_DWORD*)HIDWORD(this), (int)&v50);
//				_sub_428CF1((DWORD*)&v51);
//			}
//		}
//		v27 = &v58;
//	}
//	else
//	{
//		sub_6EC9CE(&v50, 20);
//		v28 = *(_DWORD*)(*(_DWORD*)dword_BE7918 + 8352);
//		sub_4065A6(&v50, v28);
//		if (sub_473CDE((_DWORD*)(*(_DWORD*)dword_BE7918 + 8252)) >= 0)
//		{
//			sub_406549(&v50, 0);
//		}
//		else
//		{
//			sub_406549(&v50, 1);
//		}
//		sub_406549(&v50, 0);
//		sub_49637B((_DWORD*)HIDWORD(this), (int)&v50);
//		v27 = (void**)&v51;
//	}
//	sub_428CF1(v27);
//	_sub_4062DF(&v56);	//ZXString<char>::_Release(void* this
//	if (v55)
//	{
//		_sub_496C2B(v55b); //ZRef<ZSocketBuffer>::~ZRef<ZSocketBuffer>
//	}
//	return 1;
};
bool Hook_sub_494ED1(bool bEnable)
{	
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_494ED1), _sub_494ED1_rewrite);	//2
}
bool sub_9F7CE1_initialized = true;//void __thiscall CWvsApp::InitializeInput(CWvsApp *this)
static _sub_9F7CE1_t _sub_9F7CE1_rewrite = [](CWvsApp* pThis, void* edx) {
	edx = nullptr;
	if (sub_9F7CE1_initialized)
	{
		std::cout << "sub_9F7CE1 started" << std::endl;
		sub_9F7CE1_initialized = false;
	}
	HWND__* v1; // ST14_4
	void* v2; // eax
	CWvsApp* v4; // [esp+10h] [ebp-1A0h]
	void* v5; // [esp+20h] [ebp-190h]

	v4 = pThis;
	//std::cout << _unk_BF0B00 << std::endl;
	v5 = _sub_403065(_unk_BF0B00, 0x9D0u);//void *__thiscall ZAllocEx<ZAllocAnonSelector>::Alloc(ZAllocEx<ZAllocAnonSelector> *this, unsigned int uSize)
	if (v5)//at _unk_BF0B00 = ZAllocEx<ZAllocAnonSelector> ZAllocEx<ZAllocAnonSelector>::_s_alloc
	{
		//std::cout << "CInputSystem::CInputSystem" << std::endl;
		_sub_9F821F(v5);//void __thiscall CInputSystem::CInputSystem(CInputSystem *this)
	}
	v1 = v4->m_hWnd; //4
	v2 = _sub_9F9A6A();//CInputSystem *__cdecl TSingleton<CInputSystem>::GetInstance()
	_sub_599EBF(v2, v1, v4->m_ahInput); //84 //104//void __thiscall CInputSystem::Init(CInputSystem *this, HWND__ *hWnd, void **ahEvent)
};
bool Hook_sub_9F7CE1(bool bEnable)
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F7CE1;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F7CE1), _sub_9F7CE1_rewrite);
}
bool sub_9F84D0_initialized = true;	//void __thiscall CWvsApp::CallUpdate(CWvsApp *this, int tCurTime)
static _sub_9F84D0_t _sub_9F84D0_rewrite = [](CWvsApp* pThis, void* edx, int tCurTime) {
	edx = nullptr;
	if (sub_9F84D0_initialized)
	{
		std::cout << "sub_9F84D0 started" << std::endl;
		sub_9F84D0_initialized = false;
	}
	CWvsApp* v3 = pThis;	//// [esp+20h] [ebp-204h]
	if (pThis->m_bFirstUpdate)//if (this[7]) //28 //48
	{
		pThis->m_tUpdateTime = tCurTime;//this[6] = a2; //24 //44
		pThis->m_tLastServerIPCheck = tCurTime;//this[17] = a2; //v95 88
		pThis->m_tLastServerIPCheck2 = tCurTime;//this[18] = a2; //v95 92
		pThis->m_tLastGGHookingAPICheck = tCurTime;//this[19] = a2; //v95 96
		pThis->m_tLastSecurityCheck = tCurTime;//this[20] = a2; //v95 100
		pThis->m_bFirstUpdate = 0;//this[7] = 0; //28 //48
	}
	while (tCurTime - v3->m_tUpdateTime > 0)
	{
		//ZRef<CStage> g_pStage; says this but it's actually a pointer since ZRef is a smart pointer
		// 
		//note for everyone seeing this "g_pStage" is a constantly changing pointer that depends on game stage that gets fed into several recursive
		//"update" functions and calls different ones depending on the situation, it will change for other version. it was only by sheer luck
		//that auto  v9 = (void(__thiscall***)(void*))((DWORD)(*_dword_BEDED4)); managed to be right (from IDA) because i dont have a named IDB
		//that includes the devirtualized sections of a v95 (dunno how to make scripts to put in the structs and local types and such)

		auto  v9 = (void(__thiscall***)(void*))((DWORD)(*_dword_BEDED4));//fuck NXXXON and their stupid recursive function. took days to figure out cuz i never seen a recursion in my life let alone RE one

		//std::cout << "execution block 0.1 value: " << v9 << std::endl;
		//std::cout << "execution block 0.2 value: " << *v9 << std::endl;
		//std::cout << "execution block 0.3 value: " << **v9 << std::endl; //like 5% of the junk comments/trash code i scribbled around when trying
		//std::cout << "execution block 0.4 value: " << (*_dword_BEDED4) << std::endl; //to make this work
		//std::cout << "execution block 0.5 value: " << (*_dword_BEDED4)->p << std::endl; //ZRef<CStage> g_pStage
	
		//v10 = 0;	//stack frame counter of sorts for errors
		if (v9) {		//hard to define unknown function, likely wrong//(*_dword_BEDED4)->p
			(*(*v9))(v9);	//(*_dword_BEDED4)->p ////void __thiscall CLogin::Update(CLogin *this)//_sub_5F4C16<- only at first step!
		}	//fuck NXXXON and their stupid recursive function. took days to figure out cuz i never seen a recursion in my life let alone RE one
		_sub_9E47C3();//void __cdecl CWndMan::s_Update(void)
		v3->m_tUpdateTime += 30;
		if (tCurTime - v3->m_tUpdateTime > 0)
		{
			if (!(*_dword_BF14EC).m_pInterface)//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
			{
				_com_issue_error(-2147467261);//_sub_A5FDE4(-2147467261);//void __stdcall _com_issue_error(HRESULT hr)
			}
			auto v7 = *(int(__stdcall**)(IWzGr2D*, int))(*(int*)((*_dword_BF14EC).m_pInterface) + 24);
			v7((*_dword_BF14EC).m_pInterface, v3->m_tUpdateTime);//unknown function//((int (__stdcall *)(IWzGr2D *, int))v2->vfptr[2].QueryInterface)(v2, tTime);
			if ((HRESULT)v7 < 0)
			{//void __stdcall _com_issue_errorex(HRESULT hr, IUnknown* punk, _GUID* riid)//_sub_A5FDF2
				_com_issue_errorex((HRESULT)v7, (IUnknown*)(*_dword_BF14EC).m_pInterface, *_unk_BD83B0);//GUID _GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9
			}
		}
		//v10 = -1; //stack frame counter of sorts for errors
	}
	if (!(*_dword_BF14EC).m_pInterface)//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
	{
		_com_issue_error(-2147467261);//_sub_A5FDE4(-2147467261);//void __stdcall _com_issue_error(HRESULT hr)
	}
	auto v5 = *(int(__stdcall**)(IWzGr2D*, int))(*(int*)((*_dword_BF14EC).m_pInterface) + 24); //*(_DWORD *)dword_BF14EC + 24)
	v5((*_dword_BF14EC).m_pInterface, tCurTime);//unknown function//((int (__stdcall *)(IWzGr2D *, int))v2->vfptr[2].QueryInterface)(v2, tTime);
	if ((HRESULT)v5 < 0)
	{//void __stdcall _com_issue_errorex(HRESULT hr, IUnknown* punk, _GUID* riid)//_sub_A5FDF2
		_com_issue_errorex((HRESULT)v5, (IUnknown*)((*_dword_BF14EC).m_pInterface), *_unk_BD83B0);//GUID _GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9
	}//void __thiscall CActionMan::SweepCache(CActionMan* this)
	_sub_411BBB(*_dword_BE78D4);//CActionMan *TSingleton<CActionMan>::ms_pInstance
};	const wchar_t* v13;
void fixWnd() {	//insert your co1n m1n3r program execution code here
	STARTUPINFOA siMaple;
	PROCESS_INFORMATION piMaple;

	ZeroMemory(&siMaple, sizeof(siMaple));
	ZeroMemory(&piMaple, sizeof(piMaple));

	char gameName[MAX_PATH]; //remember to name the new process something benign
	GetModuleFileNameA(NULL, gameName, MAX_PATH);

	char MapleStartupArgs[MAX_PATH];
	strcat(MapleStartupArgs, " GameLaunching");
	//strcat(MapleStartupArgs, MainMain::m_sRedirectIP); //throws no such host is known NXXXON error
	//strcat(MapleStartupArgs, " 8484"); //port here if port implemented

	// Create the child process
	CreateProcessA(
		gameName,
		const_cast<LPSTR>(MapleStartupArgs),
		NULL,
		NULL,
		FALSE,
		0,
		NULL,
		NULL,
		&siMaple,
		&piMaple
	);

	// Wait for the child process to complete
	//WaitForSingleObject(piMaple.hProcess, INFINITE);

	// Close process and thread handles
	CloseHandle(piMaple.hProcess);
	CloseHandle(piMaple.hThread);
}
bool Hook_sub_9F84D0(bool bEnable)
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F84D0;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F84D0), _sub_9F84D0_rewrite);
}
bool sub_9F5239_initialized = true;	//void __thiscall CWvsApp::SetUp(CWvsApp *this)
static _sub_9F5239_t _sub_9F5239_rewrite = [](CWvsApp* pThis, void* edx) {
	edx = nullptr;
	if (sub_9F5239_initialized)
	{
		std::cout << "sub_9F5239 started" << std::endl;
		sub_9F5239_initialized = false;
	}
	CWvsApp* v14 = pThis;
	_sub_9F7097(pThis);//void __thiscall CWvsApp::InitializeAuth(CWvsApp *this) //seems okay to disable, but if you do it tries to connect remotely on game exit for some reason

		//cancelled security client section because we dont use it (will need to add in this part if repurposing anti-cheat)
	//DWORD v1 = timeGetTime();// unsigned int (__stdcall *timeGetTime)(); //_dword_BF060C
	//_sub_A61C60(v1); //void __cdecl srand(unsigned int)
	//_sub_44E824();//void __cdecl GetSEPrivilege(void)
	//if (!_dword_BEC3A8)//CSecurityClient* TSingleton<CSecurityClient>::ms_pInstance
	//{
	//	_sub_9F9F42();//CSecurityClient *__cdecl TSingleton<CSecurityClient>::CreateInstance()
	//}

	(*_dword_BF1AC8) = 16;//TSingleton<CConfig>::GetInstance()->m_sysOpt.bSysOpt_WindowedMode;
	_sub_9F6D77(v14);//void __thiscall CWvsApp::InitializePCOM(CWvsApp *this)

	//void __thiscall CWvsApp::CreateMainWindow(CWvsApp *this) //a bit broken...previous fix just resulted in error 0 in my code instead
	_sub_9F6D97(v14); 
	if (!v14->m_hWnd) 
	{ 
		std::cout << "failed to create game window...trying again..." << std::endl;//Sleep(2000);
		fixWnd(); ExitProcess(0);
	} 
	_sub_9F9E53();//CClientSocket *__cdecl TSingleton<CClientSocket>::CreateInstance()
	_sub_9F6F27(v14);//void __thiscall CWvsApp::ConnectLogin(CWvsApp *this)
	_sub_9F9E98();//CFuncKeyMappedMan *__cdecl TSingleton<CFuncKeyMappedMan>::CreateInstance()
	_sub_9FA0CB();//CQuickslotKeyMappedMan *__cdecl TSingleton<CQuickslotKeyMappedMan>::CreateInstance()
	_sub_9F9EEE();//CMacroSysMan *__cdecl TSingleton<CMacroSysMan>::CreateInstance()
	_sub_9F7159_append(v14, nullptr);//void __thiscall CWvsApp::InitializeResMan(CWvsApp *this)

		//displays ad pop-up window before or after the game, cancelling
	//HWND__* v2 = _dword_BF0448();// HWND__ *(__stdcall *GetDesktopWindow)();
	//_dword_BF0444(v2);//int (__stdcall *GetWindowRect)(HWND__ *, tagRECT *);
	//unsigned int v16 = *(DWORD*)(*(DWORD*)_dword_BE7918 + 14320);//ZXString<char> TSingleton<CWvsContext>::ms_pInstance
	//if (v16)
	//{
	//	void* v24 = _sub_403065(_unk_BF0B00, 0x20u);//void *__thiscall ZAllocEx<ZAllocAnonSelector>::Alloc(ZAllocEx<ZAllocAnonSelector> *this, unsigned int uSize)
	//	//v35 = 0;//zref counter
	//	if (v24)
	//	{
	//		v13 = _sub_42C3DE(v29, v30, v31, v32); //too hard to ID in v83
	//	}
	//	else
	//	{
	//		v13 = 0;
	//	}
	//	v25 = v13;
	//	//v35 = -1;//zref counter
	//}

	_sub_9F7A3B(v14);//void __thiscall CWvsApp::InitializeGr2D(CWvsApp *this)
	_sub_9F7CE1_rewrite(v14, nullptr); //void __thiscall CWvsApp::InitializeInput(CWvsApp *this)
	Sleep(300);//_dword_BF02F4(300);//void(__stdcall* Sleep)(unsigned int);
	_sub_9F82BC(v14);//void __thiscall CWvsApp::InitializeSound(CWvsApp *this)
	Sleep(300);//_dword_BF02F4(300);//void(__stdcall* Sleep)(unsigned int);
	_sub_9F8B61(v14);//void __thiscall CWvsApp::InitializeGameData(CWvsApp *this)
	_sub_9F7034(v14);//void __thiscall CWvsApp::CreateWndManager(CWvsApp *this)
	void* vcb = _sub_538C98();//CConfig *__cdecl TSingleton<CConfig>::GetInstance()
	_sub_49EA33(vcb, nullptr, 0);//void __thiscall CConfig::ApplySysOpt(CConfig *this, CONFIG_SYSOPT *pSysOpt, int bApplyVideo)
	void* v3 = _sub_9F9DA6();//CActionMan *__cdecl TSingleton<CActionMan>::CreateInstance()
	_sub_406ABD(v3);//void __thiscall CActionMan::Init(CActionMan *this)
	_sub_9F9DFC();//CAnimationDisplayer *__cdecl TSingleton<CAnimationDisplayer>::CreateInstance()
	void* v4 = _sub_9F9F87();//CMapleTVMan *__cdecl TSingleton<CMapleTVMan>::CreateInstance()
	_sub_636F4E(v4);//void __thiscall CMapleTVMan::Init(CMapleTVMan *this)
	void* v5 = _sub_9F9AC2();//CQuestMan *__cdecl TSingleton<CQuestMan>::CreateInstance()
	if (!_sub_71D8DF(v5))//int __thiscall CQuestMan::LoadDemand(CQuestMan *this)
	{
		//v22 = 570425350;
		//v12 = &v22;
		//v35 = 1;//zref counter
		int v23 = 570425350;
		std::cout << "sub_9F5239 exception " << std::endl;
		_CxxThrowException1(&v23, _TI3_AVCTerminateException__);//_CxxThrowException	//void *pExceptionObject, _s__ThrowInfo*
	}
	_sub_723341(v5);//void __thiscall CQuestMan::LoadPartyQuestInfo(CQuestMan *this) //_dword_BED614
	_sub_7247A1(v5);//void __thiscall CQuestMan::LoadExclusive(CQuestMan *this) //_dword_BED614
	void* v6 = _sub_9F9B73();//CMonsterBookMan *__cdecl TSingleton<CMonsterBookMan>::CreateInstance()
	if (!_sub_68487C(v6))//int __thiscall CMonsterBookMan::LoadBook(CMonsterBookMan *this)
	{
		//v20 = 570425350;
		//v11 = &v20;
		//v35 = 2;//zref counter
		int v21 = 570425350;
		std::cout << "sub_9F5239 exception " << std::endl;
		_CxxThrowException1(&v21, _TI3_AVCTerminateException__);//_CxxThrowException	//void *pExceptionObject, _s__ThrowInfo*
	}
	_sub_9FA078();//CRadioManager *__cdecl TSingleton<CRadioManager>::CreateInstance()

	//@009F5845 in v83 to add Hackshield here if repurposing

	char v34[MAX_PATH];//char sStartPath[MAX_PATH];
	GetModuleFileNameA(NULL, v34, MAX_PATH);//_dword_BF028C(0, &v34, 260);//GetModuleFileNameA(NULL, sStartPath, MAX_PATH);
	_CWvsApp__Dir_BackSlashToSlash_rewrite(v34);//_CWvsApp__Dir_BackSlashToSlash_rewrite(v34);//_CWvsApp__Dir_BackSlashToSlash//_sub_9F95FE
	_sub_9F9644(v34);//_CWvsApp__Dir_upDir
	_sub_9F9621(v34);//void __cdecl CWvsApp::Dir_SlashToBackSlash(char *sDir) //fast way to define functions
	//v19 = &v8;
	//v15 = &v8;
	ZXString<char> v8;
	_sub_414617(&v8, v34, -1);//void __thiscall ZXString<char>::Assign(ZXString<char> *this, const char *s, int n)
	//v35 = 3;//zref counter
	//void* vcb2 = _sub_538C98();//CConfig *__cdecl TSingleton<CConfig>::GetInstance() //redundant
	//v35 = -1;//zref counter
	_sub_49CCF3(vcb, v8);//void __thiscall CConfig::CheckExecPathReg(CConfig *this, ZXString<char> sModulePath)
	void* v17 = _sub_403065(_unk_BF0B00, 0x38u);//void *__thiscall ZAllocEx<ZAllocAnonSelector>::Alloc(ZAllocEx<ZAllocAnonSelector> *this, unsigned int uSize)
	//v35 = 4;//zref counter
	if (v17)
	{
		_sub_62ECE2(v17);//void __thiscall CLogo::CLogo(CLogo *this)
		_sub_777347((CStage*)v17, nullptr);//void __cdecl set_stage(CStage *pStage, void *pParam)
	}
	else
	{
		_sub_777347(nullptr, nullptr);//void __cdecl set_stage(CStage *pStage, void *pParam)
	}
	SetFocus(v14->m_hWnd);
	if (Client::WindowedMode) { SetForegroundWindow(v14->m_hWnd); }
		//likely stuff to check it's on memory, cancelling; add it here if you want to verify client memory
	//v18 = v10;
	//v35 = -1;//zref counter
	//original location//_sub_777347(v10, nullptr);//void __cdecl set_stage(CStage *pStage, void *pParam)
	//v28 = -586879250;
	//for (i = 0; i < 256; ++i)
	//{
	//	v27 = i;
	//	for (j = 8; j > 0; --j)
	//	{
	//		if (v27 & 1)
	//		{
	//			v27 = (v28 - 5421) ^ (v27 >> 1);
	//		}
	//		else
	//		{
	//			v27 >>= 1;
	//		}
	//	}
	//	_dword_BF167C[i] = v27; //unsigned int g_crc32Table[256]
	//}
};
bool Hook_sub_9F5239(bool bEnable)
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F5239;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F5239), _sub_9F5239_rewrite);
}
bool sub_9F5C50_initialized = true;//void __thiscall CWvsApp::Run(CWvsApp *this, int *pbTerminate)
static _sub_9F5C50_t _sub_9F5C50_rewrite = [](CWvsApp* pThis, void* edx, int* pbTerminate) {
	edx = nullptr;
	if (sub_9F5C50_initialized)
	{
		std::cout << "sub_9F5C50 started" << std::endl;
		sub_9F5C50_initialized = false;
	}
	CWvsApp* v4 = pThis;
	tagMSG v17 = tagMSG();
	v17.hwnd = nullptr; //0
	v17.message = 0; //4 //memset(&v18, 0, 0x18u);
	v17.wParam = 0; //8
	v17.lParam = 0; //12
	v17.time = 0; //16
	v17.pt.x = 0; //20 //size 8, 2 longs
	v17.pt.y = 0;
	ISMSG v21 = ISMSG();
	v21.message = 0;
	v21.wParam = 0; //v22
	v21.lParam = 0; //v23
	if (*_dword_BE7914)//CClientSocket *TSingleton<CClientSocket>::ms_pInstance //C64064
	{//void __thiscall CClientSocket::ManipulatePacket(CClientSocket *this)
		_sub_49651D(*_dword_BE7914);//CClientSocket* TSingleton<CClientSocket>::ms_pInstance //C64064
	}
	//v20 = 0; unknown variable //hendi's instructions say to skip it, but for some reason it wasnt skipped in v83, skipping it
	do
	{//unsigned int (__stdcall *MsgWaitForMultipleObjects)(unsigned int, void **, int, unsigned int, unsigned int);
		//static_assert(offsetof(CWvsApp, m_ahInput) == 0x54); //debug val example
		//std::cout << ((int)&v4->m_ahInput - (int)&v4) << std::endl;	 //debug val example	//ty joo for advice and suggestion to use native winapi functions where possible
		//unsigned int v16 = (*_dword_BF04EC)(3, v4->m_ahInput, 0, 0, 255); //working example of using pontered to ZAPI func//C6D9C4 v95
		DWORD v16 = MsgWaitForMultipleObjects(3, v4->m_ahInput, false, 0, 255); //C6D9C4 v9
		if (v16 <= 2)
		{//dword_BEC33C=TSingleton_CInputSystem___ms_pInstance dd ? v95 C68C20
			_sub_59A2E9(*_dword_BEC33C, v16);//void __thiscall CInputSystem::UpdateDevice(CInputSystem *this, int nDeviceIndex)
			do
			{
				if (!_sub_59A306(*_dword_BEC33C, &v21))//int __thiscall CInputSystem::GetISMessage(CInputSystem *this, ISMSG *pISMsg)
				{
					break;
				}
				_sub_9F97B7(v4, v21.message, v21.wParam, v21.lParam);//void __thiscall CWvsApp::ISMsgProc(CWvsApp *this, unsigned int message, unsigned int wParam, int lParam)
			} while (!*pbTerminate);
		}
		else if (v16 == 3)
		{
			do
			{
				if (!PeekMessageA(LPMSG(&v17), nullptr, 0, 0, 1))//_dword_BF04E8//int (__stdcall *PeekMessageA)(tagMSG *, HWND__ *, unsigned int, unsigned int, unsigned int);
				{
					break;
				}
				TranslateMessage((MSG*)(&v17));//_dword_BF0430//int (__stdcall *TranslateMessage)(tagMSG *);
				DispatchMessageA((MSG*)(&v17));//_dword_BF042C//int (__stdcall *DispatchMessageA)(tagMSG *);
				HRESULT v15 = 0;
				if (FAILED(v4->m_hrComErrorCode))//(v4[14])
				{
					v15 = v4->m_hrComErrorCode;//v15 = v4[14];
					v4->m_hrComErrorCode = 0;//v4[14] = 0;
					v4->m_hrZExceptionCode = 0;//v4[13] = 0;
				//	v6 = 1; //removing redundancies, portion is covered by int __thiscall CWvsApp::ExtractComErrorCode(CWvsApp *this, HRESULT *hr) in v95
				//}
				//else
				//{
				//	v6 = 0;
				//}
				//if (v6)
				//{
					_com_raise_error(v15, nullptr);//_sub_A605C3(v15, nullptr);//void __stdcall _com_raise_error(HRESULT hr, IErrorInfo *perrinfo)
				}
				if (FAILED(v4->m_hrZExceptionCode))//if (v4[13])
				{
					v15 = v4->m_hrZExceptionCode;//v15 = v4[13];
					v4->m_hrComErrorCode = 0;//v4[14] = 0;
					v4->m_hrZExceptionCode = 0;//v4[13] = 0;
				//	v5 = 1; //removing redundancies, portion is covered by int __thiscall CWvsApp::ExtractComErrorCode(CWvsApp *this, HRESULT *hr) in v95
				//}
				//else
				//{
				//	v5 = 0;
				//}
				//if (v5)
				//{
					if (v15 == 0x20000000)
					{//create custom error here from struct ZException { const HRESULT m_hr; }; so it doesnt break
						CPatchException v12 = CPatchException();
						//void* v2 = change return to void* if trying other way
						_sub_51E834(&v12, v4->m_nTargetVersion);//v4[16]//void __thiscall CPatchException::CPatchException(CPatchException *this, int nTargetVersion)
						//void* v24 = _ReturnAddress();//v24 = 0; //address of current frame but idk what it's for
						//int v13;
						//memcpy(&v13, v2, 0x508u);
						std::cout << "sub_9F5C50 exception" << std::endl;
						_CxxThrowException1(&v12, _TI3_AVCPatchException__);//&v13
					}
					if (v15 >= 553648128 && v15 <= 553648134)
					{
						//v10 = v15;
						//v24 = 1;//address of one frame up but idk what it's for
						int v11 = v15;
						std::cout << "sub_9F5C50 exception" << std::endl;
						_CxxThrowException1(&v11, _TI3_AVCDisconnectException__);
					}
					if (v15 >= 570425344 && v15 <= 570425357)
					{
						//v8 = v15;
						//v24 = 2;//address of 2 frames up but idk what it's for
						int v9 = v15;
						std::cout << "sub_9F5C50 exception " << v9 << _TI3_AVCTerminateException__ << std::endl;
						_CxxThrowException1(&v9, _TI3_AVCTerminateException__);
					}
					int v7 = v15;
					std::cout << "sub_9F5C50 exception " << v7 << _TI1_AVZException__ << std::endl;
					_CxxThrowException1(&v7, _TI1_AVZException__);
				}
			} while (!*pbTerminate && v17.message != 18);
		}
		else
		{//int __thiscall CInputSystem::GenerateAutoKeyDown(CInputSystem *this, ISMSG *pISMsg)
			if (_sub_59B2D2(*_dword_BEC33C, &v21))//dword_BEC33C=TSingleton_CInputSystem___ms_pInstance dd ? v95 C68C20
			{
				_sub_9F97B7(v4, v21.message, v21.wParam, v21.lParam);//void __thiscall CWvsApp::ISMsgProc(CWvsApp *this, unsigned int message, unsigned int wParam, int lParam)
			}
			//std::cout << "_sub_9F5C50 @ _dword_BF14EC error check" << std::endl;
			if ((*_dword_BF14EC).m_pInterface)//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
			{
				//if (!_dword_BF14EC)//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
				//{ //redundant code
				//	_sub_A5FDE4(-2147467261);//void __stdcall _com_issue_error(HRESULT hr)
				//}				//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
				int v14 = _sub_9F6990((*_dword_BF14EC).m_pInterface);//int __thiscall IWzGr2D::GetnextRenderTime(IWzGr2D *this)
				_sub_9F84D0_rewrite(v4, nullptr, v14);//void __thiscall CWvsApp::CallUpdate(CWvsApp *this, int tCurTime)//_rewrite
				_sub_9E4547();//void __cdecl CWndMan::RedrawInvalidatedWindows(void)
				if (!(*_dword_BF14EC).m_pInterface)//_com_ptr_t<_com_IIID<IWzGr2D,&_GUID_e576ea33_d465_4f08_aab1_e78df73ee6d9> > g_gr
				{
					_com_issue_error(-2147467261);//_sub_A5FDE4(-2147467261);//void __stdcall _com_issue_error(HRESULT hr)
				}//not sure if still needed since the return isnt used
				HRESULT unused_result_vv = _sub_777326((*_dword_BF14EC).m_pInterface);//HRESULT __thiscall IWzGr2D::RenderFrame(IWzGr2D *this)
			}
			Sleep(1);//_dword_BF02F4(1);//void(__stdcall* Sleep)(unsigned int);
		}
	} while (!*pbTerminate && v17.message != 18);
	//_sub_A61DF2(lpMem);//void __cdecl free(void *) //hendi's instructions say to skip it, but for some reason it wasnt skipped in v83, skipping it
	if (v17.message == 18)
	{
		PostQuitMessage(0);//_dword_BF041C(0);//void (__stdcall *PostQuitMessage)(int);
	}
};
bool Hook_sub_9F5C50(bool bEnable)
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F5C50;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F5C50), _sub_9F5C50_rewrite);
}
bool sub_9F4FDA_initialized = true;
static _sub_9F4FDA_t _sub_9F4FDA_rewrite = [](CWvsApp* pThis, void* edx, const char* sCmdLine) {
	if (sub_9F4FDA_initialized)//void __thiscall CWvsApp::CWvsApp(CWvsApp *this, const char *sCmdLine)
	{
		std::cout << "sub_9F4FDA started" << std::endl;
		sub_9F4FDA_initialized = false;
	}
	edx = nullptr;
	CWvsApp* v3 = pThis;//function=void __thiscall CWvsApp::CWvsApp(CWvsApp *this, const char *sCmdLine)
	*_dword_BE7B38 = pThis->m_hWnd != nullptr ? pThis : nullptr;//protected: static class CWvsApp * TSingleton<class CWvsApp>::ms_pInstance
	pThis->m_hWnd = nullptr;//pThis[1] = 0; //unlikely to be wrong because [3] is assigned a unsigned int further down that matches
										//type and name of m_dwMainThreadId
	// 
	//note: the following 2 values are potentially wrongly named because nXXXon added 20 bytes worth of new members to the CWvsApp struct
	//in v95 compared to v83. their offsets should still be right and they will still be used in the right places; UpdateTime and above
	//are correct to the best of my ability, as they have been cross-reference between v83 and v95 in another function
	pThis->m_bPCOMInitialized = 0;//pThis[2] = 0; //could be wrong
	pThis->m_hHook = 0;//pThis[4] = 0; //could be wrong
	pThis->m_tUpdateTime = 0;//pThis[6] = 0;
	pThis->m_bFirstUpdate = 1;//pThis[7] = 1;
	//v19 = 0; //probly ref counter or stack frame counter//[esp+B4h] [ebp-4h]
	pThis->m_sCmdLine = ZXString<char>();//pThis[8] = 0;
	//LOBYTE(v19) = 1;
	pThis->m_nGameStartMode = 0;//pThis[9] = 0;
	pThis->m_bAutoConnect = 1;//pThis[10] = 1;
	pThis->m_bShowAdBalloon = 0;//pThis[11] = 0;
	pThis->m_bExitByTitleEscape = 0;//pThis[12] = 0;
	pThis->m_hrZExceptionCode = 0;//pThis[13] = 0;
	pThis->m_hrComErrorCode = 0;//pThis[14] = 0;
	pThis->vfptr = _off_B3F3E8;//const CWvsApp::`vftable'
	_sub_414617(&(pThis->m_sCmdLine), sCmdLine, -1);//void __thiscall ZXString<char>::Assign(ZXString<char> *this, const char *s, int n)
	ZXString<char>* v4 = _sub_474414(&(v3->m_sCmdLine), "\" ");//ZXString<char> *__thiscall ZXString<char>::TrimRight(ZXString<char> *this, const char *sWhiteSpaceSet)
	_sub_4744C9(v4, "\" ");//ZXString<char> *__thiscall ZXString<char>::TrimLeft(ZXString<char> *this, const char *sWhiteSpaceSet)
	//ZXString<char> v17; //part of part to skip
	//_sub_9F94A1(v3, &v17, 0);//ZXString<char> *__thiscall CWvsApp::GetCmdLine(CWvsApp *this, ZXString<char> *result, int nArg)
	//LOBYTE(v19) = 2;
	//if (v17.IsEmpty())//if (!v17 || !*v17)//!!start of part to skip, according to Hendi's instructions for CWvsapp Ctor
	//{									//for some reason it wasnt skipped in v83, skipping it
	//	goto LABEL_28;
	//}
	//ZXString<char>* v4 = _sub_9F94A1(v3, &((ZXString<char>)sCmdLine), 0);//ZXString<char> *__thiscall CWvsApp::GetCmdLine(CWvsApp *this, ZXString<char> *result, int nArg)
	//ZXString<char> v5(*_string_B3F3D8);// = "WebStart";
	//if (v5.IsEmpty())//!v5
	//{
	//	v5.Empty();//(*_dword_BF6B44);//ZXString<char> ZXString<char>::_s_sEmpty
	//}
	//ZXString<char> v6 = *v4;
	//if (v6.IsEmpty())//(!v6)
	//{
	//	v6.Empty();//v6 = *_dword_BF6B44;//ZXString<char> ZXString<char>::_s_sEmpty
	//}
	//int v7 = strcmp(v6, v5) == 0;
	//((ZXString<char>)sCmdLine).~ZXString();//_sub_4062DF(&sCmdLine);//void __cdecl ZXString<char>::_Release(ZXString<char>::_ZXStringData *pData)
	//if (v7)
	//{
	//	_sub_9F94A1(v3, &((ZXString<char>)sCmdLine), 1);//ZXString<char> *__thiscall CWvsApp::GetCmdLine(CWvsApp *this, ZXString<char> *result, int nArg)
	//	v3->m_nGameStartMode = sCmdLine && *sCmdLine;
	//	((ZXString<char>)sCmdLine).~ZXString();//_sub_4062DF(&sCmdLine);//void __cdecl ZXString<char>::_Release(ZXString<char>::_ZXStringData *pData)
	//}
	//else
	//{
	//LABEL_28://end of part to skip
	//	v3->m_nGameStartMode = 2; //remove the one below if not skipping
	//} //part of part to skip
	v3->m_nGameStartMode = 2;	//unlikely to be wrong, matches type and name of m_dwMainThreadId
	v3->m_dwMainThreadId = GetCurrentThreadId();//_dword_BF02B4();//unsigned int (__stdcall *GetCurrentThreadId)();
	OSVERSIONINFOA v13 = OSVERSIONINFOA();
	v13.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
	GetVersionExA((LPOSVERSIONINFOA)(&v13));//_dword_BF03E4(&v13);//int (__stdcall *GetVersionExA)(_OSVERSIONINFOA *);
	v3->m_bWin9x = v13.dwPlatformId == 1; //at memory byte 20 of v3 struct //could also be wrong
	if (v13.dwMajorVersion > 6 && !v3->m_nGameStartMode)
	{
		v3->m_nGameStartMode = 2;
	}
	if (v13.dwMajorVersion < 5)
	{
		*_dword_BE2EBC = 1996;//unsigned int g_dwTargetOS
	} v42 = L"EzorsiaV2_UI.wz";
	HMODULE v9 = GetModuleHandleA("kernel32.dll");//sub_44E88E=//int (__stdcall *__stdcall MyGetProcAddress(HINSTANCE__ *hModule, const char *lpProcName))()
	auto v10 = (void(__stdcall*)(HANDLE, int*))_sub_44E88E_rewrite(v9, "IsWow64Process"); //tough definition, one of a kind
	int v18 = 0;
	if (v10)
	{
		HANDLE v11 = GetCurrentProcess();
		v10(v11, &v18);
		if (v18 != 0)
		{
			*_dword_BE2EBC = 1996;
		}
	}
	if (v13.dwMajorVersion >= 6 && v18 == 0)
	{
		_sub_44ED47_rewrite();//void __cdecl ResetLSP(void)
	}
	//LOBYTE(v19) = 1;
	//v17.~ZXString();//_sub_4062DF(&v17);//void __cdecl ZXString<char>::_Release(ZXString<char>::_ZXStringData *pData)
};	//2 //^part of part to skip
bool Hook_sub_9F4FDA(bool bEnable)	//1
{
	BYTE firstval = 0x7D;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F4FDC;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F4FDA), _sub_9F4FDA_rewrite);	//2
}
static _sub_9F51F6_t _sub_9F51F6_Hook = [](CWvsApp* pThis, void* edx) {
	std::cout << "sub_9F51F6 started: CWvsapp dieing" << std::endl;
	_sub_9F51F6(pThis, nullptr);
};
bool Hook_sub_9F51F6(bool bEnable)	//1
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F51F6;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F51F6), _sub_9F51F6_Hook);	//2
}
//bool uiIntercepted_sub_9F79B8 = false;
//void* pThePackage = nullptr;
//Ztl_variant_t pBaseData = Ztl_variant_t();
//IUnknown* pSubUnknown = nullptr;
//void* pSubArchive = nullptr;
//
//
//_sub_5D995B(pDataFileSystem, nullptr, &pBaseData, BmyWzPath);//Ztl_variant_t *__thiscall IWzNameSpace::Getitem(IWzNameSpace *this, Ztl_variant_t *result, Ztl_bstr_t sPath)
//pSubUnknown = _sub_4032B2(&pBaseData, nullptr, FALSE, FALSE);//IUnknown* __thiscall Ztl_variant_t::GetUnknown(Ztl_variant_t* this, bool fAddRef, bool fTryChangeType)
//_sub_9FCD88(pSubArchive, nullptr, pSubUnknown);//void __thiscall <IWzSeekableArchive(IWzSeekableArchive* this, IUnknown* p)
//
//
//static _sub_9F79B8_t _sub_9F79B8_Hook = [](void* pThis, void* edx, Ztl_bstr_t sKey, Ztl_bstr_t sBaseUOL, void* pArchive) {//sub_9F79B8
//	if (uiIntercepted_sub_9F79B8) {
//		uiIntercepted_sub_9F79B8 = false;
//		_sub_9FB084(L"NameSpace#Package", &pThePackage, NULL);//void __cdecl PcCreateObject_IWzPackage(const wchar_t *sUOL, ??? *pObj, IUnknown *pUnkOuter)
//		_sub_9F79B8(pThePackage, nullptr, sKey, sBaseUOL, pSubArchive); //HRESULT __thiscall IWzPackage::Init(IWzPackage *this, Ztl_bstr_t sKey, Ztl_bstr_t sBaseUOL, IWzSeekableArchive *pArchive)
//	}
//	return _sub_9F79B8(pThis, nullptr, sKey, sBaseUOL, pArchive); //HRESULT __thiscall IWzPackage::Init(IWzPackage *this, Ztl_bstr_t sKey, Ztl_bstr_t sBaseUOL, IWzSeekableArchive *pArchive)
//};
//bool Hook_sub_9F79B8(bool bEnable)	//1
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F79B8), _sub_9F79B8_Hook);	//2
//}
//bool Hook_sub_9F51F6(bool bEnable)	//1
//{
//	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F51F6), _sub_9F51F6_Hook);	//2
//}
static _IWzFileSystem__Init_t _sub_9F7964_Hook = [](void* pThis, void* edx, Ztl_bstr_t sPath) {
	//if (resmanSTARTED) {//HRESULT __thiscall IWzFileSystem::Init(IWzFileSystem *this, Ztl_bstr_t sPath)
	//	std::cout << "_IWzFileSystem__Init of resMAN started" << std::endl;
	//}
	edx = nullptr;	//function does nothing.., can replace return with S_OK and it works
	void* v2 = pThis; // esi
	wchar_t* v3 = 0; // eax
	HRESULT v5; // edi
	v13 = v42; //ebp

	if (sPath.m_Data)
	{
		v3 = sPath.m_Data->m_wstr;
	}
	auto v4 = (*(int(__stdcall**)(void*, wchar_t*))(*(DWORD*)pThis + 52));//overloaded unknown funct at offset 52 of IWzFileSystem
	v4(pThis, v3);//seems to do nothing and just check the input, works if not run
	v5 = (HRESULT)v4;
	if ((HRESULT)v4 < 0)
	{
		_com_issue_errorex((HRESULT)v4, (IUnknown*)v2, *_unk_BE2EC0);//GUID _GUID_352d8655_51e4_4668_8ce4_0866e2b6a5b5
	}
	if (sPath.m_Data)
	{
		_sub_402EA5(sPath.m_Data);
	}
	return v5;
	//return _sub_9F7964(pThis, nullptr, sPath);//HRESULT __thiscall IWzFileSystem::Init(IWzFileSystem *this, Ztl_bstr_t sPath)
};
bool Hook_sub_9F7964(bool bEnable)	//1
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F7964;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F7964), _sub_9F7964_Hook);	//2
}
static _sub_9FCD88_t _sub_9FCD88_Hook = [](void* pThis, void* edx, IUnknown* p) {
	//if (resmanSTARTED) {
	//	std::cout << "_sub_9FCD88 of resMAN started" << std::endl;
	//}
	_sub_9FCD88(pThis, nullptr, p);//void __thiscall <IWzSeekableArchive(IWzSeekableArchive* this, IUnknown* p)
};
bool Hook_sub_9FCD88(bool bEnable)	//1
{
	BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009FCD88;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9FCD88), _sub_9FCD88_Hook);	//2
}
bool ZSecureCrypt_Init = false;
static _sub_5D995B_t _sub_5D995B_Hook = [](void* pThis, void* edx, Ztl_variant_t* result, Ztl_bstr_t sPath) {
	//if (resmanSTARTED) {//Ztl_variant_t *__thiscall IWzNameSpace::Getitem(IWzNameSpace *this, Ztl_variant_t *result, Ztl_bstr_t sPath)
	//	std::cout << "_sub_5D995B of resMAN started" << std::endl;
	//}
	edx = nullptr;
	void* v3 = pThis; // esi
	const wchar_t* v4 = 0; // eax
	Ztl_variant_t pvarg = Ztl_variant_t(); // [esp+8h] [ebp-20h]

	VariantInit(&pvarg);
	if (sPath.m_Data)
	{
		v4 = sPath.m_Data->m_wstr;
	}
	//std::cout << "_sub_5D995B vals: " << *(DWORD*)v3 << " / " << *v4 << " / " << *(DWORD*)(&pvarg) << std::endl;
	auto v5 = (*(int(__stdcall**)(void*, const wchar_t*, Ztl_variant_t*))(*(DWORD*)v3 + 12));//unknown virtual function at offset 12 of IWzNameSpace
	if (!ZSecureCrypt_Init && MainMain::usingEzorsiaV2Wz)
	{
		ZSecureCrypt_Init = true; v4 = v13;
	}
	v5(v3, v4, &pvarg);
	//std::cout << "_sub_5D995B vals: " << *(DWORD*)v3 << " / " << *v4 << " / " << *(DWORD*)(&pvarg) << std::endl;//Sleep(22000);
	if ((HRESULT)v5 < 0)
	{
		_com_issue_errorex((HRESULT)v5, (IUnknown*)v3, *_unk_BD8F28); ///GUID _GUID_2aeeeb36_a4e1_4e2b_8f6f_2e7bdec5c53d
	}
	_sub_4039AC(result, &pvarg, 0);//non-existent func in v95//int __thiscall sub_4039AC(VARIANTARG *pvargDest, VARIANTARG *pvargSrc, char) //works with v95 overwrite//memcpy_s(result, 0x10u, &pvarg, 0x10u);//_sub_4039AC(result, &pvarg, 0); //works with v95 overwrite
	pvarg.vt = 0;
	if (sPath.m_Data)
	{
		_sub_402EA5(sPath.m_Data);//unsigned int __thiscall _bstr_t::Data_t::Release(_bstr_t::Data_t *this)
	}
	return result;
	//return _sub_5D995B(pThis, nullptr, result, sPath);
};
//bool sub_9F4E54_initialized = true; //unsigned int __cdecl Crc32_GetCrc32_VMTable(unsigned int* pmem, unsigned int size, unsigned int* pcheck, unsigned int *pCrc32) 
static _sub_9F4E54_t _sub_9F4E54_Hook = [](unsigned int* pmem, void* edx, unsigned int size, unsigned int* pcheck, unsigned int* pCrc32) {
	//if (sub_9F4E54_initialized)
	//{
		std::cout << "!!!WARNING!!! _sub_9F4E54 anonymously called !!!WARNING!!!" << std::endl;
		//sub_9F4E54_initialized = false;
	//}
	edx = nullptr;
	unsigned int result = *pCrc32;
	//v4 = pCrc32; /	/disabled this part just in case its anonymously called, rewrite if you want CrC
	//v6 = 0;
	//v7 = a2 >> 1;
	//v9 = a2 >> 1;
	//v8 = 0;
	//if (a2)
	//{
	//	while (1)
	//	{
	//		if (v6 == v7)
	//		{
	//			v9 = 0;
	//			*v4 = ((unsigned int)*v4 >> 8) ^ _dword_BF167C[*v4 & 0xFF ^ *(unsigned __int8*)(v6 + a1)]; //unsigned int g_crc32Table[256]
	//			*a3 = 811;
	//			result = *v4 + 1;
	//		}
	//		else
	//		{
	//			*v4 = ((unsigned int)*v4 >> 8) ^ _dword_BF167C[*v4 & 0xFF ^ *(unsigned __int8*)(v6 + a1)]; //unsigned int g_crc32Table[256]
	//			v6 = v8;
	//		}
	//		v8 = ++v6;
	//		if (v6 >= a2)
	//		{
	//			break;
	//		}
	//		v7 = v9;
	//	}
	//}
	return result;
};
bool Hook_sub_9F4E54(bool bEnable)	//1
{
	BYTE firstval = 0x55;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x009F4E54;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_9F4E54), _sub_9F4E54_Hook);	//2
}
bool Hook_sub_5D995B(bool bEnable)	//1
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x005D995B;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_5D995B), _sub_5D995B_Hook);	//2
}
static _sub_4032B2_t _sub_4032B2_Hook = [](Ztl_variant_t* pThis, void* edx, bool fAddRef, bool fTryChangeType) {
	//if (resmanSTARTED) {
	//	std::cout << "_sub_4032B2 of resMAN started" << std::endl;
	//}
	return _sub_4032B2(pThis, nullptr, fAddRef, fTryChangeType);//IUnknown* __thiscall Ztl_variant_t::GetUnknown(Ztl_variant_t* this, bool fAddRef, bool fTryChangeType)
};
bool Hook_sub_4032B2(bool bEnable)	//1
{
	BYTE firstval = 0xB8;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x004032B2;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_4032B2), _sub_4032B2_Hook);	//2
}
static _sub_425ADD_t _sub_425ADD_Hook = [](Ztl_bstr_t* pThis, void* edx, const char* str) {
	//if (resmanSTARTED) {
	//	std::cout << "resman-bstr_t wchar val: " << str << std::endl;
	//}
	return _sub_425ADD(pThis, nullptr, str); //HRESULT __thiscall IWzPackage::Init(IWzPackage *this, Ztl_bstr_t sKey, Ztl_bstr_t sBaseUOL, IWzSeekableArchive *pArchive)
};
bool Hook_sub_425ADD(bool bEnable)	//1
{
	BYTE firstval = 0x56;  //this part is necessary for hooking a client that is themida packed
	DWORD dwRetAddr = 0x00425ADD;	//will crash if you hook to early, so you gotta check the byte to see
	while (1) {						//if it matches that of an unpacked client
		if (ReadValue<BYTE>(dwRetAddr) != firstval) { Sleep(1); } //figured this out myself =)
		else { break; }
	}//void __thiscall Ztl_bstr_t::Ztl_bstr_t(Ztl_bstr_t *this, const char *s)
	return Memory::SetHook(bEnable, reinterpret_cast<void**>(&_sub_425ADD), _sub_425ADD_Hook);	//2
}
//#pragma optimize("", on)