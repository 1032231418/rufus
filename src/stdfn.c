/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard Windows function calls
 * Copyright © 2013-2014 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <sddl.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "localization.h"

int  nWindowsVersion = WINDOWS_UNDEFINED;
char WindowsVersionStr[128] = "Windows ";

BOOL is_x64(void)
{
	BOOL ret = FALSE;
	BOOL (__stdcall *pIsWow64Process)(HANDLE, PBOOL) = NULL;
	// Detect if we're running a 32 or 64 bit system
	if (sizeof(uintptr_t) < 8) {
		pIsWow64Process = (BOOL (__stdcall *)(HANDLE, PBOOL))
			GetProcAddress(GetDLLHandle("KERNEL32"), "IsWow64Process");
		if (pIsWow64Process != NULL) {
			(*pIsWow64Process)(GetCurrentProcess(), &ret);
		}
	} else {
		ret = TRUE;
	}
	return ret;
}

// From smartmontools os_win32.cpp
void GetWindowsVersion(void)
{
	OSVERSIONINFOEXA vi, vi2;
	const char* w = 0;
	const char* w64 = "32 bit";
	char* vptr;
	size_t vlen;
	unsigned major, minor;
	ULONGLONG major_equal, minor_equal;
	BOOL ws;

	nWindowsVersion = WINDOWS_UNDEFINED;
	safe_strcpy(WindowsVersionStr, sizeof(WindowsVersionStr), "Windows Undefined");

	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionExA((OSVERSIONINFOA *)&vi)) {
		memset(&vi, 0, sizeof(vi));
		vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
		if (!GetVersionExA((OSVERSIONINFOA *)&vi))
			return;
	}

	if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT) {

		if (vi.dwMajorVersion > 6 || (vi.dwMajorVersion == 6 && vi.dwMinorVersion >= 2)) {
			// Starting with Windows 8.1 Preview, GetVersionEx() does no longer report the actual OS version
			// See: http://msdn.microsoft.com/en-us/library/windows/desktop/dn302074.aspx

			major_equal = VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL);
			for (major = vi.dwMajorVersion; major <= 9; major++) {
				memset(&vi2, 0, sizeof(vi2));
				vi2.dwOSVersionInfoSize = sizeof(vi2); vi2.dwMajorVersion = major;
				if (!VerifyVersionInfoA(&vi2, VER_MAJORVERSION, major_equal))
					continue;
				if (vi.dwMajorVersion < major) {
					vi.dwMajorVersion = major; vi.dwMinorVersion = 0;
				}

				minor_equal = VerSetConditionMask(0, VER_MINORVERSION, VER_EQUAL);
				for (minor = vi.dwMinorVersion; minor <= 9; minor++) {
					memset(&vi2, 0, sizeof(vi2)); vi2.dwOSVersionInfoSize = sizeof(vi2);
					vi2.dwMinorVersion = minor;
					if (!VerifyVersionInfoA(&vi2, VER_MINORVERSION, minor_equal))
						continue;
					vi.dwMinorVersion = minor;
					break;
				}

				break;
			}
		}

		if (vi.dwMajorVersion <= 0xf && vi.dwMinorVersion <= 0xf) {
			ws = (vi.wProductType <= VER_NT_WORKSTATION);
			nWindowsVersion = vi.dwMajorVersion << 4 | vi.dwMinorVersion;
			switch (nWindowsVersion) {
			case 0x50: w = "2000";
				break;
			case 0x51: w = "XP";
				break;
			case 0x52: w = (!GetSystemMetrics(89)?"2003":"2003_R2");
				break;
			case 0x60: w = (ws?"Vista":"2008");
				break;
			case 0x61: w = (ws?"7":"2008_R2");
				break;
			case 0x62: w = (ws?"8":"2012");
				break;
			case 0x63: w = (ws?"8.1":"2012_R2");
				break;
			case 0x64: w = (ws?"8.2":"2012_R3");
				break;
			default:
				if (nWindowsVersion < 0x50)
					nWindowsVersion = WINDOWS_UNSUPPORTED;
				else
					w = "9 or later";
				break;
			}
		}
	}

	if (is_x64())
		w64 = "64-bit";

	vptr = &WindowsVersionStr[sizeof("Windows ") - 1];
	vlen = sizeof(WindowsVersionStr) - sizeof("Windows ") - 1;
	if (!w)
		safe_sprintf(vptr, vlen, "%s %u.%u %s", (vi.dwPlatformId==VER_PLATFORM_WIN32_NT?"NT":"??"),
			(unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion, w64);
	else if (vi.wServicePackMinor)
		safe_sprintf(vptr, vlen, "%s SP%u.%u %s", w, vi.wServicePackMajor, vi.wServicePackMinor, w64);
	else if (vi.wServicePackMajor)
		safe_sprintf(vptr, vlen, "%s SP%u %s", w, vi.wServicePackMajor, w64);
	else
		safe_sprintf(vptr, vlen, "%s %s", w, w64);
}

