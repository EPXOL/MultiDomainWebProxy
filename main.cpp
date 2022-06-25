//General
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <sqlite3.h>
#include <zmqpp/zmqpp.hpp>
//Multi-threding
#include <thread>
//Async functions
#include <future>
//Networking
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

//Global variables
sqlite3 *DB;
string helpString = "    ____________________________________________________________________COMMAND LIST____________________________________________________________________________\n    ) help  <==Lists all commands                                                                                                                              (\n    )__________________________________________________________________________________________________________________________________________________________(\n    )                                                                                                                                                          (\n    ) proxy  <==Proxy server configuration                                                                                                                     (\n    )     |-setport-|  <==Edits port that proxy's listening to                                                                                                 (\n    )          || <port>     <<==Port you want your proxy to listen at, example: 80                                                                            (\n    )     |-getPort-|  <==Outputs port that's set as proxy's listening port                                                                                    (\n    )__________________________________________________________________________________________________________________________________________________________(\n    )                                                                                                                                                          (\n    ) domain                                                                                                                                                   (\n    )      |--list--|  <==Lists all domains registered under this proxy                                                                                        (\n    )                                                                                                                                                          (\n    )      |-create-|  <==Registeres a new domain under this proxy                                                                                             (\n    )            || <domain name>       <==Domain address, examples: \"subdomain.example.com\" or \"example.com\" or \"www.example.com\", etc.                       (\n    )            || <proxy endAddress>  <==Domain/IP Address of server domain will be forwarded to, examples: \"server.myhost.net\" or \"546.414.354.51\"          (\n    )            || <proxy endPort>     <==Port of webserver the doamin will be forwarded to                                                                   (\n    )                                                                                                                                                          (\n    )      |--edit--|  <==Edits specific domain's settings                                                                                                     (\n    )          || <domain name>         <==Domain address | Get all registered domains in \"#>> domain list\"                                                    (\n    )                        || proxyAddress  <==Domain/IP Address of server domain will be forwarded to, examples: \"server.myhost.net\" or \"546.414.354.51\"    (\n    )                        || proxyPort     <==Port of webserver the doamin will be forwarded to                                                             (\n    )                                                                                                                                                          (\n    )     |--delete-|  <==Delets specific domain from register of this proxy                                                                                   (\n    )          || <domain name>         <==Domain address | Get all registered domains in \"#>> domain list\"                                                    (\n    )__________________________________________________________________________________________________________________________________________________________(";
int f = 0;
int *ServerSocket = &f;

//--------Setting up utils
//--Spliting string with character
vector<string> split(string input, char splitChar) {
    istringstream StringStream(input);
    vector<string> list;
    string target;
    while(getline(StringStream, target, splitChar)) {
        list.push_back(target);
    };
    return list;
}
//Pre-Definitions
void server();

//Loading domains
class Domain {
    public:
        string domain;
        string host;
        string port;
        Domain(string dmn, string hst, string prt) {
            domain = dmn;
            host = hst;
            port = prt;
        }
        void Edit(string hst, string prt) {
            host = hst;
            port = prt;
        }
};
map<string, Domain> domains;
int domainLoadCallback(void *data, int argc, char **values, char **names) {
    string domain = values[0];
    string host = values[1];
    string port = values[2];
    Domain dmn(domain, host, port);
    domains.insert(pair<string, Domain>(domain, dmn));
    return 0;
}

//--Callbacks
int initProxyPortCallback(void *data, int argc, char **values, char **names);
int setportCallback(void *data, int argc, char **values, char **names) {
    string &port = *(static_cast<string*>(data));
    if(values[0] != port) {
        string commandString = "UPDATE webproxy_data SET value='" + port + "' WHERE name='port';";
        char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
        sqlite3_exec(DB, command, 0, 0, 0);
        cout << "Port " << port << " has been writen into database restarting proxy server ...\n";
        close(*ServerSocket);
        *ServerSocket = 0;
        cout << "Server has been closed, running new server ...\n";
        server();
        while(true) {
            if(*ServerSocket != 0) break;
        }
    } else {
        cout << "Port unchanged, changes discarded.\n";
    }
    return 0;
}
int getportCallback(void *data, int argc, char **values, char **names) {
    cout << "Current port is set to: " << values[0] << "\n";
    return 0;
}
int editDomainCallback(void *data, int args, char **values, char **names) {
    vector<string> &passedData = *(static_cast<vector<string>*>(data));
    if(stoi(values[0]) == 0) {
        cout << "This domain is not registered under this proxy !\n";
        return 0;
    }
    string commandString = "UPDATE webproxy_domains SET host='"+passedData[1]+"', port='"+passedData[2]+"' WHERE domain='"+passedData[0]+"';";
    char* command = strcpy(new char[commandString.length()], commandString.c_str());
    sqlite3_exec(DB, command, 0, 0, 0);
    domains.find(passedData[0])->second.Edit(passedData[1], passedData[2]);
    cout << "Domain has been edited, you can list all domains by using: \">> domain list\"\n";
    return 0;
}
int deleteDomainCallback(void *data, int argc, char **values, char **names) {
    string &domain = *(static_cast<string*>(data));
    if(stoi(values[0]) == 0) {
        cout << "This domain is not registered under this proxy !\n";
        return 0;
    }
    string commandString = "DELETE FROM webproxy_domains WHERE domain='" + domain + "';";
    char* command = strcpy(new char[commandString.length()+1], commandString.c_str());
    sqlite3_exec(DB, command, 0, 0, 0);
    domains.erase(domain);
    cout << "Domain has been removed from this proxy server.\n";
    return 0;
}

