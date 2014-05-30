#include "myipinfo.h"

#include <boost/algorithm/string.hpp>
#include <regex>
#include <iostream>

myipinfo::myipinfo(boost::asio::io_service &ioservice)
	: m_io_service(ioservice)
	, m_stream(new avhttp::http_stream(ioservice) )
{
	m_content.reserve(1024);
	clear();
}

void myipinfo::clear()
{
	m_ip.s_addr = 0;
	m_downflow = 0;
	m_upflow   = 0;
	m_bInfoState = false;
	boost::system::error_code ec;
	m_stream->close(ec);
}

void myipinfo::reset()
{
	clear();
	m_stream.reset(new avhttp::http_stream(m_io_service));
}

bool myipinfo::checkstate()
{
	return m_bInfoState;
}

void myipinfo::asyn_getinfo(const std::string &url)
{
	m_stream->async_open(url, 
		boost::bind(&myipinfo::handle_open, shared_from_this(), boost::asio::placeholders::error));
}

void myipinfo::handle_open(const boost::system::error_code &ec)
{
	if (!ec){
		m_stream->async_read_some(boost::asio::buffer(m_buffer), 
			boost::bind(&myipinfo::handle_read, shared_from_this(), boost::asio::placeholders::bytes_transferred, boost::asio::placeholders::error));
	}
}

void myipinfo::handle_read(int bytes_transferred, const boost::system::error_code &ec)
{
	if (!ec)
	{
		// std::cout.write(m_buffer.data(), bytes_transferred);
		m_content.insert(m_content.end(), m_buffer.begin(), m_buffer.begin() + bytes_transferred );
		m_stream->async_read_some(boost::asio::buffer(m_buffer),
				boost::bind(&myipinfo::handle_read, shared_from_this(),
				boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error
			)
		);
	}
	else if (ec == boost::asio::error::eof)
	{
		// std::cout.write(m_buffer.data(), bytes_transferred);
		// std::cout.flush();
		m_content.insert(m_content.end(), m_buffer.begin(), m_buffer.begin() + bytes_transferred );
		m_io_service.post(boost::bind(&myipinfo::handle_charset, shared_from_this()));
	}
}

void myipinfo::handle_charset()
{
	avhttp::response_opts opt = m_stream->response_options();
	std::string contentType = opt.find("Content-Type");
	if (!contentType.empty() && contentType.find("charset=") != std::string::npos)
	{
		boost::to_lower(contentType);
		if (contentType.find("utf-8") != std::string::npos)
		{
			int count = MultiByteToWideChar(CP_UTF8, 0, m_content.data(), m_content.size(), NULL, 0);
			
			std::vector<wchar_t> unicodeData(count, 0);

			MultiByteToWideChar(CP_UTF8, 0, m_content.data(), m_content.size(), unicodeData.data(), count);

			if (unicodeData.back() != 0)
				unicodeData.push_back(0);
			
			std::move(unicodeData.begin(), unicodeData.end(), std::back_inserter(m_decodedContent));

			m_io_service.post(boost::bind(&myipinfo::handle_data, shared_from_this()));

			// 清理m_content
			std::vector<char> tmp;
			m_content.swap(tmp);
		}
		else
		{
			// 编码咋变了？
			;
		}
	}
}

void myipinfo::handle_data()
{
	if (m_decodedContent.empty()) return;

	try
	{
		// OK，在这里我们取得ip,使用的流量等信息
		std::wregex ip_expr(L"ip地址:\\s*([\\d\\.]*)$", std::regex_constants::ECMAScript | std::regex_constants::icase);
		std::wsmatch match;
		bool bExists = std::regex_search(m_decodedContent, match, ip_expr);

		
		if (bExists)
		{
			std::wstring curip = match[1].str();
			std::vector<std::wstring> parts;
			boost::split(parts, curip, boost::is_any_of(L"."));
			if (parts.size() != 4) return;

			boost::array<int, 4> ip;
			std::transform(parts.begin(), parts.end(), ip.begin(), static_cast<int(*)(const std::wstring&)>(boost::lexical_cast<int, const std::wstring&>));

			m_ip.s_net = (u_char)ip[0];
			m_ip.s_host = (u_char)ip[1];
			m_ip.s_lh = (u_char)ip[2];
			m_ip.s_impno = (u_char)ip[3];
		}
		else return;

		std::wregex downflow_expr(L"下载流量:\\s*([\\d\\.]*)\\sMB", std::regex_constants::ECMAScript | std::regex_constants::icase);
		bExists = std::regex_search(m_decodedContent, match, downflow_expr);
		if (bExists)
		{
			std::wstring downflow = match[1].str();
			m_downflow = boost::lexical_cast<double>(downflow);
		}
		else
			return;

		std::wregex up_expr(L"上传流量:\\s*([\\d\\.]*)\\sMB", std::regex_constants::ECMAScript | std::regex_constants::icase);
		bExists = std::regex_search(m_decodedContent, match, up_expr);
		if (bExists)
		{
			std::wstring upflow = match[1].str();
			m_upflow = boost::lexical_cast<double>(upflow);
		}
		else
			return;
	}
	catch (...)
	{
		return;
	}

	m_bInfoState = true;
}