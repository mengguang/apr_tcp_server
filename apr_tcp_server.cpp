#include <apr.h>
#include <apr_general.h>
#include <apr_network_io.h>
#include <apr_thread_proc.h>
#include <apr_poll.h>

#include <iostream>
#include <cstdint>

using namespace std;

apr_pool_t* server_pool;

void process_request(uint8_t* request, size_t request_length,
	uint8_t* response, size_t* response_length)
{
	auto n_copy = request_length;
	if (*response_length < request_length)
	{
		n_copy = *response_length;
	}
	memcpy(response, request, n_copy);
	*response_length = n_copy;
}

bool read_request(apr_socket_t* socket, uint8_t* request, size_t* request_length)
{
	apr_pool_t* read_request_pool;
	apr_pool_create(&read_request_pool, nullptr);

	size_t request_offset = 0;
	while (true)
	{
		apr_pollfd_t pfd_read = {
			read_request_pool, APR_POLL_SOCKET,APR_POLLIN,
			0, {nullptr}, nullptr
		};
		pfd_read.desc.s = socket;
		apr_int32_t nsds = 0;
		apr_poll(&pfd_read, 1, &nsds, 100000);
		if (request_offset >= *request_length)
		{
			cerr << "read buffer overflow, close connection" << endl;
			apr_socket_close(socket);
			break;
		}
		apr_size_t n_read = *request_length - request_offset;
		apr_socket_recv(socket, (char*)request + request_offset, &n_read);
		cerr << "n_read: " << n_read << ", nsds: " << nsds << endl;
		if (nsds > 0 && n_read == 0)
		{
			cerr << "read error, close connection" << endl;
			apr_socket_close(socket);
			break;
		}
		if (nsds == 0)
		{
			continue;
		}
		request_offset += n_read;
		if (request[request_offset - 1] == '\n')
		{
			*request_length = request_offset;
			apr_pool_destroy(read_request_pool);
			return true;
		}
	}
	apr_pool_destroy(read_request_pool);
	return false;
}

bool write_response(apr_socket_t* socket, uint8_t* response, size_t response_length)
{
	apr_pool_t* write_response_pool;
	apr_pool_create(&write_response_pool, nullptr);

	size_t write_offset = 0;
	while (write_offset < response_length)
	{
		size_t n_write = response_length - write_offset;
		apr_pollfd_t pfd_write = {
			write_response_pool, APR_POLL_SOCKET,APR_POLLOUT,
			0, {nullptr}, nullptr
		};
		pfd_write.desc.s = socket;
		apr_int32_t nsds_write = 0;
		apr_poll(&pfd_write, 1, &nsds_write, 100000);
		apr_socket_send(socket, (char*)response + write_offset, &n_write);
		cerr << "n_write: " << n_write << ", nsds: " << nsds_write << endl;
		if (nsds_write > 0 && n_write == 0)
		{
			cerr << "write error, close connection" << endl;
			apr_socket_close(socket);
			apr_pool_destroy(write_response_pool);
			return false;
		}
		write_offset += n_write;
	}
	apr_pool_destroy(write_response_pool);
	return true;
}

void* APR_THREAD_FUNC worker_function(apr_thread_t* thread_id, void* data)
{
	cerr << "new thread" << endl;
	uint8_t request[4096] = { 0 };
	uint8_t response[4096] = { 0 };
	auto s = (apr_socket_t*)data;
	apr_socket_opt_set(s, APR_SO_NONBLOCK, 1);
	apr_socket_timeout_set(s, 0);
	bool result;

	while (true)
	{
		auto request_length = sizeof(request);
		result = read_request(s, request, &request_length);
		if (!result)
		{
			apr_thread_exit(thread_id, APR_EGENERAL);
			return nullptr;
		}

		cerr << "request: " << (const char*)request << endl;

		size_t response_length = sizeof(response);
		process_request(request, request_length, response, &response_length);

		result = write_response(s, response, response_length);
		if (!result)
		{
			apr_thread_exit(thread_id, APR_EGENERAL);
			return nullptr;
		}
		memset(request, 0, sizeof(request));
	}
}

void process_new_connection(apr_socket_t* ns, apr_pool_t* worker_pool)
{
	cerr << "new connection" << endl;
	apr_thread_t* thread_id;
	apr_thread_create(&thread_id, nullptr,
		worker_function, (void*)ns, worker_pool);
	apr_thread_detach(thread_id);
}

int main()
{
	//apr_status_t result;
	apr_initialize();
	const apr_port_t server_port = 9688;

	apr_pool_create(&server_pool, nullptr);
	apr_socket_t* server_socket;
	apr_socket_create(&server_socket, APR_INET,
		SOCK_STREAM, APR_PROTO_TCP, server_pool);

	apr_sockaddr_t* sa;

	apr_sockaddr_info_get(&sa, nullptr, APR_INET, server_port,
		0, server_pool);
	apr_socket_bind(server_socket, sa);
	apr_socket_opt_set(server_socket, APR_SO_REUSEADDR, 1);
	apr_socket_listen(server_socket, SOMAXCONN);
	while (true)
	{
		apr_socket_t* ns;
		apr_socket_accept(&ns, server_socket, server_pool);
		process_new_connection(ns, server_pool);
	}
	// apr_pool_destroy(server_pool);
	// apr_terminate();
	// return 0;
}