//Networking definitions
//Pre-Define socket piping function for compiler
int pipeTCPSocket(int *from, int to);
//SocketData that we save to `sockets` (map)
class SockData {
    public: 
        int id = 0;
        char initMessage[2000];
        bool closed = false;
        SockData(int *SockID) {
            id = *SockID;
        }
        void Close() {
            if(closed != true) {
                shutdown(id, SHUT_RDWR);
                close(id);
            }
            closed = true;
        }
};
//Define `sockets` (map)
map<int, SockData> sockets;
//TCP Client definition
class ClientTCPSocket {
    public:
      int id = 0;
      int connectionStatus = 0;
      ClientTCPSocket(string *hst, string *prt) {
            int port = stoi(*prt);
            char* host = strcpy(new char[(*hst).length() + 1], (*hst).c_str());
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if(sock < 0) {
                cout << "Error opening socket for clientTCPSocket: " << sock << endl;
                return;
            }
            sockaddr_in server_address;
            bzero((char *) &server_address, sizeof(server_address));
            server_address.sin_family = AF_INET;
            inet_pton(AF_INET, host, &server_address.sin_addr);
            server_address.sin_port = htons(port);

            int connected = connect(sock, (struct sockaddr *) &server_address, sizeof(server_address));
            connectionStatus = connected;
            if(connected < 0) {
                cout << endl << "Error connecting to tcp socket: \"" << *hst << ":" << *prt << "\"" << endl;
                cout << ">> ";
                return;
            }
            id = sock;
      }
};
//Socket custom read function declaration
int readAndAssign(int *from, char* buffer, int *assignTo) {
    *assignTo = read(*from, buffer, 2000);
    return *assignTo;
}
//Define function for init socket message (Header detection and forward)
void processInitSocketMessage(int *sock) {
    char buffer[2000]; // Create buffer of 2000 length for reading out off socket
    bzero(buffer, 2000); // Clear buffer
    int canRead = 0;
    while(readAndAssign(sock, buffer, &canRead) != 0) { // Read 2000 bytes from socket
        if(canRead > 0) {
            //Convert char array to string
            string message = buffer;
            //Check if "Host" header is presnet if not close socket
            string searchString = "Host: ";
            if(!strstr(message.c_str(), searchString.c_str())) {
            };
            //Detect what is the value of "Host: " header
            vector<string> args = split(message, '\n');
            for(string &str: args) {
                vector<string> subarg = split(str, ' ');
                if(subarg[0] == "Host:") {
                    string domain = subarg[1].substr(0, subarg[1].length()-1);
                    if(domains.find(domain)->first.length() > 0) {
                        //getting domain info
                        Domain domainInfo = domains.find(domain)->second;
                        //Making connection to target server
                        ClientTCPSocket remoteSocket(&domainInfo.host, &domainInfo.port);
                        if(remoteSocket.connectionStatus > -1) {
                            SockData RemoteSocketData(&remoteSocket.id);
                            strncpy(sockets.find(*sock)->second.initMessage, buffer, 2001);
                            sockets.insert(pair<int, SockData>(RemoteSocketData.id, RemoteSocketData));
                            //Running piping
                            future<int> remoteToLocalPipe(async(pipeTCPSocket, &RemoteSocketData.id, *sock));
                            future<int> localToRemotePipe(async(pipeTCPSocket, sock, RemoteSocketData.id));
                            write(RemoteSocketData.id, sockets.find(*sock)->second.initMessage, 2000);
                            break;
                        }
                    } else {
                        sockets.find(*sock)->second.Close();
                        break;
                    }
                }
            }
            sockets.find(*sock)->second.Close();
            break;
        } else {
            sockets.find(*sock)->second.Close();
            break;
        }
    }
}
//Socket piping function declaration
int pipeTCPSocket(int *from, int to) {
    char buffer[2000]; // Create buffer of 256 length for reading out off socket
    bzero(buffer, 2000); // Clear buffer

    int size = 0;
    while(readAndAssign(from, buffer, &size) != 0) {
        if(size < 0) {
            sockets.find(to)->second.Close();
            break;
        }
        write(to, buffer, 2000);
    }
    return 0;
}
//Starting TCP server
//Accept and Declare socket identifier into pointer value
int acceptAndDeclare(int sock, int *identifier) {
    //Accept a new connection
    struct sockaddr client_address;
    socklen_t size = sizeof(&client_address);
    *identifier = accept(sock, &client_address, &size);
    return *identifier;
}
//ProxyServer Definition
int proxyServerFinalInition(int port) {
    //Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cout << "Error opening a socket, error code: " << sock << endl;
        return 0;
    }
    //Prevent Address in use at restarts
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    //Make a server address structure with configuration;
    sockaddr_in server_address;
    bzero((char *) &server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);
    //Bind host information to the socket
    bind(sock, (struct sockaddr *) &server_address, sizeof(server_address));
    //Init litening for the socket
    listen(sock, 5);
    if(*ServerSocket != 0) cout << endl;
    cout << "Started up proxy server on port " << port << " !\n";
    ServerSocket = &sock;

    //Await new sockets and accept connection to the server
    int newSocketIdentifier = 0;
    //Accept new sockets
    while(!(acceptAndDeclare(sock, &newSocketIdentifier) < 0)) {
        int pid = fork();
        if(pid == 0) {
            close(sock);
            //Registring socket into sockets memory
            SockData socketData(&newSocketIdentifier);
            sockets.insert(pair<int, SockData>(newSocketIdentifier, socketData));
            //Running host header listener
            processInitSocketMessage(&newSocketIdentifier);
        }
    }
    return 0;
}
//Callback for port on proxy server boot
int initProxyPortCallback(void *data, int argc, char **values, char **names) {
    //Defining port
    int port = stoi(values[0]);

    thread ProxyServerThred(proxyServerFinalInition, port);
    ProxyServerThred.detach();
    return 0;
}
//Proxy server init function definition
void server() {
    cout << "Starting up proxy server..." << endl;
    string commandString = "SELECT value FROM webproxy_data WHERE name='port';";
    char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
    int executed = sqlite3_exec(DB, command, initProxyPortCallback, 0, 0);
    if(executed != SQLITE_OK) {
        cout << "There was an error loading port at proxy startup, please restart the proxy.";
    }
}

