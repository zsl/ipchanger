#include "iputil.h"

#include <vector>
#include <cassert>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace iputil{

bool is_ip_available(const in_addr &dest)
{
	ULONG macaddr[2];
	ULONG phyaddrlen = 6;
	if (NO_ERROR == ::SendARP(dest.s_addr, INADDR_ANY, macaddr, &phyaddrlen))
		return false;

	return true;
}

std::shared_ptr<IP_ADAPTER_INFO> getAdapterInfo(const in_addr &ip, IP_ADDR_STRING *&pOutAddr)
{
	std::shared_ptr<IP_ADAPTER_INFO> result;

	std::vector<unsigned char> data;
	IP_ADAPTER_INFO *pAdapterInfo = NULL;
	ULONG ulOutBufLen = 0;
	if (GetAdaptersInfo(NULL, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
		data.reserve(ulOutBufLen);
		pAdapterInfo = reinterpret_cast<IP_ADAPTER_INFO*>(data.data());
	}

	if (!pAdapterInfo) return result;

	DWORD ret;
	ret = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
	if (ret != NO_ERROR) return result;

	IP_ADAPTER_INFO *pAdatper = pAdapterInfo;
	while (pAdatper)
	{
		IP_ADDR_STRING *pAddr = &(pAdatper->IpAddressList);
		while (pAddr)
		{
			if (ip.s_addr == inet_addr(pAddr->IpAddress.String))
			{
				pOutAddr = pAddr;
				break;
			}
			pAddr = pAddr->Next;
		}
		if (pAddr) break;
		pAdatper = pAdatper->Next;
	}

	// 找到了
	if (pAdatper)
	{
		result.reset(new IP_ADAPTER_INFO);
		memcpy(result.get(), pAdatper, sizeof IP_ADAPTER_INFO);
		result->Next = NULL;
	}

	return result;
}

in_addr find_available_ip(in_addr inaddr)
{
	// 可能会进入死循环，每个ip都被占用了！！！
	while (!is_ip_available(inaddr))
	{
		inaddr.s_impno++;
		if (inaddr.s_impno >= 255)
			inaddr.s_impno = 2; // 1一般作为网关，所以从2开始
	}

	return inaddr;
}

DWORD replace_ip(std::shared_ptr<IP_ADAPTER_INFO> &adapterInfo, const in_addr &oldIp, const in_addr &newIp)
{
	// 先增加新ip，再删除旧ip

	IP_ADDR_STRING *pAddr = &(adapterInfo->IpAddressList);
	while (pAddr)
	{
		if (oldIp.s_addr == inet_addr(pAddr->IpAddress.String))
		{
			break;
		}
		pAddr = pAddr->Next;
	}

	if (!pAddr) return 0;

	ULONG NTEContext = 0;
	ULONG NTEInstance = 0;
	DWORD ret = ::AddIPAddress(newIp.s_addr, inet_addr(pAddr->IpMask.String), adapterInfo->Index, &NTEContext, &NTEInstance);
	if (NO_ERROR != ret) return 0;

	ret = ::DeleteIPAddress(pAddr->Context);
	if (ret != NO_ERROR)
	{
		ret = ::DeleteIPAddress(NTEContext);
		NTEContext = 0;
	}

	return NTEContext;
}

DWORD replace_ip(DWORD adapterIndex, DWORD oldContext, const in_addr &newIp, const IPMask &newMask)
{
	ULONG NTEContext = 0;
	ULONG NTEInstance = 0;
	DWORD ret = ::AddIPAddress(newIp.s_addr, newMask, adapterIndex, &NTEContext, &NTEInstance);
	if (NO_ERROR != ret) return 0;

	ret = ::DeleteIPAddress(oldContext);
	if (ret != NO_ERROR) return 0;

	return NTEContext;
}

bool reg_replace_ip(std::shared_ptr<IP_ADAPTER_INFO> &adapterInfo, const in_addr &oldIp, const in_addr &newIp)
{
	std::string regkey = "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\";
	regkey += adapterInfo->AdapterName;

	HKEY hKey = 0;
	if (ERROR_SUCCESS != ::RegOpenKeyExA(HKEY_LOCAL_MACHINE, regkey.c_str(), 0, KEY_READ|KEY_WRITE, &hKey))
	{
		return false;
	}

	std::shared_ptr<void> hkeyCloser(static_cast<void*>(hKey), [](void * key){ RegCloseKey(reinterpret_cast<HKEY>(key)); });

	// 先取到ip，然后替换，再然后设置
	DWORD dwType = REG_MULTI_SZ;		
	DWORD ipLen = 0;
	LONG ret = 0;
	ret = RegQueryValueExA(hKey,"IPAddress", NULL, &dwType, (LPBYTE)NULL, &ipLen);

	char *ips = NULL;
	std::vector<char> buf;
	if (ret == ERROR_SUCCESS || ret != ERROR_SUCCESS && ret == ERROR_MORE_DATA)
	{
		buf.reserve(ipLen);
		ips = reinterpret_cast<char*>(buf.data());
	}
	else return false;

	ret = RegQueryValueExA(hKey,"IPAddress", NULL, &dwType, (LPBYTE)ips, &ipLen);

	std::vector<std::string> vecIps;
	char *ip = ips;
	while (*ip)
	{
		if (inet_addr(ip) == oldIp.s_addr)
			vecIps.push_back(inet_ntoa(newIp));
		else
			vecIps.push_back(ip);

		ip += strlen(ip) + 1;
	}

	size_t bufcount = 1;
	for (auto &sip : vecIps) bufcount += sip.size() + 1;

	buf.reserve(bufcount);

	ips = reinterpret_cast<char *>(buf.data());
	size_t bufindex = 0;
	for (auto &sip : vecIps)
	{
		memcpy(ips, sip.c_str(), sip.size());
		bufindex += sip.size();
		ips[bufindex++] = 0;
	}

	ips[bufindex++] = 0;
	assert(bufindex == bufcount);

	ret = ::RegSetValueExA(hKey, "IPAddress", 0, dwType, (BYTE*)ips, bufcount);

	if (ret != ERROR_SUCCESS)
		return false;

	return true;
}

bool apply_change(char *lpszAdapterName)
{
	return true;
// 	typedef BOOL (DHCPNOTIFYPROC*)(
// 		LPWSTR lpwszServerName, // 本地机器为NULL
// 		LPWSTR lpwszAdapterName, // 适配器名称
// 		BOOL bNewIpAddress, // TRUE表示更改IP
// 		DWORD dwIpIndex, // 指明第几个IP地址，如果只有该接口只有一个IP地址则为0
// 		DWORD dwIpAddress, // IP地址
// 		DWORD dwSubNetMask, // 子网掩码
// 		int nDhcpAction ); // 对DHCP的操作 0:不修改, 1:启用 DHCP，2:禁用 DHCP

//     BOOL            bResult = FALSE;  
//     HINSTANCE       hDhcpDll;  
//     DHCPNOTIFYPROC  pDhcpNotifyProc;  
//     WCHAR wcAdapterName[256];  
//       
//     MultiByteToWideChar(CP_ACP, 0, lpszAdapterName, -1, wcAdapterName,256);  
//   
//     if((hDhcpDll = LoadLibrary("dhcpcsvc")) == NULL)  
//         return FALSE;  
//   
//     if((pDhcpNotifyProc = (DHCPNOTIFYPROC)GetProcAddress(hDhcpDll, "DhcpNotifyConfigChange")) != NULL)  
//         if((pDhcpNotifyProc)(NULL, wcAdapterName, TRUE, nIndex, inet_addr(pIPAddress), inet_addr(pNetMask), 0) == ERROR_SUCCESS)  
//             bResult = TRUE;  
//   
//     FreeLibrary(hDhcpDll);  
//     return bResult;  
}

}