/*
 * String array manipulation
 */
void StrArrayCreate(StrArray* arr, size_t initial_size)
{
	if (arr == NULL) return;
	arr->Max = initial_size; arr->Index = 0;
	arr->String = (char**)calloc(arr->Max, sizeof(char*));
	if (arr->String == NULL)
		uprintf("Could not allocate string array\n");
}

void StrArrayAdd(StrArray* arr, const char* str)
{
	char** old_table;
	if ((arr == NULL) || (arr->String == NULL))
		return;
	if (arr->Index == arr->Max) {
		arr->Max *= 2;
		old_table = arr->String;
		arr->String = (char**)realloc(arr->String, arr->Max*sizeof(char*));
		if (arr->String == NULL) {
			free(old_table);
			uprintf("Could not reallocate string array\n");
			return;
		}
	}
	arr->String[arr->Index] = safe_strdup(str);
	if (arr->String[arr->Index++] == NULL) {
		uprintf("Could not store string in array\n");
	}
}

void StrArrayClear(StrArray* arr)
{
	size_t i;
	if ((arr == NULL) || (arr->String == NULL))
		return;
	for (i=0; i<arr->Index; i++) {
		safe_free(arr->String[i]);
	}
	arr->Index = 0;
}

void StrArrayDestroy(StrArray* arr)
{
	StrArrayClear(arr);
	if (arr != NULL)
		safe_free(arr->String);
}

/*
 * Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
 */
static PSID GetSID(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		uprintf("OpenProcessToken failed: %s\n", WindowsErrorString());
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			uprintf("GetTokenInformation (pre) failed: %s\n", WindowsErrorString());
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
	}
	if (tu == NULL) {
		return NULL;
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			uprintf("Unable to convert SID to string: %s\n", WindowsErrorString());
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				uprintf("Unable to convert string back to SID: %s\n", WindowsErrorString());
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		uprintf("GetTokenInformation (real) failed: %s\n", WindowsErrorString());
	}
	free(tu);
	return ret;
}

/*
 * read or write I/O to a file
 * buffer is allocated by the procedure. path is UTF-8
 */
BOOL FileIO(BOOL save, char* path, char** buffer, DWORD* size)
{
	SECURITY_ATTRIBUTES s_attr, *ps = NULL;
	SECURITY_DESCRIPTOR s_desc;
	PSID sid = NULL;
	HANDLE handle;
	BOOL r;
	BOOL ret = FALSE;

	// Change the owner from admin to regular user
	sid = GetSID();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		uprintf("Could not set security descriptor: %s\n", WindowsErrorString());
	}

	if (!save) {
		*buffer = NULL;
	}
	handle = CreateFileU(path, save?GENERIC_WRITE:GENERIC_READ, FILE_SHARE_READ,
		ps, save?CREATE_ALWAYS:OPEN_EXISTING, 0, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not %s file '%s'\n", save?"create":"open", path);
		goto out;
	}

	if (save) {
		r = WriteFile(handle, *buffer, *size, size, NULL);
	} else {
		*size = GetFileSize(handle, NULL);
		*buffer = (char*)malloc(*size);
		if (*buffer == NULL) {
			uprintf("Could not allocate buffer for reading file\n");
			goto out;
		}
		r = ReadFile(handle, *buffer, *size, size, NULL);
	}

	if (!r) {
		uprintf("I/O Error: %s\n", WindowsErrorString());
		goto out;
	}

	PrintStatus(0, TRUE, save?MSG_216:MSG_215, path);
	ret = TRUE;