//Creating commands functions
class CommandsConstructor {
    public: 
        //Help command declaration
        void help() {
            cout << helpString << endl;
        }
        //Proxy command declaration
        int proxy(string command) {
            vector<string> args = split(command, ' ');
            if(args.size() < 2) {
                cout << "No subcommand specified !\n";
                return 0;
            }
            string subCommand = args[1];
            if(subCommand == "setport") {
                if(args.size() < 3) {
                    cout << "No port specified !\n";
                    return 0;
                }
                if(!stoi(args[2])&&args[2] != "0") {
                    cout << "Port must be a number !\n";
                    return 0;
                }

                //Executing SQlite command
                string commandString = "SELECT value FROM webproxy_data WHERE name='port';";
                char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
                int executed = sqlite3_exec(DB, command, setportCallback, static_cast<void*>(&args[2]), 0);
                if(executed != SQLITE_OK) cout << "There was an error reading from database file, changes discarded.\n";
            } else if(subCommand == "getport") {
                string commandString = "SELECT value FROM webproxy_data WHERE name='port';";
                char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
                int executed = sqlite3_exec(DB, command, getportCallback, 0, 0);
                if(executed != SQLITE_OK) cout << "There was an error reading from database file.\n";
            } else {
                cout << "Invalid subcommand !!\n";
                help();
            }
            return 0;
        }
        //Domain command delclaration
        void domain(string command) {
            vector<string> args = split(command, ' ');
            if(args.size() < 2) {
                cout << "No subcommand specified !\n";
                return;
            }
            string subCommand = args[1];
            if(subCommand == "list") {
                for(auto& dmn : domains) {
                    Domain domain = dmn.second;
                    cout << domain.domain << " --> " << domain.host << ":" << domain.port << endl;
                }
            } else if(subCommand == "create") {
                if(args.size() < 5) {
                    cout << "Not enough arguments !\n>> domain create <domain name> <proxy endAddress> <proxy endPort>\nSee \">> help\" for more.\n";
                    return;
                }
                string domain = args[2];
                string host = args[3];
                string port = args[4];
                if(!stoi(port)) {
                    cout << "Port must be a number !";
                    return;
                }
                string commandString = "INSERT INTO webproxy_domains(domain, host, port) VALUES('"+domain+"', '"+host+"', '"+port+"');";
                char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
                int executed = sqlite3_exec(DB, command, 0, 0, 0);
                if(executed == SQLITE_CONSTRAINT) {
                    cout << "This domain is already registered under this proxy!\n";
                    return;
                }
                if(executed != SQLITE_OK) {
                    cout << "There was an error reading from database file.\n";
                    return; 
                }
                Domain dmn(domain, host, port);
                domains.insert(pair<string, Domain>(domain, dmn));
                cout << "Hostname was registered in database and will be redirected now.\n";
            } else if(subCommand == "edit") {
                if(args.size() < 5) {
                    cout << "Not enough arguments !\n>> domain edit <domain name> <proxy endAddress> <proxy endPort>\nSee \">> help\" for more.\n";
                    return;
                }
                string domain = args[2];
                string host = args[3];
                string port = args[4];
                if(!stoi(port)) {
                    cout << "Port must be a number !";
                    return;
                }
                vector<string> passingData;
                passingData.push_back(domain);
                passingData.push_back(host);
                passingData.push_back(port);
                string commandString = "SELECT EXISTS(SELECT 1 FROM webproxy_domains WHERE domain='"+domain+"');";
                char* command = strcpy(new char[commandString.length()+1], commandString.c_str());
                sqlite3_exec(DB, command, editDomainCallback, static_cast<void*>(&passingData), 0);
            } else if(subCommand == "delete") {
                if(args.size() < 3) {
                    cout << "Not enough arguments !\n>> domain delete <domain name>\nSee \">> help\" for more.\n";
                    return;
                }
                string domain = args[2];
                string commandString = "SELECT EXISTS(SELECT 1 FROM webproxy_domains WHERE domain='"+domain+"')";
                char* command = strcpy(new char[commandString.length()+1], commandString.c_str());
                sqlite3_exec(DB, command, deleteDomainCallback, static_cast<void*>(&domain), 0);
            } else {
                cout << "Invalid subcommand !!\n";
                help();
            }
        }
};



