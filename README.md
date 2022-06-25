# MultiDomainWebProxy
This http proxy allows servers to host multiple domains on one port such as 80 without using nearly any compute power and avarage time that it takes the proxy from moment it recieves a new request to moment it requests the target server 4ms.

This repository includes whole C++ source code of the proxy. In builds folder you can find already compiled version for linux. 
You are free to edit the source code for your needs and maybe suggest changes so I can upload them to the official version.

**Compiling**
If you want to compile the code yourself you will need a couple aditional libraries such as sqlite3.h and zmqpp.hpp
Aditional arguments you need to add to the compiler command are: "-lsqlite3 -lpthread"

Tools yoused to make helpString are in devtools folder uploaded here.

**Usage**
Proxy is fully controlled by CLI, when you run the proxy first time it will make a "database.db" file in the directory its located. 
You can get full list of commands with typing "help" and pressing enter after the proxy starts up.
