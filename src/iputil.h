#ifndef IP_UTIL_H__
#define IP_UTIL_H__

#include <windows.h>
#include <IPHlpApi.h>
#include <memory>

namespace iputil {

// 判断一个ip地址是否被占用
bool is_ip_available(const in_addr &dest);

// 获取包含指定ip的适配器接口
std::shared_ptr<IP_ADAPTER_INFO> getAdapterInfo(const in_addr &ip, IP_ADDR_STRING *&pOutAddr);

// 从inaddr开始，查找一个可用的ip
in_addr find_available_ip(in_addr inaddr);

// 替换ip
DWORD replace_ip(std::shared_ptr<IP_ADAPTER_INFO> &adapterInfo, const in_addr &oldIp, const in_addr &newIp);
DWORD replace_ip(DWORD adapterIndex, DWORD oldContext, const in_addr &newIp, const IPMask &newMask);
class ip_manager{
	
};
}

#endif