//Making input detector
void getInput() {
    //Getting user input
    string input;
    cout << "\n>> ";
    getline(cin, input);
    //Getting commands Object
    CommandsConstructor commands;

    //Selecting and executing command
    vector<string> args = split(input, ' ');
    cout << "\n";
    if(args.size() > 0) {
        if(args[0] == "proxy") {
            commands.proxy(input);
        } else if(args[0] == "domain") {
            commands.domain(input);
        } else {
            commands.help();
        }
    }
    //Looping function
    getInput();
}



//Initing program
int main() {
    cout << "Starting up system...\n";

    cout << "Verifiing sqlite database...\n";
    //Logging into database and building tables if dont exist yet
    sqlite3_open("database.db", &DB);
    char *ErrMsg = 0;
    /*
            SQLITE INIT COMMANDS

        ------TABLE "domains"-------
    CREATE TABLE IF NOT EXISTS `webproxy_domains` ( `domain` varchar(255), `host` varchar(255), `port` varchar(255) );
    CREATE UNIQUE index Constrain_Domains on webproxy_domains(domain);

        --------TABLE "data"--------
    CREATE TABLE IF NOT EXISTS `webproxy_data` ( `name` varchar(255), `value` varchar(255) );
    CREATE UNIQUE index Constrain_Names on webproxy_data(name);
    INSERT INTO webproxy_data(name, value) VALUES('port', '80');
    */
    string commandString = "CREATE TABLE IF NOT EXISTS `webproxy_domains` ( `domain` varchar(255), `host` varchar(255), `port` varchar(255) );CREATE TABLE IF NOT EXISTS `webproxy_data` ( `name` varchar(255), `value` varchar(255) );CREATE UNIQUE index Constrain_Names on webproxy_data(name);CREATE UNIQUE index Constrain_Domains on webproxy_domains(domain);INSERT INTO webproxy_data(name, value) VALUES('port', '80');";
    char* command = strcpy(new char[commandString.length() + 1], commandString.c_str());
    sqlite3_exec(DB, command, 0, 0, 0);

    //Load all domains into memory
    cout << "Loading domains into program..." << endl;
    string commandString1 = "SELECT * FROM webproxy_domains;";
    char* command1 = strcpy(new char[commandString1.length() + 1], commandString1.c_str());
    sqlite3_exec(DB, command1, domainLoadCallback, 0, 0);

    //Init proxy server
    server();
    //Init command listener after Proxy Server started
    while(true) {
        if(*ServerSocket != 0) {
            cout << "Starting command listener...\n";
            getInput();
            break;
        }
    }
    return 0;
}