out:
	CloseHandle(handle);
	if (!ret) {
		// Only leave a buffer allocated if successful
		*size = 0;
		if (!save) {
			safe_free(*buffer);
		}
	}
	return ret;
}

unsigned char* GetResource(HMODULE module, char* name, char* type, const char* desc, DWORD* len, BOOL duplicate)
{
	HGLOBAL res_handle;
	HRSRC res;
	unsigned char* p = NULL;

	res = FindResourceA(module, name, type);
	if (res == NULL) {
		uprintf("Unable to locate resource '%s': %s\n", desc, WindowsErrorString());
		goto out;
	}
	res_handle = LoadResource(module, res);
	if (res_handle == NULL) {
		uprintf("Unable to load resource '%s': %s\n", desc, WindowsErrorString());
		goto out;
	}
	*len = SizeofResource(module, res);

	if (duplicate) {
		p = (unsigned char*)malloc(*len);
		if (p == NULL) {
			uprintf("Unable to allocate resource '%s'\n", desc);
			goto out;
		}
		memcpy(p, LockResource(res_handle), *len);
	} else {
		p = (unsigned char*)LockResource(res_handle);
	}

out:
	return p;
}


/*
 * Set or restore a Local Group Policy DWORD key indexed by szPath/SzPolicy
 */
#pragma push_macro("INTERFACE")
#undef  INTERFACE
#define INTERFACE IGroupPolicyObject
#define REGISTRY_EXTENSION_GUID { 0x35378EAC, 0x683F, 0x11D2, {0xA8, 0x9A, 0x00, 0xC0, 0x4F, 0xBB, 0xCF, 0xA2} }
#define GPO_OPEN_LOAD_REGISTRY  1
#define GPO_SECTION_MACHINE     2
typedef enum _GROUP_POLICY_OBJECT_TYPE {
	GPOTypeLocal = 0, GPOTypeRemote, GPOTypeDS
} GROUP_POLICY_OBJECT_TYPE, *PGROUP_POLICY_OBJECT_TYPE;
DECLARE_INTERFACE_(IGroupPolicyObject, IUnknown) {
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID *ppvObj) PURE;
	STDMETHOD_(ULONG, AddRef) (THIS) PURE;
	STDMETHOD_(ULONG, Release) (THIS) PURE;
	STDMETHOD(New) (THIS_ LPOLESTR pszDomainName, LPOLESTR pszDisplayName, DWORD dwFlags) PURE;
	STDMETHOD(OpenDSGPO) (THIS_ LPOLESTR pszPath, DWORD dwFlags) PURE;
	STDMETHOD(OpenLocalMachineGPO) (THIS_ DWORD dwFlags) PURE;
	STDMETHOD(OpenRemoteMachineGPO) (THIS_ LPOLESTR pszComputerName, DWORD dwFlags) PURE;
	STDMETHOD(Save) (THIS_ BOOL bMachine, BOOL bAdd,GUID *pGuidExtension, GUID *pGuid) PURE;
	STDMETHOD(Delete) (THIS) PURE;
	STDMETHOD(GetName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetDisplayName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(SetDisplayName) (THIS_ LPOLESTR pszName) PURE;
	STDMETHOD(GetPath) (THIS_ LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetDSPath) (THIS_ DWORD dwSection, LPOLESTR pszPath ,int cchMaxPath) PURE;
	STDMETHOD(GetFileSysPath) (THIS_ DWORD dwSection, LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetRegistryKey) (THIS_ DWORD dwSection, HKEY *hKey) PURE;
	STDMETHOD(GetOptions) (THIS_ DWORD *dwOptions) PURE;
	STDMETHOD(SetOptions) (THIS_ DWORD dwOptions, DWORD dwMask) PURE;
	STDMETHOD(GetType) (THIS_ GROUP_POLICY_OBJECT_TYPE *gpoType) PURE;
	STDMETHOD(GetMachineName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetPropertySheetPages) (THIS_ HPROPSHEETPAGE **hPages, UINT *uPageCount) PURE;
};
typedef IGroupPolicyObject *LPGROUPPOLICYOBJECT;

