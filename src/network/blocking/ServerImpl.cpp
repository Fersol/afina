#include "ServerImpl.h"
#include "../../protocol/Parser.h"
#include <afina/execute/Command.h>


#include <sstream>
#include <algorithm>

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>

namespace Afina {
namespace Network {
namespace Blocking {

static const ssize_t BUF_SIZE = 2;

void *ServerImpl::RunConnectionProxy(void *p) {
    ServerImpl *srv;
    int client_socket;

    auto args = reinterpret_cast<RunConnectionProxyArgs *>(p);
    std::tie(srv, client_socket) = *args;
    //delete args;

    try {
        srv->RunConnection(client_socket);
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }

    
    close(client_socket);
    std::unique_lock<std::mutex> lock(srv->connections_mutex);
    srv->connections.erase(pthread_self());
    srv->connections_cv.notify_all();

    return 0;
}



// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunAcceptorProxy, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }

}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
    shutdown(server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);
}

void *ServerImpl::RunAcceptorProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        srv->RunAcceptor();
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makesthe OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);
    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(client_socket);
            throw std::runtime_error("Socket accept() failed");
        }
           
        {   
            std::lock_guard<std::mutex> lock(connections_mutex);
            if (connections.size() < max_workers){
                pthread_t worker;
                auto args = new RunConnectionProxyArgs(this, client_socket);
                if (pthread_create(&worker, NULL, ServerImpl::RunConnectionProxy, args) < 0) {
                    //if (server.state != Run) {break;}
                    throw std::runtime_error("Could not create server thread");
                }
                connections.insert(worker);
            } 
            else {
                close(client_socket);
            }
        }

    }

    std::unique_lock<std::mutex> lock(connections_mutex);
    while(connections.size() != 0){
      connections_cv.wait(lock);
    }
    // Cleanup on exit...
    close(server_socket);
}

// See Server.h
void ServerImpl::RunConnection(int client_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    char buf[BUF_SIZE];
    Protocol::Parser parser;

    ssize_t buf_readed;

    while ((buf_readed = recv(client_socket, buf, BUF_SIZE, 0)) > 0) {
        uint32_t body_size;
        //std::cout << "bufin:" << buf << "|" <<std::endl;
        size_t parsed;
        if (parser.Parse(buf, buf_readed, parsed)) {
            
            
            auto command = parser.Build(body_size);
            //std::cout << "build:" << body_size << "\n";
            //write start of value to args
            std::string args;
            for(auto pos = parsed; body_size >0 &&  pos < buf_readed; pos++){
                args.push_back(buf[pos]);
                body_size--;
            } 
            //std::cout << "argsfirst:" << args << std::endl;
            if (body_size > 0){
              ReadData(client_socket, args, body_size); 
            }
        
            //std::cout << "args:" << args << std::endl;
            //for(auto it = parser.keys.begin(), it != parser.keys.end(); it++){
             //   args += *it + " ";
           // }
            std::string out;
   
            try {
                command->Execute(*pStorage, args, out);
                out += "\r\n";
            } catch (std::runtime_error &ex) {
                out = std::string("SERVER_ERROR ") + ex.what() + "\r\n";
            }

            if (send(client_socket, out.c_str(), out.size(), 0) <= 0) {
                throw std::runtime_error("Socket send() failed");
            }
            //std::cout << "prereset" << std::endl;
            parser.Reset();
        }
    }

    if (buf_readed == -1) {
        throw std::runtime_error("Socket recv() failed");
    }
}


std::string ServerImpl::ReadData(int client_socket, std::string& args, uint32_t& body_size) {
  size_t pos;
  //size_t skip = 0;  // \r\n
  char buf[BUF_SIZE];
  ssize_t buf_readed; 
  bool isend = false;
  //std::cout << "readdata: " << std::endl;

  while (!isend && (buf_readed = recv(client_socket, buf, BUF_SIZE, 0)) > 0) {
    //std::cout << "buf:" << args << "size: "<< buf_readed << std::endl;
    for (pos = 0; pos < buf_readed; pos++) {
          char c = buf[pos];
          if (body_size > 0){
              args.push_back(c);
              body_size--;
          }
          else{
            //std::cout << "c:" << int(c) << std::endl;
            if (c == '\n'){
                isend = true;
              break;
            }
          }
    }  
  }
  //std::cout << "args:" << args <<std::endl;
}



} // namespace Blocking
} // namespace Network
} // namespace Afina
