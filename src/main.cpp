#include <iostream>
#include <boost/program_options.hpp>

#include "config.h"
#include "myipinfo.h"
#include "iputil.h"

namespace po = boost::program_options;

void exit_with_info(std::string info, int exitCode = 1)
{
	info.append("\n请按回车键退出...\n");
	std::cout << info;
	getchar();
	exit(exitCode);
}

void print_ip_info(std::shared_ptr<myipinfo> &info)
{
	const in_addr &ip = info->ip();
	std::string strip = inet_ntoa(ip);
	std::cout << "您的ip地址: " << strip << std::endl;
	std::cout << "当前下载流量: "  << info->downflow() << "MB" << std::endl;
	std::cout << "当前上传流量: "  << info->upflow() << "MB" << std::endl;
}

enum{err_success, err_replaceip, err_checkstate};
int changeip(std::shared_ptr<myipinfo> &info, std::shared_ptr<IP_ADAPTER_INFO> &adapter)
{
	std::cout << "\n正在查找一个可用的ip...\n";
	in_addr newaddr = iputil::find_available_ip(info->ip());
	std::string strAddr = inet_ntoa(newaddr);
	std::cout << "find it: " << strAddr << std::endl;
	std::cout << "开始修改ip...\n";
	DWORD context = iputil::replace_ip(adapter, info->ip() , newaddr);
	if (!context) return err_replaceip;

	return err_success;
}

void check_changeip(boost::asio::deadline_timer *timer, std::shared_ptr<myipinfo> &info, std::shared_ptr<IP_ADAPTER_INFO> &adapter)
{
	int bRet = err_success;
	while (bRet == err_replaceip || info->downflow() > 900 || info->upflow() > 900){
		bRet = changeip(info, adapter);
	}

	timer->async_wait(boost::bind(&check_changeip, timer, info, adapter));
}

class ipchanger{
public:
	ipchanger(std::shared_ptr<myipinfo> &info, std::shared_ptr<IP_ADAPTER_INFO> &adapter)
		: m_info(info), m_adapter(adapter)
	{}
public:
	void asyn_start_changeip()
	{
		;
	}
private:
	std::shared_ptr<myipinfo> m_info; 
	std::shared_ptr<IP_ADAPTER_INFO> m_adapter;
};

int main(int argc, char *argv[])
{
	po::options_description opts("usage:ipchanger [-c|-t]\n可用的选项");
	opts.add_options()
		("changeip,c", "直接换一个ip，保证此ip流量足够使用")
		("find,f", "检测一个可用的ip, 只保证此ip没有人占用,\n不保证此ip的流量已用完")
		("info,i", "当前ip流量的使用情况")
		("background,b", "此为默认选项，\n在后台运行，每隔5分钟检测一次，如果发现流量用完，则自动切换ip")
		("help,h", "帮助信息")
		;

	po::variables_map vm;
	try{
		po::store(po::parse_command_line(argc, argv, opts), vm);
		po::notify(vm);
	}
	catch(std::exception &e){
		std::cout << e.what() << std::endl;
		std::cout << opts << std::endl;
		return 0;
	}

	if (vm.count("help") != 0 ){
		std::cout << opts << std::endl;
		return 0;
	}

	boost::asio::io_service ioservice;
	std::shared_ptr<myipinfo> info(new myipinfo(ioservice));
	info->asyn_getinfo(ip_checker_server);

	std::cout << "正在检测ip使用状态...\n";

	ioservice.run();
	ioservice.reset();

	if (info->checkstate()){
		print_ip_info(info);
	}
	else {
		exit_with_info("检查ip使用状态失败");
	}

	if ( 0 == vm.size()) goto background;

	if (vm.count("find")){

		std::cout << "\n正在查找一个可用的ip...\n";
		in_addr addr = iputil::find_available_ip(info->ip());
		std::string strAddr = inet_ntoa(addr);
		std::cout << "find it: " << strAddr << std::endl;
	}
	else if (vm.count("changeip")){
		IP_ADDR_STRING *pAddr = NULL;
		std::shared_ptr<IP_ADAPTER_INFO> adapter = iputil::getAdapterInfo(info->ip(), pAddr);

		if (!adapter) exit_with_info("获取适配器信息失败");
		if (adapter->DhcpEnabled == TRUE)
			exit_with_info("动态分配的ip，不能修改");

		DWORD adapterIndex = adapter->Index;
		DWORD oldContext = pAddr->Context;
		DWORD netmask = inet_addr(pAddr->IpMask.String);

		do {
			int ret = changeip(info, adapter);
			if (err_replaceip == ret) exit_with_info("修改ip失败");
			std::cout << "ip修改成功!!!\n请等待5秒钟...\n";
			::Sleep(5000);

			std::cout << "开始重新检测ip使用状态...\n";
			info->clear();
			info.reset(new myipinfo(ioservice));
			info->asyn_getinfo(ip_checker_server);

			ioservice.run();
			ioservice.reset();

			if (info->checkstate()){
				print_ip_info(info);
			}
			else {
				exit_with_info("检查ip使用状态失败");
			}

			
		} while (info->downflow() > 100 || info->upflow() > 100);

		exit_with_info("已经切换到合适的Ip :)", 0);
	}
	else if (vm.count("background")){
background:

checkandset:
		// check_changeip(&timer, info, adapter);
		while (info->downflow() > 900 || info->upflow() > 900)
		{
			IP_ADDR_STRING *pAddr = NULL;
			std::shared_ptr<IP_ADAPTER_INFO> adapter = iputil::getAdapterInfo(info->ip(), pAddr);

			if (!adapter) exit_with_info("获取适配器信息失败");
			if (adapter->DhcpEnabled == TRUE)
				exit_with_info("动态分配的ip，不能修改");

			int ret = changeip(info, adapter);
			if (err_replaceip == ret) exit_with_info("修改ip失败");
			std::cout << "ip修改成功!!!\n请等待5秒钟...\n";
			::Sleep(5000);

			std::cout << "开始重新检测ip使用状态...\n";
			// info->reset();
			info->clear();
			info.reset(new myipinfo(ioservice));
			info->asyn_getinfo(ip_checker_server);

			ioservice.run();
			ioservice.reset();

			if (info->checkstate()){
				print_ip_info(info);
			}
			else {
				exit_with_info("检查ip使用状态失败");
			}
		}
		{
			boost::asio::deadline_timer timer(ioservice);
			boost::posix_time::ptime expertime = boost::posix_time::second_clock::local_time();
			expertime += boost::posix_time::minutes(5);

			std::cout << "将在 " << boost::posix_time::to_simple_string(expertime) << " 后进行下一次检查\n";

			timer.expires_from_now(boost::posix_time::minutes(5));

			boost::system::error_code err;
			timer.wait(err);
		}

		info->clear();
		info.reset(new myipinfo(ioservice));
		info->asyn_getinfo(ip_checker_server);

		ioservice.run();
		ioservice.reset();

		if (info->checkstate()){
			print_ip_info(info);
		}
		else {
			exit_with_info("检查ip使用状态失败");
		}
		goto checkandset;
	}

}