BOOL SetLGP(BOOL bRestore, BOOL* bExistingKey, const char* szPath, const char* szPolicy, DWORD dwValue)
{
	LONG r;
	DWORD disp, regtype, val=0, val_size=sizeof(DWORD);
	HRESULT hr;
	IGroupPolicyObject* pLGPO;
	// Along with global 'existing_key', this static value is used to restore initial state
	static DWORD original_val;
	HKEY path_key = NULL, policy_key = NULL;
	// MSVC is finicky about these ones => redefine them
	const IID my_IID_IGroupPolicyObject = 
		{ 0xea502723, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	const IID my_CLSID_GroupPolicyObject = 
		{ 0xea502722, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	GUID ext_guid = REGISTRY_EXTENSION_GUID;
	// Can be anything really
	GUID snap_guid = { 0x3D271CFC, 0x2BC6, 0x4AC2, {0xB6, 0x33, 0x3B, 0xDF, 0xF5, 0xBD, 0xAB, 0x2A} };

	// We need an IGroupPolicyObject instance to set a Local Group Policy
	hr = CoCreateInstance(&my_CLSID_GroupPolicyObject, NULL, CLSCTX_INPROC_SERVER, &my_IID_IGroupPolicyObject, (LPVOID*)&pLGPO);
	if (FAILED(hr)) {
		uprintf("SetLGP: CoCreateInstance failed; hr = %x\n", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->OpenLocalMachineGPO(pLGPO, GPO_OPEN_LOAD_REGISTRY);
	if (FAILED(hr)) {
		uprintf("SetLGP: OpenLocalMachineGPO failed - error %x\n", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->GetRegistryKey(pLGPO, GPO_SECTION_MACHINE, &path_key);
	if (FAILED(hr)) {
		uprintf("SetLGP: GetRegistryKey failed - error %x\n", hr);
		goto error;
	}

	// The DisableSystemRestore is set in Software\Policies\Microsoft\Windows\DeviceInstall\Settings
	r = RegCreateKeyExA(path_key, szPath, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE,
		NULL, &policy_key, &disp);
	if (r != ERROR_SUCCESS) {
		uprintf("SetLGP: Failed to open LGPO path %s - error %x\n", szPath, hr);
		goto error;
	}

	if ((disp == REG_OPENED_EXISTING_KEY) && (!bRestore) && (!(*bExistingKey))) {
		// backup existing value for restore
		*bExistingKey = TRUE;
		regtype = REG_DWORD;
		r = RegQueryValueExA(policy_key, szPolicy, NULL, &regtype, (LPBYTE)&original_val, &val_size);
		if (r == ERROR_FILE_NOT_FOUND) {
			// The Key exists but not its value, which is OK
			*bExistingKey = FALSE;
		} else if (r != ERROR_SUCCESS) {
			uprintf("SetLGP: Failed to read original %s policy value - error %x\n", szPolicy, r);
		}
	}

	if ((!bRestore) || (*bExistingKey)) {
		val = (bRestore)?original_val:dwValue;
		r = RegSetValueExA(policy_key, szPolicy, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
	} else {
		r = RegDeleteValueA(policy_key, szPolicy);
	}
	if (r != ERROR_SUCCESS) {
		uprintf("SetLGP: RegSetValueEx / RegDeleteValue failed - error %x\n", r);
	}
	RegCloseKey(policy_key);
	policy_key = NULL;

	// Apply policy
	hr = pLGPO->lpVtbl->Save(pLGPO, TRUE, (bRestore)?FALSE:TRUE, &ext_guid, &snap_guid);
	if (r != S_OK) {
		uprintf("SetLGP: Unable to apply %s policy - error %x\n", szPolicy, hr);
		goto error;
	} else {
		if ((!bRestore) || (*bExistingKey)) {
			uprintf("SetLGP: Successfully %s %s policy to 0x%08X\n", (bRestore)?"restored":"set", szPolicy, val);
		} else {
			uprintf("SetLGP: Successfully removed %s policy key\n", szPolicy);
		}
	}

	RegCloseKey(path_key);
	pLGPO->lpVtbl->Release(pLGPO);
	return TRUE;

error:
	if (path_key != NULL) RegCloseKey(path_key);
	if (policy_key != NULL) RegCloseKey(policy_key);
	if (pLGPO != NULL) pLGPO->lpVtbl->Release(pLGPO);
	return FALSE;
}
#pragma pop_macro("INTERFACE")
