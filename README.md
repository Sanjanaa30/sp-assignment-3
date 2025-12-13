Run Code
To run the server  cd to sp-assignment-3
Run server: ./server
Run client: ./client

Run code:
To run the server
1. cd to sp-assignment-3
2. to run server ./server
3. to run client ./client

To rebuild the code :
I have used gcc, please,
use command gcc -o server server.c -pthread for server and
 gcc -o client client.c for client


2. Same Commands as 1 to rebuild and run 
    gcc -o server server.c -pthread
    gcc -o client client.c


    
    3. Test Write Operation 
        1. Verify that the file is created on the server: ls -lh xyz (update txt file) if doesnt exist create one - dd if=/dev/zero of=xyz bs=1M count=20.
        2. Configuration Changes - go to server_conf, Ensure the port number is set correctly: PORT_NO 8449
        3. Go to client_conf (WRITE mode) and verify the following 
                    PORT_NO 8449
                    SERVER_IP 127.0.0.1
                    FILE test20m.txt (FILE refers to a file present on the client machine.) - If DATA_FILE_PATH or any path points to another user’s directory, update it to a valid local file.
        4. Start server: ./server
        5. Start Client in another terminal: ./client
        6. Run this to simulate a WRITE header and send a file - (printf "WRITE test20m.txt\n"; cat test20m.txt) | nc 127.0.0.1 8449 and Verify file exists on server.
    

    4. Test Read Operations 
        1. Start server: ./server
        2. Start Client in another terminal: ./client
        3. Use netcat to request the file from server:  printf "READ test20m.txt\n" | nc 127.0.0.1 8449 > downloaded_test20m.txt
        4. Verify the downloaded file: cmp ./shared/test20m.txt downloaded_test20m.txt - If there is no output from cmp, the READ operation is successful


    5. Concurrency Behavior
        1. Multiple clients can read the same file concurrently.
        2. Only one client can write to a file at a time.
        3. Additional writers block automatically until the file becomes available.
        4. Synchronization is implemented using pthread_rwlock_t and mutexes.
        5. Run two READs quickly in two terminals and show both acquire RDLOCK.
        6. Run two WRITEs quickly and show second waits on WRLOCK.






