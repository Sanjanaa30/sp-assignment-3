# SP Assignment 3 – Concurrent TCP File Server

This project implements a **TCP-based client–server file system** supporting:

- Concurrent file access
- Multiple readers / single writer
- Handshake-based authentication
- Real-time client notifications
- Graceful shutdown using signals

---

## 📁 Project Structure
```
sp-assignment-3/
├── server.c
├── server.h
├── client.c
├── client.h
├── client_ops.c
├── server_conf
├── client_conf
├── client_ops_conf
├── empty_upload/
│   └── text1.txt
└── README.md
```

---

## ⚙️ Configuration Files

### `server_conf`
```
PORT_NO 8449
```

### `client_conf`
```
PORT_NO 8449
SERVER_IP 127.0.0.1
DATA_FILE_PATH ./empty_upload
```

> If `DATA_FILE_PATH` points to another user's directory, update it to a valid local path.

### `client_ops_conf`
```
PORT_NO 8449
SERVER_IP 127.0.0.1
```

---

## 🔨 Build Instructions

### ✅ Step 0 — Prerequisites

- Linux / macOS
- `gcc`
- `pthread`
- `nc` (netcat) for optional manual testing

---

`cd to sp-assignment-3`

From the project root directory:
```bash
gcc -o server server.c -pthread
gcc -o client client.c
gcc -o client_ops client_ops.c
```

---

## ▶️ How to Run and Test the Project (Step-by-Step)

Follow the steps in order. **No code changes are required.**

### ✅ Step 1 — Start the Server

**Open Terminal 1:**
```bash
./server
```

**Expected output:**
```
Server will be Listening to the Port : 8449
Server is Listening on the Port 8449...
```

#### Server startup behavior

- Automatically creates the `shared/` directory if it does not exist
- Uses `shared/` as the common directory for all file operations
- Waits for incoming client connections

> **Note:** It is safe to delete `shared/` before running the server. The server recreates it automatically.

---

### ✅ Step 2 — Run the Basic Client (File Upload)

**Open Terminal 2:**
```bash
echo "test upload" > shared/text1.txt
./client
```

**verify**
```
cat shared/text1.txt
```

#### What this does

- Performs handshake automatically
- Uploads a predefined file only if it exists
- If no file exists, exits silently (expected behavior)

**Expected output (example):**
```
OK WRITE text1.txt
TCP: sent XX bytes in X.XXX s
```

**Verify on server:**
```bash
ls -lh shared/
```

---

### ✅ Step 3 — Run the Interactive Client (READ / WRITE)

**Open Terminal 3:**
```bash
./client_ops
```

**You will see:**
```
=== Client Ops Menu ===
1) Read file (cat)
2) Write/edit file (nano-like)
3) Exit
```

#### 🔹 READ Operation (cat equivalent)

- Choose option `1`
- Enter filename (e.g., `text1.txt`)

**Behavior:**

- File contents are printed
- Multiple clients may READ concurrently

---

#### 🔹 WRITE Operation (nano-like editor)

- Choose option `2`
- Enter filename
- Type something line by line

**Editor commands:**

- `:wq` → save and quit
- `:q!` → quit without saving

**Expected output:**
```
Write lock granted. Enter text now.
File Received by server
```

**Verify on server:**
```bash
cat shared/
```

---

## 🔐 Handshake & Authentication

Before any READ or WRITE operation:

**Client sends:**
```
HELLO <client_id>
```

**Server responds:**
```
OK
```

Requests without a handshake are rejected.

> This handshake is performed automatically by `client` and `client_ops`.

### 🧪 Optional Netcat Testing

#### READ without handshake (Rejected)
In a new terminal when the server is still running

```bash
nc 127.0.0.1 8449
```
```
READ text1.txt
```

**Expected:**
```
ERR Handshake required
```

---

#### READ with handshake

```bash
nc 127.0.0.1 8449
```
```
HELLO demo_client
READ text1.txt
```

---

## 🔄 Concurrency Behavior

- Multiple clients **can read the same file concurrently**
- Only **one writer is allowed** at a time
- Additional writers **wait automatically**
- Synchronization is implemented using `pthread_rwlock_t` and mutexes

---

## 🔔 Real-Time Notifications

If a client requests WRITE access while another client is editing the same file:

### Test Scenario: Multiple Clients Writing to the Same File - Alternative Test with Netcat

You can also test notifications manually using `nc`:

#### Terminal 1: Start a WRITE with Netcat
```bash
nc 127.0.0.1 8449
```

**Type:**
```
HELLO client1
WRITE text1.txt
```

**Then type some content but don't close the connection.**

---

#### Terminal 2: Try to WRITE the Same File
```bash
nc 127.0.0.1 8449
```

**Type:**
```
HELLO client2
WRITE text1.txt
```

**Expected response:**
```
NOTIFY BUSY test.txt
```

---

#### Terminal 1: Close the Connection

Press `Ctrl + C` to disconnect.

---

#### Terminal 2: Observe Notification

**Expected response:**
```
OK WRITE text1.txt
```

> The second client is now granted write access.

---

### Summary of Notification Flow

| Event | Server Action | Client Receives |
|-------|---------------|-----------------|
| Client 1 starts writing | Grants write lock | `OK WRITE <filename>` |
| Client 2 requests same file | Detects lock conflict | `NOTIFY BUSY <filename>` |
| Client 1 finishes writing | Releases write lock | — |
| Lock becomes available | Notifies waiting client | `OK WRITE <filename>` |

---

## 📂 Shared Directory & File Behavior

### Case 1: `shared/` does NOT exist
- Server creates it automatically on startup

### Case 2: `shared/` exists, file does NOT exist
- **READ** → `ERR file not found`
- **WRITE** → file is created and written

### Case 3: `shared/` and file exist
- **READ** → file contents returned
- **WRITE** → file overwritten safely

---

## 🛑 Signal Handling & Graceful Shutdown

### Server Shutdown

Press `Ctrl + C` on server

- Server stops accepting connections
- Server sends:
```
  SERVER_SHUTDOWN
```

### Client Behavior

- Clients receive shutdown notification
- Sockets close cleanly
- Clients exit gracefully

### Client `Ctrl + C`

- Active socket is closed
- Client exits cleanly

---

## 📝 Notes

- The `shared/` directory and sample files may be included for demonstration purposes
- The server recreates `shared/` automatically if deleted
- Configuration files control ports and paths

---

## ✅ End of Testing

Following all steps above confirms:

- ✓ TCP communication
- ✓ Authentication handshake
- ✓ Concurrency control
- ✓ Real-time notifications
- ✓ Graceful shutdown handling




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


  3) Client Operations & Real-Time Notifications

        1. Reading files from the server (cat equivalent)
        2. Writing/editing files using a simple line-based editor (nano-like)
        3. Real-time notifications when a file is already being edited by another client
        4. The server sends custom protocol messages to notify clients when a write lock is unavailable.

         Build (Part 3 Client)
         gcc -o server server.c -pthread
         gcc -o client_ops client_ops.c

         Run (Part 3)
         ./server

         In another terminal:
         ./client_ops

         Client Operations
         When running client_ops, select:
         Read file – prints file contents from the server
         Write/edit file – opens a simple editor

          Type text line by line
          :wq → save and quit
          :q! → quit without saving

          Real-Time Write Notifications
          If a client requests write access while another client is editing the same file:
          The server sends a notification:
          NOTIFY BUSY <filename>

          The client displays:
          [Notification] <filename> is currently being edited by another client.
          Once the file becomes available, the server sends:
          OK WRITE <filename>
          and the client can begin editing.
````
