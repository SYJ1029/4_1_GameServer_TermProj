#include "pch.h"
#include "server.h"

// Global variable definitions
concurrency::concurrent_priority_queue<event_type> timer_queue;
tbb::concurrent_unordered_map<int, std::atomic<std::shared_ptr<CObject>>> clients;
SectorManager sector_manager;
SOCKET g_server;
HANDLE g_iocp;
std::atomic<int> player_index = 1;

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	g_server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	::bind(g_server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_server, SOMAXCONN);

	InitializeNPC();

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	CreateIoCompletionPort((HANDLE)g_server, g_iocp, -1, 0);

	EXP_OVER accept_over(IO_ACCEPT);
	accept_over.m_client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	AcceptEx(g_server, accept_over.m_client_socket, &accept_over.m_buff, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, &accept_over.m_over);

	vector<thread> worker_threads;
	thread timer_th(timer_thread);
	unsigned int hw_threads = thread::hardware_concurrency();
	int num_threads = hw_threads > 1 ? static_cast<int>(hw_threads - 1) : 1;
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread);

	for (auto& th : worker_threads)
		th.join();
	timer_th.join();

	closesocket(g_server);
	WSACleanup();
}
