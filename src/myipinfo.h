#ifndef MY_IP_INFO_H__
#define MY_IP_INFO_H__

#include <boost/asio.hpp>
#include <avhttp.hpp>

#include <string>
#include <array>
#include <memory>

#include <windows.h>

class myipinfo : public std::enable_shared_from_this<myipinfo>{
public:
	myipinfo(boost::asio::io_service &ioservice);
	void asyn_getinfo(const std::string &url);
	void clear();
	void reset();
	bool checkstate();

public:
	inline const in_addr &ip()const { return m_ip; }
	inline double upflow() { return m_upflow; }
	inline double downflow(){ return m_downflow; }
private:
	void handle_open(const boost::system::error_code &ec);
	void handle_read(int bytes_transferred, const boost::system::error_code &ec);
	void handle_charset();
	void handle_data();

	boost::asio::io_service &get_io_service(){ return m_io_service; }
private:
	boost::asio::io_service &m_io_service;
	std::shared_ptr<avhttp::http_stream> m_stream;

	std::array<char, 1024> m_buffer;
	std::vector<char> m_content;
	std::wstring m_decodedContent;

	in_addr            m_ip;
	double             m_downflow;
	double             m_upflow; 
	bool               m_bInfoState;
};

#endif