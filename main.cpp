#define _WIN32_WINNT 0x0A00  
#pragma warning(disable:4996)

#include <iostream>
#include <chrono>
#include <atomic>
#include <queue>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>


#include "date.h"

template <class T>
class ThreadSafeQueue {
public:
	auto Enqueue(T t) -> void {
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Queue.push(t);
		m_CondVar.notify_one();
	}	
	auto Dequeue() -> T {
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_CondVar.wait(lock, [&] { return !m_Queue.empty(); });
		T val = m_Queue.front();
		m_Queue.pop();
		return val;
	}

private:
	std::queue<T>           m_Queue;
	mutable std::mutex      m_Mutex;
	std::condition_variable m_CondVar;
};


struct ApplicationDesc {
	std::string   Address = "127.0.0.1";
	std::uint16_t Port    = 123;
	std::uint64_t Time    = 2;

	static ApplicationDesc ParseCommandLine(int argc, char* argv[]) {
		namespace po = boost::program_options;
		po::options_description desc("Options");

		ApplicationDesc app{};

		desc.add_options()
			("help,h",   "Procedure help message")
			("address,a", po::value<std::string>()->default_value(app.Address), "Input IP adress")
			("port,p",    po::value<uint16_t>()->default_value(app.Port), "Input port")
			("time,t",    po::value<uint64_t>()->default_value(app.Time), "Input delta time");
		
		po::variables_map vm;
		po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();

		po::store(parsed, vm);
		po::notify(vm);

		app.Address = vm["address"].as<std::string>();
		app.Port    = vm["port"].as<uint16_t>();
		app.Time    = vm["time"].as<uint64_t>();
	
		if (vm.count("help"))
			std::cout << desc << std::endl;

		return app;

	}
};

class Application {
public:
	Application(ApplicationDesc const& desc) {
		m_IsRun = false;
		m_IsExit = false;
		m_pIOService = std::make_unique<boost::asio::io_service>(2);
		m_pUDPSock = std::make_unique<boost::asio::ip::udp::socket>(*m_pIOService);
		m_pUDPSock->open(boost::asio::ip::udp::v4());
		m_pUDPSock->bind({ boost::asio::ip::address::from_string(desc.Address), desc.Port });
	}
	~Application() {
		this->Shutdown();
	}

	auto IsRun() const -> bool {
		return m_IsRun;
	}
	auto Run() -> void {	
		m_IsRun = true;
		using ItemType = std::pair<std::chrono::time_point<std::chrono::system_clock>, boost::asio::ip::udp::endpoint>;
		std::cout << "Application run ";
		std::cout << " IP: " << m_pUDPSock->local_endpoint().address().to_string();
		std::cout << " Port: " << m_pUDPSock->local_endpoint().port() << std::endl;

		ThreadSafeQueue<ItemType> queue;
		std::thread threadWrite([&] {
			try {
				while (m_IsRun) {
					using namespace date;
					boost::asio::streambuf buffer;
					std::ostream os(&buffer);
				
					auto [time, endpoint] = queue.Dequeue();
					os << time;
					std::cout << endpoint << " -> "<< time << std::endl;
					m_pUDPSock->send_to(boost::asio::buffer(buffer.data(), buffer.size()), endpoint);
					
				}
			} catch (const std::exception& e) {
				m_IsRun = false;
				std::cerr << e.what();
			}

		});

		std::thread threadRead([&] {

			try {
				while (m_IsRun) {
					std::array<uint8_t, 1024> buffer;
					boost::asio::ip::udp::endpoint endpoint;
					m_pUDPSock->receive_from(boost::asio::buffer(buffer), endpoint);
					queue.Enqueue({ std::chrono::system_clock::now() + std::chrono::seconds(m_DeltaTime), endpoint });
				};
			} catch (const std::exception& e) {
				m_IsRun = false;
				std::cerr << e.what();
			}
		});
		
		threadRead.join();
		threadWrite.join();
	

		m_pUDPSock->shutdown(boost::asio::ip::udp::socket::shutdown_both);
		m_pUDPSock->close();
		m_pIOService->stop();
		m_IsExit = false;
	}

	auto Shutdown() -> void {
		if (m_IsRun) {
			m_IsRun = false;
			boost::asio::ip::udp::socket sock(*m_pIOService);
			sock.send_to(boost::asio::buffer("Enable"), m_pUDPSock->local_endpoint());
			while (!m_IsExit);
		}
	}
private:
	std::unique_ptr<boost::asio::io_service>      m_pIOService;
	std::unique_ptr<boost::asio::ip::udp::socket> m_pUDPSock;
	std::atomic<bool>                             m_IsRun;
	std::atomic<bool>                             m_IsExit;
	uint64_t                                      m_DeltaTime;
};


std::unique_ptr<Application> pApp;

int main(int argc, char* argv[]) {

	try {	
		pApp = std::make_unique<Application>(ApplicationDesc::ParseCommandLine(argc, argv));
		pApp->Run();
	} catch (const std::exception& e) {
		std::cerr << e.what();
	}


}


BOOL WINAPI CtrlHandler(DWORD EventType) {
	
	pApp->Shutdown();

	switch (EventType) {
		case CTRL_C_EVENT:
			return(TRUE);
		case CTRL_CLOSE_EVENT:
			return(TRUE);
		case CTRL_BREAK_EVENT:
			return FALSE;
		case CTRL_LOGOFF_EVENT:
			return FALSE;
		case CTRL_SHUTDOWN_EVENT:
			return FALSE;
		default:
			return FALSE;
	}
}
